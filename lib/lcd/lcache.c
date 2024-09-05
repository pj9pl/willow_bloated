/* lcd/lcache.c */

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

/* A write cache between the secretaries and plcd.
 *
 * A fly in the ointment. If the battery glyph always uses the same charcode.
 * it will always be detected as clean, regardless of the glyph. It could
 * circumvent the cache, leaving the cache location holding a space character.
 *
 * The cache is sequentially scanned for differences.
 *  When a difference is found, a start point is recorded,
 *  when a match is found, an end point is recorded.
 * The span of characters between start and end is what constitutes the job
 * sent to PLCD.
 */

#include <string.h>
#include <avr/io.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "lcd/plcd.h"
#include "lcd/lcache.h"

/* I am .. */
#define SELF LCACHE
#define this lcache

typedef enum {
    IDLE = 0,
    BUSY
} __attribute__ ((packed)) state_t;

#define BUFSIZ 80

typedef struct {
    state_t state;
    uchar_t on_screen[BUFSIZ];
    uchar_t as_delivered[BUFSIZ];
    uchar_t idx;
    plcd_info plcd;
} lcache_t;

/* I have .. */
static lcache_t this;

/* I can .. */
PRIVATE void check_for_differences(void);

PUBLIC void config_lcache(void)
{
    memset(this.on_screen, ' ', BUFSIZ);
    memset(this.as_delivered, ' ', BUFSIZ);
}

PUBLIC uchar_t receive_lcache(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (m_ptr->sender == PLCD) {
            this.state = IDLE;
            check_for_differences();
        }
        break;

    case JOB:
        /* We can handle jobs as they arrive. No need to queue. */
        {
            uchar_t ret = EOK;
            lcache_info *ip = m_ptr->INFO;
            uchar_t addr = ip->instr & 0x7F;
            if (addr > 63)
                addr -= 24;
            if (addr + ip->n < BUFSIZ) {
                memcpy(this.as_delivered + addr, ip->p, ip->n);
                if (this.state == IDLE)
                    check_for_differences();
            } else {
                ret = EINVAL;
            }
            send_REPLY_INFO(m_ptr->sender, ret, ip);
        }
        break;

    case INIT:
        memset(this.on_screen, ' ', BUFSIZ);
        memset(this.as_delivered, ' ', BUFSIZ);
        send_REPLY_RESULT(m_ptr->sender, EOK);
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void check_for_differences(void)
{
    uchar_t start = 0;
    uchar_t end = 0;
    bool_t seen = FALSE;

    if (this.idx >= BUFSIZ)
        this.idx = 0;

    for (; this.idx < BUFSIZ; this.idx++) {
        if (this.on_screen[this.idx] != this.as_delivered[this.idx]) {
            if (!seen) {
                seen = TRUE;
                start = this.idx;
                end = this.idx;
            } else {
                end = this.idx;
            }
        } else {
            if (seen) {
                break;
            }
        }
    }
    if (seen) {
        uchar_t len = 1 + end - start;
        if (len > 0 && start + len < BUFSIZ) {
            memcpy(this.on_screen + start, this.as_delivered + start, len);
            this.plcd.p = (uchar_t *)this.on_screen + start;
            this.plcd.n = len;
            if (start > 39) {
                start += 24;
            }
            this.plcd.instr = LCD_SET_DDRAM_ADDR | start;
            this.state = BUSY;
            send_JOB(PLCD, &this.plcd);
        }
    }
}

/* end code */
