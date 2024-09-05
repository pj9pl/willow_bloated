/* cli/pwd.c */

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

/* 'pwd' command using PATH and INDIR.
 *
 * e.g. pwd
 */
 
#include <string.h>
#include <stdlib.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/ostream.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "cli/pwd.h"

/* I am .. */
#define SELF PWD
#define this pwd

typedef enum {
    IDLE = 0,
    FETCHING_INODE,
    FETCHING_COMPONENT
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    pwd_info *headp;
    inum_t parent;
    inum_t child;
    char tmpname[NAME_SIZE +1];
    char dotdot[3];
    char path[PATH_MAX +1];
    char *cp;
    inode_t myno;
    union {
        fsd_msg fsd;
        ostream_msg ostream;
    } msg;
    union {
        twi_info twi;
    } info;
} pwd_t;

/* I have .. */
static pwd_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void fetch_inode(void);
PRIVATE void fetch_component(void);
PRIVATE void print_path(void);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_pwd(message *m_ptr)
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
                    m_ptr->RESULT = this->msg.ostream.reply.result;
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
            pwd_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                pwd_info *tp;
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
    this->child = this->headp->cwd;
    this->cp = this->path + sizeof(this->path) -1;
    *this->cp = '\0';

    if (this->child == ROOT_INODE_NR) {
        *--this->cp = '/';
        print_path();
    } else {
        this->dotdot[0] = '.';
        this->dotdot[1] = '.';
        this->state = FETCHING_INODE;
        fetch_inode();
    }
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_INODE:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            this->parent = this->msg.fsd.reply.p.path.base_inum;
            this->state = FETCHING_COMPONENT;
            memset(this->tmpname, '\0', sizeof(this->tmpname));
            fetch_component();
        }
        break;

    case FETCHING_COMPONENT:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            uchar_t n = strlen(this->tmpname);
            if (this->cp - n < this->path +1) {
                send_REPLY_RESULT(SELF, ENAMETOOLONG);
            } else {
                this->cp -= n;
                memcpy(this->cp, this->tmpname, n);
                *--this->cp = '/'; 
                if (this->parent == ROOT_INODE_NR) {
                    this->state = IDLE;
                    print_path();
                } else {
                    this->child = this->parent;
                    this->state = FETCHING_INODE;
                    fetch_inode();
                }
            }
        }
        break;
    }
}

PRIVATE void fetch_inode(void)
{
    this->msg.fsd.request.op = OP_PATH;
    this->msg.fsd.request.p.path.src = this->dotdot;
    this->msg.fsd.request.p.path.len = strlen(this->dotdot);
    this->msg.fsd.request.p.path.cwd = this->child;
    this->msg.fsd.request.p.path.ip = &this->myno;
    send_fsd();
}   

PRIVATE void fetch_component(void)
{
    this->msg.fsd.request.op = OP_INDIR;
    this->msg.fsd.request.p.indir.bp = this->tmpname;
    this->msg.fsd.request.p.indir.dir_inum = this->parent;
    this->msg.fsd.request.p.indir.base_inum = this->child;
    send_fsd();
}

PRIVATE void print_path(void)
{
    this->msg.ostream.request.taskid = SELF;
    this->msg.ostream.request.jobref = &this->info.twi;
    this->msg.ostream.request.sender_addr = HOST_ADDRESS;
    this->msg.ostream.request.src = this->cp;
    this->msg.ostream.request.len = strlen(this->cp);
    sae2_TWI_MTSR(this->info.twi, this->headp->dest,
             OSTREAM_REQUEST, this->msg.ostream.request,
             OSTREAM_REPLY, this->msg.ostream.reply);
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
