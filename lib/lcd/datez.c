/* lcd/datez.c */

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

/* LCD date secretary.
 * 
 * This accepts a DATE_NOTIFY message containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 */

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "lcd/plcd.h"
#include "lcd/lcache.h"
#include "lcd/datez.h"

/* I am .. */
#define SELF DATEZ
#define this datez

/* The row/col position within the display.
 *     xxxxxxxx--------
 *     ----------------
 */

#define XBUFLEN 24             /* 2021-10-17 14:33:54 */
#define DATEZ_DADDR 0          /* The row/col position within the display. */

typedef enum {
    IDLE = 0,
    ENSLAVED,
    WRITING_DATA
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    char buf[XBUFLEN];
    dbuf_t sm; /* service message */
    uchar_t cnt;
    union {
        twi_info twi;
        lcache_info lcache;
    } info;
} datez_t;

/* I have .. */
static datez_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void print_date(void);
PRIVATE void get_request(void);

PUBLIC uchar_t receive_datez(message *m_ptr)
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
    char *cp = this.buf;
    uchar_t len = 0;
    this.state = WRITING_DATA;
    struct tm time;

    /* avr-libc uses Y2K_EPOCH. subtract the 30 year difference. */
    time_t now = this.sm.res - UNIX_OFFSET;
    localtime_r(&now, &time);

    if (this.sm.mtype == DATE_ONLY || this.sm.mtype == DATE_TIME) {
        sprintf_P(cp, PSTR("%04d-%02d-%02d"), time.tm_year + 1900,
                             time.tm_mon, time.tm_mday);
    }
    
    if (this.sm.mtype == DATE_TIME) {
        cp = strcat(cp, " ");
        len = strlen(cp);
    }
 
    if (this.sm.mtype == TIME_ONLY || this.sm.mtype == DATE_TIME) {
        sprintf_P(cp + len, PSTR("%02d:%02d:%02d"), time.tm_hour,
                             time.tm_min, time.tm_sec);
        this.info.lcache.p = this.buf;
        this.info.lcache.n = strlen(this.buf);
    }
    this.info.lcache.instr = LCD_SET_DDRAM_ADDR | DATEZ_DADDR;
    send_JOB(LCACHE, &this.info.lcache);
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.taskid = ANY;
    sae2_TWI_GCSR(this.info.twi, DATE_NOTIFY, this.sm);
}

/* end code */
