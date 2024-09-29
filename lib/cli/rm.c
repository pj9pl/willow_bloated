/* cli/rm.c */

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

/* 'rm [-rf] item [...]'
 *
 * usage rm file [...]
 *       rm -f file [...]
 *       rm -r directory [...]
 *       rm -r -f directory [...]
 *       rm -rf directory [...]
 *       rm -fr directory [...]
 *       rmdir directory [...]
 *
 * n.b. the -r and -f options are not yet fully implemented.
 */
 
#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "cli/rm.h"

/* I am .. */
#define SELF RM
#define this rm

typedef enum {
    IDLE = 0,
    UNLINKING_ITEM,
    FETCHING_ITEM_INODE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned dironly : 1;
    unsigned recursive : 1;
    unsigned forced : 1;
    rm_info *headp;
    uchar_t optind;
    inode_t myno;
    union {
        fsd_msg fsd;
    } msg;
    union {
        twi_info twi;
    } info;
} rm_t;

/* I have .. */
static rm_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_rm(message *m_ptr)
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
            rm_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                rm_info *tp;
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
    if (strcmp_P(this->headp->argv[0], PSTR("rm")) == 0) {
        this->dironly = FALSE;
    } else if (strcmp_P(this->headp->argv[0], PSTR("rmdir")) == 0) {
        this->dironly = TRUE;
    }
    for (this->optind = 1; this->optind < this->headp->argc; this->optind++) {
        if (this->headp->argv[this->optind][0] == '-') {
            if (strchr(this->headp->argv[this->optind], 'r')) {
                this->recursive = TRUE;
            }
            if (strchr(this->headp->argv[this->optind], 'f')) {
                this->forced = TRUE;
            }
        } else {
            break;
        }
    }

    if (this->optind < this->headp->argc) {
        this->state = UNLINKING_ITEM;
        this->msg.fsd.reply.result = EOK;
        resume();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}    

PRIVATE void resume(void)
{
    inum_t dwd;

    switch (this->state) {
    case IDLE:
        break;

    case UNLINKING_ITEM:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            this->state = FETCHING_ITEM_INODE;
            this->msg.fsd.request.op = OP_PATH;
            this->msg.fsd.request.p.path.src =
                               this->headp->argv[this->optind];
            this->msg.fsd.request.p.path.len =
                               strlen(this->headp->argv[this->optind]);
            this->msg.fsd.request.p.path.cwd = this->headp->cwd;
            this->msg.fsd.request.p.path.ip = &this->myno;
            send_fsd();
        }
        break;

    case FETCHING_ITEM_INODE:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            dwd = this->msg.fsd.reply.p.path.dir_inum;
            if (dwd == INVALID_INODE_NR) {
                send_REPLY_RESULT(SELF, ENOENT);
            } else {
                uchar_t mode = this->myno.i_mode & I_TYPE;
                if ((mode == I_DIRECTORY && this->dironly) ||
                                    (mode == I_REGULAR && !this->dironly)) {
                    char *sp = strrchr(this->headp->argv[this->optind], '/');
                    if (sp) {
                        sp++;
                    } else {
                        sp = this->headp->argv[this->optind];
                    }
                    this->msg.fsd.request.op = OP_UNLINK;
                    this->msg.fsd.request.p.unlink.src = sp;
                    this->msg.fsd.request.p.unlink.len = strlen(sp);
                    this->msg.fsd.request.p.unlink.dir_inum = dwd;
                    send_fsd();
                } else {
                    send_REPLY_RESULT(SELF, EPERM);
                }
                this->state = (++this->optind < this->headp->argc) ?
                                                 UNLINKING_ITEM : IDLE;
            }
        }
        break;
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
