/* fs/indir.c */

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

/* A directory scanner.
 * Scans a directory for the first item with a given inode number.
 * If an item is found the reply result is EOK.
 * For all other result values the index is of the last compared item. 
 *
 * This task uses sd_admin.buf.
 */

#include <string.h>
#include <stdlib.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/ino.h"
#include "fs/indir.h"

/* I am .. */
#define SELF INDIR
#define this indir

typedef enum {
    IDLE = 0,
    FETCHING_PARENT_INODE,
    READING_SECTOR
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    indir_info *headp;
    ushort_t n_items;
    ushort_t cur_sector;
    inode_t myno;
    union {
        ino_info ino;
        ssd_info ssd;
    } info;
} indir_t;

/* I have .. */
static indir_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_indir(message *m_ptr)
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
            indir_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                indir_info *tp;
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
    this->state = FETCHING_PARENT_INODE;
    sae_GET_INODE(this->info.ino, this->headp->dir_inum, &this->myno,
                                                              sd_admin.buf);
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_PARENT_INODE:
        this->state = READING_SECTOR;
        this->n_items = DIRENT_ITEMS(this->myno.i_size);
        this->headp->d_idx = 0;
        this->cur_sector = ZONE_SECTORS(this->myno.i_zone);
        sae_READ_SSD(this->info.ssd, this->cur_sector, sd_admin.buf);
        break;

    case READING_SECTOR:
        {
            dir_struct *dp = (dir_struct *)sd_admin.buf;
            ushort_t limit = MIN(DIRENT_PER_BLOCK, this->n_items);
            if (this->n_items < DIRENT_PER_BLOCK) {
                this->n_items = 0;
            } else {
                this->n_items -= DIRENT_PER_BLOCK;
            }
            for (ushort_t i = 0; i < limit; i++, this->headp->d_idx++) {
                if (dp[i].d_inum == this->headp->base_inum) {
                    this->state = IDLE;
                    if (this->headp->bname) {
                        strncpy(this->headp->bname, dp[i].d_name, NAME_SIZE);
                    }
                    send_REPLY_RESULT(SELF, EOK);
                    return;
                }
            }
            if (this->n_items) {
                this->cur_sector++;
                sae_READ_SSD(this->info.ssd, this->cur_sector, sd_admin.buf);
            } else {
                this->state = IDLE;
                send_REPLY_RESULT(SELF, ENOENT);
            }
        }
        break;
    }
}

/* end code */
