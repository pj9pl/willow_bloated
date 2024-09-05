/* hc05/bcy.c */
 
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

/* A Bluetooth ZS_040 BC352 HC-05 manager.
 *
 * This driver manages four pins:-
 * PIND2 (#4) - tristate  low/Hi-Z = transparent UART, high = AT_MODE.
 *              reading the pin value during tristate might differentiate
 *              between the BC352 (low) and the BC417 (high) adapters.
 *
 * PIND3 (#5) - input  low = disconnected, high = connected.
 *
 * PIND4 (#6) - input  low = wake_up.
 *
 * PIND5 (#11) - output  high == enabled, low == disabled.
 *
 * Accepts status requests and produces a long reply containing the
 * enabled and connected status as bit 3 (connected) and bit 5 (enabled).
 *
 * HC-05 adapters are a mixed bunch. Those that feature a BC352 don't respond
 * to a low level on the ENABLE input, whereas those that feature a BC417 do.
 *
 * The current solution for BC352 adapters involves an external +5v VDD switch
 * using an IRF9530, a 2N7000 and a 33k resistor.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "hc05/hc05.h"
#include "hc05/bcy.h"

/* I am .. */
#define SELF HC05
#define this bcy

#define UNPAIRED_TIMEOUT 30000L

#define HC05_KEY        _BV(PIND2) /* (#4) tristate */
#define HC05_STATUS     _BV(PIND3) /* (#5) same value as PCINT19 */
#define WAKE_UP         _BV(PIND4) /* (#6)  ..    ..  .. PCINT20 */
#define HC05_ENABLE     _BV(PIND5) /* (#11) */
#define HCO5_PCI        _BV(PCIE2) /* the third pinchange byte: PIND */

typedef enum {
    IDLE = 0, /* disabled */
    DISCONNECTED,
    CONNECTED
} __attribute__ ((packed)) bcy_status;

typedef struct {
    bcy_status status;
    union {
        clk_info clk;
    } info;
} bcy_t;

/* I have .. */
static bcy_t this;

/* I can .. */
PRIVATE uchar_t is_connected(void);
PRIVATE uchar_t is_enabled(void);
PRIVATE void enable_bcy(void);
PRIVATE void disable_bcy(void);

PUBLIC void config_bcy(void)
{
    /* Pin D2 (#4) connects to the HC-05_KEY pin.
     * An error in the silkscreen marks it as EN.
     * Default to low-Z low output..
     */
    PORTD &= ~HC05_KEY;
    DDRD |= HC05_KEY;

    /* Pin D3 (#5) connects to the HC-05 STATUS pin.
     * Configure a pin change interrupt [p.82] on PCINT19.
     * Note that PCINT19 == PIND3 == 3.
     */
    PCIFR |= HCO5_PCI;     /* set it to clear it */
    PCICR |= HCO5_PCI;
    PCMSK2 |= HC05_STATUS;

    /* Pin D4 (#6) connects to the WAKE_UP button.
     * The PCINT19 bit in PCMSK2 is set and cleared during execution. 
     * Enable the pullup. 
     */
    PORTD |= WAKE_UP;

    /* Pin D5 (#11) connects to the +5v VDD switch.
     * An active LOW disables the HC-05.
     * The pullup is enabled first, then the pin is changed to an output.
     * Both types are enabled when the pin is left floating.
     */
    PORTD |= HC05_ENABLE;
    DDRD |= HC05_ENABLE; /* aka PIND5 */

    if (!is_connected()) {
        if (BL_PIN & _BV(BL))
            disable_bcy();
        this.status = IDLE;
    } else {
        this.status = CONNECTED;
    }
}

PUBLIC uchar_t receive_bcy(message *m_ptr)
{
    long lv;
    switch (m_ptr->opcode) {
    case ALARM:
        if (!is_connected()) {
            disable_bcy();
            this.status = IDLE;
        }
        break;

    case SET_IOCTL:
        switch (m_ptr->IOCTL) {
        case SIOC_HC05_COMMAND:
            switch (m_ptr->LCOUNT) {
            case HC05_ENQUIRE:
                lv = is_enabled() | is_connected(); 
                send_REPLY_DATA(m_ptr->sender, EOK, lv);
                break;

            case HC05_POWEROFF:
                disable_bcy();
                this.status = IDLE;
                send_REPLY_RESULT(m_ptr->sender, EOK);
                break;

            case HC05_POWERON:
                if (this.status == IDLE && !is_enabled()) {
                    enable_bcy();
                    sae_CLK_SET_ALARM(this.info.clk, UNPAIRED_TIMEOUT);
                    this.status = DISCONNECTED;
                }
                if (m_ptr->sender != SELF) {
                    send_REPLY_RESULT(m_ptr->sender, EOK); /* always EOK */
                }
                break;

            case SET_KEY:
                /* Low-Z high output */
                PORTD |= HC05_KEY;
                DDRD |= HC05_KEY;
                send_REPLY_RESULT(m_ptr->sender, EOK);
                break;

            case CLEAR_KEY:
                /* Low-Z low output */
                PORTD &= ~HC05_KEY;
                DDRD |= HC05_KEY;
                send_REPLY_RESULT(m_ptr->sender, EOK);
                break;

            case HIZ_KEY:
                /* Hi-Z input */
                DDRD &= ~HC05_KEY;
                PORTD &= ~HC05_KEY;
                send_REPLY_RESULT(m_ptr->sender, EOK);
                break;
                
            default:
                send_REPLY_RESULT(m_ptr->sender, EBUSY);
                break;
            }
            break;
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE uchar_t is_connected(void)
{
    return (PIND & HC05_STATUS) ? TRUE : FALSE;
}

PRIVATE uchar_t is_enabled(void)
{
    return (PIND & HC05_ENABLE) ? TRUE : FALSE;
}

PRIVATE void enable_bcy(void)
{
    PORTD |= HC05_ENABLE;
    PCMSK2 &= ~WAKE_UP;
}

PRIVATE void disable_bcy(void)
{
    PORTD &= ~HC05_ENABLE;
    PCIFR |= HCO5_PCI;
    PCMSK2 |= WAKE_UP;
}

/* -----------------------------------------------------
   Handle a PCINT2 interrupt.
   This appears as <__vector_5>: in the .lst file.

   Pin change interrupts occur when the HC-05 STATE output changes,
   or the wake_up button has been pressed.
   -----------------------------------------------------*/
ISR(PCINT2_vect)
{
    if ((PCMSK2 & HC05_STATUS) && (PIND & WAKE_UP) == 0) { 
        PCMSK2 &= ~WAKE_UP; /* disable interrupt to debounce */
        send_SET_IOCTL(SELF, SIOC_HC05_COMMAND, HC05_POWERON);
    } else {
        /* HC-05_STATE */
        if (!is_connected()) {
            if (this.status == CONNECTED) {
                this.status = DISCONNECTED;
                sae_CLK_SET_ALARM(this.info.clk, UNPAIRED_TIMEOUT);
            }
        } else {
            if (this.status == DISCONNECTED) {
                this.status = CONNECTED;
                sae_CLK_CANCEL(this.info.clk);
            }
        }
    }
}

/* end code */
