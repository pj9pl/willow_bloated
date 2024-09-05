/* lima/inp.c */

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

/* handle incoming characters */
 
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/tty.h"
#include "net/i2c.h"
#include "oled/common.h"
#include "oled/oled.h"
#include "sys/ver.h"
#include "sys/inp.h"

/* I am .. */
#define SELF INP
#define this inp

#define MAX_ARGS 6

typedef enum {
    IDLE = 0,
    SETTING_CONTRAST,
    INITIALIZING_OLED,
    SETTING_DISPLAY,
    SETTING_ORIGIN,
    SETTING_LINESTART,
    SENDING_RECT_PARAM,
    SELECTING_BARP_UNITS,
    SELECTING_TTY_OUTPUT,
    DRAWING_LINE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned in_comment : 1;
    unsigned insign : 1;
    uchar_t incount;
    long inval;
    CharProc vptr;
    union {
        oled_info oled;
    } info;
    short args[MAX_ARGS];
    uchar_t narg;
} inp_t;

/* I have .. */
static inp_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void consume(void);
PRIVATE void drain(void);

PUBLIC uchar_t receive_inp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        if (this.vptr && this.vptr != m_ptr->VPTR)
            drain();
        this.vptr = m_ptr->VPTR;
        if (this.state == IDLE && this.vptr)
            consume();
        break;

    case REPLY_DATA:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                tty_puts_P(PSTR("err: "));
                tty_printl(this.state);
                tty_putc(',');
                tty_printl(m_ptr->RESULT);
                tty_putc('\n');
                this.state = IDLE;
            }
        }

        if (this.state == IDLE && this.vptr) {
            consume();
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    uchar_t ok = TRUE;

    switch (this.state) {
    case IDLE:
        return;

    case SETTING_CONTRAST:
        tty_puts_P(PSTR("co: "));
        break;

    case INITIALIZING_OLED:
        tty_puts_P(PSTR("in: "));
        break;

    case SETTING_DISPLAY:
        tty_puts_P(PSTR("di: "));
        break;

    case SETTING_ORIGIN:
        tty_puts_P(PSTR("or: "));
        break;

    case SETTING_LINESTART:
        tty_puts_P(PSTR("li: "));
        break;

    case SENDING_RECT_PARAM:
        tty_puts_P(PSTR("re: "));
        break;

    case SELECTING_BARP_UNITS:
        tty_puts_P(PSTR("bu: "));
        break;

    case SELECTING_TTY_OUTPUT:
        tty_puts_P(PSTR("ou: "));
        break;

    case DRAWING_LINE:
        tty_puts_P(PSTR("dl: "));
        break;
    }

    this.state = IDLE;
    if (ok)
        tty_puts_P(PSTR("ok\n"));
}

PRIVATE void consume(void)
{
    char ch;

    while (this.vptr) {
        switch ((this.vptr) (&ch)) {
        case EWOULDBLOCK:
            this.vptr = NULL;
            return;

        case EOK:
            if (ch == '#') {
                this.in_comment = TRUE;
                tty_putc(ch);
            } else if (this.in_comment) {
                if (ch == '\r') {
                    /* do nothing */
                } else if (ch == '\n') {
                    this.in_comment = FALSE;
                    tty_putc('\n');
                } else {
                    tty_putc(ch);
                }
            } else {
                switch (ch) {
                case 'c':
                    /* print cycle count */
                    tty_puts_P(PSTR("cc:"));
                    tty_printl(msg_count());
                    tty_putc(',');
                    tty_printl(msg_depth());
                    tty_putc('\n');
                    break;

                case 'C':
                    /*  contrast: [0..255] */
                    if (this.incount) {
                        this.state = SETTING_CONTRAST;
                        this.info.oled.op = SET_CONTRAST;
                        this.info.oled.u.contrast.value = this.inval & 0xff;
                        send_JOB(OLED, &this.info.oled);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'e':
                    /* print the build ident */
                    tty_puts_P(version);
                    break;

                case 'i':
                    this.state = INITIALIZING_OLED;
                    send_INIT(OLED);
                    return;

                case 'j':
                    /* send SIOC_OLED_COMMAND
                     * 0j turn the display off
                     * 1j turn the display on
                     * 2j clear the display
                     * 3j illuminate all pixels
                     * 4j illuminate selected pixels 
                     * 5j reverse illumination
                     * 6j normal illumination
                     */
                    if (this.incount) {
                        this.state = SETTING_DISPLAY;
                        this.info.oled.op = SET_DISPLAY;
                        this.info.oled.u.display.value = this.inval;
                        send_JOB(OLED, &this.info.oled);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'l':
                    /*  vertical displacement: [0..63] */

                    if (this.incount) {
                        this.state = SETTING_LINESTART;
                        this.info.oled.op = SET_LINESTART;
                        this.info.oled.u.linestart.value = this.inval & 0x3F;
                        send_JOB(OLED, &this.info.oled);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'L' :
                    /* '<x1,y1,x2,y2,rop,inh> L'
                     * draw a line from (x1,y1) to (x2,y2) using rop and inh
                     */
                    if (this.narg == 5 && this.incount &&
                          this.args[0] >= 0 && this.args[0] < NR_COLUMNS &&
                          this.args[1] >= 0 && this.args[1] < NR_ROWS &&
                          this.args[2] >= 0 && this.args[2] < NR_COLUMNS &&
                          this.args[3] >= 0 && this.args[3] < NR_ROWS &&
                          this.args[4] >= 0 && this.args[4] < 4 &&
                          this.args[5] >= 0 && this.args[5] < 2 &&
                          this.inval >= 0 && this.inval < 2) {
                        this.state = DRAWING_LINE;
                        this.info.oled.op = DRAW_LINE;
                        this.info.oled.u.line.x1 = this.args[0];
                        this.info.oled.u.line.y1 = this.args[1];
                        this.info.oled.u.line.x2 = this.args[2];
                        this.info.oled.u.line.y2 = this.args[3];
                        this.info.oled.rop = this.args[4];
                        this.info.oled.inhibit = this.inval;
                        this.info.oled.u.line.dashed = 0xff;
                        send_JOB(OLED, &this.info.oled);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    tty_puts_P(PSTR("usage: <x1,y1,x2,y2,rop,inh> L\n"));
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'o':
                    /* Set the OLED screen origin.
                     * send SIOC_OLED_COMMAND
                     * 0o place the y origin at bottom
                     * 1o place the y origin at top
                     * 2o place the x origin at right
                     * 3o place the x origin at left
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = SETTING_ORIGIN;
                            this.info.oled.op = SET_ORIGIN;
                            this.info.oled.u.origin.value = ORIGIN_BOTTOM;
                            send_JOB(OLED, &this.info.oled);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = SETTING_ORIGIN;
                            this.info.oled.op = SET_ORIGIN;
                            this.info.oled.u.origin.value = ORIGIN_TOP;
                            send_JOB(OLED, &this.info.oled);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 2:
                            this.state = SETTING_ORIGIN;
                            this.info.oled.op = SET_ORIGIN;
                            this.info.oled.u.origin.value = ORIGIN_LEFT;
                            send_JOB(OLED, &this.info.oled);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 3:
                            this.state = SETTING_ORIGIN;
                            this.info.oled.op = SET_ORIGIN;
                            this.info.oled.u.origin.value = ORIGIN_RIGHT;
                            send_JOB(OLED, &this.info.oled);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                    }
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'u':
                    /* Set the BARP display units
                     * 1u: Celsius and mBar
                     * 0u: Fahrenheit and Inches of mercury
                     */
                    if (this.incount) {
                        this.state = SELECTING_BARP_UNITS;
                        send_SET_IOCTL(BARP, SIOC_STANDARD, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'z':
                    /* set the tty output destination
                     * 0z  local SER device
                     * 1z  GATEWAY OSTREAM device
                     * 2z  TWI_OLED_ADDRESS OSTREAM device
                     * 3z  SPI_OLED_ADDRESS OSTREAM device
                     */
                    if (this.incount) {
                        this.state = SELECTING_TTY_OUTPUT;
                        send_SET_IOCTL(TTY, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.narg = 0;
                    this.insign = FALSE;
                    break; 

                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    if (this.incount == 0) {
                        this.inval = 0;
                    } else {
                        this.inval *= 10;
                    }
                    this.inval += this.insign ? -(ch - '0') : (ch - '0');
                    this.incount++;
                    break;

                case ',':
                    if (this.narg < MAX_ARGS) {
                        this.args[this.narg] = (short) this.inval;
                        this.narg++;
                        this.incount = 0;
                        this.insign = FALSE;
                    }
                    break;

                case '\n': /* 0x0a line terminator */
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;
                }
            }
            break;
        }
    }
}

PRIVATE void drain(void)
{
    char ch;

    while (this.vptr) {
        if ((this.vptr) (&ch) == EWOULDBLOCK) {
            this.vptr = NULL;
            return;
        }
    }
}

/* end code */
