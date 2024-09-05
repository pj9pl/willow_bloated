/* net/ostream.c */

/* Copyright (c) 2024 Peter Welch
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
   * Neither the name of the copyright holders nor the names of
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

/* A network secretary that writes to the serial port.
 * 
 * The OSTREAM task accepts an OSTREAM_REQUEST message via the TWI
 * bus and transfers the data to the local SER device. When the SER
 * device has completed, an OSTREAM_REPLY is sent back to the client. 
 *
 * This task needs to register the service within the TWI secretary pool
 * in order to receive a request. This is normally accomplished by including
 * OSTREAM in the inittab array in sysinit.c. From then on it re-registers
 * itself following the completion of the SER output.
 */

#include <stdlib.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/memz.h"
#include "net/ostream.h"

/* I am .. */
#define SELF OSTREAM
#define this ostream

typedef enum {
    IDLE = 0,
    ENSLAVED,
    FETCHING_DATA,
    WRITING_DATA,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    char *rbuf;
    ostream_msg sm; /* service message */
    union {
        memz_msg memz;
    } msg;
    union {
        twi_info twi;
        ser_info ser;
    } info;
} ostream_t;

/* I have .. */
static ostream_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void handle_error(uchar_t err);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_ostream(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        if (m_ptr->RESULT == EOK) {
            resume();
        } else {
            handle_error(m_ptr->RESULT);
        }
        break;

    case INIT:
        {
            uchar_t result = EBUSY;
            if (this.state == IDLE) {
                get_request();
                result = EOK;
            }
            send_REPLY_RESULT(m_ptr->sender, result);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case ENSLAVED:
        if ((this.rbuf = malloc(this.sm.request.len)) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_DATA;
            this.msg.memz.request.src = this.sm.request.src;
            this.msg.memz.request.len = this.sm.request.len;
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                     MEMZ_REQUEST,
                    &this.msg.memz.request, sizeof(this.msg.memz.request),
                     this.rbuf, this.sm.request.len);
        }
        break;

    case FETCHING_DATA:
        this.state = WRITING_DATA;
        this.sm.reply.count = this.sm.request.len - this.info.twi.rcnt;
        sae_SER(this.info.ser, this.rbuf, this.sm.reply.count);
        break;

    case WRITING_DATA:
        send_reply(EOK);
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void handle_error(uchar_t err)
{
    /* If there is a client waiting, a reply should be sent. */
    switch (err) {
    case EACCES:
    case EAGAIN:
        send_reply(err);
        break;

    default:
        get_request();
        break;
    }
}

PRIVATE void get_request(void)
{
    if (this.rbuf) {
        free(this.rbuf);
        this.rbuf = NULL;
    }
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, OSTREAM_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, OSTREAM_REPLY, this.sm.reply);
}

/* end code */
