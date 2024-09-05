/* net/istream.c */

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

/* An INP secretary.
 * 
 * This waits for an ISTREAM_REQUEST message to arrive via the TWI
 * It then fetches a string from the remote elient using MEMZ and
 * sends a NOT_EMPTY message to the INP task which reads each character
 * in the buffer until it is empty. When the last character is removed
 * from the buffer, an ISTREAM_REPLY is sent to the remote client.
 *
 * When the the ISTREAM_REPLY has been sent, ISTREAM registers with the
 * TWI to receive the next ISTREAM_REQUEST message.
 */

#include <stdlib.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/memz.h"
#include "net/istream.h"

/* I am .. */
#define SELF ISTREAM
#define this istream

typedef enum {
    IDLE = 0,
    ENSLAVED,
    FETCHING_DATA,
    WRITING_DATA,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber consumer;
    char *bp;
    ushort_t rcnt;
    char *rbuf;
    istream_msg sm;   /* service message */
    union {
        memz_msg memz;
    } msg;
    union {
        twi_info twi;
    } info;
} istream_t;

/* I have .. */
static istream_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE uchar_t readchar(char *cp);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_istream(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        if (m_ptr->RESULT == EOK) {
            resume();
        } else {
            send_reply(m_ptr->RESULT);
        }
        break;

    case INIT:
        {
            this.consumer = INP;
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
        this.rcnt = this.sm.request.len - this.info.twi.rcnt;
        this.bp = this.rbuf;
        send_NOT_EMPTY(this.consumer, readchar);
        break;

    case WRITING_DATA:
        /* send_reply() isn't called here since it is called from readchar()
         * when the consumer reads the last char from the buffer.
         */
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE uchar_t readchar(char *cp)
{
    if (this.rcnt == 0) {
        this.sm.reply.count = this.sm.request.len;
        send_reply(EOK);
        return EWOULDBLOCK;
    }
    this.rcnt--;
    *cp = *(this.bp)++;
    return EOK;
}

PRIVATE void get_request(void)
{
    if (this.rbuf) {
        free(this.rbuf);
        this.rbuf = NULL;
    }
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, ISTREAM_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, ISTREAM_REPLY, this.sm.reply);
}

/* end code */
