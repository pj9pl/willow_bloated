/* fs/unlink.c */

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

/* Receives job requests from fsd.
 *   unlink basename
 */

#include <string.h>
#include <stdlib.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/ssd.h"
#include "fs/ino.h"
#include "fs/map.h"
#include "fs/scan.h"
#include "fs/fsd.h"
#include "fs/unlink.h"

/* I am .. */
#define SELF UNLINK
#define this unlink

typedef enum {
    IDLE = 0,
    FETCHING_PARENT_INODE,
    SCANNING_DIRECTORY,
    FETCHING_INODE,
    COUNTING_ENTRIES,
    FREEING_ZMAP,
    FREEING_IMAP,
    WRITING_OLD_INODE,
    REFETCHING_PARENT_INODE,
    READING_PARENT_SECTOR,
    WRITING_PARENT_SECTOR
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unlink_info *headp;
    inode_t myno;
    inum_t old_inum;
    ushort_t old_dirent_idx;
    ushort_t sector_nr;
    ushort_t tot_dirent;
    ushort_t n_dirent;
    union {
        scan_info scan;
        ino_info ino;
        map_info map;
        ssd_info ssd;
    } info;
} unlink_t;

/* I have .. */
static unlink_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_unlink(message *m_ptr)
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
            unlink_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                unlink_info *tp;
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
    sae_GET_INODE(this->info.ino, this->headp->cwd,
                    &this->myno, sd_admin.buf);
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_PARENT_INODE:
        if ((this->myno.i_mode & W_BIT) == 0) {
            send_REPLY_RESULT(SELF, EPERM);
        } else {
            this->state = SCANNING_DIRECTORY;
            this->info.scan.namep = this->headp->bname;
            this->info.scan.ip = &this->myno;
            send_JOB(SCAN, &this->info.scan);
        }
        break;

    case SCANNING_DIRECTORY:
        if (this->info.scan.inum == INVALID_INODE_NR) {
            send_REPLY_RESULT(SELF, ENOENT);
        } else {
            this->state = FETCHING_INODE;
            this->old_inum = this->info.scan.inum;
            this->old_dirent_idx = this->info.scan.dirent_idx;
            sae_GET_INODE(this->info.ino, this->old_inum,
                        &this->myno, sd_admin.buf);
        }
        break;

    case FETCHING_INODE:
        if ((this->myno.i_mode & W_BIT) == 0) {
            send_REPLY_RESULT(SELF, EPERM);
        } else if ((this->myno.i_mode & I_TYPE) == I_DIRECTORY) {
            if (this->myno.i_nlinks > 2) {
                /* The directory is not empty or there are hard links.
                 * The link count represents the number of directory items
                 * plus the number of hard links to this inode.
                 * Count the items to ascertain whether a hard link exists.
                 */
                /* Fetch the sector containing the first zone. */
                this->state = COUNTING_ENTRIES;
                this->tot_dirent = DIRENT_ITEMS(this->myno.i_size);
                this->n_dirent = 0;
                this->sector_nr = ZONE_SECTORS(this->myno.i_zone);
                sae_READ_SSD(this->info.ssd, this->sector_nr, sd_admin.buf);
            } else {
                this->state = FREEING_ZMAP;
                sae_FREE_ZMAP(this->info.map, this->myno.i_zone,
                                                    this->myno.i_nzones);
            }
        } else if ((this->myno.i_mode & I_TYPE) == I_REGULAR) {
            if (this->myno.i_nlinks > 1) {
                /* If there is more than one link to the myno,
                 * decrement the link count, and write it back.
                 * Skip the freeing of the myno bit and the zone bits
                 * and resume at the point where the parent dsector is
                 * read in so that the item inum can be zeroed.
                 */
                this->myno.i_nlinks--;
                this->state = WRITING_OLD_INODE;
                sae_PUT_INODE(this->info.ino, this->old_inum,
                            &this->myno, sd_admin.buf);
            } else {
                this->state = FREEING_ZMAP;
                sae_FREE_ZMAP(this->info.map, this->myno.i_zone,
                                                    this->myno.i_nzones);
            }
        } else {
            send_REPLY_RESULT(SELF, EINVAL);
        }
        break;

    case COUNTING_ENTRIES:
        {
            dir_struct *dp = (dir_struct *)sd_admin.buf;
            ushort_t limit = MIN(DIRENT_PER_BLOCK, this->tot_dirent);
            if (this->tot_dirent < DIRENT_PER_BLOCK) {
                this->tot_dirent = 0;
            } else {
                this->tot_dirent -= DIRENT_PER_BLOCK;
            }
            for (ushort_t i = 0; i < limit; i++) {
                if (dp[i].d_inum) {
                    this->n_dirent++;
                }
            }
            if (this->tot_dirent) {
                this->sector_nr++;
                sae_READ_SSD(this->info.ssd, this->sector_nr, sd_admin.buf);
            } else if (this->n_dirent < this->myno.i_nlinks) {
                this->myno.i_nlinks--;
                this->state = WRITING_OLD_INODE;
                sae_PUT_INODE(this->info.ino, this->old_inum,
                        &this->myno, sd_admin.buf);
            } else {
                send_REPLY_RESULT(SELF, ENOTEMPTY);
            }
        }
        break;
 
    case FREEING_ZMAP:
        this->state = FREEING_IMAP;
        sae_FREE_IMAP(this->info.map, this->myno.i_inum, 1);
        break;

    case FREEING_IMAP:
        this->state = WRITING_OLD_INODE;
        this->myno.i_mode = 0;
        this->myno.i_size = 0;
        this->myno.i_nlinks = 0;
        this->myno.i_nzones = 0;
        this->myno.i_zone = 0;
        this->myno.i_inum = 0;
        sae_PUT_INODE(this->info.ino, this->old_inum,
                    &this->myno, sd_admin.buf);
        break;

    case WRITING_OLD_INODE:
        this->state = REFETCHING_PARENT_INODE;
        sae_GET_INODE(this->info.ino, this->headp->cwd,
                    &this->myno, sd_admin.buf);
        break;

    case REFETCHING_PARENT_INODE:
        this->state = READING_PARENT_SECTOR;
        this->sector_nr = ZONE_SECTORS(this->myno.i_zone) +
                          DIRENT_SECTOR(this->old_dirent_idx);
        sae_READ_SSD(this->info.ssd, this->sector_nr, sd_admin.buf);
        break;

    case READING_PARENT_SECTOR:
        {
            this->state = WRITING_PARENT_SECTOR;
            dir_struct *dp = (dir_struct *)sd_admin.buf;
            dp[this->old_dirent_idx & DIRENT_PER_BLOCK_MASK].d_inum =
                                                      INVALID_INODE_NR;
            sae_WRITE_SSD(this->info.ssd, this->sector_nr, sd_admin.buf);
            this->myno.i_nlinks--;
        }
        break;

    case WRITING_PARENT_SECTOR:
        this->state = IDLE;
        sae_PUT_INODE(this->info.ino, this->headp->cwd, &this->myno,
                sd_admin.buf);
        break;
    }
}

/* end code */
