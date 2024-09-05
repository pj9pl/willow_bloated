/* fs/mkfs.c */

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

/* Make file system.
 *
 * This utility creates a new file system in a type 0xfa partition.
 *
 * It is accessed by remote through an FSD OP_MKFS message. 
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/utc.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/mbr.h"
#include "fs/sdc.h"
#include "fs/fsd.h"
#include "fs/mkfs.h"

/* I am .. */
#define SELF MKFS
#define this mkfs

#define SECTORS_PER_INODE 17
#define FIRST_TWO_BITS 3 /* bits 1,0 */

typedef enum {
    IDLE = 0,
    FETCHING_PARTITION_TABLE,
    WRITING_SUPERBLOCK,
    WRITING_ROOT_INODE,
    WRITING_ROOT_DIRECTORY,
    WRITING_BOOTBLOCK,
    ZEROING_IMAP,
    ZEROING_ZMAP,
    ZEROING_ITABLE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    super_t super;
    ushort_t down_counter;
    ushort_t cur_sector;
    union {
        ssd_info ssd;
    } info;
} mkfs_t;

/* I have .. */
static mkfs_t *this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_mkfs(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this->state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            send_REPLY_RESULT(this->replyTo, m_ptr->RESULT);
            free(this);
            this = NULL;
        }
        break;

    case SET_IOCTL:
        if (m_ptr->IOCTL == SIOC_MKFS_COMMAND && m_ptr->LCOUNT == 1) {
            if (this == NULL && (this = calloc(1, sizeof(*this))) == NULL) {
                send_REPLY_RESULT(m_ptr->sender, ENOMEM);
            } else {
                if (this->state == IDLE) {
                    this->replyTo = m_ptr->sender;

                    /* Read sector zero. The SSD convenience function cannot
                     * be used as it operates with sector numbers relative to
                     * the start of the SFA partition.
                     */ 
                    this->state = FETCHING_PARTITION_TABLE;
                    this->info.ssd.buf = sd_admin.buf;
                    this->info.ssd.phys_sector = PARTITION_TABLE_SECTOR;
                    this->info.ssd.op = READ_SECTOR;
                    send_JOB(SSD, &this->info.ssd);
                } else {
                    send_REPLY_RESULT(SELF, EBUSY);
                }
            }
        } else {
            send_REPLY_RESULT(m_ptr->sender, EINVAL);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_PARTITION_TABLE:
        {
            uchar_t i;
            /* find the location and size of the partition to use. */
            if ((i = read_partition_table()) != EOK) {
                this->state = IDLE;
                send_REPLY_RESULT(this->replyTo, i);
            } else {
                this->state = WRITING_SUPERBLOCK;
                ushort_t n = sd_meta.totalSectors >> ZONE_SHIFT;
                this->super.s_nzones = MIN(n, 4096);
                this->super.s_ninodes = 384; /* 12 sectors @ 32/sector */
                this->super.s_max_size = MAX_FILE_SIZE; 
                this->super.s_magic = SUPER_MAGIC;
                memcpy(&sd_meta.super, &this->super, SUPER_SIZE);
                memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
                memcpy(sd_admin.buf, &this->super, SUPER_SIZE);
                sae_WRITE_SSD(this->info.ssd, SUPER_SECTOR_NUMBER,
                                                           sd_admin.buf);
            }
        }
        break;

    case WRITING_SUPERBLOCK:
        this->state = WRITING_ROOT_INODE;
        memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
        inode_t *ip = (inode_t *)sd_admin.buf;
        ip++;       /* move pointer to &inode[1] */
        ip->i_mode = I_DIRECTORY | R_BIT | W_BIT | X_BIT;
        ip->i_nlinks = 2;
        ip->i_size = DIRENT_SIZE * 2;
        ip->i_mtime = get_utc();
        ip->i_nzones = 1;
        ip->i_zone = 1;
        ip->i_inum = ROOT_INODE_NR;
        sae_WRITE_SSD(this->info.ssd, ITABLE_SECTOR_NUMBER, sd_admin.buf);
        break;

    case WRITING_ROOT_INODE:
        this->state = WRITING_ROOT_DIRECTORY;
        memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
        dir_struct *dp = (dir_struct *)sd_admin.buf;
        dp->d_inum = ROOT_INODE_NR;
        dp->d_name[0] = '.';
        dp++;
        dp->d_inum = ROOT_INODE_NR;
        dp->d_name[0] = '.';
        dp->d_name[1] = '.';
        sae_WRITE_SSD(this->info.ssd, ZONE_SECTORS(FIRST_DATA_ZONE),
                                                          sd_admin.buf);
        break;

    case WRITING_ROOT_DIRECTORY:
        this->state = WRITING_BOOTBLOCK;
        memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
        sd_admin.buf[0] = 0xde;
        sd_admin.buf[1] = 0xad;
        sd_admin.buf[2] = 0xbe;
        sd_admin.buf[3] = 0xef;
        sae_WRITE_SSD(this->info.ssd, BOOT_SECTOR_NUMBER, sd_admin.buf);
        break;

    case WRITING_BOOTBLOCK:
        this->state = ZEROING_IMAP;
        memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
        sd_admin.buf[0] = FIRST_TWO_BITS;
        sae_WRITE_SSD(this->info.ssd, IMAP_SECTOR_NUMBER, sd_admin.buf);
        break;

    case ZEROING_IMAP:
        this->state = ZEROING_ZMAP; 
        memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
        sd_admin.buf[0] = FIRST_TWO_BITS;
        this->state = ZEROING_ZMAP;
        sae_WRITE_SSD(this->info.ssd, ZMAP_SECTOR_NUMBER, sd_admin.buf);
        break;

    case ZEROING_ZMAP:
        this->state = ZEROING_ITABLE;
        this->down_counter = NR_ITABLE_SECTORS -1;
        this->cur_sector = ITABLE_SECTOR_NUMBER +1;
        memset(sd_admin.buf, '\0', sizeof(sd_admin.buf));
        /* fallthrough */

    case ZEROING_ITABLE:
        if (--this->down_counter == 0) {
            this->state = IDLE;
        }
        sae_WRITE_SSD(this->info.ssd, this->cur_sector++, sd_admin.buf);
        break;
    }
}

/* end code */
