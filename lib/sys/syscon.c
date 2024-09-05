/* sys/syscon.c */

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

/* Generic version. Copy to the app directory before making any changes.
 *
 * This module handles incoming SYSCON_REQUEST messages.
 *    OP_REBOOT
 *    OP_CYCLES
 *    OP_RESTART
 *    OP_BOOTTIME
 */

#include <time.h>
#include <avr/io.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "sys/rv3028c7.h"
#include "sys/syscon.h"

/* I am .. */
#define SELF SYSCON
#define this syscon

typedef enum {
    IDLE = 0,
    FETCHING_UNIXTIME,
    ENSLAVED,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    syscon_msg sm; /* service message */
    time_t boottime;
    union {
        twi_info twi;
    } info;
} syscon_t;

/* I have .. */
static syscon_t this;

/* I can .. */
PRIVATE void exec_command(void);
PRIVATE void resume(uchar_t result);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_syscon(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state == ENSLAVED && m_ptr->sender == TWI) {
            if (m_ptr->RESULT == EOK) {
                exec_command();
            } else {
                get_request();
            }
        } else if (this.state) {
            resume(m_ptr->RESULT);
        }
        break;

    case INIT:
        if (this.state == IDLE) {
            /* get the unixtime from the RV3028C7 */
            this.state = FETCHING_UNIXTIME;
            this.replyTo = m_ptr->sender;
            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_UNIX_TIME_0, this.boottime);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void exec_command(void)
{
    switch (this.sm.request.op) {
    case OP_REBOOT:
        generate_external_reset();
        /* notreached */
        break;

    case OP_CYCLES:
        this.sm.reply.p.cycles.depth = msg_depth();
        this.sm.reply.p.cycles.count = msg_count();
        this.sm.reply.p.cycles.lost = msg_lost();
        send_reply(EOK);
        break;

    case OP_RESTART:
        /* invoke watchdog timeout */
        for (;;)
            ;
        /* notreached */
        break;

    case OP_BOOTTIME:
        this.sm.reply.p.lastreset.boottime = this.boottime;
        send_reply(EOK);
        break;

    default:
        send_reply(ENOSYS);
        break;
    }
}

PRIVATE void resume(uchar_t result)
{
    switch (this.state) {
    case IDLE:
    case ENSLAVED:
        break;

    case FETCHING_UNIXTIME:
        send_REPLY_RESULT(this.replyTo, result);
        this.replyTo = 0;
        get_request();
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, SYSCON_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, SYSCON_REPLY, this.sm.reply);
}

/* end code */
