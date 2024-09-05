/* alba/patch.c */

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

/* Patch is a jobbing server that configures the AD7124, MCP4728 and OLED.
 * It is provided with the inode number of a config file that is executed
 * one line at a time.
 */

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "oled/common.h"
#include "oled/oled.h"
#include "oled/osetup.h"
#include "alba/ad7124.h"
#include "alba/alba.h"
#include "alba/mdac.h"
#include "alba/patch.h"

/* I am .. */
#define SELF PATCH
#define this patch

/* 39.06us minimum delay between consecutive operations [AD7124-8: p.11,13] */
#define t12_DELAY 40.0 

#define BUFSIZE 128

typedef enum {
    IDLE = 0,
    FETCHING_BUFFER,
    PROCESSING_RECORD
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned more : 1;
    unsigned cache : 1;
    patch_info *headp;
    char *bp;
    ushort_t buf_bytes;
    int regno;
    long val;
    off_t fpos;
    union {
        fsd_msg fsd;
        osetup_msg osetup;
    } msg;
    union {
        twi_info twi;
        alba_info alba;
        mdac_info mdac;
    } info;
    char sbuf[BUFSIZE +1];
} patch_t;

/* I have .. */
static patch_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void fetch_buffer(void);
PRIVATE void send_osetup(void);

PUBLIC uchar_t receive_patch(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ADC_RDY:
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
            patch_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                patch_info *tp;
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
    this->fpos = 0;
    this->cache = FALSE;
    this->headp->nlines = 0;
    fetch_buffer();
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_BUFFER:
        this->buf_bytes = this->msg.fsd.reply.p.readf.nbytes;
        if (this->buf_bytes == 0 || this->msg.fsd.reply.result != EOK) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
            break;
        }
        if (this->buf_bytes < BUFSIZE) {
            this->more = FALSE;
            this->sbuf[this->buf_bytes] = '\0';
        } else {
            this->more = TRUE;
            /* the +1 of sbuf serves as a null terminator */
        }

        char *ep = this->sbuf + this->buf_bytes -1;
        char *final_newline = strrchr(this->sbuf, '\n');
        if (final_newline) {
            /* Discard any partial line and set fpos
             * to read the next buffer from that location.
             */
            this->fpos = this->msg.fsd.reply.p.readf.fpos -
                                                 (ep - final_newline);
            *final_newline = '\0';
        } else {
            /* line length exceeds buffer size: it's too big */
            send_REPLY_RESULT(SELF, E2BIG);
            break;
        }
        this->bp = this->sbuf;
        this->state = PROCESSING_RECORD;
        resume();
        break;

    case PROCESSING_RECORD:
        {
            char *tp;
            for (;;) {
                if (this->bp == NULL || *this->bp == '\0') {
                    if (this->more) {
                        fetch_buffer();
                    } else {
                        this->state = IDLE;
                        send_REPLY_RESULT(SELF, EOK);
                    }
                    return;
                }

                tp = this->bp;

                while (tp && *tp && *tp != '\n') {
                    if (tp < this->sbuf + this->buf_bytes) {
                        tp++;
                    } else {
                        this->state = IDLE;
                        send_REPLY_RESULT(SELF, EOK);
                        return;
                    }
                }

                if (tp && *tp == '\n') {
                    *tp++ = '\0';
                }

                this->headp->nlines++;

                if (this->bp[0] == '#') {
                    /* comment, continue with the next line */
                    this->bp = tp;
                } else {
                    /* not a comment */
                    break;
                }
            }

            _delay_us(t12_DELAY);

            switch (*this->bp) {
            case RESET_AD7124:
                this->info.alba.mode = RESET_MODE;
                this->info.alba.regno = this->regno;
                this->info.alba.rb.val = this->val;
                send_JOB(ALBA, &this->info.alba);
                break;

            case READ_AD7124:
                if (sscanf_P(this->bp, PSTR("%*c,%i"), &this->regno) != 1) {
                    send_REPLY_RESULT(SELF, EINVAL);
                    break;
                }
                this->info.alba.mode = READ_MODE;
                this->info.alba.regno = this->regno;
                this->info.alba.rb.val = this->val;
                send_JOB(ALBA, &this->info.alba);
                break;

            case WRITE_AD7124:
                if (sscanf_P(this->bp, PSTR("%*c,%i,%li"),
                               &this->regno, &this->val) != 2) {
                    send_REPLY_RESULT(SELF, EINVAL);
                    break;
                }
                this->info.alba.mode = WRITE_MODE;
                this->info.alba.regno = this->regno;
                this->info.alba.rb.val = this->val;
                send_JOB(ALBA, &this->info.alba);
                break;

            case WAIT_AD7124_READY:
                send_RDY_REQUEST(ALBA);
                break;

            case WRITE_MCP4728:
                {
                    int chan, ee, val, inh, ref, pm, ga;
                    if (sscanf_P(this->bp, PSTR("%*c,%i,%i,%i,%i,%i,%i,%i"),
                              &chan, &ee, &val, &inh, &ref, &pm, &ga) != 7) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    sae_MDAC_WRITE(this->info.mdac, chan, ee, val, inh, ref,
                                                                      pm, ga);
                }
                break;

            case SET_EGOR_COUNT:
                {
                    int count;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &count) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    send_SET_IOCTL(EGOR, SIOC_LOOP_COUNT, count);
                }
                break;

            case SET_EGOR_DISPLAY_MODE:
                {
                    int mode;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &mode) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    send_SET_IOCTL(EGOR, SIOC_DISPLAY_MODE, mode);
                }
                break;

            case SET_EGOR_OUTPUT:
                {
                    int dest;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &dest) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    send_SET_IOCTL(EGOR, SIOC_SELECT_OUTPUT, dest);
                }
                break;

            case START_STOP_EGOR:
                {
                    int val;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &val) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    if (val == 1)
                        send_START(EGOR);
                    else
                        send_STOP(EGOR);
                }
                break;

            case SET_OLED_CONTRAST:
                {
                    int val;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &val) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    this->msg.osetup.request.op = SET_CONTRAST;
                    this->msg.osetup.request.u.contrast.value = val & 0xFF;
                    send_osetup();
                }
                break;

            case SET_OLED_DISPLAY:
                {
                    int val;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &val) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    this->msg.osetup.request.op = SET_DISPLAY;
                    this->msg.osetup.request.u.display.value = val & 0x3;
                    send_osetup();
                }
                break;

            case SET_OLED_ORIGIN:
                {
                    int val;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &val) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    this->msg.osetup.request.op = SET_ORIGIN;
                    this->msg.osetup.request.u.origin.value = val & 0x3;
                    send_osetup();
                }
                break;

            case SET_OLED_LINESTART:
                {
                    int val;
                    if (sscanf_P(this->bp, PSTR("%*c,%i"), &val) != 1) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    this->msg.osetup.request.op = SET_LINESTART;
                    this->msg.osetup.request.u.linestart.value = val & 0x3F;
                    send_osetup();
                }
                break;

            case DRAW_OLED_TEXT:
                {
                    int x, y, rop, inh;
                    if (sscanf_P(this->bp, PSTR("%*c,%i,%i,%i,%i,"),
                                               &x, &y, &rop, &inh) != 4) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    char *s = this->bp;
                    uchar_t n = 5;
                    while (n && *s) {
                        if (*s++ == ',')
                            n--;
                    }
                    this->msg.osetup.request.op = DRAW_TEXT;
                    this->msg.osetup.request.u.text.x = x;
                    this->msg.osetup.request.u.text.y = y;
                    this->msg.osetup.request.u.text.cp = s;
                    this->msg.osetup.request.u.text.len = strlen(s);
                    this->msg.osetup.request.rop = rop;
                    this->msg.osetup.request.inh = inh;
                    send_osetup();
                }
                break;

            case DRAW_OLED_RECT:
                {
                    int x, y, w, h, rop, inh;
                    if (sscanf_P(this->bp, PSTR("%*c,%i,%i,%i,%i,%i,%i"),
                                        &x, &y, &w, &h, &rop, &inh) != 6) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    this->msg.osetup.request.op = DRAW_RECT;
                    this->msg.osetup.request.u.rect.x = x;
                    this->msg.osetup.request.u.rect.y = y;
                    this->msg.osetup.request.u.rect.w = w;
                    this->msg.osetup.request.u.rect.h = h;
                    this->msg.osetup.request.rop = rop;
                    this->msg.osetup.request.inh = inh;
                    send_osetup();
                }
                break;

            case DRAW_OLED_LINE:
                {
                    int x1, y1, x2, y2, rop, inh;
                    if (sscanf_P(this->bp, PSTR("%*c,%i,%i,%i,%i,%i,%i"),
                                    &x1, &y1, &x2, &y2, &rop, &inh) != 6) {
                        send_REPLY_RESULT(SELF, EINVAL);
                        break;
                    }
                    this->msg.osetup.request.op = DRAW_LINE;
                    this->msg.osetup.request.u.line.x1 = x1;
                    this->msg.osetup.request.u.line.y1 = y1;
                    this->msg.osetup.request.u.line.x2 = x2;
                    this->msg.osetup.request.u.line.y2 = y2;
                    this->msg.osetup.request.rop = rop;
                    this->msg.osetup.request.inh = inh;
                    send_osetup();
                }
                break;

            default:
                send_REPLY_RESULT(SELF, ENOSYS);
                return;
            }
            this->bp = tp;
        }
        break;
    }
}

PRIVATE void fetch_buffer(void)
{
    this->state = FETCHING_BUFFER;
    this->msg.fsd.request.taskid = SELF;
    this->msg.fsd.request.jobref = &this->info.twi;
    this->msg.fsd.request.sender_addr = HOST_ADDRESS;
    this->msg.fsd.request.op = OP_READ;
    this->msg.fsd.request.p.readf.inum = this->headp->inum;
    this->msg.fsd.request.p.readf.offset = this->fpos;
    this->msg.fsd.request.p.readf.len = BUFSIZE;
    this->msg.fsd.request.p.readf.whence = SEEK_SET;
    this->msg.fsd.request.p.readf.use_cache = this->cache;
    this->msg.fsd.request.p.readf.dst = this->sbuf;
    sae2_TWI_MTSR(this->info.twi, FS_ADDRESS,
            FSD_REQUEST, this->msg.fsd.request,
            FSD_REPLY, this->msg.fsd.reply);
    this->cache = TRUE;
}

PRIVATE void send_osetup(void)
{
    this->msg.osetup.request.taskid = SELF;
    this->msg.osetup.request.jobref = &this->info.twi;
    this->msg.osetup.request.sender_addr = HOST_ADDRESS;
    sae2_TWI_MTSR(this->info.twi, SPI_OLED_ADDRESS,
            OSETUP_REQUEST, this->msg.osetup.request,
            OSETUP_REPLY, this->msg.osetup.reply);
}

/* end code */
