/* cli/mv.c */

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

/* 'mv source [...] destination'
 *
 * e.g. mv item [...] directory
 *      mv item existing_file_name
 *      mv item new_name
 */
 
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "cli/mv.h"

/* I am .. */
#define SELF MV
#define this mv

#define DEPTH 20 /* arbitrary size of pathtoroot */

typedef enum {
    IDLE = 0,
    RESOLVING_DST_DIR_INUM,
    RESOLVING_DST_BASE_INUM,
    FETCHING_PARENT_INUM,
    PARSE_SRC_PATH,
    RESOLVING_SRC_DIR_INUM,
    RESOLVING_SRC_BASE_INUM,
    UNLINKING_DST,
    LINKING_FILE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned dst_isdir : 1;
    mv_info *headp;
    char *dst_base_name;
    inum_t dst_dir_inum;
    inum_t dst_base_inum;
    char *src_base_name;
    inum_t src_dir_inum;
    inum_t src_base_inum;
    uchar_t optind;
    inode_t myno;
    inum_t *pathtoroot;
    uchar_t pidx;
    union {
        fsd_msg fsd;
    } msg;
    union {
        twi_info twi;
    } info;
} mv_t;

/* I have .. */
static mv_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE uchar_t checknamechars(char *sp);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_mv(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this->state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this->state = IDLE;
            if (this->pathtoroot) {
                free(this->pathtoroot);
                this->pathtoroot = NULL;
            }
            if (this->headp) {
                if (m_ptr->RESULT == EOK)
                    m_ptr->RESULT = this->msg.fsd.reply.result;
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
            mv_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                mv_info *tp;
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
    if (this->headp->argc < 3) {
        send_REPLY_RESULT(SELF, EINVAL);
        return;
    }

    if ((this->pathtoroot = calloc(DEPTH, sizeof(inum_t))) == NULL) {
        send_REPLY_RESULT(SELF, ENOMEM);
        return;
    }

    /* Process the destination path first to determine
     * whether it is a directory or a regular file
     * as a directory can accomodate multiple sources
     * whereas a regular file cannot.
     */

    char *sp = this->headp->argv[this->headp->argc -1]; 
    uchar_t len = strlen(sp);
    if (len == 1) {
        if (*sp == '/') {
            this->dst_dir_inum = ROOT_INODE_NR;
            this->msg.fsd.reply.p.path.base_inum = ROOT_INODE_NR;
            this->myno.i_mode = I_DIRECTORY;
            this->state = RESOLVING_DST_BASE_INUM;
            resume();
        } else {
            this->msg.fsd.reply.p.path.base_inum = this->headp->cwd;
            this->dst_base_name = sp;
            this->state = RESOLVING_DST_DIR_INUM;
            resume();
        }
    } else {
        char *tp = sp + strlen(sp) -1;
        inum_t dwd = this->headp->cwd;
        while (*tp == '/') {
            *tp-- = '\0';
        }
        if (*sp == '/') {
            dwd = ROOT_INODE_NR;
            while (*sp == '/') {
                sp++;
            }
        }
        tp = strrchr(sp, '/');
        if (tp) {
            *tp++ = '\0';
            this->dst_base_name = tp;
        } else {
            this->dst_base_name = sp;
        }

        if (strlen(this->dst_base_name) > NAME_SIZE) {
            send_REPLY_RESULT(SELF, ENAMETOOLONG);
            return;
        }

        if (checknamechars(this->dst_base_name)) {
            send_REPLY_RESULT(SELF, EINVAL);
            return;
        }

        if (tp) {
            this->state = RESOLVING_DST_DIR_INUM;
            this->msg.fsd.request.op = OP_PATH;
            this->msg.fsd.request.p.path.src = sp;
            this->msg.fsd.request.p.path.len = strlen(sp);
            this->msg.fsd.request.p.path.cwd = dwd;
            this->msg.fsd.request.p.path.ip = &this->myno;
            send_fsd();
        } else {
            this->msg.fsd.reply.result = EOK;
            this->msg.fsd.reply.p.path.base_inum = this->headp->cwd;
            this->state = RESOLVING_DST_DIR_INUM;
            resume();
        }
    }
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case RESOLVING_DST_DIR_INUM:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            this->dst_dir_inum = this->msg.fsd.reply.p.path.base_inum;
            if (this->dst_dir_inum == INVALID_INODE_NR) {
                send_REPLY_RESULT(SELF, ENOENT);
            } else {
                this->state = RESOLVING_DST_BASE_INUM;
                this->msg.fsd.request.op = OP_PATH;
                this->msg.fsd.request.p.path.src = this->dst_base_name;
                this->msg.fsd.request.p.path.len = strlen(this->dst_base_name);
                this->msg.fsd.request.p.path.cwd = this->dst_dir_inum;
                this->msg.fsd.request.p.path.ip = &this->myno;
                send_fsd();
            }
        } 
        break;

    case RESOLVING_DST_BASE_INUM:
        if (this->msg.fsd.reply.result) {
            if (this->msg.fsd.reply.result == ENOENT) {
                /* The dst basename does not exist. Rename a single src. */
                if (strlen(this->dst_base_name) > NAME_SIZE) {
                    send_REPLY_RESULT(SELF, ENAMETOOLONG);
                    break;
                }
            } else {
                send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
                break;
            }
        }
        this->dst_base_inum = this->msg.fsd.reply.p.path.base_inum;
        if (this->dst_base_inum != INVALID_INODE_NR) {
            this->dst_isdir = FALSE;
            if ((this->myno.i_mode & I_TYPE) == I_DIRECTORY) {
                this->dst_isdir = TRUE;
                this->pidx = 0;
                this->pathtoroot[this->pidx] = this->myno.i_inum;
                this->state = FETCHING_PARENT_INUM;
                this->msg.fsd.reply.result = EOK;
                resume();
                break;
            } else if ((this->myno.i_mode & I_TYPE) == I_REGULAR) {
                if ((this->myno.i_mode & W_BIT) == 0) {
                    send_REPLY_RESULT(SELF, EPERM);
                    break;
                }
            }
        }
        this->pidx = 0;
        this->pathtoroot[this->pidx] = this->dst_dir_inum;
        this->state = FETCHING_PARENT_INUM;
        this->msg.fsd.reply.result = EOK;
        resume();
        break;

    case FETCHING_PARENT_INUM:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else if (this->pathtoroot[this->pidx] == ROOT_INODE_NR) {
            this->state = PARSE_SRC_PATH;
            resume();
        } else {
            this->msg.fsd.request.op = OP_READ;
            this->msg.fsd.request.p.readf.offset = sizeof(dir_struct);
            this->msg.fsd.request.p.readf.use_cache = FALSE;
            this->msg.fsd.request.p.readf.inum = this->pathtoroot[this->pidx];
            this->msg.fsd.request.p.readf.len = sizeof(inum_t);
            this->msg.fsd.request.p.readf.whence = SEEK_SET;
            this->msg.fsd.request.p.readf.dst = &this->pathtoroot[++this->pidx];
            send_fsd();
        }
        break;

    case PARSE_SRC_PATH:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            /* process a source path */
            char *sp = this->headp->argv[++this->optind];
            uchar_t len = strlen(sp);
            if (len == 1) {
                if (*sp == '/') {
                    /* cannot move the root directory */
                    send_REPLY_RESULT(SELF, EPERM);
                } else {
                    this->src_base_name = sp;
                    this->state = RESOLVING_SRC_DIR_INUM;
                    this->msg.fsd.request.op = OP_IFETCH;
                    this->msg.fsd.request.p.ifetch.inum = this->headp->cwd;
                    this->msg.fsd.request.p.ifetch.ip = &this->myno;
                    send_fsd();
                }
            } else {
                char *tp = sp + len -1;
                inum_t dwd = this->headp->cwd;
                /* trim any trailing slashes */
                while (*tp == '/') {
                    *tp-- = '\0';
                }
                /* detect and step over any leading slashes */
                if (*sp == '/') {
                    /* src is relative to ROOT i.e. absolute */
                    dwd = ROOT_INODE_NR;
                    while (*sp == '/') {
                        sp++;
                    }
                }

                /* detect the last slash to separate dirname from basename */
                char *loc = strrchr(sp, '/');
                if (loc) {
                    *loc++ = '\0';
                    this->src_base_name = loc;
                    this->state = RESOLVING_SRC_DIR_INUM;
                    this->msg.fsd.request.op = OP_PATH;
                    this->msg.fsd.request.p.path.src = sp;
                    this->msg.fsd.request.p.path.len = strlen(sp);
                    this->msg.fsd.request.p.path.cwd = dwd;
                    this->msg.fsd.request.p.path.ip = &this->myno;
                    send_fsd();
                } else {
                    this->src_base_name = sp;
                    this->state = RESOLVING_SRC_DIR_INUM;
                    this->msg.fsd.reply.result = EOK;
                    this->msg.fsd.reply.p.path.base_inum = dwd;
                    resume();
                }
            }
        }
        break;

    case RESOLVING_SRC_DIR_INUM:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            this->src_dir_inum = this->msg.fsd.reply.p.path.base_inum;
            if (this->src_dir_inum == INVALID_INODE_NR) {
                send_REPLY_RESULT(SELF, ENOENT);
            } else {
                this->state = RESOLVING_SRC_BASE_INUM;
                this->msg.fsd.request.op = OP_PATH;
                this->msg.fsd.request.p.path.src = this->src_base_name;
                this->msg.fsd.request.p.path.len = strlen(this->src_base_name);
                this->msg.fsd.request.p.path.cwd = this->src_dir_inum;
                this->msg.fsd.request.p.path.ip = &this->myno;
                send_fsd();
            }
        }
        break;

    case RESOLVING_SRC_BASE_INUM:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            this->src_base_inum = this->msg.fsd.reply.p.path.base_inum;
            if (this->src_base_inum == INVALID_INODE_NR) {
                send_REPLY_RESULT(SELF, ENOENT);
            } else {
                /* check that the src is not a path component of dst */
                for (uchar_t n = 0; this->pathtoroot[n]; n++) {
                    if (this->src_base_inum == this->pathtoroot[n]) {
                        send_REPLY_RESULT(SELF, EPERM);
                        return;
                    } 
                }
                if (this->dst_base_inum != INVALID_INODE_NR) {
                    /* dst_base_name exists */
                    if (this->dst_isdir) {
                        this->state = LINKING_FILE;
                        this->msg.fsd.request.op = OP_LINK;
                        this->msg.fsd.request.p.link.src =
                                                   this->src_base_name;
                        this->msg.fsd.request.p.link.len =
                                                   strlen(this->src_base_name);
                        this->msg.fsd.request.p.link.base_inum =
                                                   this->src_base_inum;
                        this->msg.fsd.request.p.link.dir_inum =
                                                   this->dst_base_inum;
                        send_fsd();
                    } else {
                        /* dst_base_name is not a directory.
                         * check that there is only one src
                         * then unlink the dst.
                         */
                        if (this->headp->argc == 3) {
                            this->state = UNLINKING_DST;
                            this->msg.fsd.request.op = OP_UNLINK;
                            this->msg.fsd.request.p.unlink.src =
                                                   this->dst_base_name;
                            this->msg.fsd.request.p.unlink.len =
                                                   strlen(this->dst_base_name);
                            this->msg.fsd.request.p.unlink.dir_inum =
                                                   this->dst_dir_inum;
                            send_fsd();
                        } else {
                            send_REPLY_RESULT(SELF, ENOTDIR);
                        }
                    }
                } else {
                    /* dst_base_name does not exist */
                    if (this->headp->argc == 3) {
                        this->state = UNLINKING_DST;
                        this->msg.fsd.reply.result = EOK;
                        resume();
                    } else {
                        send_REPLY_RESULT(SELF, ENOTDIR);
                    }
                }
            }
        }
        break;

    case UNLINKING_DST:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
           /* link a single src to dst_base_name in dst_dir_inum */
            this->state = LINKING_FILE;
            this->msg.fsd.request.op = OP_LINK;
            this->msg.fsd.request.p.link.src = this->dst_base_name;
            this->msg.fsd.request.p.link.len = strlen(this->dst_base_name);
            this->msg.fsd.request.p.link.base_inum = this->src_base_inum;
            this->msg.fsd.request.p.link.dir_inum = this->dst_dir_inum;
            send_fsd();
        }
        break;

    case LINKING_FILE:
        if (this->msg.fsd.reply.result) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        } else {
            if (this->optind == this->headp->argc -2) {
                this->state = IDLE;
            } else {
                this->state = PARSE_SRC_PATH;
            }
            this->msg.fsd.request.op = OP_UNLINK;
            this->msg.fsd.request.p.unlink.src = this->src_base_name;
            this->msg.fsd.request.p.unlink.len = strlen(this->src_base_name);
            this->msg.fsd.request.p.unlink.dir_inum = this->src_dir_inum;
            send_fsd();
        }
        break;
    }
}

PRIVATE uchar_t checknamechars(char *sp)
{
    while (*sp) {
        if (isalnum(*sp) || *sp == '.' || *sp == '_' || *sp == '-')
            sp++;
        else
            return EINVAL;
    }
    return EOK;
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
