/* sys/tty.c */

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

/*  putc() 
 *
 *  A circular buffer into which tty_putc() can append characters.
 *  In raw mode, characters are sent to the serial output as soon as possible.
 *  In cooked mode, a newline or a nearly full buffer is required in order to
 *  send the accumulated characters to the serial output.
 */

#include <string.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/ostream.h"
#include "sys/tty.h"

/* I am .. */
#define SELF TTY
#define this tty

#define XBUFLEN 64  /* must be power of 2 */

typedef struct {
    char xbuf[XBUFLEN];  /* circular buffer. */
    uchar_t cnt;  /* offset at which to put a byte. */
    uchar_t pos;  /* index from which to send. */
    uchar_t nsent; /* count of bytes sent. */
    hostid_t dest_addr;  /* i2c address of where to send ext output. */
    unsigned destination : 1; /* where to send the output: int or ext. */
    unsigned busy : 1; /* whether a message is in transit. */
    unsigned raw : 1;  /* output mode: raw or cooked */
    union {
        ostream_msg ostream;
    } msg;
    union {
        ser_info ser;
        twi_info twi;
    } info;
} tty_t;

/* I have .. */
static tty_t this;

/* I can .. */
PRIVATE void put_nibble(uchar_t v);

PUBLIC uchar_t receive_tty(message *m_ptr)
{
    uchar_t ret = EOK;

    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        this.busy = FALSE;
        if (this.nsent) {
            this.cnt -= this.nsent;
            this.pos = (this.pos + this.nsent) & (XBUFLEN -1);
            this.nsent = 0;
        }
        /* if there is any more, send [part of] it, else clear busy.  */
        tty_flush();
        break;

    case SET_IOCTL:
        switch (m_ptr->IOCTL) {
        case SIOC_OMODE:
            switch (m_ptr->LCOUNT) {
            case OMODE_COOKED:
                this.raw = FALSE;
                break;

            case OMODE_RAW:
                this.raw = TRUE;
                break;

            default:
                ret = EINVAL;
                break;
            }
            break;

        case SIOC_SELECT_OUTPUT:
            switch (m_ptr->LCOUNT) {
            case 0:
                this.destination = FALSE; /* SER */
                break;

            case 1:
                this.destination = TRUE; /* GATEWAY OSTREAM */
                this.dest_addr = GATEWAY_ADDRESS;
                break;

            case 2:
                this.destination = TRUE; /* TWI_OLED_ADDRESS OSTREAM */
                this.dest_addr = TWI_OLED_ADDRESS;
                break;

            case 3:
                this.destination = TRUE; /* SPI_OLED_ADDRESS OSTREAM */
                this.dest_addr = SPI_OLED_ADDRESS;
                break;

            default:
                ret = EINVAL;
                break;
            }
            break;

        default:
            ret = EINVAL;
            break;
        }
        send_REPLY_RESULT(m_ptr->sender, ret);
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PUBLIC void tty_putc(char ch)
{
    if (this.cnt < XBUFLEN) {
        this.xbuf[(this.pos + this.cnt++) & (XBUFLEN -1)] = ch;

        if (this.raw || ch == '\n' || this.cnt > XBUFLEN -5) {
            tty_flush();
        }
    }
}

PUBLIC void tty_flush(void)
{
    if (!this.busy && this.cnt) {
        this.busy = TRUE;
        this.nsent = ((this.pos + this.cnt) < XBUFLEN) ?
                     this.cnt : XBUFLEN - this.pos;
        if (this.destination == FALSE) {
            sae_SER(this.info.ser, this.xbuf + this.pos, this.nsent);
        } else {
            this.msg.ostream.request.taskid = SELF;
            this.msg.ostream.request.jobref = &this.info.twi;
            this.msg.ostream.request.sender_addr = HOST_ADDRESS;
            this.msg.ostream.request.src = this.xbuf + this.pos;
            this.msg.ostream.request.len = this.nsent;
            sae2_TWI_MTSR(this.info.twi, this.dest_addr,
                    OSTREAM_REQUEST, this.msg.ostream.request,
                    OSTREAM_REPLY, this.msg.ostream.reply);
        }
    }
}

PUBLIC void tty_puts(char *s)
{
    if (s) {
        while (*s) {
            tty_putc(*s++);
        }
    }
}

PUBLIC void tty_puts_P(PGM_P str)
{
    char ch;
    while ((ch = pgm_read_byte_near(str++)) != 0) {
        tty_putc(ch);
    }
}

PRIVATE void put_nibble(uchar_t ch)
{
    tty_putc((ch < 10 ? '0' : '7') + ch);
}

PUBLIC void tty_puthex(uchar_t ch)
{
#define HIGH_NIBBLE(c)         ((c) >> 4 & 0x0f)
#define LOW_NIBBLE(c)          ((c) & 0x0f)

    put_nibble(HIGH_NIBBLE(ch));
    put_nibble(LOW_NIBBLE(ch));
}

/* adapted from K&R p.85 */
PUBLIC void tty_printl(long n)
{
    char s[11];
    long i = 0;

    if (n < 0) {
        tty_putc('-');
        n = -n;
    }

    do {
        s[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);

    while (--i >= 0)
        tty_putc(s[i]);
}

PUBLIC uchar_t tty_can_xmt (void)
{
   return ((XBUFLEN -1) - this.cnt);
}

/* end code */
