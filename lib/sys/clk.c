/* sys/clk.c */
 
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

/* An alarm clock server using one of TIMER0, TIMER1 or TIMER2
 *                                 [p.102-119, 120-149, 150-168].
 *
 * Accepts SET_ALARM and CANCEL requests, both of which carry a clk_info
 * pointer.
 *
 * SET_ALARM specifies the number of milliseconds delay before sending
 * an ALARM reply.
 *
 * A valid ALARM carries an EOK RESULT, whereas an invalid ALARM carries an
 * EINVAL RESULT. An invalid ALARM is caused by an invalid delay specification.
 * 
 * CANCEL references the job to be cancelled. This generates a
 * REPLY_INFO with EOK if it succeeded or ESRCH if it was not found.
 *
 * Uses TIMER n in normal mode. [p.131]
 *
 * Where two alarms expire in the same instant, sequence them over successive
 * interrupts. This avoids having an overrun in the message fifo.
 *
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"

/* The millisecond value supplied by the client is converted to the expiry
 * time in ticks i.e. the duration plus the current ticks value.
 *
 * F_CPU = 8MHz
 * prescaler = 1024
 * tick period = 128 us
 * freq = 7812.5 Hz
 * 8-bit overflow = 32.768 ms = 30.51757813 Hz
 * NUMERATOR = 125
 * 125/16 = 7.8125 ticks/millisecond
 * MAX_MILLIS = 549755808 ~ 6 days, 8 hours
 *             ~549690278 ~  ..      ..
 *
 * F_CPU = 11.06 MHz 
 * prescaler = 1024
 * tick period = 92.59 us
 * freq = 10800 Hz
 * 8-bit overflow = 23.7 ms = 42.1875 Hz
 * NUMERATOR = 172
 * 172/16 = 10.75 ticks/millisecond
 * MAX_MILLIS = 407226528 ~ 4d 17h 7m 6.52s
 *
 * F_CPU = 16MHz
 * prescaler = 1024
 * tick period = 64 us
 * freq = 15625 Hz
 * 8-bit overflow = 15.25878906 ms = 65.536 Hz 
 * NUMERATOR = 250
 * 250/16 = 15.625 ticks/millisecond
 * MAX_MILLIS = 274877904 ~ 3 days, 4 hours
 */

/* I am .. */
#define SELF CLK
#define this clk

#ifndef CLK_TIMER
#define CLK_TIMER TIMER0
#endif

#if (CLK_TIMER == TIMER0)
# define MAX_COUNT 0xFF
# define TIMSKn TIMSK0
# define CSn2 CS02
# define CSn0 CS00
# define TCNTn TCNT0
# define TOIEn TOIE0
# define TCCRnB TCCR0B
# define PRTIMn PRTIM0
# define TIFRn TIFR0
# define TOVn TOV0
# define TIMERn_OVF_vect TIMER0_OVF_vect
#elif (CLK_TIMER == TIMER1)
# define MAX_COUNT 0xFFFF
# define TIMSKn TIMSK1
# define CSn2 CS12
# define CSn0 CS10
# define TCNTn TCNT1
# define TOIEn TOIE1
# define TCCRnB TCCR1B
# define PRTIMn PRTIM1
# define TIFRn TIFR1
# define TOVn TOV1
# define TIMERn_OVF_vect TIMER1_OVF_vect
#elif (CLK_TIMER == TIMER2)
# define MAX_COUNT 0xFF
# define TIMSKn TIMSK2
# define CSn2 CS22
# define CSn0 CS20
# define TCNTn TCNT2
# define TOIEn TOIE2
# define TCCRnB TCCR2B
# define PRTIMn PRTIM2
# define TIFRn TIFR2
# define TOVn TOV2
# define TIMERn_OVF_vect TIMER2_OVF_vect
#endif

#define STEP_SIZE (MAX_COUNT + 1L)
#define NUMERATOR (F_CPU / 64000L)
#define DENOMINATOR 4
#define ZERO 0
#define DIVIDE_1024 (_BV(CSn2) | _BV(CSn0))
#define REMAINDER (MAX_COUNT - TCNTn)
#define THRESHOLD 0x00100000               /* 1048576 */
#define SPACING 15 /* minimum ticks between adjacent alarms */

#define is_active() bit_is_clear(PRR, PRTIMn)
#define is_inactive() bit_is_set(PRR, PRTIMn)

/* enable/disable overflow interrupt [p.118-9] */
#define enable_interrupt() TIMSKn |= _BV(TOIEn)
#define disable_interrupt() TIMSKn &= ~_BV(TOIEn)

#define enable_timer() TCCRnB = DIVIDE_1024  /* mode 0, clkIO/1024 [p.116-7] */
#define disable_timer() TCCRnB = 0x00        /* mode 0, stopped */

/* maximum millisecond value that can be represented as ticks in a ulong_t. */
#define MAX_MILLIS ((UINT32_MAX / NUMERATOR << DENOMINATOR) - THRESHOLD)

typedef struct {
    clk_info *headp;
    ulong_t ticks;
} clk_t;

/* I have .. */
static clk_t this;

/* I can .. */
PRIVATE void activate(void);
PRIVATE void deactivate(void);

PUBLIC uchar_t receive_clk(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case SET_ALARM:
        {
            clk_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;

            /* convert the user-supplied millisecond value
             * to the corresponding number of timer ticks.
             */
            if (ip->uval <= MAX_MILLIS) {
                /* add an extra tick to account for any rounding error */
                ip->uval = 1 + (ulong_t)((uint64_t)ip->uval * NUMERATOR >>
                                                                 DENOMINATOR);
            } else {
                send_ALARM(m_ptr->sender, EINVAL, m_ptr->INFO);
                break;
            }

            disable_interrupt();
            if (is_inactive()) {
                /* easy case: the timer is inactive with no pending alarms */
                activate();
                this.headp = ip;
                if (this.headp->uval < STEP_SIZE) {
                    /* the alarm is imminent */
                    TCNTn = MAX_COUNT - this.headp->uval;
                    this.ticks = this.headp->uval;
                } else {
                    this.ticks = STEP_SIZE;
                }
            } else {
                /* minimise this.ticks */
                if (this.ticks > THRESHOLD) {
                    ulong_t n = this.ticks - STEP_SIZE;
                    if (n < this.headp->uval) {
                        this.ticks -= n;
                        for (clk_info *tp = this.headp; tp; tp = tp->nextp) {
                            tp->uval -= n;
                        }
                    }
                }

                /* convert the user-supplied tick value to the expiry time */
                ulong_t nticks = ip->uval;
                ip->uval += this.ticks - REMAINDER;

                /* insert this item into the list of pending alarms */
                if (ip->uval < this.headp->uval) {
                    /* insert at the front */
                    ip->nextp = this.headp;
                    this.headp = ip;
                    for ( ; ip->nextp && ip->nextp->uval < ip->uval + SPACING;
                                                       ip = ip->nextp) {
                        ip->nextp->uval = ip->uval + SPACING;
                    }
                    if (this.headp->uval < this.ticks) {
                        /* the alarm is imminent */
                        TCNTn = MAX_COUNT - nticks; 
                        TIFRn |= _BV(TOVn); /* clear any pending interrupt */
                        this.ticks = this.headp->uval;
                    }
                } else {
                    for (clk_info *tp = this.headp; tp; tp = tp->nextp) {
                        if (tp->nextp == NULL || ip->uval < tp->nextp->uval) {
                            ip->nextp = tp->nextp;
                            tp->nextp = ip;
                            for ( ; tp->nextp &&
                                    tp->nextp->uval < tp->uval + SPACING;
                                                           tp = tp->nextp) {
                                tp->nextp->uval = tp->uval + SPACING;
                            }
                            break;
                        }
                    }
                }
            }
            enable_interrupt();
        }
        break;

    case CANCEL:
        {
            /* remove the clk_info from the linked list */
            uchar_t result = ESRCH;
            disable_interrupt();
            if (is_active()) {
                clk_info *ip = m_ptr->INFO;
                if (ip == this.headp) {
                    this.headp = this.headp->nextp;
                    result = EOK;
                } else {
                    for (clk_info *tp = this.headp; tp->nextp; tp = tp->nextp) {
                        if (tp->nextp == ip) {
                            tp->nextp = ip->nextp;
                            result = EOK;
                            break;
                        }
                    }
                }
                if (this.headp) {
                    enable_interrupt();
                } else {
                    deactivate();
                }
            }
            send_REPLY_INFO(m_ptr->sender, result, m_ptr->INFO);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/* -----------------------------------------------------
   Handle a Timer n Overflow interrupt.
   This appears as <__vector_16> TIMER0
                or <__vector_13> TIMER1
                or <__vector_9>  TIMER2
  in the .lst file.
   -----------------------------------------------------*/
ISR(TIMERn_OVF_vect)
{
    while (this.headp && this.headp->uval <= this.ticks) {
        send_ALARM(this.headp->replyTo, EOK, this.headp);
        this.headp = this.headp->nextp;
    }
    if (this.headp) {
        ulong_t nticks = this.headp->uval - this.ticks;    
        if (nticks < STEP_SIZE) {
            /* the alarm is imminent */
            TCNTn = MAX_COUNT - nticks;
            this.ticks = this.headp->uval;
        } else {
            this.ticks += STEP_SIZE;
        }
    } else {
        deactivate();
    }
}

PRIVATE void activate(void)
{
    PRR &= ~_BV(PRTIMn);
    TCNTn = ZERO;
    enable_timer();
}

PRIVATE void deactivate(void)
{
    disable_timer();
    disable_interrupt();
    PRR |= _BV(PRTIMn);
    this.ticks = ZERO;
}

/* convenience functions */

PUBLIC void send_CLK_SET_ALARM(ProcNumber sender, clk_info *cp, ulong_t delay)
{
    cp->uval = delay;
    send_m3(sender, SELF, SET_ALARM, cp);
}

PUBLIC void send_CLK_CANCEL(ProcNumber sender, clk_info *cp)
{
    send_m3(sender, SELF, CANCEL, cp);
}

/* end code */
