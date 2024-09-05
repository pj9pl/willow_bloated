/* fs/ino.c */

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

/* An inode server.
 * Accepts get and put requests for inodes.
 *
 * This is used by both FSD and RWR, which use separate buffers.
 * The sector buffer to use is specified in the info.
 *
 * The private storage is permanent to prevent any failure caused by 
 * insufficient memory.
 *
 */

#include <string.h>
#include <time.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/utc.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/ino.h"

/* I am .. */
#define SELF INO
#define this ino

typedef enum {
    IDLE = 0,
    READING_ISECTOR
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ino_info *headp;
    union {
        ssd_info ssd;
    } info;
} ino_t;

/* I have .. */
static ino_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_ino(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL)
                    start_job();
            }
        }
        break;

    case JOB:
        {
            ino_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                ino_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
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
    this.state = READING_ISECTOR; 
    sae_READ_SSD(this.info.ssd,
        ITABLE_SECTOR_NUMBER + ITABLE_SECTOR(this.headp->inum),
        this.headp->buf);
}

PRIVATE void resume(void)
{
    int n = this.headp->inum & INODES_PER_BLOCK_MASK;
    inode_t *dp = (inode_t *)this.headp->buf;

    switch (this.state) {
    case IDLE:
        break;

    case READING_ISECTOR:
        switch (this.headp->op) {
        case GET_INODE:
            this.state = IDLE; 
            memcpy(this.headp->ip, dp + n, INODE_SIZE);
            this.headp->ip->i_inum = this.headp->inum;
            send_REPLY_RESULT(SELF, EOK);
            break;

        case PUT_INODE:
            this.state = IDLE; 
            this.headp->ip->i_mtime = get_utc();
            memcpy(dp + n, this.headp->ip,  INODE_SIZE);
            sae_WRITE_SSD(this.info.ssd,
                ITABLE_SECTOR_NUMBER + ITABLE_SECTOR(this.headp->inum),
                          this.headp->buf);
            break;

        default:
            send_REPLY_RESULT(SELF, ENOSYS);
            break;
        }
        break;
    }
}

/* convenience function */

PUBLIC void send_INO_JOB(ProcNumber sender, ino_info *cp, uchar_t op,
                                       inum_t inum, inode_t *ip, void *bp)
{
    cp->op = op;
    cp->inum = inum;
    cp->ip = ip;
    cp->buf = bp;
    send_m3(sender, SELF, JOB, cp);
}

/* end code */
