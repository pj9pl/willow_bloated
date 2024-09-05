/* goat/inp.c */

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
#include "sys/ser.h"
#include "sys/tty.h"
#include "sys/dmp.h"
#include "isp/hvpp.h"
#include "sys/ver.h"
#include "sys/inp.h"

/* I am .. */
#define SELF INP
#define this inp

typedef enum {
    IDLE = 0,
    DUMPING_REGISTERS,
    DUMPING_RAM,
    DUMPING_ALL,
    IN_HVPP,
    REDIRECTING_TO_SELF
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned in_comment : 1;
    uchar_t incount;
    ulong_t inval;
    CharProc vptr;
    union {
        ser_info ser;
        dmp_info dmp;
        hvpp_info hvpp;
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
                tty_puts_P(PSTR("err: "));
                tty_printl(m_ptr->RESULT);
                tty_puts_P(PSTR(", state: "));
                tty_printl(this.state);
                if (this.state == IN_HVPP) {
                    tty_puts_P(PSTR(", line: "));
                    tty_printl(this.info.hvpp.lineno);
                }
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
    switch (this.state) {
    case IDLE:
        return;

    case DUMPING_REGISTERS:
    case DUMPING_RAM:
    case DUMPING_ALL:
        this.state = IDLE;
        tty_puts_P(PSTR("ok\n"));
        break;

    case IN_HVPP:
        this.state = REDIRECTING_TO_SELF;
        send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
        break;

    case REDIRECTING_TO_SELF:
        this.state = IDLE;
        tty_putc('\n');
        break;
    }
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
                    tty_putc(',');
                    tty_printl(msg_lost());
                    tty_putc('\n');
                    break;

                case 'd':
                    /* 1d : dump registers
                     * 2d : dump sram
                     * 3d : dump the lot
                     */ 
                    if (this.incount) {
                        switch (this.inval) {
                        case 1:
                            this.state = DUMPING_REGISTERS;
                            this.info.dmp.src = (uchar_t *)0;
                            this.info.dmp.cnt = RAMSTART;
                            send_JOB(DMP, &this.info.dmp);
                            this.incount = 0;
                            this.inval = 0;
                            return;

                        case 2:
                            this.state = DUMPING_RAM;
                            this.info.dmp.src = (uchar_t *)RAMSTART;
                            this.info.dmp.cnt = RAMEND + 1 - RAMSTART;
                            send_JOB(DMP, &this.info.dmp);
                            this.incount = 0;
                            this.inval = 0;
                            return;

                        case 3:
                            this.state = DUMPING_ALL;
                            this.info.dmp.src = (uchar_t *)0;
                            this.info.dmp.cnt = RAMEND + 1;
                            send_JOB(DMP, &this.info.dmp);
                            this.incount = 0;
                            this.inval = 0;
                            return;
                        }
                        this.incount = 0;
                        this.inval = 0;
                    }
                    break;

                case 'e':
                    /* print the build ident */
                    tty_puts_P(version);
                    break;

                case 'L':
                    /* 1L : run HVPP */
                    if (this.inval == 1) {
                        this.state = IN_HVPP;
                        send_JOB(HVPP, &this.info.hvpp);
                        this.incount = 0;
                        this.inval = 0;
                        return;
                    }
                    this.incount = 0;
                    this.inval = 0;
                    break;

                case 'W':
                    /* '999W'  invoke watchdog timeout with an infinite loop */
                    if (this.inval == 999) {
                        for (;;)
                            ;
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
