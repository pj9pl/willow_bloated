/* cli/canon.c */

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

/* canonise incoming characters into lines, send them to the CLI. */
 
#include <string.h>
#include <ctype.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "cli/cli.h"
#include "cli/canon.h"

/* I am .. */
#define SELF CANON
#define this canon

#define LINE_MAX 81    /* 80 chars + '\0' */
#define NR_BUFS 2

typedef enum {
    IDLE = 0,
    BUSY
} __attribute__ ((packed)) state_t;

typedef struct {
    char buf[LINE_MAX];
    ushort_t count;
} silo_t;

typedef struct {
    state_t state;
    silo_t silo[NR_BUFS];
    uchar_t active; /* indicates which buffer is accumulating input */
    uchar_t sent;   /* indicates which buffer is under the auspices of CLI */
    cli_info cli;
    CharProc vptr;
} canon_t;

/* I have .. */
static canon_t this;

/* I can .. */
PRIVATE void consume(void);
PRIVATE void drain(void);

PUBLIC uchar_t receive_canon(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        if (this.vptr && this.vptr != m_ptr->VPTR)
            drain();
        this.vptr = m_ptr->VPTR;
        if (this.vptr)
            consume();
        break;

    case REPLY_INFO:
    case REPLY_RESULT:
        this.state = IDLE;
        this.silo[this.sent].count = 0;
        memset(this.silo[this.sent].buf, '\0', LINE_MAX);
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void consume(void)
{
    char ch;
    silo_t *sp = &this.silo[this.active];

    while (this.vptr) {
        switch ((this.vptr) (&ch)) {
        case EWOULDBLOCK:
            this.vptr = NULL;
            return;

        case EOK:
            if (sp->count < LINE_MAX) {
                if (ch == '\r') {
                    continue;
                } else if (ch == '\n') {
                    if (this.state == IDLE) {
                        this.state = BUSY;
                        sp->buf[sp->count++] = 0;
                        this.cli.bp = sp->buf;
                        this.cli.len = sp->count;
                        this.sent = this.active;
                        this.active = (this.active == 1) ? 0 : 1;
                        send_JOB(CLI, &this.cli);
                    } else {
                        /* cli still busy. discard the line */
                        sp->count = 0;
                    }
                } else if (isprint(ch)) {
                    sp->buf[sp->count++] = ch;
                }
            } else {
                sp->count = 0;
                sp->buf[sp->count++] = ch;
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
