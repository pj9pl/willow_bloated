/* sys/stw.c */

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

/* a stopwatch.
 * Uses TIMER 0 [p.102].
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/stw.h"

/* I am .. */
#define this stw

#ifndef STW_TIMER
#define STW_TIMER TIMER1
#endif

#if (STW_TIMER == TIMER0)
# define MAX_COUNT 0xFF
# define TIMSKn TIMSK0
# define CSn0 CS00
# define TCNTn TCNT0
# define TOIEn TOIE0
# define TCCRnA TCCR0A
# define TCCRnB TCCR0B
# define PRTIMn PRTIM0
# define TIFRn TIFR0
# define TOVn TOV0
# define TIMERn_OVF_vect TIMER0_OVF_vect
#elif (STW_TIMER == TIMER1)
# define MAX_COUNT 0xFFFF
# define TIMSKn TIMSK1
# define CSn0 CS10
# define TCNTn TCNT1
# define TOIEn TOIE1
# define TCCRnA TCCR1A
# define TCCRnB TCCR1B
# define PRTIMn PRTIM1
# define TIFRn TIFR1
# define TOVn TOV1
# define TIMERn_OVF_vect TIMER1_OVF_vect
#elif (STW_TIMER == TIMER2)
# define MAX_COUNT 0xFF
# define TIMSKn TIMSK2
# define CSn0 CS20
# define TCNTn TCNT2
# define TOIEn TOIE2
# define TCCRnA TCCR2A
# define TCCRnB TCCR2B
# define PRTIMn PRTIM2
# define TIFRn TIFR2
# define TOVn TOV2
# define TIMERn_OVF_vect TIMER2_OVF_vect
#endif

#define STEP_SIZE (MAX_COUNT + 1L)

/* I have .. */
static stw_t this;

/* I can .. */
PUBLIC void stw_start(void)
{
    PRR &= ~_BV(PRTIMn);
    /* Initialize TIMERn. */
    TCCRnB = 0x00;  /* stop the timer. */
    TCCRnA = 0x00;     /* normal mode 0. [p.107] */
    this.lcnt = 0;
    TCNTn = 0;
    TIFRn |= _BV(TOVn);    /* set the bit to clear it [p.118] */
    TIMSKn |= _BV(TOIEn);  /* enable overflow interrupt only. [p.118] */
    this.running = TRUE;
    TCCRnB = _BV(CSn0);    /* clkio (no prescaling) [p.117] */
}

PUBLIC void stw_stop(void)
{
    TCCRnB = 0x00;  /* stop the timer. */
    this.ccnt = TCNTn;
    PRR |= _BV(PRTIMn);
    this.running = FALSE;
}

PUBLIC void stw_read(stw_t *ip)
{
    uchar_t cTCCRnB = TCCRnB;
    TCCRnB = 0;
    this.ccnt = TCNTn;
    ip->lcnt = this.lcnt;
    ip->ccnt = this.ccnt;
    TCCRnB = cTCCRnB;
    ip->running = this.running;
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
    this.lcnt++;
}

/* end code */
