/* oled/vespa.c */

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

/* An SPI driver for the Velleman VMA437 OLED display.
 * This uses a 4-wire SPI interface as described in [SH1106.pdf p.10].
 *
 * The signals in [SH1106.pdf p.10] Figure 2 correspond as follows:-
 *   RES_BIT  -  /CS
 *   MOSI_BIT -   SI  (D1)
 *   SCK_BIT  -   SCL (D0)
 *   DC_BIT   -   A0
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "oled/vespa.h"

/* I am .. */
#define SELF VESPA
#define this vespa

#define SCK_BIT  _BV(PINB5)  /* mpu clock out */
#define MISO_BIT _BV(PINB4)  /* not connected */
#define MOSI_BIT _BV(PINB3)  /* mpu data out */
#define CS_BIT   _BV(PINB2)  /* unselected = 1, selected = 0 */ 
#define DC_BIT   _BV(PINB1)  /* data = 1, command = 0 */
#define RES_BIT  _BV(PINB0)  /* operating = 1, reset = 0 */

/* power-on reset pulse duration */
#define TEN_MICROSECONDS 10.0 /* minimum */
#define TWENTY_MICROSECONDS 20.0 /* plenty */
#define POR_DURATION TWENTY_MICROSECONDS

typedef struct {
    vespa_info *headp;
} vespa_t;

/* I have .. */
static vespa_t this;

/* I can .. */
PRIVATE void start_job(void);

PUBLIC void config_vespa(void)
{
    /* The CS_BIT and the RES_BIT control pins are active low, so initialized
     * to a high (inactive) condition..
     * The DC_BIT is initialized to low output (control).
     * At power-on, the RES_BIT should be held low for POR_DURATION (10us)
     * before setting it high. 
     *
     * On peru there is a 20k pulldown resistor between pin #14 (RES_BIT)
     * and ground to ensure a low level whilst the pin is undriven.
     */
    PORTB |= SCK_BIT | MISO_BIT | MOSI_BIT | CS_BIT;
    DDRB  |= SCK_BIT | MOSI_BIT | CS_BIT | DC_BIT | RES_BIT;
    _delay_us(POR_DURATION);
    PORTB |= RES_BIT;

    /* Configure the SPI [ATmega328P: p.174-7]
     * MODE 3
     * DORD is zero == MSB first
     * SPR1 and SPR0 = 0, SPI2X = 1 == F_CPU / 2 == 4MHz [p.177] 
     */
    PRR &= ~_BV(PRSPI);
    SPCR = _BV(SPIE) | _BV(SPE) | _BV(MSTR) | _BV(CPOL) | _BV(CPHA);
    SPSR |= _BV(SPI2X);
}

PUBLIC uchar_t receive_vespa(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_BUSY:
    case REPLY_RESULT:
        if (this.headp) {
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL)
                start_job();
        }
        break;

    case JOB:
        {
            vespa_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                vespa_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    if (this.headp->cnt) {
        if (this.headp->dc) {
            PORTB &= ~CS_BIT;
        } else {
            PORTB &= ~(CS_BIT | DC_BIT);
        }
        this.headp->cnt--;
        SPDR = *this.headp->bp++;
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

ISR(SPI_STC_vect)
{
    if (this.headp->cnt) {
        this.headp->cnt--;
        SPDR = *this.headp->bp++;
    } else {
        PORTB |= CS_BIT | DC_BIT;
        send_NOT_BUSY(SELF);
    }
}

/* end code */
