/* key/keysec.c */

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

/* keypad secretary.
 * 
 * This accepts KEY_REQUEST messages containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 *
 */

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "fs/sfa.h"
#include "key/keyconf.h"
#include "key/keysec.h"

/* I am .. */
#define SELF KEYSEC
#define this keysec

typedef enum {
    IDLE = 0,
    ENSLAVED,
    LOADING_KEYCONF,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    key_msg sm; /* service message */
    union {
        twi_info twi;
        keyconf_info keyconf;
    } info;
} keysec_t;

/* I have .. */
PRIVATE keysec_t this;

/* I can .. */
PRIVATE void exec_keysec(void);
PRIVATE void resume(message *m_ptr);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_keysec(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        if (this.state == ENSLAVED && m_ptr->sender == TWI) {
            if (m_ptr->RESULT == EOK) {
                exec_keysec();
            } else {
                get_request();
            }
        } else {
            resume(m_ptr);
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

PRIVATE void exec_keysec(void)
{
    uchar_t result = EOK;

    switch (this.sm.request.op) {
    case OP_LOADKEY:
        /* load a config file into keyconf */
        /* the numeric value corresponds to the file inode number */
        this.state = LOADING_KEYCONF;
        this.info.keyconf.inum = this.sm.request.val;
        send_JOB(KEYCONF, &this.info.keyconf);
        return;

    default:
        result = ENOSYS;
        break;
    }
    send_reply(result);
}

PRIVATE void resume(message *m_ptr)
{
    switch (this.state) {
    case IDLE:
    case ENSLAVED:
        break;

    case LOADING_KEYCONF:
        this.sm.reply.val = this.info.keyconf.nlines;
        send_reply(m_ptr->RESULT);
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
    sae2_TWI_SR(this.info.twi, KEY_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, KEY_REPLY, this.sm.reply);
}

/* end code */
