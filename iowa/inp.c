/* iowa/inp.c */

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
 
#include <stdio.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/tty.h"
#include "isp/icsp.h"
#include "sys/ser.h"
#include "sys/ver.h"
#include "sys/inp.h"

/* I am .. */
#define SELF INP
#define this inp

#define MAX_ARGS 6

typedef enum {
    IDLE = 0,
    SETTING_SER_BAUDRATE,
    SETTING_SER_CONSUMER,
    IN_ICSP,
    REDIRECTING_TO_SELF,
    SELECTING_TTY_OUTPUT
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned in_comment : 1;
    unsigned insign : 1;
    uchar_t incount;
    long inval;
    CharProc vptr;
    short args[MAX_ARGS];
    uchar_t narg;
    union {
        icsp_info icsp;
    } info;
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
                char sbuf[10];
                sprintf_P(sbuf, PSTR("err:%d,%d\n"), this.state, m_ptr->RESULT);
                tty_puts(sbuf);
                if (this.state == IN_ICSP) {
                    this.state = REDIRECTING_TO_SELF;
                    send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
                    break;
                }
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

    case SETTING_SER_BAUDRATE:
        tty_puts_P(PSTR("sb: "));
        ok = TRUE;
        break;

    case SETTING_SER_CONSUMER:
        tty_puts_P(PSTR("sc: "));
        ok = TRUE;
        break;

    case SELECTING_TTY_OUTPUT:
        tty_puts_P(PSTR("ou: "));
        ok = TRUE;
        break;

    case IN_ICSP:
        this.state = REDIRECTING_TO_SELF;
        send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
        return;

    case REDIRECTING_TO_SELF:
        tty_putc('\n');
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
                    /* set baud rate
                     * 1B   9600
                     * 2B   19200
                     * 3B   38400
                     * 4B   57600
                     * 5B   115200
                     * 6B   230400
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case B9600:
                        case B19200:
                        case B38400:
                        case B57600:
                        case B115200:
                        case B230400:
                            this.state = SETTING_SER_BAUDRATE;
                            send_SET_IOCTL(SER, SIOC_BAUDRATE, this.inval);
                            this.incount = 0;
                            this.inval = 0;
                            return;
                        }
                        this.incount = 0;
                        this.inval = 0;
                    }
                    break;

                case 'c':
                    /* c : print cycle count */
                    {
                        char sbuf[22];
                        sprintf_P(sbuf, PSTR("cc:%lu,%d\n"), msg_count(),
                                                              msg_depth());
                        tty_puts(sbuf);
                    }
                    break;

                case 'C':
                    /* set SER consumer i.e. who receives NOT_EMPTY
                     * 0C: set consumer to INP
                     * 1C: set consumer to SERIN
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = SETTING_SER_CONSUMER;
                            send_SET_IOCTL(SER, SIOC_CONSUMER, INP);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = SETTING_SER_CONSUMER;
                            send_SET_IOCTL(SER, SIOC_CONSUMER, SERIN);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;
                    
                case 'e':
                    /* print the build ident */
                    tty_puts_P(version);
                    break;

                case 'L':
                    /* 1L : run ICSP */
                    if (this.inval == 1) {
                        this.state = IN_ICSP;
                        send_JOB(ICSP, &this.info.icsp);
                        this.incount = 0;
                        this.inval = 0;
                        return;
                    }
                    this.incount = 0;
                    this.inval = 0;
                    break;

                case 'z':
                    /* set the tty output destination
                     * 0z: local SER device
                     * 1z: GATEWAY OSTREAM device
                     * 2z: TWI_OLED_ADDRESS OSTREAM device
                     * 3z: SPI_OLED_ADDRESS OSTREAM device
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
