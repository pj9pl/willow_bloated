/* cli/dump.c */

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

/* 'dump <host> <start_loc> <end_loc>' command.
 *
 * e.g. dump oslo 800 +100
 */
 
#include <stdlib.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/memz.h"
#include "cli/dump.h"

/* I am .. */
#define SELF DUMP
#define this dump

#define BUF_SIZE 128
#define LINE_MAX 54

typedef enum {
    IDLE = 0,
    DUMPING_DATA_MEMORY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    dump_info *headp;
    uchar_t lindex;
    uchar_t lbuf[LINE_MAX];
    uchar_t n_bytes;        /* number of bytes contained within readbuf */
    uchar_t pindex;         /* iterative loop hex record start point */
    memz_request mz;
    union {
        twi_info twi;
        ser_info ser;
    } info;
    uchar_t readbuf[BUF_SIZE];
} dump_t;

/* I have .. */
static dump_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void fetch_buffer(void);
PRIVATE void bputc(uchar_t c);
PRIVATE void put_nibble(uchar_t v);
PRIVATE void print_one_line(void);

PUBLIC uchar_t receive_dump(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this->state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this->state = IDLE;
            if (this->headp) {
                send_REPLY_INFO(this->headp->replyTo, m_ptr->RESULT,
                                                            this->headp);
                if ((this->headp = this->headp->nextp) != NULL)
                    start_job();
            }
            if (this->headp == NULL) {
                free(this);
                this = NULL;
            }
        }
        break;

    case JOB:
        if (this == NULL && (this = calloc(1, sizeof(*this))) == NULL) {
            send_REPLY_INFO(m_ptr->sender, ENOMEM, m_ptr->INFO);
        } else {
            dump_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                dump_info *tp;
                for (tp = this->headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    this->pindex = 0;
    this->state = DUMPING_DATA_MEMORY;
    fetch_buffer();
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case DUMPING_DATA_MEMORY:
        /* We arrive here after either fetching a buffer or printing a line.
         * this->pindex is zero after fetching a buffer, non-zero after
         * printing a line.
         * The number of bytes in the buffer is this->n_bytes.
         */

        if (this->pindex < this->n_bytes) {
            print_one_line();
        } else if (this->headp->start_loc < this->headp->end_loc) {
            fetch_buffer();
        } else {
            this->state = IDLE;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;
    }
}

PRIVATE void fetch_buffer(void)
{
    this->headp->start_loc += this->pindex;
    this->pindex = 0;
    ushort_t len = this->headp->end_loc - this->headp->start_loc;
    this->n_bytes = MIN(BUF_SIZE, len);

    if (this->n_bytes) {
        this->mz.taskid = SELF;
        this->mz.src = this->headp->start_loc;
        this->mz.len = this->n_bytes;
        sae1_TWI_MTMR(this->info.twi, this->headp->target,
                     MEMZ_REQUEST,
                    &this->mz, sizeof(this->mz),
                     this->readbuf, this->n_bytes);
    } else {
        send_REPLY_RESULT(SELF, EOK);
    }
}

PRIVATE void bputc(uchar_t c)
{
    if (this->lindex < LINE_MAX) {
        this->lbuf[this->lindex++] = c;
    }
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

#define MAX_HEXLINE_BYTES 16

PRIVATE void print_one_line(void)
{
    uchar_t num = MIN(MAX_HEXLINE_BYTES, this->n_bytes - this->pindex);
    ushort_t ofs = (ushort_t)this->headp->start_loc + this->pindex;
    uchar_t *ptr = this->readbuf + this->pindex;

    this->lindex = 0;

    puthex(ofs >> 8 & 0xff);
    puthex(ofs & 0xff);

    for (uchar_t i = 0; i < num; i++) {
        bputc(i & 3 ? ':' : ' ');
        puthex(*ptr++);
    }

    bputc('\n');
    this->pindex += num;
    sae_SER(this->info.ser, this->lbuf, this->lindex);
}

/* end code */
