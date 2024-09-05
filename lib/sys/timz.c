/* sys/timz.c */

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

/* A periodic daemon to read the UTC and forward it to a destination in a
 * DATE_NOTIFY message.
 * 
 * This accepts START, STOP and SET_IOCTL messages where SIOC_SELECT_OUTPUT
 * specifies:-
 *        0 = OFF
 *        1 = LCD (sumo)
 *        2 = OLED (jira)
 *        3 = OLED (peru)
 *        7 = GCALL (any/none)
 *
 * There is a heuristic one second interval that might indicate the
 * relative inaccuracy of the internal RC oscillator.
 */

#include <time.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "sys/clk.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "sys/utc.h"
#include "lcd/datez.h"
#include "sys/timz.h"

/* I am .. */
#define SELF TIMZ
#define this timz

#define HALF_SECOND 500
#define ONE_SECOND 1000
#define ONE_MINUTE 60000
#define FIVE_MINUTES 300000

typedef enum {
    IDLE = 0,
    FETCHING_TIME,
    WRITING_DATA,
    AWAITING_ALARM
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    uchar_t dest;
    dbuf_t dbuf;
    ushort_t naptime;
    ulong_t prev_time;
    union {
        utc_msg utc;
    } msg;
    union {
        clk_info clk;
        twi_info twi;
    } info;
} timz_t;

/* I have .. */
static timz_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_timz(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                this.state = WRITING_DATA;
                resume();
            }
        } else if (this.replyTo) {
            send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
            this.replyTo = 0;
        }
        break;

    case SET_IOCTL:
        {
            uchar_t ret = EOK;
            switch (m_ptr->IOCTL) {
            case SIOC_SELECT_OUTPUT:
                switch (m_ptr->LCOUNT) {
                case 1:
                    this.dest = LCD_ADDRESS;
                    break;

                case 2:
                    this.dest = TWI_OLED_ADDRESS;
                    break;

                case 3:
                    this.dest = SPI_OLED_ADDRESS;
                    break;

                case 7:
                    this.dest = GCALL_I2C_ADDRESS;
                    break;

                default:
                    ret = EINVAL;
                    break;
                }
                break;

            default:
                ret = EINVAL;
                break;
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
        }
        break;

    case STOP:
        if (this.state == IDLE) {
            send_REPLY_RESULT(m_ptr->sender, EOK);
        } else {
            this.state = IDLE;
            this.replyTo = m_ptr->sender;
        }
        break;

    case START:
        if (this.naptime == 0)
            this.naptime = ONE_SECOND;
        if (this.state == IDLE) {
            this.state = WRITING_DATA;
            resume();
            send_REPLY_RESULT(m_ptr->sender, EOK);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
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

    case AWAITING_ALARM:
        this.state = FETCHING_TIME;
        this.msg.utc.request.taskid = SELF;
        this.msg.utc.request.op = GET_TIME;
        sae2_TWI_MTMR(this.info.twi, UTC_ADDRESS,
                  UTC_REQUEST, this.msg.utc, this.msg.utc);
        break;

    case FETCHING_TIME:
        this.state = WRITING_DATA;
        /* fiddle a naptime heuristic */
        if (this.prev_time == this.msg.utc.reply.val)
            this.naptime++;
        else if (this.prev_time >= this.msg.utc.reply.val + 2)
            this.naptime--;
        this.prev_time = this.msg.utc.reply.val;

        this.dbuf.taskid = SELF;
        this.dbuf.jobref = &this.info.twi;
        this.dbuf.sender_addr = HOST_ADDRESS;
        this.dbuf.res = this.msg.utc.reply.val;
        this.dbuf.mtype = TIME_ONLY;
        sae2_TWI_MT(this.info.twi, this.dest,
                DATE_NOTIFY, this.dbuf);
        break;

    case WRITING_DATA:
        this.state = AWAITING_ALARM;
        sae_CLK_SET_ALARM(this.info.clk, this.naptime);
        break;
    }
}

/* end code */
