/* bmp/tempest.c */

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

/* An iterative daemon process to read the BMP180 and broadcast the
 * measurements in a BAROMETER_NOTIFY to any host that accepts it.
 *
 * Both the temperature and pressure values are contained in a single
 * unsigned long.
 *
 * An ENODEV indicates that the slave didn't acknowledge its address.
 * No such device.
 *
 * It replies to the START message before the process commences since
 * it is an infinite loop.
 *
 */

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "bmp/bmp.h"
#include "bmp/tempest.h"

/* I am .. */
#define SELF TEMPEST
#define this tempest

#define BAROMETER_TYPE 0x9    /* version of the data record */

/* interval with correction */
#define TWO_SECONDS (2000 - 11)
#define TEN_SECONDS (10000 - 53)
#define ONE_MINUTE (60000 - 320)

#define MEASUREMENT_INTERVAL TEN_SECONDS

/* 1528 is adding 1 second every 15-20 minutes.
 * 3000000/1200 == 250, so add 250 to 1528 and try 1778
 */
//#define FIVE_MINUTES (300000 - 1528)
#define FIVE_MINUTES (300000 - 1778)

typedef enum {
    IDLE = 0,
    READING_BAROMETER,
    WRITING_BARZ,
    AWAITING_ALARM
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    dbuf_t dbuf;
    uchar_t device_address;
    unsigned output_flag : 1;
    union {
        clk_info clk;
        bmp_info bmp;
        twi_info twi;
    } info;
} tempest_t;

/* I have .. */
static tempest_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_tempest(message *m_ptr)
{
    uchar_t ret = EOK;

    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                this.state = WRITING_BARZ;
                resume();
            }
        } else if (this.replyTo) {
            send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
            this.replyTo = 0;
        }
        break;

    case START:
        if (this.state == IDLE) {
            this.replyTo = m_ptr->sender;
            /* we start with the delay so that the reply is unhindered */
            this.state = WRITING_BARZ;
            resume();
            send_REPLY_RESULT(m_ptr->sender, EOK);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    case STOP:
        if (this.state) {
            if (this.state == AWAITING_ALARM) {
                sae_CLK_CANCEL(this.info.clk);
            }
            this.state = IDLE;
        } else {
            send_REPLY_RESULT(m_ptr->sender, EOK);
        }
        break;

    case SET_IOCTL:
        switch (m_ptr->IOCTL) {
        case SIOC_SELECT_OUTPUT:
            this.output_flag = TRUE;
            switch (m_ptr->LCOUNT) {
            case 0:
                this.output_flag = FALSE;
                break;
                
            case 1: /* PLCD via BARZ */
                this.device_address = LCD_ADDRESS;
                break;

            case 2: /* TWI_OLED via BARP */
                this.device_address = TWI_OLED_ADDRESS;
                break;

            case 3: /* SPI_OLED via (another) BARP */
                this.device_address = SPI_OLED_ADDRESS;
                break;

            case 7: /* GENERAL CALL (peru,lima,sumo) */
                this.device_address = GCALL_I2C_ADDRESS;
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
        this.state = READING_BAROMETER;
        this.info.bmp.mode = READ_BMP;
        send_JOB(BMP, &this.info.bmp);
        break;

    case READING_BAROMETER:
        if (this.output_flag) {
            this.state = WRITING_BARZ;
            this.dbuf.taskid = SELF;
            this.dbuf.jobref = &this.info.twi;
            this.dbuf.sender_addr = HOST_ADDRESS;
            this.dbuf.res = (this.info.bmp.T << 18) |
                            (this.info.bmp.p & 0x0003ffff);
            this.dbuf.mtype = BAROMETER_TYPE;
            sae2_TWI_MT(this.info.twi, this.device_address,
                BAROMETER_NOTIFY, this.dbuf);
            break;
        }
            /* else fallthrough */

    case WRITING_BARZ:
        this.state = AWAITING_ALARM;
        sae_CLK_SET_ALARM(this.info.clk, MEASUREMENT_INTERVAL);
        break;
    }
}

/* end code */
