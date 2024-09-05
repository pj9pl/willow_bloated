/* alba/ramp.c */

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

/* RAMP is an iterative process director that sets the MDAC to a value
 * and then asks CLK for an alarm after delay in ms. When an ALARM arrives
 * the value is increased by stepsize then the process is repeated until
 * value reaches MAX_VALUE.
 */

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "alba/mdac.h"
#include "alba/ramp.h"

/* I am .. */
#define SELF RAMP
#define this ramp

#define MAX_VALUE 4095
#define UP 1
#define DOWN 0

typedef enum {
    IDLE = 0,
    WRITING_MDAC,
    AWAITING_ALARM
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned running : 1;
    unsigned channel : 2;
    unsigned direction : 1;
    ulong_t delay;
    uchar_t stepsize;
    ulong_t start_val;
    ulong_t end_val;
    ProcNumber replyTo;
    union {
        mdac_info mdac;
        clk_info clk;
    } info;
} ramp_t;

/* I have .. */
static ramp_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_ramp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.running && this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
        }
        break;

    case START:
        if (this.state == IDLE) {
            if ((this.start_val == this.end_val) ||
                (this.delay == 0)) {
                send_REPLY_RESULT(m_ptr->sender, EINVAL);
                break;
            } else {
                this.direction = (this.start_val < this.end_val) ? UP : DOWN;
            }
            this.state = AWAITING_ALARM;
            this.running = TRUE;
            this.replyTo = m_ptr->sender;
            resume();
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    case STOP:
        if (this.state) {
            this.running = FALSE;
        } else {
            send_REPLY_RESULT(m_ptr->sender, EOK);
        }
        break;

    case SET_IOCTL:
        {
            uchar_t ret = EOK;
            switch (m_ptr->IOCTL) {
            case SIOC_STEPSIZE:
                this.stepsize = m_ptr->LCOUNT;
                break;

            case SIOC_CHANNEL:
                this.channel = m_ptr->LCOUNT;
                break;

            case SIOC_START_VALUE:
                this.start_val = m_ptr->LCOUNT;
                break;

            case SIOC_END_VALUE:
                this.end_val = m_ptr->LCOUNT;
                break;

            case SIOC_DELAY:
                this.delay = m_ptr->LCOUNT;
                break;

            default:
                ret = EINVAL;
                break;
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
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

    case WRITING_MDAC:
        this.state = AWAITING_ALARM;
        sae_CLK_SET_ALARM(this.info.clk, this.delay);
        break;

    case AWAITING_ALARM:
        this.state = WRITING_MDAC;
        this.info.mdac.channel = this.channel;
        this.info.mdac.val = this.start_val;
        this.info.mdac.read_flag = FALSE;
        this.info.mdac.access_eeprom = FALSE;
        this.info.mdac.inhibit_update = FALSE;
        this.info.mdac.reference = INTERNAL_REFERENCE;
        this.info.mdac.powermode = NORMAL;
        this.info.mdac.gain = ZERO_DB;
        send_JOB(MDAC, &this.info.mdac);
        if (this.direction == UP) {
            if (this.start_val >= this.end_val) {
                this.running = FALSE;
            } else {
                this.start_val += this.stepsize;
            }      
        } else {
            if (this.start_val <= this.end_val) { 
                this.running = FALSE;
            } else {
                this.start_val -= this.stepsize;
            }
        }    
        break;
    }
}

/* end code */
