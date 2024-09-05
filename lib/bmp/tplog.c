/* bmp/tplog.c */

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

/* A director task to read the BMP180 and send the
 * temperature and pressure to the RWR secretary on oslo.
 *
 * The 24-byte record is generated here and the buffer address
 * is sent to RWR on oslo, which pulls it using fido's MEMZ.
 *
 * SYSINIT sends an INIT message at start-up which starts the
 * process if the bootloader switch is open.
 *
 * START and STOP messages control its operation.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "sys/clk.h"
#include "sys/rv3028c7.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/fsd.h"
#include "fs/rwr.h"
#include "bmp/bmp.h"
#include "bmp/tplog.h"

/* I am .. */
#define SELF TPLOG
#define this tplog

#define BAROMETER_TYPE 0x9

#define ONE_HUNDRED_MILLISECONDS 100
#define TEN_SECONDS 10000
#define SIX_MINUTES 360000

#define RETRY_DELAY ONE_HUNDRED_MILLISECONDS
#define STARTUP_DELAY TEN_SECONDS
#define LOGGING_INTERVAL SIX_MINUTES

#define RECORD_LEN 24             /* be,XXXXXXXX,XX,XXXXXXXX\n */
#define MAX_FILES 4

typedef enum {
    IDLE = 0,
    FETCHING_INODES,
    FETCHING_UNIXTIME,
    READING_BAROMETER,
    WRITING_BAROGRAPH,
    AWAITING_ALARM
} __attribute__ ((packed)) state_t;

typedef struct {
    inum_t i_inum;
    ushort_t i_nzones;
} nbuf_t;

typedef struct {
    state_t state;
    unsigned next_file : 1;
    unsigned auto_start : 1;
    ProcNumber replyTo;
    uchar_t nerrors;
    time_t now;
    char cbuf[RECORD_LEN +1];
    char *basename;
    uchar_t idx;
    nbuf_t nbuf[MAX_FILES];
    inode_t *ibuf;
    union {
        fsd_msg fsd;
        rwr_msg rwr;
    } msg;
    union {
        clk_info clk;
        bmp_info bmp;
        twi_info twi;
    } info;
} tplog_t;

/* I have .. */
static tplog_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_tplog(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                this.nerrors++;
                if (this.state == WRITING_BAROGRAPH) {
                    switch (m_ptr->RESULT) {
                    case EACCES:
                    case ENODEV:
                        this.state = READING_BAROMETER;
                        sae_CLK_SET_ALARM(this.info.clk, RETRY_DELAY);
                        break;
                    }
                } else {
                    this.state = AWAITING_ALARM;
                    resume();
                }
            }
        } else if (this.replyTo) {
            send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
            this.replyTo = 0;
        }
        break;

    case INIT:
        if (bit_is_clear(BL_PIN, BL)) {
            /* If the bootloader switch is closed
             * don't start tplog after reset.
             */ 
            send_REPLY_RESULT(m_ptr->sender, EOK);
            break;
        }
        this.auto_start = TRUE;
        /* fallthrough */

    case START:
        if (this.state == IDLE) {
            if ((this.basename = calloc(1, NAME_SIZE +1)) == NULL) {
                send_REPLY_RESULT(m_ptr->sender, ENOMEM);
                break;
            }

            if ((this.ibuf = (inode_t *)calloc(MAX_FILES, sizeof(inode_t))) ==
                                                                    NULL) {
                free(this.basename);
                send_REPLY_RESULT(m_ptr->sender, ENOMEM);
                break;
            }
            
            this.idx = 0;
            this.replyTo = m_ptr->sender;
            this.state = FETCHING_INODES;
            if (this.auto_start == TRUE) {
                this.auto_start = FALSE;
                sae_CLK_SET_ALARM(this.info.clk, STARTUP_DELAY);
            } else {
                /* skip start-up delay */
                resume();
            }
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    case STOP:
        if (this.state) {
            if (this.state == AWAITING_ALARM) {
                this.replyTo = m_ptr->sender;
                sae_CLK_CANCEL(this.info.clk);
            } else {
                send_REPLY_RESULT(m_ptr->sender, EOK);
            }
            this.state = IDLE;
        } else {
            send_REPLY_RESULT(m_ptr->sender, EOK);
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
        break;

    case FETCHING_INODES:
        if (this.idx && this.msg.fsd.reply.result) {
            this.state = IDLE;
            send_REPLY_RESULT(this.replyTo, this.msg.fsd.reply.result);
            this.replyTo = 0;
            return;
        }

        if (this.idx < MAX_FILES) {
            sprintf_P(this.basename, PSTR("bar%d"), this.idx + 1);
            this.msg.fsd.request.taskid = SELF;
            this.msg.fsd.request.jobref = &this.info.twi;
            this.msg.fsd.request.sender_addr = HOST_ADDRESS;
            this.msg.fsd.request.op = OP_PATH;
            this.msg.fsd.request.p.path.src = this.basename;
            this.msg.fsd.request.p.path.len = strlen(this.basename);
            this.msg.fsd.request.p.path.ip = this.ibuf + this.idx;
            this.msg.fsd.request.p.path.cwd = ROOT_INODE_NR;
            sae2_TWI_MTSR(this.info.twi, FS_ADDRESS,
                    FSD_REQUEST, this.msg.fsd.request,
                    FSD_REPLY, this.msg.fsd.reply);
            this.idx++;
            break;        
        } else {
            time_t f_time = this.ibuf[0].i_mtime;
            this.idx = 0;
            /* compare #0 with #1, #2 and #3 */
            for (uchar_t i = 1; i < MAX_FILES; i++) {
                if (f_time < this.ibuf[i].i_mtime) {
                    f_time = this.ibuf[i].i_mtime;
                    this.idx = i;
                }
            }
            
            for (uchar_t i = 0; i < MAX_FILES; i++) {
                this.nbuf[i].i_inum = this.ibuf[i].i_inum;
                this.nbuf[i].i_nzones = this.ibuf[i].i_nzones;
            }

            free(this.ibuf);
            this.ibuf = NULL;
            free(this.basename);
            this.basename = NULL;
            send_REPLY_RESULT(this.replyTo, EOK);
            this.replyTo = 0;
        }
        /* fallthrough */

    case AWAITING_ALARM:
        this.state = FETCHING_UNIXTIME;
        sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                 RV_UNIX_TIME_0, this.now);
        break;

    case FETCHING_UNIXTIME:
        this.state = READING_BAROMETER;
        this.info.bmp.mode = READ_BMP;
        send_JOB(BMP, &this.info.bmp);
        break;

    case READING_BAROMETER:
        this.state = WRITING_BAROGRAPH;
        {
            ulong_t val = (this.info.bmp.T << 18) |
                          (this.info.bmp.p & 0x0003ffff);

            sprintf_P(this.cbuf, PSTR("be,%08lX,%02X,%08lX\n"),
                     this.now, BAROMETER_TYPE, val);
        }
        if (this.next_file) {
            if (++this.idx >= MAX_FILES)
                this.idx = 0;
        }    

        this.msg.rwr.request.taskid = SELF;
        this.msg.rwr.request.jobref = &this.info.twi;
        this.msg.rwr.request.sender_addr = HOST_ADDRESS;
        this.msg.rwr.request.inum = this.nbuf[this.idx].i_inum;
        this.msg.rwr.request.src = (uchar_t *)this.cbuf;
        this.msg.rwr.request.len = RECORD_LEN;
        this.msg.rwr.request.offset = 0;
        this.msg.rwr.request.whence = SEEK_END;
        this.msg.rwr.request.truncate = this.next_file ? TRUE : FALSE;
        sae2_TWI_MTSR(this.info.twi, FS_ADDRESS,
              RWR_REQUEST, this.msg.rwr.request,
              RWR_REPLY, this.msg.rwr.reply);
        this.next_file = FALSE;
        break;

    case WRITING_BAROGRAPH:
        if (this.msg.rwr.reply.result == EXFULL) {
            if (++this.idx >= MAX_FILES)
                this.idx = 0;
            this.msg.rwr.request.taskid = SELF;
            this.msg.rwr.request.jobref = &this.info.twi;
            this.msg.rwr.request.sender_addr = HOST_ADDRESS;
            this.msg.rwr.request.inum = this.nbuf[this.idx].i_inum;
            this.msg.rwr.request.src = (uchar_t *)this.cbuf;
            this.msg.rwr.request.len = RECORD_LEN;
            this.msg.rwr.request.offset = 0;
            this.msg.rwr.request.whence = SEEK_SET;
            this.msg.rwr.request.truncate = TRUE;
            sae2_TWI_MTSR(this.info.twi, FS_ADDRESS,
                  RWR_REQUEST, this.msg.rwr.request,
                  RWR_REPLY, this.msg.rwr.reply);
        } else {
            if (BYTE_ZONE(this.msg.rwr.reply.fpos + RECORD_LEN) >=
                                              this.nbuf[this.idx].i_nzones) {
                /* insufficient space for the next record in current file */
                this.next_file = TRUE;
            }
            this.state = AWAITING_ALARM;
            sae_CLK_SET_ALARM(this.info.clk, LOGGING_INTERVAL);
        }
        break;
    }
}

/* end code */
