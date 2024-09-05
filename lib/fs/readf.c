/* fs/readf.c */

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

/* A file read agent.
 *
 * Read a portion of a file and write it to a remote buffer address.
 *
 */

#include <string.h>
#include <avr/io.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/memp.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/ino.h"
#include "fs/sdc.h"
#include "fs/fsd.h"
#include "fs/readf.h"

/* I am .. */
#define SELF READF
#define this readf

typedef enum {
    IDLE = 0,
    READING_INODE,
    READING_SECTOR,
    WRITING_OUTPUT
}  __attribute__ ((packed))  state_t;

typedef struct {
    state_t state;
    readf_info *headp;
    ushort_t sect_nr;
    ushort_t sect_ofs;
    ulong_t bytes_remaining;
    ushort_t nbytes;
    inode_t myno;
    union {
        memp_msg memp;
    } msg;
    union {
        ino_info ino;
        ssd_info ssd;
        twi_info twi;
    } info;
} readf_t;

/* I have .. */
static readf_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_readf(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        /* Reply to the headp->replyTo.
         * Point headp to headp->nextp, releasing the caller's resource.
         * If headp is not null, start the job.
         */
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
            readf_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                readf_info *tp;
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
    this.state = READING_INODE;
    if (this.headp->use_cache && this.myno.i_inum == this.headp->inum) {
        resume();
    } else {
        sae_GET_INODE(this.info.ino, this.headp->inum,
                       &this.myno, sd_admin.buf);
    }
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case READING_INODE:
        if (this.myno.i_size) {
            if (this.headp->whence == SEEK_END) {
                this.headp->offset += this.myno.i_size;
            }

            /* reject any position not within the file bounds */
            if (this.headp->offset < 0 || this.headp->offset >=
                                                    this.myno.i_size) {
                send_REPLY_RESULT(SELF, EIO);
                return;
            }

            /* shorten the request to EOF */
            if (this.myno.i_size < this.headp->offset + this.headp->len) {
                this.headp->len = this.myno.i_size - this.headp->offset;
            }

            long n = this.myno.i_size - this.headp->offset;
            this.bytes_remaining = MIN(this.headp->len, n);
            this.nbytes = 0;
            
            this.state = READING_SECTOR;
            this.sect_nr = BYTE_SECTOR(this.headp->offset) +
                           ZONE_SECTORS(this.myno.i_zone);
            this.sect_ofs = this.headp->offset & BLOCK_SIZE_MASK;

            sae_READ_SSD(this.info.ssd, this.sect_nr++, sd_admin.buf);
        } else {
            /* no data to output */
            this.state = IDLE;
            this.headp->offset = 0;
            this.headp->len = 0;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;

    case READING_SECTOR:
        this.state = WRITING_OUTPUT;
        this.msg.memp.request.taskid = SELF;
        this.msg.memp.request.jobref = &this.info.twi;
        this.msg.memp.request.sender_addr = HOST_ADDRESS;
        this.msg.memp.request.src = sd_admin.buf + this.sect_ofs;
        this.msg.memp.request.dst = this.headp->dst;
        this.msg.memp.request.len = MIN(this.bytes_remaining,
                                   BLOCK_SIZE - this.sect_ofs);
        sae2_TWI_MTSR(this.info.twi, this.headp->sender_addr,
              MEMP_REQUEST, this.msg.memp.request,
              MEMP_REPLY, this.msg.memp.reply);
        break;

    case WRITING_OUTPUT:
        this.bytes_remaining -= this.msg.memp.reply.count;
        this.headp->dst += this.msg.memp.reply.count;
        this.nbytes += this.msg.memp.reply.count;
        this.headp->offset += this.msg.memp.reply.count;
        this.sect_ofs = this.headp->offset & BLOCK_SIZE_MASK;
        if (this.bytes_remaining) {
            this.state = READING_SECTOR;
            sae_READ_SSD(this.info.ssd, this.sect_nr++, sd_admin.buf);
        } else {
            this.state = IDLE;
            this.headp->len = this.nbytes;
            send_REPLY_RESULT(SELF, EOK);
        }
        break;
    }
}

/* end code */
