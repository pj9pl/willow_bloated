/* oled/barp.c */

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

/* OLED barometer secretary.
 * 
 * This accepts a BAROMETER_NOTIFY message containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 *
 * This version uses floating point arithmetic.
 * The Makefile LDFLAGS should include:-
 *     -Wl,-u,vfprintf -lprintf_flt -lm
 * see also [avr-libc-user-manual-2.0.0.pdf section 23.9.4.32 p.150 (p.160)]
 */

#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "oled/oled.h"
#include "oled/barp.h"

/* I am .. */
#define SELF BARP
#define this barp

#define XBUFLEN  20   /* [22.45 1004.67'\0'] plus a generous margin */
#define XPOS 0
#define YPOS 9

typedef enum {
    IDLE = 0,
    ENSLAVED,
    WRITING_DATA
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned units : 1;
    char buf[XBUFLEN];
    dbuf_t sm; /* service message */
    union {
        twi_info twi;
        oled_info oled;
    } info;
} barp_t;

/* I have .. */
static barp_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void print_val(void);
PRIVATE void get_request(void);

PUBLIC uchar_t receive_barp(message *m_ptr)
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

    case SET_IOCTL:
        {
            uchar_t ret = EOK;

            switch (m_ptr->IOCTL) {
            case SIOC_STANDARD:
                /* TRUE:  Celsius and mBar
                 * FALSE: Fahrenheit and Inches of mercury
                 */
                this.units = m_ptr->LCOUNT ? TRUE : FALSE;
                break;

            default:
                ret = EINVAL;
                break;
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
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
        print_val();
        break;

    case WRITING_DATA:
        get_request();
        break;
    }
}

PRIVATE void print_val(void)
{
    this.state = WRITING_DATA;
    double temperature;
    double pressure;

    if (this.units) {
        temperature = (double)(this.sm.res >> 18) / 100.0;
        pressure = (double)(this.sm.res & 0x0003ffff) / 100.0;
    } else {
        temperature = (double)(this.sm.res >> 18) * 0.018 + 32;
        pressure = (double)(this.sm.res & 0x0003ffff) / 3386.3886666667;
    }

    sprintf_P(this.buf, PSTR("%2.2f %4.2f"), temperature, pressure);
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
    sae2_TWI_GCSR(this.info.twi, BAROMETER_NOTIFY, this.sm);
}

/* end code */
