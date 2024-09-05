/* fs/fsd.c */

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

/* File system device secretary.
 *
 * This receives a variety of FSD_REQUEST messages and engages the
 * appropriate agent to perform the job. In so doing it enforces mutual
 * exclusion over the sd_admin sector buffer, which all the agents use.
 *
 * When the reply from the job is received an FSD_REPLY is sent to the caller.
 */

#include <string.h>
#include <stdlib.h>
#include <avr/io.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/memz.h"
#include "net/memp.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "fs/sdc.h"
#include "fs/ssd.h"
#include "fs/ino.h"
#include "fs/mknod.h"
#include "fs/readf.h"
#include "fs/link.h"
#include "fs/unlink.h"
#include "fs/indir.h"
#include "fs/path.h"

/* I am .. */
#define SELF FSD
#define this fsd

typedef enum {
    IDLE = 0,
    ENSLAVED,
    FETCHING_MKNOD_NAME,
    MAKING_ITEM,
    FETCHING_LINK_NAME,
    LINKING_FILE,
    FETCHING_UNLINK_NAME,
    UNLINKING_FILE,
    FETCHING_PATH,
    EATING_PATH,
    FETCHING_PATH_INODE,
    TRANSFERRING_PATH_INODE,
    FETCHING_INODE,
    TRANSFERRING_INODE,
    FETCHING_IWRITE_INODE,
    WRITING_INODE,
    READING_FILE,
    MAKING_FILESYSTEM,
    READING_SECTOR,
    RESOLVING_INUM_TO_NAME,
    SKIPPING_INDIR_TRANSFER,
    TRANSFERRING_INDIR_NAME,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    inum_t t_inum;
    inum_t dir_inum;
    union {
      char *cbuf;
      inode_t *myno;
    } hp;
    fsd_msg sm;    /* service message */
    union {
        memz_msg memz;
        memp_msg memp;
    } msg;
    union {
        mknod_info mknod;
        link_info link;
        unlink_info unlink;
        path_info path;
        readf_info readf;
        indir_info indir;
        ino_info ino;
        ssd_info ssd;
        twi_info twi;
    } info;
} fsd_t;

/* I have .. */
static fsd_t this;

/* I can .. */
PRIVATE void exec_command(void);
PRIVATE void resume(message *m_ptr);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_fsd(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state == ENSLAVED && m_ptr->sender == TWI) {
            if (m_ptr->RESULT == EOK) {
                exec_command();
            } else {
                get_request();
            }
        } else if (this.state) {
            resume(m_ptr);
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

PRIVATE void exec_command(void)
{
    switch (this.sm.request.op) {
    case OP_MKNOD:
        if ((this.hp.cbuf = calloc(this.sm.request.p.mknod.len +1,
                                                 sizeof(uchar_t))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_MKNOD_NAME;
            this.msg.memz.request.src = this.sm.request.p.mknod.src;
            this.msg.memz.request.len = this.sm.request.p.mknod.len;
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                         MEMZ_REQUEST,
                        &this.msg.memz.request, sizeof(this.msg.memz.request),
                         this.hp.cbuf, this.msg.memz.request.len);
        }
        break;

    case OP_IFETCH:
        if ((this.hp.myno = malloc(sizeof(inode_t))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_INODE;
            sae_GET_INODE(this.info.ino, this.sm.request.p.ifetch.inum,
                   this.hp.myno, sd_admin.buf);
        }
        break;

    case OP_IWRITE:
        if ((this.hp.myno = malloc(sizeof(inode_t))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_IWRITE_INODE;
            this.msg.memz.request.src = this.sm.request.p.iwrite.ip;
            this.msg.memz.request.len = sizeof(inode_t);
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                         MEMZ_REQUEST,
                        &this.msg.memz.request, sizeof(this.msg.memz.request),
                         this.hp.myno, this.msg.memz.request.len);
        }
        break;

    case OP_READ:
        this.state = READING_FILE;
        this.info.readf.sender_addr = this.sm.request.sender_addr;
        this.info.readf.inum = this.sm.request.p.readf.inum;
        this.info.readf.dst = this.sm.request.p.readf.dst;
        this.info.readf.use_cache = this.sm.request.p.readf.use_cache;
        this.info.readf.whence = this.sm.request.p.readf.whence;
        this.info.readf.offset = this.sm.request.p.readf.offset;
        this.info.readf.len = this.sm.request.p.readf.len;
        send_JOB(READF, &this.info.readf);
        break;

    case OP_MKFS:
        this.state = MAKING_FILESYSTEM;
        send_SET_IOCTL(MKFS, SIOC_MKFS_COMMAND, 1);
        break;

    case OP_SECTOR:
        this.state = READING_SECTOR;
        sae_READ_SSD(this.info.ssd, this.sm.request.p.sectf.num, sd_admin.buf);
        break;

    case OP_BUFFER_ADDRESS:
        this.sm.reply.p.bufaddr.bufp = sd_admin.buf;
        send_reply(EOK);
        break;

    case OP_LINK:
        if ((this.hp.cbuf = calloc(this.sm.request.p.link.len +1,
                                               sizeof(uchar_t))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_LINK_NAME;
            this.msg.memz.request.src = this.sm.request.p.link.src;
            this.msg.memz.request.len = this.sm.request.p.link.len;
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                         MEMZ_REQUEST,
                        &this.msg.memz.request, sizeof(this.msg.memz.request),
                         this.hp.cbuf, this.msg.memz.request.len);
        }
        break;

    case OP_UNLINK:
        if ((this.hp.cbuf = calloc(this.sm.request.p.unlink.len +1,
                                                 sizeof(uchar_t))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_UNLINK_NAME;
            this.msg.memz.request.src = this.sm.request.p.unlink.src;
            this.msg.memz.request.len = this.sm.request.p.unlink.len;
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                         MEMZ_REQUEST,
                        &this.msg.memz.request, sizeof(this.msg.memz.request),
                         this.hp.cbuf, this.msg.memz.request.len);
        }
        break;

    case OP_PATH:
        if ((this.hp.cbuf = calloc(this.sm.request.p.path.len +1,
                                               sizeof(uchar_t))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_PATH;
            this.msg.memz.request.src = this.sm.request.p.path.src;
            this.msg.memz.request.len = this.sm.request.p.path.len;
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                         MEMZ_REQUEST,
                        &this.msg.memz.request, sizeof(this.msg.memz.request),
                         this.hp.cbuf, this.msg.memz.request.len);
        }
        break;

    case OP_INDIR:
        if ((this.hp.cbuf = calloc(NAME_SIZE +1, sizeof(char))) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = this.sm.request.p.indir.bp ? RESOLVING_INUM_TO_NAME
                                                    : SKIPPING_INDIR_TRANSFER;
            this.info.indir.bname = this.hp.cbuf;
            this.info.indir.base_inum = this.sm.request.p.indir.base_inum;
            this.info.indir.dir_inum = this.sm.request.p.indir.dir_inum;
            send_JOB(INDIR, &this.info.indir);
        }
        break;

    default:
        send_reply(ENOSYS);
        break;
    }
}
 
PRIVATE void resume(message *m_ptr)
{
    switch (this.state) {
    case IDLE:
    case ENSLAVED:
        break;

    case FETCHING_MKNOD_NAME:
        this.state = MAKING_ITEM;
        this.info.mknod.bname = this.hp.cbuf;
        this.info.mknod.cwd = this.sm.request.p.mknod.p_inum;
        this.info.mknod.nzones = this.sm.request.p.mknod.nzones;
        this.info.mknod.mode = this.sm.request.p.mknod.mode;
        send_JOB(MKNOD, &this.info.mknod);
        break;

    case READING_FILE:
        this.sm.reply.p.readf.fpos = this.info.readf.offset;
        this.sm.reply.p.readf.nbytes = this.info.readf.len;
        send_reply(m_ptr->RESULT);
        break;
 
    case FETCHING_LINK_NAME:
        this.state = LINKING_FILE;
        this.info.link.bname = this.hp.cbuf;
        this.info.link.inum = this.sm.request.p.link.base_inum;
        this.info.link.cwd = this.sm.request.p.link.dir_inum;
        send_JOB(LINK, &this.info.link);
        break;

    case FETCHING_UNLINK_NAME:
        this.state = UNLINKING_FILE;
        this.info.unlink.bname = this.hp.cbuf;
        this.info.unlink.cwd = this.sm.request.p.unlink.dir_inum;
        send_JOB(UNLINK, &this.info.unlink);
        break;

    case FETCHING_IWRITE_INODE:
        this.state = WRITING_INODE;
        sae_PUT_INODE(this.info.ino, this.sm.request.p.iwrite.inum,
                    this.hp.myno, sd_admin.buf);
        break;

    case MAKING_ITEM:
    case WRITING_INODE:
    case MAKING_FILESYSTEM:
    case READING_SECTOR:
    case LINKING_FILE:
    case UNLINKING_FILE:
    case TRANSFERRING_INODE:
        send_reply(m_ptr->RESULT);
        break;

    case FETCHING_INODE:
        this.state = TRANSFERRING_INODE;
        this.msg.memp.request.taskid = SELF;
        this.msg.memp.request.jobref = &this.info.twi;
        this.msg.memp.request.sender_addr = HOST_ADDRESS;
        this.msg.memp.request.src = this.hp.cbuf;
        this.msg.memp.request.dst = this.sm.request.p.ifetch.ip;
        this.msg.memp.request.len = sizeof(inode_t);
        sae2_TWI_MTSR(this.info.twi, this.sm.request.sender_addr,
          MEMP_REQUEST, this.msg.memp.request,
          MEMP_REPLY, this.msg.memp.reply);
        break;

    case FETCHING_PATH:
        this.state = EATING_PATH;
        this.info.path.bp = this.hp.cbuf;
        this.info.path.base_inum = this.sm.request.p.path.cwd;
        send_JOB(PATH, &this.info.path);
        break;

    case EATING_PATH:
        if (m_ptr->RESULT == EOK && this.sm.request.p.path.ip) {
            this.t_inum = this.info.path.base_inum;
            this.dir_inum = this.info.path.dir_inum;
            this.state = FETCHING_PATH_INODE;
            sae_GET_INODE(this.info.ino, this.t_inum,
                     this.hp.myno, sd_admin.buf);
        } else {
            this.sm.reply.p.path.base_inum = this.info.path.base_inum;
            this.sm.reply.p.path.dir_inum = this.info.path.dir_inum;
            send_reply(m_ptr->RESULT);
        }
        break;

    case FETCHING_PATH_INODE:
        this.state = TRANSFERRING_PATH_INODE;
        this.msg.memp.request.taskid = SELF;
        this.msg.memp.request.jobref = &this.info.twi;
        this.msg.memp.request.sender_addr = HOST_ADDRESS;
        this.msg.memp.request.src = this.hp.cbuf;
        this.msg.memp.request.dst = this.sm.request.p.path.ip;
        this.msg.memp.request.len = sizeof(inode_t);
        sae2_TWI_MTSR(this.info.twi, this.sm.request.sender_addr,
          MEMP_REQUEST, this.msg.memp.request,
          MEMP_REPLY, this.msg.memp.reply);
        break;

    case TRANSFERRING_PATH_INODE:
        this.sm.reply.p.path.base_inum = this.t_inum;
        this.sm.reply.p.path.dir_inum = this.dir_inum;
        send_reply(m_ptr->RESULT);
        break;

    case RESOLVING_INUM_TO_NAME:
        this.state = TRANSFERRING_INDIR_NAME;
        this.msg.memp.request.taskid = SELF;
        this.msg.memp.request.jobref = &this.info.twi;
        this.msg.memp.request.sender_addr = HOST_ADDRESS;
        this.msg.memp.request.src = this.hp.cbuf;
        this.msg.memp.request.dst = this.sm.request.p.indir.bp;
        this.msg.memp.request.len = strlen(this.hp.cbuf);
        sae2_TWI_MTSR(this.info.twi, this.sm.request.sender_addr,
          MEMP_REQUEST, this.msg.memp.request,
          MEMP_REPLY, this.msg.memp.reply);
        break;

    case SKIPPING_INDIR_TRANSFER:
    case TRANSFERRING_INDIR_NAME:
        this.sm.reply.p.indir.d_idx = this.info.indir.d_idx;
        send_reply(m_ptr->RESULT);
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void get_request(void)
{
    if (this.hp.cbuf) {
        free(this.hp.cbuf);
        this.hp.cbuf = NULL;
    }
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, FSD_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, FSD_REPLY, this.sm.reply);
}

/* end code */
