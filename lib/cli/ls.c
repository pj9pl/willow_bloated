/* cli/ls.c */

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

/* 'ls [-ail] [filename] ...' list directory items.
 *
 * e.g. ls
 */
 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/rv3028c7.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/ostream.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "cli/ls.h"

/* I am .. */
#define SELF LS
#define this ls

#define BUF_SIZE (BLOCK_SIZE / sizeof(dir_struct))
#define LINE_LEN (66 + NAME_SIZE)

#define MONTH_LEN     3
#define SIX_MONTHS    15768000 /* seconds */

typedef enum {
    IDLE = 0,
    FETCHING_UNIXTIME,
    PROCESSING_NEXT_ARG,
    FETCHING_PARENT_INODE,
    FETCHING_BUFFER,
    COPYING_ITEM,
    FETCHING_INODE,
    PRINTING_ITEM
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned long_form : 1;
    unsigned list_all : 1;
    unsigned print_inum : 1;
    ls_info *headp;
    uchar_t optind;
    ushort_t dir_items;        /* directory size / sizeof(dir_struct) */
    ushort_t n_items;            /* number of items in current buffer */
    ushort_t cur_item;           /* current item in buffer */
    off_t fpos;                   /* file position to read from */
    inum_t d_inum;
    dir_struct dirent_buf[BUF_SIZE];
    char printbuf[LINE_LEN];
    inode_t arg_ino;
    inode_t item_ino;
    time_t now;
    union {
        fsd_msg fsd;
        ostream_msg ostream;
    } msg;
    union {
        twi_info twi;
    } info;
} ls_t;

/* I have .. */
static ls_t *this;
static const char __flash ascmonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void fetch_buffer(uchar_t use_cache);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_ls(message *m_ptr)
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
            ls_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                ls_info *tp;
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
    /* scan the input args for options */
    this->headp->n_items = 0;

    for (this->optind = 1; this->optind < this->headp->argc; this->optind++) {
        if (this->headp->argv[this->optind][0] == '-') {
            if (strchr(this->headp->argv[this->optind], 'l')) {
                this->long_form = TRUE;
            }
            if (strchr(this->headp->argv[this->optind], 'a')) {
                this->list_all = TRUE;
            }
            if (strchr(this->headp->argv[this->optind], 'i')) {
                this->print_inum = TRUE;
            }
        } else {
            break;
        }
    }

    this->state = FETCHING_UNIXTIME;
    sae2_TWI_MR(this->info.twi, RV3028C7_I2C_ADDRESS,
                RV_UNIX_TIME_0, this->now);
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_UNIXTIME:
    case PROCESSING_NEXT_ARG:
        this->d_inum = this->headp->cwd;

        this->state = FETCHING_PARENT_INODE;
        if (this->optind < this->headp->argc) {
            /* a filename arg */
            this->msg.fsd.request.op = OP_PATH;
            this->msg.fsd.request.p.path.src = this->headp->argv[this->optind];
            this->msg.fsd.request.p.path.len =
                                       strlen(this->headp->argv[this->optind]);
            this->msg.fsd.request.p.path.cwd = this->d_inum;
            this->msg.fsd.request.p.path.ip = &this->arg_ino;
        } else {
            this->msg.fsd.request.op = OP_IFETCH;
            this->msg.fsd.request.p.ifetch.inum = this->d_inum;
            this->msg.fsd.request.p.ifetch.ip = &this->arg_ino;
        }
        send_fsd();
        break;

    case FETCHING_PARENT_INODE:
        /* The parent inode has been fetched, calculate
         * the total number of dirent items from the i_size.
         */
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else if ((this->arg_ino.i_mode & I_TYPE) == I_DIRECTORY) {
            this->d_inum = this->arg_ino.i_inum;
            this->dir_items = DIRENT_ITEMS(this->arg_ino.i_size);
            this->state = FETCHING_BUFFER;
            this->fpos = 0;
            fetch_buffer(FALSE);
        } else if ((this->arg_ino.i_mode & I_TYPE) == I_REGULAR) {
            this->state = COPYING_ITEM;
            this->dir_items = 0;
            this->n_items = 1;
            this->cur_item = 0;
            memcpy(&this->item_ino, &this->arg_ino, sizeof(inode_t));
            this->dirent_buf[this->cur_item].d_inum = this->arg_ino.i_inum;
            strncpy(this->dirent_buf[this->cur_item].d_name,
                                   this->headp->argv[this->optind], NAME_SIZE);
            this->msg.fsd.reply.result = EOK;
            resume();
        } else {
            send_REPLY_RESULT(SELF, ENOENT);
        }
        break;

    case FETCHING_BUFFER:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
            break;
        } else {
            this->fpos = this->msg.fsd.reply.p.readf.fpos;
            this->n_items = DIRENT_ITEMS(this->msg.fsd.reply.p.readf.nbytes);
            if (this->dir_items < this->n_items) {
                this->dir_items = 0;
            } else {
                this->dir_items -= this->n_items;
            }
            this->cur_item = this->list_all ? 0 : 2;
        }
        /* fallthrough */

    case PRINTING_ITEM:
        while ((this->dirent_buf[this->cur_item].d_inum == INVALID_INODE_NR) ||
               ((this->list_all == FALSE) &&
                      (this->dirent_buf[this->cur_item].d_name[0] == '.'))) {
            if (this->cur_item < this->n_items) {
                this->cur_item++;
            } else {
                break;
            }
        }

        if (this->cur_item < this->n_items) {
            this->state = FETCHING_INODE;
            this->msg.fsd.request.op = OP_IFETCH;
            this->msg.fsd.request.p.ifetch.inum =
                                    this->dirent_buf[this->cur_item].d_inum;
            this->msg.fsd.request.p.ifetch.ip = &this->item_ino;
            send_fsd();
        } else if (this->dir_items > 0) {
            this->state = FETCHING_BUFFER;
            fetch_buffer(TRUE);
        } else if (++this->optind < this->headp->argc) {
            this->state = PROCESSING_NEXT_ARG;
            resume();
        } else {
            this->state = IDLE;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;

    case COPYING_ITEM:
    case FETCHING_INODE:
        /* print the item */
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            memset(this->printbuf, '\0', sizeof(this->printbuf));
            char *sp = this->printbuf;
            if (this->print_inum) {
                sprintf_P(sp + strlen(sp), PSTR("%3d "), this->item_ino.i_inum);
            }
            if (this->long_form) {
                char mon[MONTH_LEN +1];
                struct tm my_tm;
                time_t my_time = this->item_ino.i_mtime - UNIX_OFFSET;
                localtime_r(&my_time, &my_tm); 
                strncpy_P(mon, ascmonths + my_tm.tm_mon * MONTH_LEN, MONTH_LEN);
                mon[3] = '\0';
                sprintf_P(sp + strlen(sp), PSTR("%c%c%c%c %3d %3d %6ld %s %2d"),
                    (this->item_ino.i_mode & I_TYPE) == I_DIRECTORY ? 'd' : '-',
                     this->item_ino.i_mode & R_BIT ? 'r' : '-',
                     this->item_ino.i_mode & W_BIT ? 'w' : '-',
                     this->item_ino.i_mode & X_BIT ? 'x' : '-',
                     this->item_ino.i_nlinks,
                     this->item_ino.i_nzones,
                     this->item_ino.i_size,
                     mon,
                     my_tm.tm_mday);
                if (this->item_ino.i_mtime + SIX_MONTHS < this->now) {
                    sprintf_P(sp + strlen(sp), PSTR("  %04d "), my_tm.tm_year +
                                                                         1900);
                } else {
                    sprintf_P(sp + strlen(sp), PSTR(" %02d:%02d "),
                                                 my_tm.tm_hour, my_tm.tm_min);
                }
            }
            strncat(sp, this->dirent_buf[this->cur_item].d_name, NAME_SIZE);
            strcat_P(sp, PSTR("\n"));

            this->state = PRINTING_ITEM;
            this->msg.ostream.request.taskid = SELF;
            this->msg.ostream.request.jobref = &this->info.twi;
            this->msg.ostream.request.sender_addr = HOST_ADDRESS;
            this->msg.ostream.request.src = sp;
            this->msg.ostream.request.len = strlen(sp);
            sae2_TWI_MTSR(this->info.twi, this->headp->dest,
                 OSTREAM_REQUEST, this->msg.ostream.request,
                 OSTREAM_REPLY, this->msg.ostream.reply);
            this->headp->n_items++;
            this->cur_item++;
        }
        break;
    }
}

PRIVATE void fetch_buffer(uchar_t use_cache)
{
    this->msg.fsd.request.op = OP_READ;
    this->msg.fsd.request.p.readf.offset = this->fpos;
    this->msg.fsd.request.p.readf.use_cache = use_cache;
    this->msg.fsd.request.p.readf.inum = this->d_inum;
    this->msg.fsd.request.p.readf.len = sizeof(this->dirent_buf);
    this->msg.fsd.request.p.readf.whence = SEEK_SET;
    this->msg.fsd.request.p.readf.dst = this->dirent_buf;
    send_fsd();
}

PRIVATE void send_fsd(void)
{
    /* common fsd instructions */

    this->msg.fsd.request.taskid = SELF;
    this->msg.fsd.request.jobref = &this->info.twi;
    this->msg.fsd.request.sender_addr = HOST_ADDRESS;
    sae2_TWI_MTSR(this->info.twi, FS_ADDRESS,
           FSD_REQUEST, this->msg.fsd.request,
           FSD_REPLY, this->msg.fsd.reply);
}

/* end code */
