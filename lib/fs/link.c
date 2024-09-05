/* fs/link.c */

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

/* Create a link to a directory item. */

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
#include "fs/link.h"

/* I am .. */
#define SELF LINK
#define this link

typedef enum {
    IDLE = 0,
    FETCHING_PARENT_INODE,
    SCANNING_DIRECTORY,
    FETCHING_INODE,
    WRITING_INODE,
    REFETCHING_PARENT_INODE,
    READING_PARENT_SECTOR,
    WRITING_PARENT_SECTOR
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    link_info *headp;
    inode_t myno;
    ushort_t sector_nr;
    union {
        scan_info scan;
        ino_info ino;
        map_info map;
        ssd_info ssd;
    } info;
} link_t;

/* I have .. */
static link_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_link(message *m_ptr)
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
            link_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                link_info *tp;
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
    sae_GET_INODE(this->info.ino, this->headp->cwd, &this->myno, sd_admin.buf);
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_PARENT_INODE:
        if ((this->myno.i_mode & W_BIT) == 0) {
            send_REPLY_RESULT(SELF, EPERM);
        } else if (this->myno.i_nlinks == MAX_LINKS) {
            send_REPLY_RESULT(SELF, EMLINK);
        } else {
            this->state = SCANNING_DIRECTORY;
            this->info.scan.namep = this->headp->bname;
            this->info.scan.ip = &this->myno;
            send_JOB(SCAN, &this->info.scan);
        }
        break;

    case SCANNING_DIRECTORY:
        if (this->info.scan.inum == INVALID_INODE_NR) {
            this->state = FETCHING_INODE;
            sae_GET_INODE(this->info.ino, this->headp->inum, &this->myno,
                                                               sd_admin.buf);
        } else {
            send_REPLY_RESULT(SELF, EEXIST);
        }
        break;

    case FETCHING_INODE:
        this->state = WRITING_INODE;
        if (this->myno.i_nlinks == MAX_LINKS) {
            send_REPLY_RESULT(SELF, EMLINK);
            return;
        } else {
            this->myno.i_nlinks++;
        }
        sae_PUT_INODE(this->info.ino, this->headp->inum, &this->myno,
                                                           sd_admin.buf);
        break;

    case WRITING_INODE:
        this->state = REFETCHING_PARENT_INODE;
        sae_GET_INODE(this->info.ino, this->headp->cwd, &this->myno,
                                                          sd_admin.buf);
        break;

    case REFETCHING_PARENT_INODE:
        this->state = READING_PARENT_SECTOR;
        this->sector_nr = 0;
        sae_READ_SSD(this->info.ssd,
                   ZONE_SECTORS(this->myno.i_zone) + this->sector_nr,
                                                      sd_admin.buf);
        break;

    case READING_PARENT_SECTOR:
        {
            ushort_t n;
            dir_struct *dp = (dir_struct *)sd_admin.buf;

            for (n = 0; n < DIRENT_PER_BLOCK; n++) {
                if (dp[n].d_inum == INVALID_INODE_NR)
                    break;
            }
            if (n == DIRENT_PER_BLOCK) {
                if (this->sector_nr < ZONE_SECTORS(this->myno.i_nzones) -1) {
                    this->sector_nr++;
                    sae_READ_SSD(this->info.ssd,
                          ZONE_SECTORS(this->myno.i_zone) + this->sector_nr,
                                                             sd_admin.buf);
                }
            } else {
                this->state = WRITING_PARENT_SECTOR;
                dp[n].d_inum = this->headp->inum;
                memset(dp[n].d_name, '\0', NAME_SIZE);
                strncpy(dp[n].d_name, this->headp->bname, NAME_SIZE);
                sae_WRITE_SSD(this->info.ssd,
                            ZONE_SECTORS(this->myno.i_zone) + this->sector_nr,
                                                               sd_admin.buf);
                this->myno.i_nlinks++;
                off_t siz = ((n + 1) << DIRENT_SIZE_SHIFT) +
                            (this->sector_nr << BLOCK_SIZE_SHIFT);
                if (siz > this->myno.i_size)
                    this->myno.i_size = siz;
            }
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
