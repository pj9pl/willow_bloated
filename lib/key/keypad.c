/* key/keypad.c */

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

/* A cluster of eight TTP223-BA6 touch switches.
 * Active high.
 *
 *    ---------      ---------      ---------      ---------
 *    |       |      |       |      |       |      |       |
 *    |   1   |      |   3   |      |   5   |      |   7   |
 *    |       |      |       |      |       |      |       |
 *    ---------      ---------      ---------      ---------
 *
 *    ---------      ---------      ---------      ---------
 *    |       |      |       |      |       |      |       |
 *    |   2   |      |   4   |      |   6   |      |   8   |
 *    |       |      |       |      |       |      |       |
 *    ---------      ---------      ---------      ---------
 *
 *     8      7      6      5      4      3      2      1
 * ---------------------------------------------------------
 * |  B7  |  B6  |      |      |      |  B2  |  B1  |  B0  |
 * ---------------------------------------------------------
 *
 * ---------------------------------------------------------
 * |      |      |  D5  |  D4  |  D3  |      |      |      |
 * ---------------------------------------------------------
 *   bit7                                             bit0
 *
 * The soft pullups caused spurious button down events, and physical 22k
 * pulldowns caused button 8 to become unresponsive.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "key/keypad.h"

/* I am .. */
#define SELF KEYPAD
#define this keypad

#define NR_BUTTONS 8

#define TPINS (PINB & (_BV(PINB7) | _BV(PINB6) | \
                       _BV(PINB2) | _BV(PINB1) | _BV(PINB0))) \
            | (PIND & (_BV(PIND5) | _BV(PIND4) | _BV(PIND3)))

typedef struct {
    uchar_t curval;
    uchar_t diff;
    uchar_t intr_b;
    uchar_t intr_d;
    uchar_t dn[NR_BUTTONS];
    uchar_t up[NR_BUTTONS];
} keypad_t;

/* I have .. */
static keypad_t this;

/* I can .. */

PUBLIC void config_keypad(void)
{
    PCMSK2 = _BV(PCINT21) | _BV(PCINT20) | _BV(PCINT19);
    PCMSK0 = _BV(PCINT7) | _BV(PCINT6) | _BV(PCINT2) |
                                _BV(PCINT1) | _BV(PCINT0);
    PCIFR |= _BV(PCIF2) | _BV(PCIF0);
    PCICR |= _BV(PCIE2) | _BV(PCIE0);
    this.curval = TPINS;
}

PUBLIC uchar_t receive_keypad(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case BUTTON_CHANGE:
        {
            this.diff = this.curval ^ m_ptr->mtype;
            this.curval = m_ptr->mtype;

            uchar_t cv = this.curval;
            uchar_t dv = this.diff;

            for (int i = 0; i < 8; i++) { 
                if (dv & 0x01) {
                    if (cv & 0x01) {
                        if (this.dn[i]) {
                            send_BUTTON_CHANGE(this.dn[i], i);
                            this.dn[i] = 0;
                        }
                    } else {
                        if (this.up[i]) {
                            send_BUTTON_CHANGE(this.up[i], i | 0x08);
                            this.up[i] = 0;
                        }
                    }
                }
                dv >>= 1;
                cv >>= 1;
            }
        }
        break;

    case READ_BUTTON:
        /* 0..7 = button down; 8..15 = button up */
        /* 0..15 = set, 16..31 = reset */
        if (m_ptr->mtype & RST_BUTTON) {
            if (m_ptr->mtype & BUTTON_UP)
                this.up[m_ptr->mtype & BUTTON_MASK] = 0;
            else
                this.dn[m_ptr->mtype & BUTTON_MASK] = 0;
            send_BUTTON_CHANGE(m_ptr->sender, m_ptr->mtype);
        } else {
            /* install the sender as the recipient of the subsequent message */
            if (m_ptr->mtype & BUTTON_UP)
                this.up[m_ptr->mtype & BUTTON_MASK] = m_ptr->sender;
            else
                this.dn[m_ptr->mtype & BUTTON_MASK] = m_ptr->sender;
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/* -----------------------------------------------------
   Handle a pinchange 2 interrupt.
   This appears as <__vector_5>: in the .lst file.
   -----------------------------------------------------*/
ISR(PCINT2_vect)
{
    this.intr_d++;
    send_BUTTON_CHANGE(SELF, TPINS);
}

/* -----------------------------------------------------
   Handle a pinchange 0 interrupt.
   This appears as <__vector_3>: in the .lst file.
   -----------------------------------------------------*/
ISR(PCINT0_vect)
{
    this.intr_b++;
    send_BUTTON_CHANGE(SELF, TPINS);
}

/* end code */
