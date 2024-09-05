/* alba/mdac.c */
 
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

/* A manager for an MCP4728 I2C Quad DAC. The device is mounted
 * on an Adafruit board ID4470 with the 4k7 resistor array removed.
 * The 4k7 TWI pullups were rather strong when added to the 3k3
 * combined pullups of the RTC, BMP and OLED devices.
 *
 * The LDAC input is connected to PB0 (pin #14) which has an external
 * pullup via a 100k resistor, allowing bit 0 of DDRB to be used to
 * present either a low-Z low level or a high-Z high level to /LDAC.
 *
 * LDAC needs to be held low for a minimum of 210ns in order to update the DAC.
 *
 * The MCP4728 generates a high level RDY output, which is inconvenient.
 * It is therefore inverted to present a /READY = low signal to PD2 (pin #4)
 * in order to level-trigger the INT0 interrupt.
 *
 * Refer to the device manufacturer's datasheet: MCP4728.pdf (DS22187E)
 * and the board manufacturer's datasheet: adafruit-mcp4728-i2c-quad-dac.pdf
 *
 * There are three operations:-
 * - Write the input registers:
 *      The Multi-Write Command [p.39] is used to write multiple DAC
 *      input registers and leave the EEPROM unaffected. Currently, only
 *      a single channel is written in any given job.
 *
 * - Write the input registers and the EEPROM:
 *      The Single Write Command [p.41] is used to write a the Single DAC
 *      Input Register and EEPROM.
 *
 * - Read the current setup:
 *      The Read Command and Device Outputs [p.45] is used to read all 24
 *      bytes into an internal buffer.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "alba/mdac.h"

/* I am .. */
#define SELF MDAC
#define this mdac

#define MCP4728_I2C_ADDRESS 0xC0 /* 192 */

#define MULTI_WRITE_COMMAND 0x40   /* 01000000 */
#define SINGLE_WRITE_COMMAND 0x58  /* 01011000 */

#define REFERENCE(x)        (((x) & 0x1) << 7)
#define POWERMODE(x)        (((x) & 0x3) << 5)
#define GAIN(x)             (((x) & 0x1) << 4)

#define WRITE_BUF_LEN       2
#define READ_BUF_LEN        24
#define CHANNEL_OFFSET      6
#define EEPROM_OFFSET       3

#define TWOTEN_NANOSECONDS  0.210

#define READY_PINS          PIND
#define READY_PORT          PORTD
#define READY_BIT           _BV(PORTD2)

#define LDAC_PORT           PORTB
#define LDAC_DDR            DDRB
#define LDAC_BIT            _BV(DDB0)
#define LDAC_LOW_TIME       TWOTEN_NANOSECONDS

typedef enum {
    IDLE = 0,
    AWAITING_READY,
    READING_MDAC
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    mdac_info *headp;
    uchar_t wbuf[WRITE_BUF_LEN];
    uchar_t rbuf[READ_BUF_LEN];
    union {
        twi_info twi;
    } info;
} mdac_t;

/* I have .. */
static mdac_t this;

/* I can .. */
#define is_busy()                   (READY_PINS & READY_BIT)
#define enable_ready_interrupt()    (EIFR |= _BV(INT0), EIMSK |= _BV(INT0))
#define disable_ready_interrupt()   (EIMSK &= ~_BV(INT0))
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC void config_mdac(void)
{
    /* Pin D2 (#4) is an input connected to the RDY/BSY status output
     * via an open drain inverter, which renders READY = low, BUSY = high.
     * INT0 is triggered by a low level which is the default setting following
     * a reset [p.80].
     *     EICRA &= ~(_BV(ISC01) | _BV(ISC00));
     *
     * The 2N7000 open drain inverter requires a software pullup.
     */
    READY_PORT |= READY_BIT; /* apply pullup */
}

PUBLIC uchar_t receive_mdac(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_BUSY:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
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
            mdac_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                mdac_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case SYNC:
        LDAC_DDR |= LDAC_BIT;  /* configure PORTB0 as an output */
        _delay_us(LDAC_LOW_TIME);
        LDAC_DDR &= ~LDAC_BIT; /* configure PORTB0 as a high-Z input */
        send_REPLY_RESULT(m_ptr->sender, EOK);
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    this.state = AWAITING_READY;
    if (is_busy()) {
        enable_ready_interrupt();
    } else {
        resume();
    }
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case AWAITING_READY:
        if (this.headp->read_flag) {
            /* Read Command and Device Outputs [MCP4728 p.45] */
            this.state = READING_MDAC;
            sae2_TWI_MR(this.info.twi, MCP4728_I2C_ADDRESS, 0, this.rbuf);
        } else {
           /* Single Write Command [MCP4728 p.41] */
           /* Assemble the Big-Endian 12-bit value and
            * the 4 control bits into the 2-byte buffer.
            */
            this.state = IDLE;
            this.wbuf[0] = (this.headp->val >> 8 & 0x0F) |
                       REFERENCE(this.headp->reference) |
                       POWERMODE(this.headp->powermode) |
                       GAIN(this.headp->gain);
            this.wbuf[1] = this.headp->val & 0xFF;
            uchar_t mcmd = ((this.headp->access_eeprom) ?
                           SINGLE_WRITE_COMMAND : MULTI_WRITE_COMMAND) |
                           this.headp->channel << 1 |
                           this.headp->inhibit_update;
            sae2_TWI_MT(this.info.twi, MCP4728_I2C_ADDRESS, mcmd, this.wbuf);
        }
        break;

    case READING_MDAC:
        {
            this.state = IDLE;
            uchar_t ofs = this.headp->channel * CHANNEL_OFFSET +
                       ((this.headp->access_eeprom) ? EEPROM_OFFSET : 0);
            this.headp->val = ((this.rbuf[ofs + 1] & 0x0F) << 8) |
                                this.rbuf[ofs + 2];
            this.headp->inhibit_update = 0;
            this.headp->reference = (this.rbuf[ofs + 1] & 0x80) >> 7;
            this.headp->powermode = (this.rbuf[ofs + 1] & 0x60) >> 5;
            this.headp->gain = (this.rbuf[ofs + 1] & 0x10) >> 4;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;
    }
}

/* -----------------------------------------------------
   Handle an INT0 interrupt.
   This appears as <__vector_1>: in the .lst file.
   -----------------------------------------------------*/
ISR(INT0_vect)
{
    disable_ready_interrupt();
    send_NOT_BUSY(SELF);
}

/* convenience functions */

PUBLIC void send_MDAC_READ(ProcNumber sender, mdac_info *cp, uchar_t channel,
                                                                   uchar_t ee)
{
    cp->channel = channel;
    cp->access_eeprom = ee;
    cp->read_flag = TRUE;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_MDAC_WRITE(ProcNumber sender, mdac_info *cp, uchar_t channel,
                                    uchar_t ee, ushort_t val, uchar_t inhibit,
                            uchar_t reference, uchar_t powermode, uchar_t gain)
{
    cp->channel = channel;
    cp->access_eeprom = ee;
    cp->val = val;
    cp->inhibit_update = inhibit;
    cp->reference = reference;
    cp->powermode = powermode;
    cp->gain = gain;
    cp->read_flag = FALSE;
    send_m3(sender, SELF, JOB, cp);
}

/* end code */
