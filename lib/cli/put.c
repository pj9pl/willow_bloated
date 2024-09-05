/* cli/put.c */

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

/* File input. Handle incoming characters as a line of text. and until the
 * text matches a predefined EOF marker write each line to a file.
 *
 */
 
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "fs/sfa.h"
#include "fs/fsd.h"
#include "fs/rwr.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "cli/put.h"

/* I am .. */
#define SELF PUT
#define this put

#define DOLLAR_PROMPT '$'
#define DOT_PROMPT '.'

#define BUF_SIZE BLOCK_SIZE /* input buffer size must be ^2 */
#define BUF_MASK (BUF_SIZE -1)
#define CHUNK_SIZE (BUF_SIZE / 2)
#define CHUNK_MASK (CHUNK_SIZE -1)

#define APPEND_MODE TRUE
#define TRUNCATE_MODE FALSE

#define UPPER 1
#define LOWER 2
#define FRAGMENT 3

typedef enum {
    IDLE = 0,
    LISTING_FILE,
    READING_FRAGMENT,
    REDIRECTING_INPUT,
    READY,
    WRITING_BUFFER
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;    
    unsigned truncate : 1;  /* TRUE = TRUNCATE, FALSE = APPEND */
    unsigned seen_eof : 1;  /* TRUE from EOF record to POWER_OFF */
    unsigned no_match : 1;  /* keyword comparison has failed */
    unsigned upper_full : 1;
    unsigned lower_full : 1;
    put_info *headp;
    uchar_t error;
    inode_t myno;
    char *key;
    uchar_t n_matches;
    ushort_t n_lines;
    ushort_t n_chars;
    inum_t inum;
    uchar_t prompt;
    ushort_t n_bytes;       /* number of bytes contained within sector_buf */
    ulong_t ofs;            /* file position of the sector buffer */
    union {
        fsd_msg fsd;
        rwr_msg rwr;
    } msg;
    union {
        twi_info twi;
        ser_info ser;
    } info;
    uchar_t sector_buf[BUF_SIZE];
} put_t;

/* I have .. */
static put_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void consume(CharProc vp);
PRIVATE void insert(uchar_t c);
PRIVATE void parse(void);
PRIVATE void write_buf(uchar_t part);
PRIVATE void print_prompt(uchar_t c);
PRIVATE void send_fsd(void);

PUBLIC uchar_t receive_put(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        consume(m_ptr->VPTR);
        break;

    case REPLY_INFO:
    case REPLY_RESULT:
        if (this->state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this->state = IDLE;
            if (this->key) {
                free(this->key);
                this->key = NULL;
            }
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
            put_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                put_info *tp;
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
    char *bp = this->headp->bp;
    char *ep = bp;
    this->truncate = FALSE;
    this->seen_eof = FALSE;

    if (*ep == '-') {
        if (*(ep + 1) == 't')
            this->truncate = TRUE;
        while (*ep && *ep != ' ')
            ep++;
        while (*ep == ' ')
            ep++;
    }

    bp = ep;

    while (*ep && *ep != ' ')
        ep++;

    uchar_t len = ep - bp;

    while (*ep == ' ')
        ep++;

    if (*ep) {
        char *cp = ep;
        while (*cp && *cp != ' ') {
            cp++;
        }
        uchar_t keylen = cp - ep;
        if (keylen) {
            if ((this->key = calloc(keylen +1, sizeof(uchar_t))) == NULL) {
                send_REPLY_RESULT(SELF, ENOMEM);
                return;
            } else {
                memcpy(this->key, ep, keylen);
            }
        }
    }
 
    if (this->key && len) {
        this->state = LISTING_FILE;
        this->msg.fsd.request.op = OP_PATH;
        this->msg.fsd.request.p.path.src = bp;
        this->msg.fsd.request.p.path.len = len;
        this->msg.fsd.request.p.path.ip = &this->myno;
        this->msg.fsd.request.p.path.cwd = this->headp->cwd;
        send_fsd();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case LISTING_FILE:
        if (this->msg.fsd.reply.result == EOK) {
            uchar_t err = EOK;
            if ((this->myno.i_mode & I_TYPE) == I_DIRECTORY) {
                err = EISDIR;
            } else if ((this->myno.i_mode & I_TYPE) != I_REGULAR) {
                err = EPERM;
            } else if ((this->myno.i_mode & W_BIT) == 0) {
                err = EACCES;
            }
            if (err == EOK) {
                this->inum = this->msg.fsd.reply.p.path.base_inum;
                if (this->truncate) {
                    this->ofs = 0;
                    this->state = REDIRECTING_INPUT;
                    send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
                } else {
                    this->ofs = this->myno.i_size & ~BUF_MASK;
                    ushort_t fragment = this->myno.i_size & CHUNK_MASK;
                    this->n_bytes = this->myno.i_size & BUF_MASK;
                    if (fragment > 0) {
                        void *dst = (this->myno.i_size & CHUNK_SIZE)
                                         ? this->sector_buf + CHUNK_SIZE
                                         : this->sector_buf; 
                        this->state = READING_FRAGMENT;
                        this->msg.fsd.request.op = OP_READ;
                        this->msg.fsd.request.p.readf.offset =
                                            this->myno.i_size & ~CHUNK_MASK;
                        this->msg.fsd.request.p.readf.use_cache = FALSE;
                        this->msg.fsd.request.p.readf.inum = this->inum;
                        this->msg.fsd.request.p.readf.len = fragment;
                        this->msg.fsd.request.p.readf.whence = SEEK_SET;
                        this->msg.fsd.request.p.readf.dst = dst;
                        send_fsd();
                    } else {
                        this->state = REDIRECTING_INPUT;
                        send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
                    }
                }
            } else {
                send_REPLY_RESULT(SELF, err);
            }
        } else {
            send_REPLY_RESULT(SELF, this->msg.fsd.reply.result);
        }
        break;

    case READING_FRAGMENT:
        this->state = REDIRECTING_INPUT;
        send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
        break;

    case REDIRECTING_INPUT:
        this->state = READY;
        print_prompt(DOT_PROMPT);
        break;

    case READY:
        break;

    case WRITING_BUFFER:
        if (this->lower_full) {
            this->lower_full = FALSE;
        } else if (this->upper_full) {
            this->upper_full = FALSE;
            this->ofs += BUF_SIZE;
        }
        if (this->seen_eof == TRUE) {
            this->state = IDLE;
            print_prompt(DOLLAR_PROMPT);
        } else {
            this->state = READY;
            print_prompt(DOT_PROMPT);
        }
        break;
    }
}

PRIVATE void consume(CharProc vp)
{
    char ch;

    while ((vp) (&ch) == EOK) {
        if (this->seen_eof == TRUE)
            continue;
        switch (ch) {
        case '\r': /* 0x0d n.b. 'stty -onlcr < $port' turns these chars off */
            continue;

        case '\n': /* 0x0a */
            if (this->no_match == FALSE) {
                for (uchar_t i = 0; i < this->n_matches; i++) {
                    insert(this->key[i]);
                }
            }
            insert(ch);

            parse();

            this->n_lines++;
            this->n_chars = 0;
            this->n_matches = 0;
            this->no_match = FALSE;
            break;

        default:
            if (this->no_match == FALSE) {
                if (this->key[this->n_matches] == ch) {
                    this->n_matches++;
                    if (this->key[this->n_matches] == 0) {
                        /* the whole key has been matched */
                        this->seen_eof = TRUE;
                        if (this->n_bytes & CHUNK_MASK) {
                            write_buf(FRAGMENT);
                        } else {
                            this->state = IDLE;
                            print_prompt(DOLLAR_PROMPT);
                        }
                    }
                } else {
                    this->no_match = TRUE;
                    for (uchar_t i = 0; i < this->n_matches; i++) {
                        insert(this->key[i]);
                    }
                    insert(ch);
                }
            } else {
                insert(ch);
            }
            break;
        }
    }
}

/* Insert a character into the sector buffer. */
PRIVATE void insert(uchar_t ch)
{
    this->sector_buf[this->n_bytes++] = ch;
    if (this->n_bytes == CHUNK_SIZE) {
        this->lower_full = TRUE;
    } else if (this->n_bytes == BUF_SIZE) {
        this->upper_full = TRUE;
        this->n_bytes = 0;
    }
}

/* Assume the input buffer to contain a line of chars. */
PRIVATE void parse(void)
{
    if (this->lower_full) {
        write_buf(LOWER);
    } else if (this->upper_full) {
        write_buf(UPPER);
    } else {
        print_prompt(DOT_PROMPT);
    }
}

PRIVATE void write_buf(uchar_t part)
{
    this->state = WRITING_BUFFER;

    this->msg.rwr.request.taskid = SELF;
    this->msg.rwr.request.jobref = &this->info.twi;
    this->msg.rwr.request.sender_addr = HOST_ADDRESS;
    this->msg.rwr.request.inum = this->inum;

    switch (part) {
    case LOWER:
        this->msg.rwr.request.src = this->sector_buf;
        this->msg.rwr.request.len = CHUNK_SIZE;
        this->msg.rwr.request.offset = this->ofs;
        break;

    case UPPER:
        this->msg.rwr.request.src = this->sector_buf + CHUNK_SIZE;
        this->msg.rwr.request.len = CHUNK_SIZE;
        this->msg.rwr.request.offset = this->ofs + CHUNK_SIZE;
        break;

    case FRAGMENT:
        this->msg.rwr.request.src =
                             this->sector_buf + (this->n_bytes & CHUNK_SIZE);
        this->msg.rwr.request.len = this->n_bytes & CHUNK_MASK;
        this->msg.rwr.request.offset = this->ofs + (this->n_bytes & CHUNK_SIZE);
        break;
    }

    this->msg.rwr.request.whence = SEEK_SET;
    this->msg.rwr.request.truncate = this->truncate;
    this->truncate = FALSE;

    sae2_TWI_MTSR(this->info.twi, FS_ADDRESS,
          RWR_REQUEST, this->msg.rwr.request,
          RWR_REPLY, this->msg.rwr.reply);
}

PRIVATE void print_prompt(uchar_t c)
{
    this->prompt = c;
    sae_SER(this->info.ser, &this->prompt, sizeof(this->prompt));
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
