/* cli/cat.c */

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

/* 'cat [offset] <path>' cli command using FSD and SER.
 *
 * e.g. cat bar2
 *      cat 576 bar2
 */
 
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/ostream.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "cli/cat.h"

/* I am .. */
#define SELF CAT
#define this cat

#define BUF_SIZE 512

typedef enum {
    IDLE = 0,
    EATING_PATH,
    FETCHING_BUFFER,
    WRITING_BUFFER
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    cat_info *headp;
    ushort_t req_len;
    inum_t f_inum;
    char *path;
    inode_t myno;
    union {
        fsd_msg fsd;
    } msg;
    union {
        ser_info ser;
        twi_info twi;
    } info;
    uchar_t buf[BUF_SIZE];
} cat_t;

/* I have .. */
static cat_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void fetch_buffer(uchar_t use_cache);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_cat(message *m_ptr)
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
            cat_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                cat_info *tp;
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
    this->state = EATING_PATH;
    this->msg.fsd.request.op = OP_PATH;
    this->msg.fsd.request.p.path.src = this->headp->path;
    this->msg.fsd.request.p.path.len = strlen(this->headp->path);
    this->msg.fsd.request.p.path.cwd = this->headp->cwd;
    this->msg.fsd.request.p.path.ip = &this->myno;
    send_fsd();
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case EATING_PATH:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else if ((this->myno.i_mode & I_TYPE) != I_REGULAR) {
            send_REPLY_RESULT(SELF, ENOENT);
        } else if ((this->myno.i_mode & R_BIT) == 0) {
            send_REPLY_RESULT(SELF, EPERM);
        } else {
            this->f_inum = this->msg.fsd.reply.p.path.base_inum;
            fetch_buffer(FALSE);
        }
        break;

    case FETCHING_BUFFER:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            /* copy the current file position before it gets clobbered */
            this->headp->fpos = this->msg.fsd.reply.p.readf.fpos;
            ushort_t nbytes = this->msg.fsd.reply.p.readf.nbytes;
            this->state = (nbytes < this->req_len) ? IDLE : WRITING_BUFFER;
            if (nbytes) {
                sae_SER(this->info.ser, this->buf, nbytes);
            } else {
                send_REPLY_RESULT(SELF, EOK);
            }
        }
        break;

    case WRITING_BUFFER:
        fetch_buffer(TRUE);
        break;
    }
}

PRIVATE void fetch_buffer(uchar_t use_cache)
{
    if (this->headp->fpos < this->myno.i_size) {
        this->state = FETCHING_BUFFER;
        this->req_len = sizeof(this->buf);
        this->msg.fsd.request.op = OP_READ;
        this->msg.fsd.request.p.readf.offset = this->headp->fpos;
        this->msg.fsd.request.p.readf.use_cache = use_cache;
        this->msg.fsd.request.p.readf.inum = this->f_inum;
        this->msg.fsd.request.p.readf.len = sizeof(this->buf);
        this->msg.fsd.request.p.readf.whence = SEEK_SET;
        this->msg.fsd.request.p.readf.dst = this->buf;
        send_fsd();
    } else {
        this->state = IDLE;
        send_REPLY_RESULT(SELF, EOK);
    }
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
