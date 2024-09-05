/* lcd/glyph.c */

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

/* LCD glyph server.
 * 
 * This receives requests to load patterns into the CGRAM.
 *
 * The patterns are stored in program memory as 8-byte arrays.
 * The bytes are transferred into this.buf[], then the job info instr
 * is written with the CGRAM address and the job info is sent to the PLCD.
 *
 * There are 8 slots available, corresponding to character codes 0..7.
 *
 * This server is stateless. It does have a JOB interface, thus operates
 * a queue for multiple callers, as the results can take a while to complete.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "lcd/plcd.h"
#include "lcd/glyph.h"

/* I am .. */
#define SELF GLYPH
#define this glyph

#define XBUFLEN 8             /* eight bytes for the CGRAM data */

typedef struct {
    glyph_info *headp;
    uchar_t buf[XBUFLEN];
    union {
        plcd_info plcd;
    } info;
} glyph_t;

/* I have .. */
static glyph_t this;

static const uchar_t __flash patterns[] = {
  0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, /* empty */
  0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x1F,
  0x0E, 0x11, 0x11, 0x11, 0x11, 0x1F, 0x1F, 0x1F,
  0x0E, 0x11, 0x11, 0x11, 0x1F, 0x1F, 0x1F, 0x1F,
  0x0E, 0x11, 0x11, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
  0x0E, 0x11, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
  0x0E, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, /* full */
  0x0E, 0x1F, 0x1F, 0x1B, 0x11, 0x1B, 0x1F, 0x1F  /* on charge */
};

/* I can .. */
PRIVATE void start_job(void);

PUBLIC uchar_t receive_glyph(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.headp) {
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL)
                start_job();
        }
        break;

    case JOB:
        {
            glyph_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                glyph_info *tp;
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
    ushort_t ofs = this.headp->pattern << 3;
    uchar_t i;

    for (i = 0; i < XBUFLEN; i++)
        this.buf[i] = pgm_read_byte_near(&patterns[ofs + i]);

    this.info.plcd.p = this.buf;
    this.info.plcd.n = i;
    this.info.plcd.instr = LCD_SET_CGRAM_ADDR | this.headp->slot << 3;
    send_JOB(PLCD, &this.info.plcd);
}

/* end code */
