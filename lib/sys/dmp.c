/* sys/dmp.c */

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

/* a memory dump service.
 *
 */

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "sys/dmp.h"

#define LINE_MAX 54

/* I am .. */
#define SELF DMP
#define this dmp

typedef enum {
    IDLE = 0,
    DUMPING_DATA_MEMORY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    dmp_info *headp;
    uchar_t num_done;
    uchar_t lindex;
    uchar_t lbuf[LINE_MAX];
    union {
        ser_info ser;
    } info;
} dmp_t;

/* I have .. */
static dmp_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void bputc(uchar_t c);
PRIVATE void put_nibble(uchar_t v);
PRIVATE void puthex(uchar_t ch);
PRIVATE void print_one_line(void);

PUBLIC uchar_t receive_dmp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL)
                    start_job();
            }
        }
        break;

    case JOB:
        {
            dmp_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                dmp_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
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
    this.state = DUMPING_DATA_MEMORY;
    print_one_line();
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case DUMPING_DATA_MEMORY:
        print_one_line();
        break;
    }
}

PRIVATE void bputc(uchar_t c)
{
    if (this.lindex < LINE_MAX) {
        this.lbuf[this.lindex++] = c;
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

PRIVATE void print_one_line(void)
{
    this.lindex = 0;

    puthex((((unsigned int) this.headp->src) >> 8) & 0xff);
    puthex(((unsigned int) this.headp->src) & 0xff);

    for (int i = 0; i < 16 && this.headp->cnt; i++, this.headp->cnt--) {
        bputc(i & 3 ? ':' : ' ');
        puthex(*this.headp->src++);
    }

    if (this.headp->cnt == 0)
        this.state = IDLE;

    bputc('\n');
    sae_SER(this.info.ser, this.lbuf, this.lindex);
}

/* end code */
