/* oslo/sysinit.c */

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
    UTC,
    MOUNT,
    RWR,
    FSD,
    MEMZ,
    MEMP,
    ISTREAM
};

/* I can .. */
PRIVATE void resume(void);

/* Static initialization */
PUBLIC void config_sysinit(void)
{
    /* Oslo pinout:-

                         -------U-------
             EXT_RST -> -|RESET      C5|- <-> TWI_SCL
                    n/c -|D0         C4|- <-> TWI_SDA
                    n/c -|D1         C3|- n/c
                    n/c -|D2         C2|- n/c
                    n/c -|D3         C1|- n/c
                    n/c -|D4         A0|- <- BATTERY_FRACTION 1/6.75
                        -|VCC       GND|-
                        -|GND      AREF|-
          WATCH_XTAL -> -|B6       AVCC|-
          WATCH_XTAL <- -|B7         B5|- -> white  SD_CARD_SCK
                    n/c -|D5         B4|- <- brown  SD_CARD_MISO
   BOOTLOADER_SWITCH -> -|D6         B3|- -> blue   SD_CARD_MOSI
             EXT_RST <- -|D7         B2|- -> grey   SD_CARD_SS
                    n/c -|B0         B1|- n/c
                         ---------------
     */

    /* enable pullup on bootloader switch */
    BL_PORT |= _BV(BL);

    /* Disable the digital buffer for pin C0.
     * digital buffers [p.260] should be disabled on a per host basis,
     * in the host's sysinit.c in the init_sysinit() function,
     * not in the generic sys/adcn.c driver.
     */
    DIDR0 = _BV(ADC0D);

    /* Apply pullups to unconnected pins.
     * PB0, PB1 --- PC1, PC2, PC3 --- PD2
     * #14  #15 --- #24  #25  #26 ---  #4
     */
    PORTB |= _BV(PORTB1) | _BV(PORTB0);
    PORTC |= _BV(PORTC3) | _BV(PORTC2) | _BV(PORTC1);
    PORTD |= _BV(PORTD5) | _BV(PORTD4) |
             _BV(PORTD3) | _BV(PORTD2) | _BV(PORTD1) | _BV(PORTD0);

    /* Apply pullups to SCL and SDA on a per-host basis. Not everywhere. */
    //PORTC |= _BV(PORTC5) | _BV(PORTC4);

    /* Disable peripherals. Enable as required.
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
    case ALARM:
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
