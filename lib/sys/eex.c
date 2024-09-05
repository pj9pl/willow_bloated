/* sys/eex.c */

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

/* The EEX task manages the transfer of data between the RAM and the EEPROM.
 * It provides a JOB interface to accomodate multiple clients, each with four
 * parameters. 
 *
 * If either address is beyond the available space, EINVAL is returned.
 *
 * see also [p.29-35]
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/eex.h"

/* I am .. */
#define SELF EEX
#define this eex

typedef struct _eex {
    eex_info *headp;
} eex_t;

/* I have .. */
static eex_t this;

/* I can .. */
PRIVATE void start_job(void);

PUBLIC uchar_t receive_eex(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_BUSY:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.headp) {
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL)
                start_job();
        }
        break;

    case JOB:
        {
            eex_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                eex_info *tp;
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
    if ((ushort_t)this.headp->sptr >= RAMSTART &&
            (ushort_t)this.headp->sptr + this.headp->cnt <= RAMEND &&
            (ushort_t)this.headp->eptr + this.headp->cnt <= E2END) {
        switch (this.headp->mode) {
        case EEX_READ:
            while (this.headp->cnt--) {
                EEAR = (ushort_t)this.headp->eptr++;
                EECR |= _BV(EERE);
                *this.headp->sptr++ = EEDR;
            }
            send_REPLY_RESULT(SELF, EOK);
            break;

        case EEX_WRITE:
            EECR |= _BV(EERIE);
            break;
        }
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

/*----------------------------------------------------
  Handle an EEPROM Ready Interrupt.
  This is called when the EERIE bit is set and the
  EEPE bit is clear in EECR.
  This does the grunt work of writing a block to the
  EEPROM.

  If the count is zero disable the interrupt and notify
  the process context.
-----------------------------------------------------*/
ISR(EE_READY_vect)
{
    for (;;) {
        if (this.headp->cnt--) {
            EEAR = (ushort_t)this.headp->eptr++;
            EECR |= _BV(EERE);
            uchar_t ch = EEDR;
            if (ch != *this.headp->sptr) {
                /* default to erase and program */
                EECR &= ~(_BV(EEPM1) | _BV(EEPM0));
                if (*this.headp->sptr == 0xff) {
                    /* sram matches 0xff, erase only */
                    EECR |= _BV(EEPM0);
                } else if (ch == 0xff) {
                    /* eeprom matches 0xff, program only */
                    EECR |= _BV(EEPM1);
                }
                EEDR = *this.headp->sptr++;
                EECR |= _BV(EEMPE);
                EECR |= _BV(EEPE);
                return;
            } else {
                /* sram matches eeprom, progress to the next byte */
                this.headp->sptr++;
            }
        } else {
            EECR &= ~_BV(EERIE);
            send_NOT_BUSY(SELF);
            return;
        }
    }
}

/* end code */
