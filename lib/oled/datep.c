/* oled/datep.c */

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

/* OLED date secretary.
 * 
 * This accepts a DATE_NOTIFY message containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 */

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "oled/oled.h"
#include "oled/datep.h"

/* I am .. */
#define SELF DATEP
#define this datep

#define XBUFLEN 20             /* '2021-10-17 14:33:54\0' */

typedef enum {
    IDLE = 0,
    ENSLAVED,
    WRITING_DATA
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    char buf[XBUFLEN];
    dbuf_t sm; /* service message */
    union {
        twi_info twi;
        oled_info oled;
    } info;
} datep_t;

/* I have .. */
static datep_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void print_date(void);
PRIVATE void get_request(void);

PUBLIC uchar_t receive_datep(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        if (this.state) {
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

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case ENSLAVED:
        print_date();
        break;

    case WRITING_DATA:
        get_request();
        break;
    }
}

PRIVATE void print_date(void)
{
    this.state = WRITING_DATA;
    /* avr-libc uses Y2K_EPOCH. subtract the 30 year difference. */
    time_t now = this.sm.res - UNIX_OFFSET;
    struct tm time;

    localtime_r(&now, &time);
    sprintf_P(this.buf, PSTR("%04d-%02d-%02d %02d:%02d:%02d"),
                             time.tm_year + 1900,
                             time.tm_mon + 1,
                             time.tm_mday,
                             time.tm_hour,
                             time.tm_min,
                             time.tm_sec);

    this.info.oled.op = DRAW_TEXT;
    this.info.oled.inhibit = FALSE;
    this.info.oled.rop = SET;
    this.info.oled.u.text.x = 0;
    this.info.oled.u.text.y = 0;
    this.info.oled.u.text.cp = this.buf;
    this.info.oled.u.text.len = strlen(this.buf);
    this.info.oled.u.text.font = SMALLFONT;
    send_JOB(OLED, &this.info.oled);
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.taskid = ANY;    
    sae2_TWI_GCSR(this.info.twi, DATE_NOTIFY, this.sm);
}

/* end code */
