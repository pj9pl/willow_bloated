/* cli/fsu.c */

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

/* File system utilities secretary. Installed on iowa.
 *
 * This module handles incoming FSU_REQUEST messages.
 *    OP_LS
 *    OP_MV
 *    OP_PWD
 *    OP_RM
 *    OP_MK
 */

#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/sfa.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/memz.h"
#include "cli/fsu.h"
#include "cli/ls.h"
#include "cli/mv.h"
#include "cli/pwd.h"
#include "cli/rm.h"
#include "cli/mk.h"

/* I am .. */
#define SELF FSU
#define this fsu

#define MAX_ARGC 9

typedef enum {
    IDLE = 0,
    ENSLAVED,
    FETCHING_COMMAND,
    LISTING_ITEMS,
    MOVING_ITEMS,
    PRINTING_CWD,
    REMOVING_ITEMS,
    MAKING_ITEM,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    char *argp;
    uchar_t argc;
    char *argv[MAX_ARGC];
    memz_msg memz;
    fsu_msg sm;       /* service message */
    union {
        twi_info twi;
        ls_info ls;
        mv_info mv;
        pwd_info pwd;
        rm_info rm;
        mk_info mk;
    } info;
} fsu_t;

/* I have .. */
static fsu_t this;

/* I can .. */
PRIVATE void exec_command(void);
PRIVATE void resume(uchar_t result);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_fsu(message *m_ptr)
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
        } else {
            resume(m_ptr->RESULT);
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
    if ((this.argp = calloc(this.sm.request.arglen +1,
                                      sizeof(uchar_t))) == NULL) {
        send_reply(ENOMEM);
    } else {
        this.state = FETCHING_COMMAND;
        /* fetch the arg string from the caller */
        this.memz.request.taskid = SELF;
        this.memz.request.src = this.sm.request.argstr;
        this.memz.request.len = this.sm.request.arglen;
        sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                     MEMZ_REQUEST,
                    &this.memz.request, sizeof(this.memz.request),
                     this.argp, this.memz.request.len);
    }
}

PRIVATE void resume(uchar_t result)
{
    switch (this.state) {
    case IDLE:
        get_request();
        break;

    case ENSLAVED:
        break;

    case FETCHING_COMMAND:
        this.argc = 0;
        for (char *sp = this.argp; this.argc < MAX_ARGC; sp = NULL,
                                                                this.argc++) {
            if ((this.argv[this.argc] = strtok_P(sp, PSTR(" "))) == NULL) {
                break;
            }
        }

        switch (this.sm.request.op) {
        case OP_LS:
            this.state = LISTING_ITEMS;
            this.info.ls.argc = this.argc;
            this.info.ls.argv = this.argv;
            this.info.ls.cwd = this.sm.request.cwd;
            this.info.ls.dest = this.sm.request.p.ls.dest;
            send_JOB(LS, &this.info.ls);
            break;

        case OP_MV:
            this.state = MOVING_ITEMS;
            this.info.mv.argc = this.argc;
            this.info.mv.argv = this.argv;
            this.info.mv.cwd = this.sm.request.cwd;
            send_JOB(MV, &this.info.mv);
            break;

        case OP_PWD:
            this.state = PRINTING_CWD;
            this.info.pwd.argc = this.argc;
            this.info.pwd.argv = this.argv;
            this.info.pwd.cwd = this.sm.request.cwd;
            this.info.pwd.dest = this.sm.request.p.pwd.dest;
            send_JOB(PWD, &this.info.pwd);
            break;

        case OP_RM:
            this.state = REMOVING_ITEMS;
            this.info.rm.argc = this.argc;
            this.info.rm.argv = this.argv;
            this.info.rm.cwd = this.sm.request.cwd;
            send_JOB(RM, &this.info.rm);
            break;

        case OP_MK:
            this.state = MAKING_ITEM;
            this.info.mk.argc = this.argc;
            this.info.mk.argv = this.argv;
            this.info.mk.cwd = this.sm.request.cwd;
            send_JOB(MK, &this.info.mk);
            break;

        default:
            send_reply(ENOSYS);
            break;
        }
        break;

    case LISTING_ITEMS:
        this.sm.reply.p.ls.n_items = this.info.ls.n_items;
        send_reply(result);
        break;

    case MOVING_ITEMS:
        send_reply(result);
        break;

    case PRINTING_CWD:
        send_reply(result);
        break;

    case REMOVING_ITEMS:
        send_reply(result);
        break;

    case MAKING_ITEM:
        send_reply(result);
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void get_request(void)
{
    if (this.argp) {
        free(this.argp);
        this.argp = NULL;
    }
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, FSU_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, FSU_REPLY, this.sm.reply);
}

/* end code */
