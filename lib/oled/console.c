/* oled/console.c */

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

/* An OLED secretary to accept OSTREAM_REQUEST messages.
 *
 * This waits for an OSTREAM_REQUEST message containing a string to arrive via
 * the TWI, and then sends the string to the OLED device, manipulating the
 * SET_LINESTART and y position to simulate a scrolling screen. A cursor
 * maintains the current insertion point. Long lines are wrapped. A newline
 * character moves the cursor to the beginning of the next line. A newline
 * at the last line causes the screen to scroll up.
 */

#include <string.h>
#include <stdlib.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/memz.h"
#include "net/ostream.h"
#include "sys/ser.h"
#include "sys/font.h"
#include "oled/common.h"
#include "oled/oled.h"
#include "oled/console.h"

/* I am .. */
#define SELF CONSOLE
#define this console

#define LEFT_MARGIN 1
#define TERMINAL_WIDTH ((NR_COLUMNS - LEFT_MARGIN)/SMALL_FONT_WIDTH)
#define TERMINAL_HEIGHT (NR_ROWS/SMALL_FONT_HEIGHT)

typedef enum {
    IDLE = 0,
    ENSLAVED,
    FETCHING_DATA,
    WRITING_DATA,
    SCROLLING_SCREEN,
    CLEARING_TO_EOL,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned scroll_after : 1;
    uchar_t nn;
    char *sp;
    uchar_t len;
    uchar_t nbytes;
    uchar_t cx;
    uchar_t cy;
    uchar_t line_start;
    char *rbuf;
    char clear_buf[TERMINAL_WIDTH];
    ostream_msg sm; /* service message */
    union {
        memz_msg memz;
    } msg;
    union {
        twi_info twi;
        oled_info oled;
    } info;
} console_t;

/* I have .. */
static console_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void handle_error(uchar_t err);
PRIVATE void print_buf(void);
PRIVATE void scroll_screen(void);
PRIVATE void clear_to_eol(void);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_console(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (m_ptr->RESULT == EOK) {
            resume();
        } else {
            handle_error(m_ptr->RESULT);
        }
        break;

    case INIT:
        {
            uchar_t ret = EBUSY;
            memset(this.clear_buf, ' ', sizeof(this.clear_buf));
            if (this.state == IDLE) {
                get_request();
                ret = EOK;
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
        }
        break;

    case SET_IOCTL:
        {
            uchar_t res = EINVAL;
            switch (m_ptr->IOCTL) {
            case SIOC_CURSOR_POSITION:
                this.cx = m_ptr->LCOUNT % TERMINAL_WIDTH;
                this.cy = (m_ptr->LCOUNT / TERMINAL_WIDTH) % TERMINAL_HEIGHT;
                this.line_start = 0;
                res = EOK;
                break;
            }
            send_REPLY_RESULT(m_ptr->sender, res);
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

    case ENSLAVED:
        if ((this.rbuf = malloc(this.sm.request.len)) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_DATA;
            this.msg.memz.request.src = this.sm.request.src;
            this.msg.memz.request.len = this.sm.request.len;
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                     MEMZ_REQUEST,
                    &this.msg.memz.request, sizeof(this.msg.memz.request),
                     this.rbuf, this.sm.request.len);
        }
        break;

    case FETCHING_DATA:
        this.state = WRITING_DATA;
        this.nbytes = this.sm.request.len - this.info.twi.rcnt;
        this.len = this.nbytes;
        this.nn = 0;
        this.sp = this.rbuf;
        print_buf();
        break;

    case WRITING_DATA:
        if (this.len > 0) {
            print_buf();
        } else {
            send_reply(EOK);
        }
        break;

    case SCROLLING_SCREEN:
        clear_to_eol();
        break;

    case CLEARING_TO_EOL:
        if (this.len > 0) {
            print_buf();
        } else {
            send_reply(EOK);
        }
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void handle_error(uchar_t err)
{
    /* if there is a client waiting, a reply should be sent.
     * If there is no client waiting, a reply should not be sent.
     */
    switch (err) {
    case EACCES:
    case EAGAIN:
        send_reply(err);
        break;

    default:
        get_request();
        break;
    }
}

/* Print all or part of the string held in this.rbuf.
 * Wrap long lines, scrolling the screen if necessary.
 * A newline moves the cursor to the beginning of the
 * next * line and clears to the end of line. When the
 * cursor is on the last line a newline scrolls the
 * screen up.
 */
PRIVATE void print_buf(void)
{
    if (this.scroll_after == 1) {
        this.scroll_after = 0;
        scroll_screen();
        return;
    }
    if (this.sp[this.nn] == '\n') {
        this.nn++;
        this.len--;
        if (this.len <= 0) {
            this.state = CLEARING_TO_EOL;
        }
        if (this.cy < TERMINAL_HEIGHT -1) {
            this.cy++;
            if (this.state == CLEARING_TO_EOL) {
                clear_to_eol();
            }
        } else {
            scroll_screen();
        }
        this.cx = 0;
        return;
    }

    uchar_t i;
    uchar_t wid = TERMINAL_WIDTH - this.cx;
    if (this.len > wid && this.cy >= TERMINAL_HEIGHT -1) {
        this.scroll_after = 1;
    }
    uchar_t j = MIN(this.len, wid);
    for (i = 0; i < j; i++) {
        if (this.sp[this.nn + i] == '\n')
            break;
    }
    this.nn = i;

    this.info.oled.op = DRAW_TEXT;
    this.info.oled.inhibit = FALSE;
    this.info.oled.u.text.x = LEFT_MARGIN + this.cx * SMALL_FONT_WIDTH;
    this.info.oled.u.text.y = ((this.cy * SMALL_FONT_HEIGHT) +
                                          this.line_start) & (NR_ROWS -1);
    this.info.oled.rop = SET;
    this.info.oled.u.text.cp = this.sp;
    this.info.oled.u.text.len = this.nn;
    this.info.oled.u.text.font = SMALLFONT;
    send_JOB(OLED, &this.info.oled);
    this.cx += this.nn;
    this.sp += this.nn;
    this.len -= this.nn;
    this.nn = 0;
    if (this.cx >= TERMINAL_WIDTH) {
        this.cx = 0;
        this.cy++;
    }
    if (this.cy >= TERMINAL_HEIGHT) {
        this.cy = TERMINAL_HEIGHT -1;
    }
}

PRIVATE void scroll_screen(void)
{
    this.state = SCROLLING_SCREEN;
    this.line_start += SMALL_FONT_HEIGHT;
    this.line_start &= (NR_ROWS -1);
    this.info.oled.op = SET_LINESTART; /* [SH1106.pdf p.20] */
    this.info.oled.inhibit = FALSE;
    this.info.oled.u.linestart.value = this.line_start;
    this.cx = 0;
    send_JOB(OLED, &this.info.oled);
}

PRIVATE void clear_to_eol(void)
{
    this.state = CLEARING_TO_EOL;
    this.info.oled.op = DRAW_TEXT;
    this.info.oled.inhibit = FALSE;
    this.info.oled.u.text.x = LEFT_MARGIN;
    this.info.oled.u.text.y = ((this.cy * SMALL_FONT_HEIGHT) +
                                          this.line_start) & (NR_ROWS -1);
    this.info.oled.rop = SET;
    this.info.oled.u.text.cp = this.clear_buf;
    this.info.oled.u.text.len = sizeof(this.clear_buf);
    this.info.oled.u.text.font = SMALLFONT;
    send_JOB(OLED, &this.info.oled);
}

PRIVATE void get_request(void)
{
    if (this.rbuf) {
        free(this.rbuf);
        this.rbuf = NULL;
    }
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, OSTREAM_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.count = this.nbytes;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, OSTREAM_REPLY, this.sm.reply);
}

/* end code */
