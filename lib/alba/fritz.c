/* alba/fritz.c */

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

/* Fritz is an alba secretary that initializes the AD7124
 *
 * The simple commands are:-
 *  - INIT   Called following a reset to probe a device for the
 *           current state of affairs.
 *           Read the ID register. This can produce one of:-
 *            - 0000 = powered-down - needs 8 0xFF bytes to power-on.
 *            - FF14 = standby - read the control register.
 *            - 0014 = running - check if the power-on flag in the status
 *                  register is set. If so, put the device in standby mode.
 */

#include <inttypes.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "alba/ad7124.h"
#include "alba/alba.h"
#include "alba/fritz.h"

/* I am .. */
#define SELF FRITZ
#define this fritz

typedef enum {
    IDLE = 0,
    READING_ID_REG,
    READING_STATUS_REG,
    READING_CONTROL_REG
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    union {
        alba_info alba;
    } info;
} fritz_t;

/* I have .. */
static fritz_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_fritz(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.replyTo) {
                send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
                this.replyTo = 0;
            }
        }
        break;

    case INIT:
        if (this.state == IDLE) {
            this.replyTo = m_ptr->sender;
            this.state = READING_ID_REG;
            this.info.alba.mode = READ_MODE;
            this.info.alba.regno = AD7124_ID;
            send_JOB(ALBA, &this.info.alba);
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

    case READING_ID_REG:
        if (this.info.alba.rb.id.device_id == 0x01 &&
                this.info.alba.rb.id.silicon_revision == 0x04) {
            this.state = READING_STATUS_REG;
            this.info.alba.mode = READ_MODE;
            this.info.alba.regno = AD7124_Status;
            send_JOB(ALBA, &this.info.alba);
        } else {
            this.state = IDLE;
            send_REPLY_RESULT(SELF, ENODEV); /* not an AD7124: ENODEV */
        }
        break;
 
    case READING_STATUS_REG:
        /* We've got the status register in hand.
         * Test the POR bit. If it's set, put the AD7124 in powerdown.
         */
        if (this.info.alba.rb.status.por_flag == TRUE) {
            this.state = IDLE;
            this.info.alba.mode = WRITE_MODE;
            this.info.alba.regno = AD7124_ADC_Control;
            this.info.alba.rb.val = 0;
            this.info.alba.rb.adc_control.data_status = TRUE;
            this.info.alba.rb.adc_control.power_mode = LOW_POWER;
            this.info.alba.rb.adc_control.mode = STANDBY_MODE;
            this.info.alba.rb.adc_control.clk_sel = INTERNAL_CLK;
            send_JOB(ALBA, &this.info.alba);
        } else {
            this.state = READING_CONTROL_REG;
            this.info.alba.mode = READ_MODE;
            this.info.alba.regno = AD7124_ADC_Control;
            send_JOB(ALBA, &this.info.alba);
        }
        break;

    case READING_CONTROL_REG:
        this.state = IDLE;
        if (this.info.alba.rb.adc_control.mode == STANDBY_MODE) {
            send_REPLY_RESULT(SELF, EOK);
        } else {
            this.info.alba.mode = WRITE_MODE;
            this.info.alba.regno = AD7124_ADC_Control;
            this.info.alba.rb.val = 0;
            this.info.alba.rb.adc_control.data_status = TRUE;
            this.info.alba.rb.adc_control.power_mode = LOW_POWER;
            this.info.alba.rb.adc_control.mode = STANDBY_MODE;
            this.info.alba.rb.adc_control.clk_sel = INTERNAL_CLK;
            send_JOB(ALBA, &this.info.alba);
        }
        break;
    }
}

/* end code */
