/* sumo/inp.c */

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
 
#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/tty.h"
#include "sys/ver.h"
#include "sys/inp.h"

/* I am .. */
#define SELF INP
#define this inp

typedef enum {
    IDLE = 0,
    SETTING_BACKLIGHT,
    PLCD_HOUSEKEEPING,
    SELECTING_BARZ_UNITS,
    SELECTING_TTY_OUTPUT
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned in_comment : 1;
    uchar_t incount;
    ulong_t inval;
    CharProc vptr;
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
    uchar_t ok = FALSE;

    switch (this.state) {
    case IDLE:
        return;

    case SETTING_BACKLIGHT:
        tty_puts_P(PSTR("ba: "));
        ok = TRUE;
        break;

    case PLCD_HOUSEKEEPING:
        tty_puts_P(PSTR("ph: "));
        ok = TRUE;
        break;

    case SELECTING_BARZ_UNITS:
        tty_puts_P(PSTR("bu: "));
        ok = TRUE;
        break;

    case SELECTING_TTY_OUTPUT:
        tty_puts_P(PSTR("ou: "));
        ok = TRUE;
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
                case 'B':
                    /* Send control message to the PLCD
                     * 0B - switch backlight off
                     * 1B - switch backlight on
                     */
                    if (this.incount) {
                        this.state = SETTING_BACKLIGHT;
                        send_SET_IOCTL(PLCD, SIOC_BACKLIGHT, this.inval);
                        this.incount = 0;
                        return;
                    }
                    break;

                case 'c':
                    /* print cycle count */
                    tty_puts_P(PSTR("cc:"));
                    tty_printl(msg_count());
                    tty_putc(',');
                    tty_printl(msg_depth());
                    tty_putc('\n');
                    break;

                case 'e':
                    /* print the build ident */
                    tty_puts_P(version);
                    break;

                case 'j':
                    /* Send housekeeping  message to the PLCD
                     * 0j - clear display
                     * 1j - return home
                     */
                    if (this.incount) {
                        this.state = PLCD_HOUSEKEEPING;
                        send_SET_IOCTL(PLCD, SIOC_PLCD_COMMAND, this.inval);
                        this.incount = 0;
                        return;
                    }
                    break;

                case 'u':
                    /* Set the BARZ display units
                     * 1u: Celsius and mBar
                     * 0u: Fahrenheit and Inches of mercury
                     */
                    if (this.incount) {
                        this.state = SELECTING_BARZ_UNITS;
                        send_SET_IOCTL(BARZ, SIOC_STANDARD, this.inval);
                        this.incount = 0;
                        return;
                    }
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
                        return;
                    }
                    break; 

                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    this.inval = this.inval * 10 + (ch - '0');
                    this.incount++;
                    break;

                case '\n':
                    this.incount = 0;
                    this.inval = 0;
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
