/* oled/voltagep.c */

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

/* OLED voltage secretary.
 * 
 * This accepts a VOLTAGE_NOTIFY message containing an mtype byte and an
 * unsigned long. The mtype byte describes how to present the unsigned long.
 * The unsigned long contains three bytes AD7124 data and one byte AD7124
 * status.
 */

#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/font.h"
#include "net/twi.h"
#include "oled/oled.h"
#include "oled/common.h"
#include "oled/voltagep.h"

/* I am .. */
#define SELF VOLTAGEP
#define this voltagep

#define PRINT_LEN 14
#define XBUFLEN 15
#define XPOS 8
#define YPOS 20
#define CHANNEL_HEIGHT 11

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
} voltagep_t;

/* I have .. */
static voltagep_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void print_voltage(void);
PRIVATE void get_request(void);

PUBLIC uchar_t receive_voltagep(message *m_ptr)
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
        print_voltage();
        break;

    case WRITING_DATA:
        get_request();
        break;
    }
}

PRIVATE void print_voltage(void)
{
    this.state = WRITING_DATA;
    long lres = 0;
    char buf[PRINT_LEN];
    uchar_t neg = FALSE;
    uchar_t big = FALSE;
    ulong_t val = this.sm.res & 0xFFFFFF;
    uchar_t chan = this.sm.res >> 24 & 0xFF;
    uchar_t unipolar = FALSE;
    switch (this.sm.mtype) {
    case 32:
        /* bipolar */
        big = TRUE;
        break;

    case 35:
        big = TRUE;
        unipolar = TRUE; 
        break;

    case 33:
    case 34:
    case 36:
        unipolar = TRUE; 
        break;
    }
    if (unipolar) {
        lres = (long)((long long) val * 2500000 >> 24);
    } else {
        lres = (long)((long long) val * 5000000 >> 24) - 2500000;
    }
    if (lres < 0) {
        neg = TRUE;
        lres = -lres;
    }
    sprintf_P(buf, PSTR("%ld"), lres);

    if (big && chan > 1) {
        big = FALSE;
        this.info.oled.u.text.y = NR_ROWS - SMALL_FONT_HEIGHT;
        this.info.oled.u.text.x = chan == 2 ? 2
                       : NR_COLUMNS - (9 * SMALL_FONT_WIDTH);
    } else {
        this.info.oled.u.text.y = YPOS + chan *
                              (big ? CHANNEL_HEIGHT << 1 : CHANNEL_HEIGHT);
        this.info.oled.u.text.x = XPOS;
    }

    uchar_t len = strlen(buf);
    uchar_t i = 0;
    uchar_t j = 0;
    this.buf[i++] = neg ? '-' : ' ';
    this.buf[i++] = (len < 7) ? '0' : buf[j++];
    this.buf[i++] = '.';
    this.buf[i++] = (len < 6) ? '0' : buf[j++];
    this.buf[i++] = (len < 5) ? '0' : buf[j++];
    this.buf[i++] = (len < 4) ? '0' : buf[j++];
    this.buf[i++] = (len < 3) ? '0' : buf[j++];
    this.buf[i++] = (len < 2) ? '0' : buf[j++];
    this.buf[i++] = (len < 1) ? '0' : buf[j++];
    this.buf[i] = '\0';
    this.info.oled.inhibit = FALSE;
    this.info.oled.op = DRAW_TEXT;
    this.info.oled.rop = SET;
    this.info.oled.u.text.cp = this.buf;
    this.info.oled.u.text.len = strlen(this.buf);
    this.info.oled.u.text.font = big ? BIGFONT : SMALLFONT;
    send_JOB(OLED, &this.info.oled);
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.taskid = ANY;
    sae2_TWI_GCSR(this.info.twi, VOLTAGE_NOTIFY, this.sm);
}

/* end code */
