/* fs/rwr.c */

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

/* Remote file write secretary.
 *
 * Fetch remote data and write it to a file.
 *
 * This requires exclusive access to the sd_datum.buf for the entire
 * operation, not merely the processing of an individual message.
 *
 */

#include <string.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/memz.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/ino.h"
#include "fs/rwr.h"

/* I am .. */
#define SELF RWR
#define this rwr

typedef enum {
    IDLE = 0,
    ENSLAVED,
    READING_INODE,
    READING_SECTOR,
    FETCHING_DATA,
    WRITING_SECTOR,
    WRITING_LAST_SECTOR,
    WRITING_INODE,
    SENDING_REPLY
}  __attribute__ ((packed))  state_t;

typedef struct {
    state_t state;
    ushort_t sect;
    ushort_t frag;
    ushort_t n_written;
    inode_t myno;
    rwr_msg sm;  /* service message */
    union {
        memz_msg memz;
    } msg;
    union {
        ssd_info ssd;
        ino_info ino;
        twi_info twi;
    } info;
} rwr_t;

/* I have .. */
static rwr_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_rwr(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state == ENSLAVED && m_ptr->sender == TWI) {
            if (m_ptr->RESULT == EOK) {
                start_job();
            } else {
                get_request();
            }
        } else if (this.state) {
            resume();
        }
        break;

    case INIT:
        {
            uchar_t result = EBUSY;
            if (this.state == IDLE) {
                get_request();
                result = EOK;
            }
            send_REPLY_RESULT(m_ptr->sender, result);
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
    sae_GET_INODE(this.info.ino, this.sm.request.inum,
                       &this.myno, sd_datum.buf);
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
    case ENSLAVED:
        break;

    case READING_INODE:
        if ((this.myno.i_mode & W_BIT) == 0) {
            send_reply(EPERM);
            break;
        }

        if (this.sm.request.truncate) {
            this.myno.i_size = 0;
        }

        if (this.sm.request.whence == SEEK_END) {
            this.sm.request.offset += this.myno.i_size;
        }

        if (BYTE_ZONE(this.sm.request.offset + this.sm.request.len) <
                                            this.myno.i_nzones) {
            this.state = READING_SECTOR;
            this.sect = ZONE_SECTORS(this.myno.i_zone) +
                                        BYTE_SECTOR(this.sm.request.offset);
            sae_READ_SSD(this.info.ssd, this.sect, sd_datum.buf);
        } else {
           /* Not enough space available to hold the complete message.
            * Inform the caller.
            */
            send_reply(EXFULL);
        }
        break;

    case READING_SECTOR:
        {
            this.state = FETCHING_DATA;
            ushort_t ofs = this.sm.request.offset & BLOCK_SIZE_MASK;
            this.frag = MIN(BLOCK_SIZE - ofs, this.sm.request.len);
            if (this.frag) {
                this.msg.memz.request.src = this.sm.request.src;
                this.msg.memz.request.len = this.frag;
                sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                        MEMZ_REQUEST,
                       &this.msg.memz.request, sizeof(this.msg.memz.request),
                        sd_datum.buf + ofs, this.frag);
            } else {
                send_REPLY_RESULT(SELF, EOK);
            }
        }
        break;

    case FETCHING_DATA:
       {
            /* MEMZ has inserted data into sd_datum.buf */
            ushort_t n = this.frag - this.info.twi.rcnt; 
            this.sm.request.offset += n;
            this.n_written += n;
            if (this.myno.i_size < this.sm.request.offset)
                this.myno.i_size = this.sm.request.offset;
            this.sm.request.src += n;
            this.sm.request.len -= n;

            this.state = this.sm.request.len ?
                                 WRITING_SECTOR : WRITING_LAST_SECTOR;

            sae_WRITE_SSD(this.info.ssd, this.sect, sd_datum.buf);
        }
        break;

    case WRITING_SECTOR:
        this.state = READING_SECTOR;
        sae_READ_SSD(this.info.ssd, this.sect++, sd_datum.buf);
        break;

    case WRITING_LAST_SECTOR:
        this.state = WRITING_INODE;
        sae_PUT_INODE(this.info.ino, this.myno.i_inum,
                       &this.myno, sd_datum.buf);
        break;

    case WRITING_INODE:
        send_reply(EOK);
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, RWR_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    off_t fpos = this.sm.request.offset;
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    /* inform the client of the current file position */
    this.sm.reply.fpos = fpos;
    this.sm.reply.nbytes = this.n_written;
    sae2_TWI_MT(this.info.twi, reply_address, RWR_REPLY, this.sm.reply);
}

/* end code */
