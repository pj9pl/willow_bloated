/* sys/ser.c */

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

/* asynchronous serial driver.
 *
 * USART0 [p.179-204]
 *
 * Input is collected in a circular buffer whilst a consumer greedily
 * drains it. A NOT_EMPTY message is sent to the consumer whenever the
 * first character is inserted into an empty buffer. The consumer is
 * greedy through necessity as this ensures that the buffer is left
 * empty after each NOT_EMPTY message.
 *
 * The output is quite separate. This is driven by a queueable job
 * containing a character pointer and length.
 * When the length has been reduced to zero the sender is notified
 * that the buffer is no longer under SER control.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"

/* I am .. */
#define SELF SER
#define this ser

#ifndef DEFAULT_BAUDRATE
#define DEFAULT_BAUDRATE B9600
#endif

/* RBUFLEN must be a power of 2 */
#define RBUFLEN 8

typedef struct {
    ser_info *headp;
    char rbuf[RBUFLEN];
    uchar_t rcnt;
    uchar_t rpos;
    uchar_t consumer;
    uchar_t highwater;
} ser_t;

/* I have .. */
static ser_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE uchar_t readchar(uchar_t *cp);
PRIVATE uchar_t set_baudrate(ulong_t baudrate);

PUBLIC void config_ser(void)
{
    /* enable the pullup on RX */
    PORTD |= _BV(PIND0);
    /* Enable USART0 in the power reduction register. */
    PRR &= ~_BV(PRUSART0);

    /* Enable transmitter, receiver and RX Complete Interrupt.
     * Don't enable the Data Register Empty Interrupt (UDRIE0)
     * until there is data available [p.201-2].
     */
    UCSR0B = _BV(RXCIE0) | _BV(TXEN0) | _BV(RXEN0);

    /* Configure UCSR0C:
     *  - Asynchronous USART
     *  - no parity
     *  - 1 stop bit
     *  - 8 bit Character size
     */
    UCSR0C = _BV(UCSZ00) | _BV(UCSZ01);
    set_baudrate(DEFAULT_BAUDRATE);
    this.consumer = INP;
}

PUBLIC uchar_t receive_ser(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_BUSY:
        send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
        if ((this.headp = this.headp->nextp) != NULL)
            start_job();
        break;

    case JOB:
        {
            ser_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                ser_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case SET_IOCTL:
        {
            uchar_t ret = EOK;

            switch (m_ptr->IOCTL) {
            case SIOC_CONSUMER:
                /* Discard any remaining input characters. This is to
                 * ensure that the new consumer gets informed of the
                 * next character to be received.
                 *
                 * This protects against a deadlocked situation where
                 * the previous consumer has to empty the buffer before
                 * SER can generate the next NOT_EMPTY message.
                 */
                cli();
                this.consumer = m_ptr->LCOUNT;
                this.rcnt = 0;
                sei();
                break;

            case SIOC_BAUDRATE:
                ret = set_baudrate(m_ptr->LCOUNT);
                break;

            default:
                ret = EINVAL;
                break;
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    UCSR0B |= _BV(UDRIE0);
}

/* -----------------------------------------------------
   Handle a USART Data Register Empty interrupt.
   This appears as <__vector_19>: in the .lst file.
   -----------------------------------------------------*/
ISR(USART_UDRE_vect)
{
    if (this.headp->len) {
        UDR0 = *this.headp->src++;
        this.headp->len--;
    } else {
        UCSR0B &= ~_BV(UDRIE0);
        send_NOT_BUSY(SELF);
    }
}

/* -----------------------------------------------------
   Handle a USART Rx Complete interrupt.
   This appears as <__vector_18>: in the .lst file.
   -----------------------------------------------------*/
ISR(USART_RX_vect)
{
    /* The three error flags should be tested here. [p.191] */
    if (this.rcnt < RBUFLEN)
        this.rbuf [(this.rpos + this.rcnt++) & (RBUFLEN -1)] = UDR0;
    if (this.rcnt == 1)
        send_NOT_EMPTY(this.consumer, readchar);
    if (this.highwater < this.rcnt)
        this.highwater = this.rcnt;
}

/* This is the function that a consumer uses to extract a character from the
 * circular buffer. A pointer to it is sent with the NOT_EMPTY message.
*/
PRIVATE uchar_t readchar(uchar_t *cp)
{
    if (this.rcnt == 0)
        return EWOULDBLOCK;
    uchar_t cSREG = SREG;
    cli();
    this.rcnt--;
    *cp = this.rbuf [this.rpos];
    if (++this.rpos >= RBUFLEN)
        this.rpos = 0;
    SREG = cSREG;
    return EOK;
}


/* see also:-
 *  - Table 20-1 Equations for Calculating Baud Rate Register Setting [p.182].
 *  - Table 20-6 Examples of UBRRn Settings [p.198].
 *  - The UBRR0 is situated at 0xC4 (UBRR0L) and 0xC5 (UBRR0H) [p.621].
 *
 *  Note that the baudrate can be specified by either a symbolic value or
 *  an explicit integer value.
 */
PRIVATE uchar_t set_baudrate(ulong_t baudrate)
{
    uchar_t ret = EOK;
    switch (baudrate) {
    case B9600: /* 1 */
    case 9600:
        UBRR0 = F_CPU / 16 / 9600 -1;
        UCSR0A &= ~_BV(U2X0);
        break;

    case B19200: /* 2 */
    case 19200:
        UBRR0 = F_CPU / 16 / 19200 -1;
        UCSR0A &= ~_BV(U2X0);
        break;

    case B38400: /* 3 */
    case 38400:
        UBRR0 = F_CPU / 16 / 38400 -1;
        UCSR0A &= ~_BV(U2X0);
        break;

    case B57600: /* 4 */
    case 57600:
        UBRR0 = F_CPU / 16 / 57600 -1;
        UCSR0A &= ~_BV(U2X0);
        break;

    case B115200: /* 5 */
    case 115200:
        UBRR0 = F_CPU / 8 / 115200 -1;
        UCSR0A |= _BV(U2X0);
        break;

    case B230400: /* 6 */
    case 230400:
        UBRR0 = F_CPU / 8 / 230400 -1;
        UCSR0A |= _BV(U2X0);
        break;

    default:
        ret = EINVAL;
        break;
    }
    return ret;
}

/* convenience function */

PUBLIC void send_SER(ProcNumber sender, ser_info *cp, void *src, ushort_t len)
{
    cp->src = src;
    cp->len = len;
    send_m3(sender, SELF, JOB, cp);
}

/* end code */
