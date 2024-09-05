/* hc05/bcx.c */
 
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

/* A Bluetooth BC417 HC-05 manager.
 *
 * This driver manages three pins. Two inputs and one output.
 * PIND3 (#5) - input  low = disconnected, high = connected.
 * PIND4 (#6) - input  low = wake_up.
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
 *
 * There are applications where disabling the HC-05 would be inappropriate.
 * There should be the means to prevent the HC-05 from being disabled.
 * The application can add -DHC_05_ALWAYS_ON to CFLAGS. This permits the
 * removal of the wake_up button and the enable output, freeing-up two pins in
 * the process. The HC-05 STATUS is enhanced, as it is now constant, and not
 * subject to the HC-05 ENABLE condition. A saving of 248 code bytes too.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "hc05/hc05.h"
#include "hc05/bc4.h"

/* I am .. */
#define SELF HC05
#define this bc4

#define UNCONNECTED_TIMEOUT 30000L

#define HC05_STATUS     _BV(PIND3) /* (#5) same value as PCINT19 */
#define WAKE_UP         _BV(PIND4) /* (#6)  ..    ..  .. PCINT20 */
#define HC05_ENABLE     _BV(PIND5) /* (#11) */
#define HCO5_PCI        _BV(PCIE2)

typedef enum {
    IDLE = 0, /* disabled */
    DISCONNECTED,
    CONNECTED
} __attribute__ ((packed)) bc4_status;

typedef struct {
    bc4_status status;
    union {
        clk_info clk;
    } info;
} bc4_t;

/* I have .. */
static bc4_t this;

/* I can .. */
PRIVATE uchar_t is_connected(void);
PRIVATE uchar_t is_enabled(void);
PRIVATE void enable_bc4(void);
PRIVATE void disable_bc4(void);

PUBLIC void config_bc4(void)
{
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

    /* Pin D5 (#11) connects directly to the HC-05 ENABLE pin.
     * An active LOW disables the HC-05.
     * The pullup is enabled first, then the pin is changed to an output.
     * Both types are enabled when the pin is left floating.
     */
    PORTD |= HC05_ENABLE;
    DDRD |= HC05_ENABLE; /* aka PIND5 */

    if (!is_connected()) {
        disable_bc4();
        bc4.status = IDLE;
    } else {
        bc4.status = CONNECTED;
    }
}

PUBLIC uchar_t receive_bc4(message *m_ptr)
{
    long lv;
    switch (m_ptr->opcode) {
    case ALARM:
        if (!is_connected()) {
            disable_bc4();
            bc4.status = IDLE;
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
                disable_bc4();
                bc4.status = IDLE;
                send_REPLY_RESULT(m_ptr->sender, EOK);
                break;
            case HC05_POWERON:
                if (bc4.status == IDLE && !is_enabled()) {
                    enable_bc4();
                    sae_CLK_SET_ALARM(bc4.info.clk, UNCONNECTED_TIMEOUT);
                    bc4.status = DISCONNECTED;
                }
                if (m_ptr->sender != BC4) {
                    send_REPLY_RESULT(m_ptr->sender, EOK); /* always EOK */
                }
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

PRIVATE void enable_bc4(void)
{
    PORTD |= HC05_ENABLE;
    PCMSK2 &= ~WAKE_UP;
}

PRIVATE void disable_bc4(void)
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
            if (bc4.status == CONNECTED) {
                bc4.status = DISCONNECTED;
                sae_CLK_SET_ALARM(bc4.info.clk, UNCONNECTED_TIMEOUT);
            }
        } else {
            if (bc4.status == DISCONNECTED) {
                bc4.status = CONNECTED;
                sae_CLK_CANCEL(bc4.info.clk);
            }
        }
    }
}

/* end code */
