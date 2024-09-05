/* sys/batz.c */

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

/* A periodic daemon that measures the battery voltage and sends the result
 * to a destination in a BATTERY_NOTIFY.
 * 
 * This accepts START, STOP and SET_IOCTL messages:-
 *     SIOC_SAMPLING_RATE:
 *        0 = 5 minutes
 *        1 = 1 minute
 *        2 = 2 seconds
 *
 *     SIOC_SELECT_OUTPUT:
 *        0 = OFF
 *        1 = LCD (sumo)
 *        2 = OLED (peru)
 *        3 = OLED (lima)
 *        7 = GCALL (any/none)
 */

#include <avr/io.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "sys/adcn.h"
#include "sys/batz.h"

/* I am .. */
#define SELF BATZ
#define this batz

#define TWO_SECONDS 2000
#define ONE_MINUTE 60000
#define FIVE_MINUTES 300000

typedef enum {
    IDLE = 0,
    READING_BATTERY,
    WRITING_DATA,
    AWAITING_ALARM
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned running : 1;
    unsigned output : 1;
    ProcNumber replyTo;
    dbuf_t dbuf;
    ulong_t res;
    uchar_t dest;
    union {
        clk_info clk;
        adcn_info adcn;
        twi_info twi;
    } info;
} batz_t;

/* I have .. */
static batz_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_batz(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state) {
            resume();
        } else if (this.replyTo) {
            send_REPLY_RESULT(this.replyTo, EOK);
            this.replyTo = 0;
        }
        break;

    case START:
        if (this.state == IDLE) {
            this.replyTo = m_ptr->sender;
            this.state = AWAITING_ALARM;
            this.running = TRUE;
            resume();
            send_REPLY_RESULT(m_ptr->sender, EOK);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    case STOP:
        this.running = FALSE;
        if (this.state == AWAITING_ALARM) {
            sae_CLK_CANCEL(this.info.clk);
            this.state = IDLE;
        } else {
            send_REPLY_RESULT(m_ptr->sender, EOK);
        }
        break;

    case SET_IOCTL:
        {
            uchar_t ret = EOK;
            switch (m_ptr->IOCTL) {
            case SIOC_SELECT_OUTPUT:
                this.replyTo = m_ptr->sender;
                switch (m_ptr->LCOUNT) {
                case 0:
                    this.output = FALSE;
                    break;

                case 1:
                    this.output = TRUE;
                    this.dest = LCD_ADDRESS;
                    break;

                case 2:
                    this.output = TRUE;
                    this.dest = TWI_OLED_ADDRESS;
                    break;

                case 3:
                    this.output = TRUE;
                    this.dest = SPI_OLED_ADDRESS;
                    break;

                case 7:
                    this.output = TRUE;
                    this.dest = GCALL_I2C_ADDRESS;
                    break;

                default:
                    ret = EINVAL;
                    break;
                }
                break;

            case SIOC_SAMPLING_RATE:
                switch (m_ptr->LCOUNT) {
                case 2:
                    this.res = FIVE_MINUTES;
                    break;

                case 3:
                    this.res = ONE_MINUTE;
                    break;

                case 4:
                    this.res = TWO_SECONDS;
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
        this.state = READING_BATTERY;
        this.info.adcn.admux = INTERNAL_REF | CHANNEL_0;
        send_JOB(ADCN, &this.info.adcn);
        break;

    case READING_BATTERY:
        this.state = (this.running) ? WRITING_DATA : IDLE;
        if (this.output) {
            this.dbuf.taskid = SELF;
            this.dbuf.jobref = &this.info.twi;
            this.dbuf.sender_addr = HOST_ADDRESS;
            this.dbuf.res = this.info.adcn.result;
            this.dbuf.mtype = 0;
            sae2_TWI_MT(this.info.twi, this.target,
               BATTERY_NOTIFY, this.dbuf);
            break;
        }
        break;

    case WRITING_DATA:
        this.state = AWAITING_ALARM;
        sae_CLK_SET_ALARM(this.info.clk, this.res);
        break;
    }
}

/* end code */
