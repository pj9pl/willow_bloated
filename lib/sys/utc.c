/* sys/utc.c */
 
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

/* A universal time clock server.
 * Uses TIMER 2 in normal mode. [p.150]
 *
 * See also: AN2648 Selecting and Testing 32 KHz Crystal Oscillators
 *           for AVR Microcontrollers (DS00002648B).
 *
 * Pins B6 (#9) and B7 (#10) are connected to a 32.768 kHz watch crystal
 * with 18pF ceramic capacitors to ground (#8) [p.38-9,42].
 *
 * The prescaler divides by 128 which overflows TIMER2 at 1 second intervals.
 * Each interrupt increments this.uptime by 1.
 * The TCNT2 register provides a fractional value at 3.90625ms resolution. 
 *
 * To indicate current time, the boottime is added to the uptime.
 *
 * A current Unix timestamp is used to calculate the boottime from the uptime.
 *
 * To convert seconds since the epoch (1970-01-01) to a date:-
 * $ date --date='@1616702152'
 */

#include <time.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "sys/rtc.h"
#include "sys/rv3028c7.h"
#include "sys/utc.h"

/* I am .. */
#define SELF UTC
#define this utc

typedef enum {
    IDLE = 0,
    ENSLAVED,
    FETCHING_UNIXTIME
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    ulong_t uptime;
    time_t boottime;
    utc_msg sm;  /* service message */
    union {
        twi_info twi;
    } info;
} utc_t;

/* I have .. */
static utc_t this;

/* I can .. */
PRIVATE void set_txvar(twi_info *vp);
PRIVATE void get_request(void);

/* initialization */
PUBLIC void config_utc(void)
{
    PRR &= ~_BV(PRTIM2);
    /* Initialize TIMER2. */
    ASSR |= _BV(AS2);  /* external 32.768 kHz watch crystal [p.161,167-8] */
    TCCR2A = 0x00;     /* normal mode 0. [p.162-3] */
    /* prescaler set to divide by 128 [p.161,165-6] (every second) */
    TCCR2B = _BV(CS22) | _BV(CS20);
    TIMSK2 |= _BV(TOIE2);  /* enable overflow interrupt. [p.144-5] */
    TIFR2 |= _BV(TOV2); /* set the bit to clear the flag [p.145] */
}

PUBLIC uchar_t receive_utc(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        if (this.state == FETCHING_UNIXTIME) {
            if (m_ptr->RESULT == EOK) {
                GTCCR |= _BV(PSRASY);
                TCNT2 = 0;
                this.boottime -= this.uptime;
            }
            if (this.replyTo) {
                send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
                this.replyTo = 0;
            }
        }
        get_request();
        break;

    case INIT:
        /* dynamic initialization */
        if (this.state == IDLE) {
            this.state = FETCHING_UNIXTIME; 
            this.replyTo = m_ptr->sender;
            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_UNIX_TIME_0, this.boottime);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/* Direct access to the current time. */
PUBLIC time_t get_utc(void)
{
    cli();
    time_t now = this.boottime + this.uptime;
    sei();
    return now;
}

/* st_callback function.
 * This is called from the TWI driver in the interrupt context when the mode
 * switches from SR to ST, to initialize the transmit pointer and count when
 * the remote master has issued a repeated start to switch from MT to MR mode.
 *
 * It is called from tw_st_arb_lost_sla_ack()
 * which itself is called from tw_st_sla_ack().
 */ 
PRIVATE void set_txvar(twi_info *ip)
{
    uchar_t cSREG = SREG;
    cli();
    uchar_t frac = TCNT2;
    ulong_t ticks = this.uptime;
    SREG = cSREG;
    uchar_t op = this.sm.request.op;
    this.sm.reply.result = EOK;

    switch (op) {
    case SET_TIME:
        GTCCR |= _BV(PSRASY);
        /* 3.9ms compensation for the delay incurred by remote client. */
        TCNT2 = 1;
        /* record the time at which the clock was reset */
        this.uptime = 0;
        this.boottime = this.sm.request.val;
        this.sm.reply.frac = 0;
        break;

    case GET_TIME:
        this.sm.reply.val = this.boottime + ticks;
        this.sm.reply.frac = frac;
        break;

    case GET_UPTIME:
        this.sm.reply.val = ticks;
        this.sm.reply.frac = frac;
        break;

    case GET_BOOTTIME:
        this.sm.reply.val = this.boottime;
        this.sm.reply.frac = 0;
        break;

    default:
        this.sm.reply.result = ENOSYS;
        break;
    }

    ip->tptr = (uchar_t *)&this.sm;
    ip->tcnt = sizeof(this.sm);
}

/* -----------------------------------------------------
   Handle a Timer 2 Overflow interrupt.
   This appears as <__vector_9>: in the .lst file.
   -----------------------------------------------------*/
ISR(TIMER2_OVF_vect)
{
    this.uptime++;
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SRST(this.info.twi, UTC_REQUEST, this.sm, (Callback) set_txvar);
}

/* end code */
