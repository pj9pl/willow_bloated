/* oled/vitp.c */

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

/* This accepts a VITZ_NOTIFY message containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 */

#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "oled/oled.h"
#include "oled/vitp.h"

/* I am .. */
#define SELF VITP
#define this vitp

/* A single invariant character code is all that is required now that the
 * pattern is being manipulated to reflect the battery voltage value.
 */
#define XBUFLEN 20
#define XPOS 50
#define YPOS  9

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
        oled_info oled;
        twi_info twi;
    } info;
} vitp_t;

/* I have .. */
static vitp_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void print_vitp(void);
PRIVATE void get_request(void);

PUBLIC uchar_t receive_vitp(message *m_ptr)
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
        print_vitp();
        break;

    case WRITING_DATA:
        get_request();
        break;
    }
}

PRIVATE void print_vitp(void)
{
    /* There are currently 8 patterns to choose from.
     * The val ranges from 600 to 800.
     * The mtype can perhaps indicate charging status.
     */
    this.state = WRITING_DATA;
    sprintf_P(this.buf, PSTR("t: %3d v: %3d"),
                    (int)(this.sm.res >> 16),
                    (int)(this.sm.res & 0xFFFF));
    this.info.oled.op = DRAW_TEXT;
    this.info.oled.inhibit = FALSE;
    this.info.oled.u.text.x = XPOS;
    this.info.oled.u.text.y = YPOS;
    this.info.oled.rop = SET;
    this.info.oled.u.text.cp = this.buf;
    this.info.oled.u.text.len = strlen(this.buf);
    this.info.oled.u.text.font = SMALLFONT;
    send_JOB(OLED, &this.info.oled);
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.taskid = ANY;
    sae2_TWI_GCSR(this.info.twi, VITZ_NOTIFY, this.sm);
}

/* end code */
