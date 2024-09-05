/* sys/rtc.c */

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

/* A manager for an RV-3028-C7 device mounted on a Pimoroni PIM449 Real-Time
 * Clock module.
 *
 * The PIM449 module is fitted with a 1.55V 8mAh type 337 primary cell backup
 * battery. It features an active low, open drain interrupt output, but has
 * neither a clock output nor an external event input. There are 10k pullups
 * on SDA and SCL.
 *
 * See also: RV_3028_C7_App_Manual-2487794.pdf
 *
 * The default factory setting has the Backup Switchover Mode disabled. [p.39]
 * BSM0 = 0, BSM1 = 0 in the EEPROM_BACKUP register. The Backup Switchover Mode
 * should be set to Direct Switching Mode (BSM0 = 1, BSM1 = 0) to use the
 * battery when VDD is removed. 
 *
 * The BSM0 bit of the EEPROM_BACKUP register needs to be set to enable
 * or clear to disable. Using the fido '<nnn> P' input command:-
 *       20P = turn battery backup on  (0x14).
 *       16P = turn battery backup off (0x10).
 * 
 * The Trickle Charge Enable (TCE) bit of the EEPROM_BACKUP register needs
 * to be clear as the PIM449 uses a primary cell and therefore must not be
 * recharged.  The default factory setting has the trickle charge disabled
 * (TCE = 0) [p.39] This should be maintained.
 *
 * The FEDE bit of EEPROM_BACKUP needs to be set [p.39].
 * This is the 0x10 used above.
 *
 * Alterations must be written to the eeprom otherwise they are lost when
 * VDD is removed or through the automatic refresh at midnight [p.54].
 *
 * RTC Interrupts [p.58]
 * The RV-3028-C7 interrupt output is connected to PIND2/INT0.
 * The interrupt output is an open drain that requires a pullup.
 *
 * - Periodic Time Update Interrupt Function [p.66]
 *   Set USEL in Control_1 register to 0 (seconds) or 1 (minutes)
 *   Set UIE in Control_2 register to 1
 *   Test UF in Status register to check if it is set to 1.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "sys/clk.h"
#include "sys/rtc.h"
#include "sys/rv3028c7.h"

/* I am .. */
#define SELF RTC
#define this rtc

#define SIXTYFIVE_MILLISECONDS 65
#define tUPDATE SIXTYFIVE_MILLISECONDS /* [p.54] */
#define THREE_BYTES 3

typedef enum {
    IDLE = 0,
    /* a sequence to copy the working registers to the EEPROM */
    WAITING_WHILST_EEBUSY,
    READING_EEBUSY_BIT,
    READING_EEPROM_BACKUP,
    READING_EERD,
    WRITING_EERD,
    WRITING_FIRST_CMD,
    WRITING_EEPROM_BACKUP,
    WRITING_UPDATE_EEPROM,
    IN_UPDATE_DELAY,
    /* a sequence to enable / disable the Periodic Time Update Interrupt */
    READING_USEL_UIE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned pending : 1;
    unsigned periodic : 1;
    unsigned permin : 1;
    /* status, ctl_one and ctl_two must be contiguous
     * to allow the three bytes to be read as a group.
     */
    uchar_t status;
    uchar_t ctl_one;
    uchar_t ctl_two;
    uchar_t backup_reg;
    uchar_t eecmd;
    ProcNumber replyTo;
    ProcNumber periodic_replyTo;
    union {
        twi_info twi;
        clk_info clk;
    } info;
} rtc_t;

/* I have .. */
static rtc_t this;

/* I can .. */
#define enable_interrupt()    (EIMSK |= _BV(INT0))
#define disable_interrupt()   (EIMSK &= ~_BV(INT0))
PRIVATE void resume(void);
PRIVATE void handle_intr(void);

PUBLIC void config_rtc(void)
{
    /* configure the interrupt */
    PORTD |= _BV(PORTD2); /* soft pullup on INT0 input */
    EIFR |= _BV(INTF0);  /* set the interrupt flag to clear it */
    EICRA |= _BV(ISC01); /* configure falling edge detection [p.80] */
}

PUBLIC uchar_t receive_rtc(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
            this.replyTo = 0;
        }
        if (!this.state && this.pending)
            handle_intr();
        break;

    case UPDATE:
        if (this.state == IDLE) {
            this.replyTo = m_ptr->sender;
            this.state = WAITING_WHILST_EEBUSY;
            resume();
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    case RTC_INTR:
        if (this.state)
            this.pending = TRUE;
        else
            handle_intr();
        break;

    case SET_IOCTL:
        {
            switch (m_ptr->IOCTL) {
            case SIOC_PERIODIC_TIME_INTERRUPT:
                this.periodic = (m_ptr->LCOUNT & 0xFF) ? TRUE : FALSE;
                this.permin = (m_ptr->LCOUNT & 0xFF) == 2 ? TRUE : FALSE;
                this.periodic_replyTo = (m_ptr->LCOUNT >> 8) & 0xFF;
                this.replyTo = m_ptr->sender;
                this.state = READING_USEL_UIE;
                sae1_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_STATUS, &this.status, THREE_BYTES);
                break;

            default:
                send_REPLY_RESULT(m_ptr->sender, EINVAL);
                break;
            }
        }
        break;

    case INIT:
        send_REPLY_RESULT(m_ptr->sender, EOK);
        if ((PIND & _BV(PIND2)) == 0)
            handle_intr();
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

    case WAITING_WHILST_EEBUSY:
        this.state = READING_EEBUSY_BIT;
        sae1_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_STATUS, &this.status, THREE_BYTES);
        break;

    case READING_EEBUSY_BIT:
        if (this.status & RV_EEBUSY) {
            this.state = WAITING_WHILST_EEBUSY;
            sae_CLK_SET_ALARM(this.info.clk, tUPDATE);
        } else {
            this.state = READING_EEPROM_BACKUP;
            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                  RV_EEPROM_BACKUP, this.backup_reg);
        }
        break;

    case READING_EEPROM_BACKUP:
        this.state = READING_EERD;
        sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
             RV_CONTROL_1, this.ctl_one);
        break;

    case READING_EERD:
        this.state = WRITING_EERD;
        this.ctl_one |= RV_EERD;
        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
              RV_CONTROL_1, this.ctl_one);
        break;

    case WRITING_EERD:
        if (bit_is_set(this.backup_reg, RV_TCE)
              || bit_is_clear(this.backup_reg, RV_FEDE)) {
            this.state = WRITING_FIRST_CMD;
        } else {
            this.state = WRITING_EEPROM_BACKUP;
        }
        this.eecmd = RV_FIRST_CMD;
        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
              RV_EECMD, this.eecmd);
        break;

    case WRITING_FIRST_CMD:
        this.state = WRITING_EEPROM_BACKUP;
        this.backup_reg &= ~RV_TCE;
        this.backup_reg |= RV_FEDE;
        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                RV_EEPROM_BACKUP, this.backup_reg);
        break;

    case WRITING_EEPROM_BACKUP:
        this.state = WRITING_UPDATE_EEPROM;
        this.eecmd = RV_UPDATE_EEPROM;
        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
             RV_EECMD, this.eecmd);
        break;

    case WRITING_UPDATE_EEPROM:
        this.state = IN_UPDATE_DELAY;
        sae_CLK_SET_ALARM(this.info.clk, tUPDATE);
        break;

    case IN_UPDATE_DELAY:
        this.state = IDLE; /* RESETTING_EERD */
        this.ctl_one &= ~RV_EERD;
        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
             RV_CONTROL_1, this.ctl_one);
        break;

    case READING_USEL_UIE:
        this.state = IDLE;
        this.status &= ~RV_UF;
        if (this.permin) {
            this.ctl_one |= RV_USEL;
        } else {
            this.ctl_one &= ~RV_USEL;
        }
        if (this.periodic) {
            this.ctl_two |= RV_UIE;
        } else {
            this.ctl_two &= ~RV_UIE;
        }
        sae1_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
             RV_STATUS, &this.status, THREE_BYTES);
        enable_interrupt();
        break;
    }
}

PRIVATE void handle_intr(void)
{
    if (this.periodic_replyTo)
        send_PERIODIC_ALARM(this.periodic_replyTo, EOK);

    this.pending = FALSE;
    enable_interrupt();
}

/* -----------------------------------------------------
   Handle an INT0 interrupt.
   This appears as <__vector_1>: in the .lst file.
   -----------------------------------------------------*/
ISR(INT0_vect)
{
    /* respond only to a low level
     * disable further interrupts.
     */
    disable_interrupt();
    send_RTC_INTR(SELF);
}

/* end code */
