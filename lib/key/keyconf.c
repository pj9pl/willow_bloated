/* key/keyconf.c */

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

/* Keyconf is a jobbing server that reads a key config file containing
 * lines of the form:-
 *      B,key_nr,path
 * and then resolves the path to an inode number and sends:- 
 *      (key number, inode number)
 * tuplets to the keyexec task for insertion into its button transition
 * table. 1..8 = DOWN, 9..16 = UP.
 *
 * The file name is resolved to an inode number to save the resolution
 * being done during the exec phase.
 *
 * It receives a job from keysec.c containing the inode number of a config
 * file and the host ID of the caller.
 *
 */

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "key/keyexec.h"
#include "key/keyconf.h"

/* I am .. */
#define SELF KEYCONF
#define this keyconf

#define BUFSIZE 512
#define LINE_MAX 80

typedef enum {
    IDLE = 0,
    FETCHING_BUFFER,
    PROCESSING_RECORD,
    RESOLVING_PATH
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned more : 1;
    unsigned cache : 1;
    keyconf_info *headp;
    char nilbyte;  /* fudge a nilbyte at the end of sbuf */
    char *bp;
    ushort_t buf_bytes;
    int key_nr;
    int val;
    off_t fpos;
    inode_t myno;
    union {
        fsd_msg fsd;
    } msg;
    union {
        twi_info twi;
    } info;
    char lbuf[LINE_MAX +1];
    char sbuf[BUFSIZE];
} keyconf_t;

/* I have .. */
static keyconf_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void fetch_buffer(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_keyconf(message *m_ptr)
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
            keyconf_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                keyconf_info *tp;
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
    this->fpos = 0;
    this->cache = FALSE;
    this->headp->nlines = 0;
    fetch_buffer();
}

PRIVATE void fetch_buffer(void)
{
    this->state = FETCHING_BUFFER;
    this->msg.fsd.request.taskid = SELF;
    this->msg.fsd.request.jobref = &this->info.twi;
    this->msg.fsd.request.sender_addr = HOST_ADDRESS;
    this->msg.fsd.request.op = OP_READ;
    this->msg.fsd.request.p.readf.inum = this->headp->inum;
    this->msg.fsd.request.p.readf.offset = this->fpos;
    this->msg.fsd.request.p.readf.len = BUFSIZE;
    this->msg.fsd.request.p.readf.whence = SEEK_SET;
    this->msg.fsd.request.p.readf.use_cache = this->cache;
    this->msg.fsd.request.p.readf.dst = this->sbuf;
    sae2_TWI_MTSR(this->info.twi, FS_ADDRESS,
            FSD_REQUEST, this->msg.fsd.request,
            FSD_REPLY, this->msg.fsd.reply);
    this->cache = TRUE;
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_BUFFER:
        this->buf_bytes = this->msg.fsd.reply.p.readf.nbytes;
        if (this->buf_bytes == 0 || this->msg.fsd.reply.result != EOK) {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
            break;
        }
        if (this->buf_bytes < BUFSIZE) {
            this->more = FALSE;
            this->sbuf[this->buf_bytes] = '\0';
        } else {
            this->more = TRUE;
            /* this->nilbyte serves as a null terminator */
        }

        char *ep = this->sbuf + this->buf_bytes -1;
        char *final_newline = strrchr(this->sbuf, '\n');
        if (final_newline) {
            /* Discard any partial line and set fpos
             * to read the next buffer from that location.
             */
            this->fpos = this->msg.fsd.reply.p.readf.fpos -
                                                   (ep - final_newline);
            *final_newline = '\0';
        } else {
            /* line length exceeds buffer size: it's too big */
            send_REPLY_RESULT(SELF, E2BIG);
            break;
        }
        this->bp = this->sbuf;
        this->state = PROCESSING_RECORD;
        /* fallthrough */

    case PROCESSING_RECORD:
        {
            char *tp;
            while (1) {
                /* use a while loop to skip over comments */
                if (this->bp == NULL || *this->bp == '\0') {
                    if (this->more) {
                        fetch_buffer();
                    } else {
                        this->state = IDLE;
                        send_REPLY_RESULT(SELF, EOK);
                    }
                    return;
                }

                tp = this->bp;

                while (tp && *tp && *tp != '\n') {
                    if (tp < this->sbuf + this->buf_bytes) {
                        tp++;
                    } else {
                        this->state = IDLE;
                        send_REPLY_RESULT(SELF, EOK);
                        return;
                    }
                }

                if (tp && *tp == '\n') {
                    *tp++ = '\0';
                }

                this->headp->nlines++;

                if (this->bp[0] == '#') {
                    /* comment, continue with the next line */
                    this->bp = tp;
                } else {
                    /* not a comment */
                    break;
                }
            }

            switch (*this->bp) {
            case BUTTON_ASSOCIATION:
                /* set max string length to LINE_MAX */
                if (sscanf_P(this->bp, PSTR("%*c,%i,%80s"), &this->key_nr,
                                                          this->lbuf) != 2) {
                    send_REPLY_RESULT(SELF, EINVAL);
                    break;
                }
                this->key_nr--;
                this->state = RESOLVING_PATH;
                this->msg.fsd.request.taskid = SELF;
                this->msg.fsd.request.jobref = &this->info.twi;
                this->msg.fsd.request.sender_addr = HOST_ADDRESS;
                this->msg.fsd.request.op = OP_PATH;
                this->msg.fsd.request.p.path.src = this->lbuf;
                this->msg.fsd.request.p.path.len = strlen(this->lbuf);
                this->msg.fsd.request.p.path.cwd = ROOT_INODE_NR;
                this->msg.fsd.request.p.path.ip = &this->myno;
                sae2_TWI_MTSR(this->info.twi, FS_ADDRESS,
                        FSD_REQUEST, this->msg.fsd.request,
                        FSD_REPLY, this->msg.fsd.reply);
                break;

            default:
                send_REPLY_RESULT(SELF, EPERM);
                return;
            }
            this->bp = tp;
        }
        break;

    case RESOLVING_PATH:
        if (this->msg.fsd.reply.p.path.base_inum == INVALID_INODE_NR) {
            send_REPLY_RESULT(SELF, ENOENT);
        } else if ((this->myno.i_mode & I_TYPE) == I_REGULAR) {
            this->state = PROCESSING_RECORD;
            send_SET_IOCTL(KEYEXEC, SIOC_BUTTONVAL,
                                        (ulong_t)this->key_nr << 16 | 
                                        this->msg.fsd.reply.p.path.base_inum);
        } else {
            send_REPLY_RESULT(SELF, EINVAL);
        }
        break;
    }
}

/* end code */
