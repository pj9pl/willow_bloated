/* sys/msg.c */

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

/* Message fifo.
 *
 */

#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "host.h"

/* I am .. */
/* no SELF */
#define this msg

#define MSG_FIFO_SIZE  8
#define WATCHDOG_TIMEOUT WDTO_8S          /* 8 second watchdog */

typedef struct {
    message mbuf[MSG_FIFO_SIZE];
    uchar_t in;
    uchar_t out;
    uchar_t pending;
    uchar_t depth;
    ulong_t rcvd;
} msg_t;

/* I have .. */
static msg_t this;
PUBLIC uchar_t lost_msgs;

/* I can .. */
PRIVATE void insert_msg(message *m_ptr);
PRIVATE void wdti_enable (const uint8_t value);

PUBLIC void config_msg(void)
{
    set_sleep_mode(SLEEP_MODE_IDLE);
    wdti_enable(WATCHDOG_TIMEOUT);
}

/* Transfer the next message to the caller's initialized pointer. */
PUBLIC void extract_msg(message *m_ptr)
{
    /* Atomic test of this.pending == 0 to decide whether to idle
     * [avr-libc-user-manual-2.0.0.pdf section 23.25.1 p.236-8,
     *   (p.247 in the pdf)]
     */
    cli();
    for (;;) {
        wdt_reset();
        if (this.pending) {
            memcpy(m_ptr, this.mbuf + this.out, sizeof(message));
            if (++this.out >= MSG_FIFO_SIZE)
                this.out = 0;
            this.pending--;
            sei();
            this.rcvd++;
            return;
        }
        wdt_disable();
        sleep_enable();
        sleep_bod_disable();
        sei();
        sleep_cpu();
        sleep_disable();
        cli();
        wdti_enable(WATCHDOG_TIMEOUT);
    }
}


PRIVATE void insert_msg(message *m_ptr)
{
    uchar_t cSREG = SREG;
    cli();
    if (this.pending < MSG_FIFO_SIZE) {
        memcpy(this.mbuf + this.in, m_ptr, sizeof(message));
        if (++this.in >= MSG_FIFO_SIZE)
            this.in = 0;
        this.pending++;
        if (this.depth < this.pending)
            this.depth = this.pending;
    }
    SREG = cSREG;
}

/* send_m1(sender,receiver,opcode)
 *   -------------------------------------------------------------------------
 *   | sender |receiver| opcode |   --   |   --   |   --   |   --   |   --   |
 *   -------------------------------------------------------------------------
 * send a message.
 */
PUBLIC void send_m1(ProcNumber sender, ProcNumber receiver, MsgNumber opcode)
{
    message b;
    b.sender = sender;
    b.receiver = receiver;
    b.opcode = opcode;
    b.RESULT = EOK;
    b.LCOUNT = 0;
    insert_msg(&b);
}

/* send_m2(sender, receiver, opcode, mtype)
 *   -------------------------------------------------------------------------
 *   | sender |receiver| opcode | mtype  |   --   |   --   |   --   |   --   |
 *   -------------------------------------------------------------------------
 * send one unsigned char.
 */
PUBLIC void send_m2(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                              uchar_t mtype)
{
    message b;
    b.sender = sender;
    b.receiver = receiver;
    b.opcode = opcode;
    b.mtype = mtype;
    b.LCOUNT = 0;
    insert_msg(&b);
}

/* send_m3(sender, receiver, opcode, ptr)
 *   -------------------------------------------------------------------------
 *   | sender |receiver| opcode |   --   | LSB   ptr   MSB |   --   |   --   |
 *   -------------------------------------------------------------------------
 * send one void pointer.
 */
PUBLIC void send_m3(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                                  void *ptr)
{
    message b;
    b.sender = sender;
    b.receiver = receiver;
    b.opcode = opcode;
    b.mtype = 0;
    b.VPTR = ptr;
    b.m3_m3s1 = 0;
    insert_msg(&b);
}

/* send_m4(sender, receiver, opcode, mtype, ptr)
 *   -------------------------------------------------------------------------
 *   | sender |receiver| opcode | mtype  | LSB   ptr   MSB |   --   |   --   |
 *   -------------------------------------------------------------------------
 * send an unsigned char and a void pointer.
 */
PUBLIC void send_m4(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                                   uchar_t mtype, void *ptr)
{
    message b;
    b.sender = sender;
    b.receiver = receiver;
    b.opcode = opcode;
    b.mtype = mtype;
    b.VPTR = ptr;
    b.m3_m3s1 = 0;
    insert_msg(&b);
}

/* send_m5(sender, receiver, opcode, mtype, lcount)
 *   -------------------------------------------------------------------------
 *   | sender |receiver| opcode | mtype  | LSB           lcount          MSB |
 *   -------------------------------------------------------------------------
 * Send an unsigned char and an unsigned long.
 */
PUBLIC void send_m5(ProcNumber sender, ProcNumber receiver, MsgNumber opcode,
                                              uchar_t mtype, ulong_t lcount)
{
    message b;
    b.sender = sender;
    b.receiver = receiver;
    b.opcode = opcode;
    b.mtype = mtype;
    b.LCOUNT = lcount;
    insert_msg(&b);
}

/* get the depth */
PUBLIC uchar_t msg_depth(void)
{
    return this.depth;
}

/* get the count of messages received */
PUBLIC ulong_t msg_count(void)
{
    return this.rcvd;
}

PUBLIC uchar_t msg_slots_available(void)
{
    return MSG_FIFO_SIZE - this.pending;
}

PUBLIC ulong_t msg_lost(void)
{
    return lost_msgs;
}

/* An alternative to the macro in <avr/wdt.h>
 * that also enables the Watchdog interrupt.
 */
PRIVATE void wdti_enable (const uint8_t value)
{
    uchar_t cSREG = SREG;
    cli();
    WDTCSR = _BV(WDCE) | _BV(WDE);              // WDT change enable
#if WDT_DUMP
    WDTCSR = _BV(WDIE) | ((uint8_t)(value & 0x08 ? _BV(WDP3) : 0x00) |
                                                 _BV(WDE) | (value & 0x07));
#else
    WDTCSR = ((uint8_t)(value & 0x08 ? _BV(WDP3) : 0x00) |
                                                 _BV(WDE) | (value & 0x07));
#endif
    SREG = cSREG;
}

#if WDT_DUMP

static uchar_t *ptr;

PRIVATE void bputc(uchar_t c);
PRIVATE void put_nibble(uchar_t v);
PRIVATE void puthex(uchar_t ch);
PRIVATE void print_one_line(void);

/* -----------------------------------------------------
   Handle a Watchdog Timer interrupt.
   This appears as <__vector_6>: in the .lst file.
   -----------------------------------------------------*/
ISR(WDT_vect)
{
    while ((unsigned int)ptr < RAMEND)
        print_one_line();
}

PRIVATE void bputc(uchar_t c) /* [p.186] */
{
    while (!(UCSR0A & _BV(UDRE0)))
        ;
    UDR0 = c;
}

PRIVATE void put_nibble(uchar_t v)
{
    bputc((v < 10 ? '0' : '7') + v);
}

PRIVATE void puthex(uchar_t ch)
{
#define HIGH_NIBBLE(c)         ((c) >> 4 & 0x0f)
#define LOW_NIBBLE(c)          ((c) & 0x0f)

    put_nibble(HIGH_NIBBLE(ch));
    put_nibble(LOW_NIBBLE(ch));
}

PRIVATE void print_one_line(void)
{
    puthex((((unsigned int) ptr) >> 8) & 0xff);
    puthex(((unsigned int) ptr) & 0xff);
    bputc(' ');

    for (uchar_t i = 0; i < 16; i++) {
        puthex(*ptr++);
        bputc(((i + 1) & 3) ? ':' : ' ');
    }

    bputc('\n');
}

#endif

/* end code */
