/* sumo/sysinit.c */

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

/* This module configures the hardware, i.e. switch all the peripherals off
 * and activate soft pullups, prior to allowing each module to apply their own
 * configuration.
 *
 * When interrupts have been enabled this module receives the first message and
 * performs the dynamic initialization sequence which populates the TWI pool
 * with the various secretaries.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/sysinit.h"

/* I am .. */
#define SELF SYSINIT
#define this sysinit

typedef struct {
    uchar_t state;
    ProcNumber replyTo;
    ulong_t init_errs;
} sysinit_t;

/* I have .. */
static sysinit_t this;

/* These tasks receive an INIT message at start-up */
static const ProcNumber __flash inittab[] = {
    SYSCON,
    MEMZ,
    MEMP,
    ISTREAM,
    DATEZ,
    VOLTAGEZ,
    TEMPERATUREZ,
    PRESSUREZ,
    BATTERYZ,
    BARZ
};

/* I can .. */
PRIVATE void resume(void);

/* public function to configure this module during the initial
 * stage prior to interrupts being enabled.
 */ 
PUBLIC void config_sysinit(void)
{
    /* There are no unused pins on sumo.

                         -------U-------
             EXT_RST -> -|RESET      C5|- <-> TWI_SCL
             HC05_TX -> -|D0         C4|- <-> TWI_SDA
             HC05_RX <- -|D1         C3|- -> PLCD_BACKLIGHT
          PLCD_POWER <- -|D2         C2|- -> PLCD_E
         HC05_STATUS -> -|D3         C1|- -> PLCD_RW
         HC05_WAKEUP -> -|D4         C0|- -> PLCD_RS
                        -|VCC       GND|-
                        -|GND      AREF|-
            PLCD_D6 <-> -|B6       AVCC|-
            PLCD_D7 <-> -|B7         B5|- <-> PLCD_D5
         HC05_ENABLE <- -|D5         B4|- <-> PLCD_D4
   BOOTLOADER_SWITCH -> -|D6         B3|- <-> PLCD_D3
             EXT_RST <- -|D7         B2|- <-> PLCD_D2
            PLCD_D0 <-> -|B0         B1|- <-> PLCD_D1
                         ---------------
     */

    /* enable pullup on bootloader switch */
    BL_PORT |= _BV(BL);

    /* Disable all peripherals.
     * They are subsequently enabled by the drivers that use them.
     * Set the bit to power-down the peripheral. 
     * Clear the bit to power-up the peripheral. 
     * TWI, TIMER2, TIMER0, -, TIMER1, SPI, USART, ADC.
     */
    PRR |= _BV(PRTWI) | _BV(PRTIM2) | _BV(PRTIM0) |
           _BV(PRTIM1) | _BV(PRSPI) | _BV(PRUSART0) | _BV(PRADC);
}

PUBLIC uchar_t receive_sysinit(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_RESULT:
        if (this.state) {
            if (m_ptr->RESULT != EOK) {
                this.init_errs |= _BV(this.state -1);
            }
            resume();
        } else if (this.replyTo) {
            send_REPLY_RESULT(this.replyTo, EOK);
            this.replyTo = 0;
        }
        break;

    case INIT:
        this.replyTo = m_ptr->sender;
        resume();
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    if (this.state < sizeof(inittab) / sizeof(*inittab)) {
        ProcNumber task = (ProcNumber) pgm_read_byte_near(inittab + this.state);
        send_INIT(task);
        this.state++;
    } else {
        this.state = 0;
    }
}

/* end code */
