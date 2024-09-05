/* fs/path.c */

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

/* path - resolve a pathname to an inode number.
 *
 * The info request contains the pathname and cwd.
 * If the inode reference is non-zero, transfer the inode to it.
 * The info reply contains the inum of the basename.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/ino.h"
#include "fs/scan.h"
#include "fs/path.h"

/* I am .. */
#define SELF PATH
#define this path

typedef enum {
    IDLE = 0,
    FETCHING_INODE,
    SCANNING_DIRECTORY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    path_info *headp;
    char *safe;
    char *sp;
    inode_t myno;
    union {
        ino_info ino;
        scan_info scan;
    } info;
} path_t;

/* I have .. */
static path_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_path(message *m_ptr)
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
            path_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                path_info *tp;
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
    this->sp = this->headp->bp;
    if (*this->sp == '/')
        this->headp->base_inum = ROOT_INODE_NR;
    this->headp->dir_inum = this->headp->base_inum;
    this->state = FETCHING_INODE;
    sae_GET_INODE(this->info.ino,
              this->headp->base_inum, &this->myno, sd_admin.buf);
    this->sp = strtok_rP(this->sp, PSTR("/"), &this->safe);
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_INODE:
        if (this->sp) { /* another component ahead */
            if ((this->myno.i_mode & I_TYPE) == I_DIRECTORY) {
                this->headp->dir_inum = this->myno.i_inum;
                this->state = SCANNING_DIRECTORY;
                this->info.scan.namep = this->sp;
                this->info.scan.ip = &this->myno;
                send_JOB(SCAN, &this->info.scan);
            } else {
                this->headp->base_inum = INVALID_INODE_NR;
                send_REPLY_RESULT(SELF, ENOTDIR);
            }
        } else { /* the end of the path */
            this->state = IDLE;
            uchar_t ret = (this->headp->base_inum == INVALID_INODE_NR) ?
                                                              ENOENT : EOK;
            send_REPLY_RESULT(SELF, ret);
        }
        break;

    case SCANNING_DIRECTORY:
        this->headp->base_inum = this->info.scan.inum;
        this->state = FETCHING_INODE;
        sae_GET_INODE(this->info.ino,
                 this->headp->base_inum, &this->myno, sd_admin.buf);
        this->sp = strtok_rP(NULL, PSTR("/"), &this->safe);
        break;
    }
}

/* end code */
