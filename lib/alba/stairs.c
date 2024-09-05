/* alba/stairs.c */

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

/* STAIRS is an iterative process director that sets the MDAC to a value and
 * then asks EGOR to perform a given number of measurements of that value.
 * When EGOR has completed the measurements the MDAC value is incremented by a
 * given stepsize and the process is repeated until the value reaches maximum.
 */

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "alba/mdac.h"
#include "alba/stairs.h"

/* I am .. */
#define SELF STAIRS
#define this stairs

#define MAX_VALUE 4095

typedef enum {
    IDLE = 0,
    WRITING_MDAC,
    WRITING_LOOP_COUNT,
    RUNNING_EGOR
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned running : 1;
    stairs_info *headp;
    union {
        mdac_info mdac;
    } info;
} stairs_t;

/* I have .. */
static stairs_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_stairs(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.running && this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.running = FALSE;
            this.state = IDLE;
            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL)
                    start_job();
            }
        }
        break;

    case JOB:
        {
            stairs_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                stairs_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case CANCEL:
        if (this.headp && this.headp == m_ptr->INFO) {
            this.running = FALSE;
        } else {
            stairs_info *tp;
            uchar_t result = ESRCH;
            for (tp = this.headp; tp->nextp; tp = tp->nextp) {
                if (tp->nextp == m_ptr->INFO) {
                    tp->nextp = ((stairs_info *)m_ptr->INFO)->nextp;
                    result = EOK;
                    break;
                }
            }
            send_REPLY_INFO(m_ptr->sender, result, m_ptr->INFO);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    this.state = RUNNING_EGOR;
    this.running = TRUE;
    this.info.mdac.channel = this.headp->channel;
    this.info.mdac.val = this.headp->val;
    /* If either stepsize or nr_samples is zero, an endless loop results. */
    if (this.headp->stepsize == 0)
        this.headp->stepsize = 1;
    if (this.headp->nr_samples == 0)
        this.headp->nr_samples = 1;
    resume();
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case WRITING_MDAC:
        this.state = WRITING_LOOP_COUNT;
        send_SET_IOCTL(EGOR, SIOC_LOOP_COUNT, this.headp->nr_samples);
        break;

    case WRITING_LOOP_COUNT:
        this.state = RUNNING_EGOR;
        send_START(EGOR);
        break;

    case RUNNING_EGOR:
        this.state = WRITING_MDAC;
        this.info.mdac.channel = this.headp->channel;
        this.info.mdac.val = this.headp->val;
        this.info.mdac.read_flag = FALSE;
        this.info.mdac.access_eeprom = FALSE;
        this.info.mdac.inhibit_update = FALSE;
        this.info.mdac.reference = INTERNAL_REFERENCE;
        this.info.mdac.powermode = NORMAL;
        this.info.mdac.gain = ZERO_DB;
        send_JOB(MDAC, &this.info.mdac);
        this.headp->val += this.headp->stepsize;
        if (this.headp->val > MAX_VALUE)
            this.running = FALSE;
        break;
    }
}

/* end code */
