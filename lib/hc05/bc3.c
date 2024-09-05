/* hc05/bc3.c */
 
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

/* A Bluetooth BC352 HC-05 manager.
 *
 * This driver manages one pin. One input.
 * PIND3 (#5) - input  low = disconnected, high = connected.
 * PIND5 (#11) - tristate = transparent UART
 *               high = BC352: AT_MODE.
 *                      BC417: enabled
 *

 *               low  = BC352: transparent UART
 *                      BC417: disabled
 *
 *               reading the pin value during tristate can differentiate
 *               between the BC352 (low) and the BC417 (high) adapters.
 *
 * Accepts status requests and produces a long reply containing the
 * enabled and connected status as bit 3 (connected) and bit 5 (enabled).
 *
 * The four HC-05 adapters are of two types: BC352 and BC417.
 *   HOST          HC05ID        BOARD    CHIP      EN_M        VERSION
 *   pisa    00:21:13:05:65:D9     -      BC417    VREG_EN    2.0-20100601
 *   bali    00:21:13:05:74:1E     -      BC417    VREG_EN    2.0-20100601
 *   goat    98:D3:A1:F5:CD:D0   ZS_040   BC352      KEY      3.0-20170601
 *   jira    98:D3:71:F5:EF:91   ZS_040   BC352      KEY      3.0-20170601
 *
 * The ZS_040 EN input is actually a KEY input, where a high level will enter
 * AT mode, and a low level exits AT mode. The BC352 doesn't have a VREG_EN
 * input, whereas those that feature a BC417 do.
 *
 * The difference is with the EN input. When it is undriven, it defaults to
 * transparent UART. When it is driven high, The BC352 is placed in AT mode,
 * whereas the BC417 is placed in transparent UART mode. When EN is driven
 * low, the BC352 is disabled, whereas the BC417 is disabled.
 *
 * When EN is undriven, a ZS_040 floats low, whereas a BC417 floats high.
 * Reading PIND5 (#11) can identify the which type is connected.
 *
 * A BC352 is used with Goat where the HC-05 is always enabled, pins 1 and 6
 * of the HC-O5 are not connected.
 *
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "hc05/hc05.h"
#include "hc05/bc3.h"

/* I am .. */
#define SELF HC05 
#define this bc3

#define is_connected() (PIND & _BV(PIND3))
#define HC05_KEY        _BV(PIND5) /* (#11) tristate */

typedef enum {
    IDLE = 0, /* unset */
    DISCONNECTED,
    CONNECTED
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    uchar_t adapter_type;
} bc3_t;

/* I have .. */
static bc3_t this;

/* I can .. */

PUBLIC void config_bc3(void)
{
    /* Pin D3 (#5) connects to the HC-05 STATUS pin.
     * Configure a pin change interrupt [p.82] on PCINT19.
     * Note that PCINT19 == PIND3 == 3.
     */
    PCICR |= _BV(PCIE2);
    PCIFR |= _BV(PCIF2);     /* set it to clear it */
    PCMSK2 |= _BV(PCINT19);
    this.adapter_type = PIND & _BV(PIND5);
    this.state = is_connected() ? CONNECTED : DISCONNECTED;
}

PUBLIC uchar_t receive_bc3(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case SET_IOCTL:
        {
            uchar_t ret = EINVAL;
            switch (m_ptr->IOCTL) {
            case SIOC_HC05_COMMAND:
                switch (m_ptr->LCOUNT) {
                case SET_KEY:
                    /* Low-Z high output */
                    PORTD |= HC05_KEY;
                    DDRD |= HC05_KEY;
                    ret = EOK;
                    break;

                case CLEAR_KEY:
                    /* Low-Z low output */
                    PORTD &= ~HC05_KEY;
                    DDRD |= HC05_KEY;
                    ret = EOK;
                    break;

                case HIZ_KEY:
                    /* Hi-Z input */
                    DDRD &= ~HC05_KEY;
                    PORTD &= ~HC05_KEY;
                    ret = EOK;
                    break;
                }
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
        }
        break;

    case GET_IOCTL:
        {
            uchar_t ret = EINVAL;
            long lv = 0;
            switch (m_ptr->IOCTL) {
            case SIOC_HC05_COMMAND:
                switch (m_ptr->LCOUNT) {
                case HC05_ENQUIRE:
                    /* current bit 3 OR probed bit 5: 0x0, 0x04, 0x20, 0x24 */
                    lv = is_connected() | this.adapter_type;
                    ret = EOK;
                    break;
                }
            }
            send_REPLY_DATA(m_ptr->sender, ret, lv);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/* -----------------------------------------------------
   Handle a PCINT2 interrupt.
   This appears as <__vector_5>: in the .lst file.
   Pin change interrupts occur when the HC-05 STATE output changes.
   -----------------------------------------------------*/
ISR(PCINT2_vect)
{
    /* HC-05_STATE */
    this.state = is_connected() ? CONNECTED : DISCONNECTED;
}

/* end code */
