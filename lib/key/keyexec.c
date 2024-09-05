/* key/keyexec.c */

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

#include <stdio.h>
#include <string.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/fsd.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "alba/setupd.h"
#include "key/keypad.h"
#include "key/keyexec.h"

/* I am .. */
#define SELF KEYEXEC
#define this keyexec

#define NR_KEYS 16  /* 8 down, 8 up */
#define KEY_MASK (NR_KEYS -1)

typedef enum {
    IDLE = 0,
    BUSY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    uchar_t b;               /* the pending key number */
    inum_t keytab[NR_KEYS];
    union {
        setupd_msg setupd;
    } msg;
    union {
        twi_info twi;
    } info;
} keyexec_t;

/* I have .. */
static keyexec_t this;

/* I can .. */

PUBLIC uchar_t receive_keyexec(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case BUTTON_CHANGE:
        if (m_ptr->mtype < NR_KEYS) {
            if (this.state == IDLE && this.keytab[m_ptr->mtype]) {
                this.state = BUSY;
                this.b = m_ptr->mtype & KEY_MASK;
                this.msg.setupd.request.val = this.keytab[this.b];
                this.msg.setupd.request.op = OP_LOAD;
                this.msg.setupd.request.taskid = SELF;
                this.msg.setupd.request.jobref = &this.info.twi;
                this.msg.setupd.request.sender_addr = HOST_ADDRESS;
                sae2_TWI_MTSR(this.info.twi, PISA_I2C_ADDRESS,
                   SETUPD_REQUEST, this.msg.setupd.request,
                   SETUPD_REPLY, this.msg.setupd.reply);
            } else {
                send_READ_BUTTON(KEYPAD, m_ptr->mtype);
            }
        }
        break;

    case REPLY_INFO:
        /* rearm the key when the SETUPD_REPLY is received from the TWI */
        this.state = IDLE;
        send_READ_BUTTON(KEYPAD, this.b);
        this.b = 0;
        break;

    case SET_IOCTL:
        {
            uchar_t ret = EINVAL;
            if (m_ptr->IOCTL == SIOC_BUTTONVAL) {
                ret = EOK;
                inum_t f = m_ptr->LCOUNT & 0xFFFF;
                uchar_t b = m_ptr->LCOUNT >> 16;
                this.keytab[b & KEY_MASK] = f;
                send_READ_BUTTON(KEYPAD, b);
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/* end code */
