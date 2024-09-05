/* sys/serin.c */

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

/* Consume incoming characters on USART0.
 * Collect them into lines and send them to the GATEWAY OSTREAM.
 *
 * This task is used during the HC-05 AT-MODE to capture the bytes that
 * arrive at pin #2 from the HC-05 TX-out and send them to bali's OSTREAM. 
 */
 
#include <string.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/ostream.h"
#include "sys/serin.h"

/* I am .. */
#define SELF SERIN
#define this serin

typedef enum {
    IDLE = 0,
    FLUSHING_BUFFER
} __attribute__ ((packed)) state_t;

#define LEN 32 /* must be a power of 2 */
#define HEADROOM 2

typedef struct {
    state_t state;
    uchar_t ibuf[LEN];  /* circular buffer. */
    uchar_t cnt;  /* offset at which to put a byte. */
    uchar_t pos;  /* index from which to send. */
    uchar_t err;
    ushort_t nsent;
    union {
        ostream_msg ostream;
    } msg;
    union {
        twi_info twi;
    } info;
} serin_t;

/* I have .. */
static serin_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void handle_error(void);
PRIVATE void consume(CharProc vp);
PRIVATE void flush_buffer(void);

PUBLIC uchar_t receive_serin(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        consume(m_ptr->VPTR);
        break;

    case REPLY_INFO:
        if (m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.err = m_ptr->RESULT;
            handle_error();
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case FLUSHING_BUFFER:
        if (this.nsent) {
            this.cnt -= this.nsent;
            this.pos = (this.pos + this.nsent) & (LEN -1);
            this.nsent = 0;
        }
        if (this.cnt) {
            flush_buffer();
        } else {
            this.state = IDLE;
        }
        break;
    }
}

PRIVATE void handle_error(void)
{
    this.state = IDLE;
}

PRIVATE void consume(CharProc vp)
{
    char ch;

    while ((vp) (&ch) == EOK) {
        if (this.cnt < LEN) {
            this.ibuf[(this.pos + this.cnt++) & (LEN -1)] = ch;
            if (this.state == IDLE) {
                if (ch == '\n' || this.cnt > (LEN - HEADROOM)) {
                    flush_buffer();
                }
            }
        }
    }
}

PRIVATE void flush_buffer(void)
{
    this.state = FLUSHING_BUFFER;
    this.nsent = (this.pos + this.cnt < LEN) ? this.cnt : (LEN - this.pos);
    this.msg.ostream.request.taskid = SELF;
    this.msg.ostream.request.jobref = &this.info.twi;
    this.msg.ostream.request.sender_addr = HOST_ADDRESS;
    this.msg.ostream.request.src = this.ibuf + this.pos;
    this.msg.ostream.request.len = this.nsent;
    sae2_TWI_MTSR(this.info.twi, GATEWAY_ADDRESS,
           OSTREAM_REQUEST, this.msg.ostream.request,
           OSTREAM_REPLY, this.msg.ostream.reply);
}

/* end code */
