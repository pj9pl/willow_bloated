/* hc05/bc4.c */
 
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

/* The BC4 task is a network secretary that manages a BC417 HC-05 Bluetooth
 * adapter. It provides a HC05 client interface.
 *
 * It accepts three CLI commands:-
 *  - hc05 <host> on      - enable adapter
 *  - hc05 <host> off     - disable adapter
 *  - hc05 <host>         - status: 32 = enabled; 40 = enabled + connected
 *
 * A Bluetooth HC-05 manager for BC417 devices.
 *
 * This driver manages three pins. Two inputs and one output.
 * PIND3 (#5) - input  low = disconnected, high = connected.
 * PIND4 (#6) - input  low = wake_up.
 * PIND5 (#11) - input/output  hi-Z input = enabled, low output = disabled.
 *
 * Accepts SET_IOCTL request to enable from within the interrupt context.
 *
 * There are two types of HC-05 adapters in use:-
 *    BC352 type ID 98:D3:xx:xx:xx:xx
 *    BC417 type ID 00:21:xx:xx:xx:xx
 *
 * The main difference being the ENABLE input (PORTD5, #11).
 * When it is HIGH or hi-Z, both types provide a transparent UART.
 * When it is LOW, the BC352 is placed in command mode, whereas the BC417 is
 * powered down.
 *
 * It would appear that when the ENABLE pin is hi-Z, Pin D5 (#11) can be read
 * as LOW for a BC352 and HIGH for a BC417.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/services.h"
#include "hc05/hc05.h"
#include "hc05/bc4.h"

/* I am .. */
#define SELF HC05
#define this bc4

#define HC05_STATUS _BV(PIND3) /* PCINT19 */
#define HC05_WAKEUP _BV(PIND4) /* PCINT20 */
#define HC05_ENABLE _BV(PIND5)

#define THIRTY_SECONDS 30000
#define UNCONNECTED_TIMEOUT THIRTY_SECONDS

typedef enum {
    IDLE = 0, /* disabled */
    SENDING_REPLY,
    ENSLAVED
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned alarm_pending : 1;
    hc05_msg sm; /* service message */
    clk_info clk;
    union {
        twi_info twi;
    } info;
} bc4_t;

/* I have .. */
static bc4_t this;

/* I can .. */
PRIVATE void exec_request(void);
PRIVATE void resume(void);
PRIVATE uchar_t is_connected(void);
PRIVATE uchar_t is_enabled(void);
PRIVATE void enable_bc4(void);
PRIVATE void disable_bc4(void);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC void config_bc4(void)
{
    /* Pin D3 (#5) connects to the HC-05 STATUS pin.
     * Configure a pin change interrupt [p.82] on PCINT19.
     * Note that PCINT19 == PIND3 == 3.
     */
    PCICR |= _BV(PCIE2);
    PCIFR |= _BV(PCIF2);     /* set it to clear it */
    PCMSK2 |= HC05_STATUS;

    /* Pin D4 (#6) connects to the WAKE_UP button.
     * The PCINT20 bit in PCMSK2 is set and cleared during execution. 
     * Enable the pullup. 
     */
    PORTD |= HC05_WAKEUP;

    /* Pin D5 (#11) connects directly to the HC-05 ENABLE pin.
     * A hi-Z input (no pullup) enables the HC-05: no config required.
     * An active LOW disables the HC-05.
     */

    /* disable if not already connected */
    if (!is_connected()) {
        disable_bc4();
    }
}

PUBLIC uchar_t receive_bc4(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
        this.alarm_pending = FALSE;
        if (!is_connected()) {
            disable_bc4();
        }
        break;

    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state == ENSLAVED && m_ptr->sender == TWI) {
            if (m_ptr->RESULT == EOK) {
                exec_request();
            } else {
                get_request();
            }
        } else if (m_ptr->sender == CLK) {
            /* reply from a CANCEL request */
            this.alarm_pending = FALSE;
        } else if (this.state) {
            resume();
        }
        break;

    case SET_IOCTL:
        switch (m_ptr->IOCTL) {
        case SIOC_HC05_COMMAND:
            switch (m_ptr->LCOUNT) {
            case HC05_POWEROFF:
                disable_bc4();
                send_REPLY_RESULT(m_ptr->sender, EOK);
                break;

            case HC05_POWERON:
                if (!is_enabled()) {
                    enable_bc4();
                    if (this.alarm_pending == FALSE) {
                        this.alarm_pending = TRUE;
                        sae_CLK_SET_ALARM(this.clk, UNCONNECTED_TIMEOUT);
                    }
                }
                if (m_ptr->sender != SELF)
                    send_REPLY_RESULT(m_ptr->sender, EOK);
                break;

            default:
                send_REPLY_RESULT(m_ptr->sender, EINVAL);
                break;
            }
            break;

        default:
            send_REPLY_RESULT(m_ptr->sender, EINVAL);
            break;
        }
        break;

    case INIT:
        {
            uchar_t result = EBUSY;
            if (this.state == IDLE) {
                get_request();
                result = EOK;
            }
            send_REPLY_RESULT(m_ptr->sender, result);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void exec_request(void)
{
    switch (this.sm.request.op) {
    case HC05_POWEROFF:
        disable_bc4();
        send_reply(EOK);
        break;

    case HC05_POWERON:
        if (!is_enabled()) {
            enable_bc4();
            if (this.alarm_pending == FALSE) {
                this.alarm_pending = TRUE;
                sae_CLK_SET_ALARM(this.clk, UNCONNECTED_TIMEOUT);
            }
        }
        send_reply(EOK);
        break;

    case HC05_ENQUIRE:
        this.sm.reply.val = is_enabled() | is_connected();
        send_reply(EOK);
        break;

    default:
        send_reply(ENOSYS);
        break;
    }
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
    case ENSLAVED:
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE uchar_t is_connected(void)
{
    return (PIND & HC05_STATUS);
}

PRIVATE uchar_t is_enabled(void)
{
    return ((DDRD & HC05_ENABLE) == 0);
}

PRIVATE void enable_bc4(void)
{
    DDRD &= ~HC05_ENABLE;
    PCMSK2 &= ~HC05_WAKEUP;
}

PRIVATE void disable_bc4(void)
{
    DDRD |= HC05_ENABLE;
    PCIFR |= _BV(PCIF2);
    PCMSK2 |= HC05_WAKEUP;
}

/* -----------------------------------------------------
   Handle a PCINT2 interrupt.
   This appears as <__vector_5>: in the .lst file.

   Pin change interrupts occur when the HC-05 STATE output changes,
   or the wake_up button has been pressed.
   -----------------------------------------------------*/
ISR(PCINT2_vect)
{
    if ((PCMSK2 & HC05_WAKEUP) && (PIND & HC05_WAKEUP) == 0) { 
        /* wake_up button */
        send_SET_IOCTL(SELF, SIOC_HC05_COMMAND, HC05_POWERON);
        PCMSK2 &= ~HC05_WAKEUP; /* disable interrupt to debounce */
    } else {
        /* HC-05_STATE */
        if (!is_connected()) {
            if (this.alarm_pending == FALSE) {
                this.alarm_pending = TRUE;
                sae_CLK_SET_ALARM(this.clk, UNCONNECTED_TIMEOUT);
            }
        } else {
            if (this.alarm_pending == TRUE) {
                sae_CLK_CANCEL(this.clk);
            }
        }
    }
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, HC05_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, HC05_REPLY, this.sm.reply);
}


/* end code */
