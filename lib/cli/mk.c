/* cli/mk.c */

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

/* create a file or make a directory.
 *
 * usage:  creat [nzones] <path>
 *         create [nzones] <path>
 *         mk [nzones] <path>
 *         mkdir <path>
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "cli/mk.h"

/* I am .. */
#define SELF MK
#define this mk

typedef enum {
    IDLE = 0,
    RESOLVING_PATH
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    mk_info *headp;
    inum_t dwd;
    char *basename;
    char *path;
    ushort_t nzones;
    uchar_t mode;
    union {
        fsd_msg fsd;
    } msg;
    union {
        twi_info twi;
    } info;
} mk_t;

/* I have .. */
static mk_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE uchar_t checknamechars(char *sp);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_mk(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this->state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this->state = IDLE;
            if (this->headp) {
                if (m_ptr->RESULT == EOK)
                    m_ptr->RESULT = this->msg.fsd.reply.result;
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
            mk_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                mk_info *tp;
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
    uchar_t ret = EINVAL;
    if (strcmp_P(this->headp->argv[0], PSTR("mkdir")) == 0) {
        if (this->headp->argc == 2) {
            this->nzones = 1;
            this->mode = I_DIRECTORY | X_BIT | R_BIT | W_BIT;
            this->path = this->headp->argv[1];
            ret = EOK;
        }
    } else { /* creat,create,mk */
        int tval = 0;
        if (this->headp->argc == 2) {
            tval = 1;
            this->path = this->headp->argv[1];
        } else if (this->headp->argc == 3) {
            char *bp = this->headp->argv[1];

            while (*bp && isdigit(*bp))
                tval = tval * 10 + *bp++ - '0';  

            this->path = this->headp->argv[2];
        }
        if (tval > 0 && tval < 4096) {
            this->nzones = (ushort_t) tval;
            this->mode = I_REGULAR | R_BIT | W_BIT;
            ret = EOK;
        }
    }
    if (ret == EOK) {
        char *sp = this->path;
        this->dwd = this->headp->cwd;
        if (*sp == '/') {
            this->dwd = ROOT_INODE_NR;
            while (*sp == '/')
                sp++;
        }
        char *bp = strrchr(sp, '/');
        if (bp) {
            *bp++ = '\0';
            this->basename = bp;
        } else {
            this->basename = sp;
        }

        if (strlen(this->basename) > NAME_SIZE) {
            send_REPLY_RESULT(SELF, ENAMETOOLONG);
            return;
        }

        if (checknamechars(this->basename)) {
            send_REPLY_RESULT(SELF, EINVAL);
            return;
        }

        this->state = RESOLVING_PATH;
        if (bp) {
            this->msg.fsd.request.op = OP_PATH;
            this->msg.fsd.request.p.path.src = sp;
            this->msg.fsd.request.p.path.len = strlen(sp);
            this->msg.fsd.request.p.path.cwd = this->dwd;
            this->msg.fsd.request.p.path.ip = NULL;
            send_fsd();
        } else {
            this->msg.fsd.reply.p.path.base_inum = this->dwd;
            resume();
        }
    } else {
        send_REPLY_RESULT(SELF, ret);
    }
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case  RESOLVING_PATH:
        this->dwd = this->msg.fsd.reply.p.path.base_inum;
        this->state = IDLE;
        this->msg.fsd.request.op = OP_MKNOD;
        this->msg.fsd.request.p.mknod.src = this->basename;
        this->msg.fsd.request.p.mknod.len = strlen(this->basename);
        this->msg.fsd.request.p.mknod.p_inum = this->dwd;
        this->msg.fsd.request.p.mknod.nzones = this->nzones;
        this->msg.fsd.request.p.mknod.mode = this->mode;
        send_fsd();
        break;
    }
}

PRIVATE uchar_t checknamechars(char *sp)
{
    while (*sp) {
        if (isalnum(*sp) || *sp == '.' || *sp == '_' || *sp == '-')
            sp++;
        else
            return EINVAL;
    }
    return EOK;
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
