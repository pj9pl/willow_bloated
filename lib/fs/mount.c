/* fs/mount.c */

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

/* Mount the first type 250 partition.
 *
 * This module should check a little filesystem in a type 0xfa partition.
 *
 */

#include <string.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/mbr.h"
#include "fs/ino.h"
#include "fs/sdc.h"
#include "fs/mount.h"

/* I am .. */
#define SELF MOUNT
#define this mount

typedef enum {
    IDLE = 0,
    AWAITING_PARTITION_TABLE,
    AWAITING_SUPER_BLOCK
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    ProcNumber replyTo;
    union {
        ssd_info ssd;
        ino_info ino;
    } info;
} mount_t;

/* I have .. */
static mount_t this;

/* I can .. */
PRIVATE void resume(void);

PUBLIC uchar_t receive_mount(message *m_ptr)
{
    switch (m_ptr->opcode) {

    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.replyTo) {
                send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
                this.replyTo = 0;
            }
        }
        break;

    case INIT:
        this.state = AWAITING_PARTITION_TABLE;
        this.replyTo = m_ptr->sender;
        /* The convenience function cannot be used as this sector is
         * absolute i.e. not relative to the start of the partition.
         */
        this.info.ssd.buf = sd_admin.buf;
        this.info.ssd.phys_sector = PARTITION_TABLE_SECTOR;
        this.info.ssd.op = READ_SECTOR;
        send_JOB(SSD, &this.info.ssd);
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    uchar_t err;

    switch (this.state) {
    case IDLE:
        break;

    case AWAITING_PARTITION_TABLE:
        /* find the location and size of the partition to use. */
        if ((err = read_partition_table()) != EOK) {
            send_REPLY_RESULT(SELF, err);
        } else {
            this.state = AWAITING_SUPER_BLOCK;
            sae_READ_SSD(this.info.ssd, SUPER_SECTOR_NUMBER,
                                         sd_admin.buf);
        }
        break;

    case AWAITING_SUPER_BLOCK:
        this.state = IDLE;
        memcpy(&sd_meta.super, sd_admin.buf, SUPER_SIZE);
        send_REPLY_RESULT(SELF, EOK);
        break;
    }
}

/* end code */
