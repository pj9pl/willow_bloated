/* sys/adcn.c */

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

/* The ADCN is an interface with the native analogue to digital convertor.
 *    [p.246-261]
 *
 * Its task is to:-
 *           receive a request specifying the reference and the channel
 *           power-up the native ADC
 *           wait for the ADC to warm up
 *           start a conversion
 *           handle the interrupt: read the value, power-down the ADC
 *           send the reply with the value in the info
 *
 * The digital buffers should be disabled on a per host basis,
 * in the [app]/sysinit.c file, in the config_sysinit() function,
 * according to need, and not here in the generic driver.
 *
 * For example, to disable PORTC0/PINC0: DIDR0 = _BV(ADC0D);
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "sys/adcn.h"

/* I am .. */
#define SELF ADCN
#define this adcn

/* The ADC needs time to warm up. */
#define TWOFIFTY_MILLISECONDS 250
#define POWER_UP_DELAY TWOFIFTY_MILLISECONDS

typedef enum {
    IDLE = 0,
    AWAITING_ALARM
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    adcn_info *headp;
    union {
        clk_info clk;
    } info;
} adcn_t;

/* I have .. */
static adcn_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void enable_adcn(void);
PRIVATE void disable_adcn(void);
PRIVATE void start_conversion(void);

PUBLIC uchar_t receive_adcn(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case EOC:
    case ALARM:
    case REPLY_INFO:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL)
                    start_job();
            }
        }
        break;

    case JOB:
        {
            adcn_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                adcn_info *tp;
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
    this.state = AWAITING_ALARM;
    ADMUX = this.headp->admux;
    enable_adcn();
    sae_CLK_SET_ALARM(this.info.clk, POWER_UP_DELAY);
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case AWAITING_ALARM:
        this.state = IDLE;
        start_conversion();
        break;
    }
}

PRIVATE void enable_adcn(void)
{
    PRR &= ~_BV(PRADC); /* power-up ADC */
    ACSR |= _BV(ACD); /* disable analog comparator */
    ADCSRA = _BV(ADEN) | _BV(ADIF) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
}

PRIVATE void disable_adcn(void)
{
    ADCSRA &= ~(_BV(ADEN) | _BV(ADIE));
    ACSR &= ~_BV(ACD); /* enable analog comparator */
    PRR |= _BV(PRADC); /* power-down ADC */ 
}

PRIVATE void start_conversion(void)
{
    ADCSRA |= _BV(ADSC) | _BV(ADIE);
}

/* -----------------------------------------------------
   Handle an ADC end of conversion interrupt.
   This appears as <__vector_21>: in the .lst file.
   -----------------------------------------------------*/
ISR(ADC_vect)
{
    this.headp->result = ADCW;
    disable_adcn();
    send_EOC(SELF);
}

/* end code */
