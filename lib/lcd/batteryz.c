/* lcd/batteryz.c */

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

/* LCD battery voltage secretary.
 * 
 * This accepts a BATTERY_NOTIFY message containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 */

#include <avr/io.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/twi.h"
#include "lcd/plcd.h"
#include "lcd/glyph.h"
#include "lcd/batteryz.h"

/* I am .. */
#define SELF BATTERYZ
#define this batteryz

/* A single invariant character code is all that is required now that the
 * pattern is being manipulated to reflect the battery voltage value.
 */
#define ONE_BYTE 1
#define XBUFLEN ONE_BYTE

/* The row/col position within the display.
 *     ---------------x
 *     ----------------
 */
#define BATTERYZ_DADDR 15     /* The row/col position within the display. */
#define BATTERY_CHARCODE 0    /* the first slot in CGRAM */
#define MAX_LEVEL 800
#define MIN_LEVEL 600

typedef enum {
    IDLE = 0,
    ENSLAVED,
    WRITING_GLYPH,
    WRITING_DATA
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    uchar_t buf[XBUFLEN];
    dbuf_t sm; /* service message */
    union {
        glyph_info glyph;
        plcd_info plcd;
        twi_info twi;
    } info;
} batteryz_t;

/* I have .. */
static batteryz_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void print_battery(void);
PRIVATE void print_data(void);
#define bputc(c) (this.buf[0] = (c))
PRIVATE void get_request(void);

PUBLIC uchar_t receive_batteryz(message *m_ptr)
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
        print_battery();
        break;

    case WRITING_GLYPH:
        print_data();
        break;

    case WRITING_DATA:
        get_request();
        break;
    }
}

PRIVATE void print_battery(void)
{
    /* There are currently 8 patterns to choose from.
     * The val ranges from 600 to 800.
     */

    this.state = WRITING_GLYPH;
    uchar_t level = 0;
    if ((this.sm.res - MIN_LEVEL) > 0) {
        if ((level = (this.sm.res - MIN_LEVEL) >> 5) > 7) {
            level = 7;
        }
    }
    this.info.glyph.pattern = level;
    this.info.glyph.slot = BATTERY_CHARCODE;
    send_JOB(GLYPH, &this.info.glyph);
}

PRIVATE void print_data(void)
{
    this.state = WRITING_DATA;
    bputc(BATTERY_CHARCODE);
    this.info.plcd.p = this.buf;
    this.info.plcd.n = ONE_BYTE;
    this.info.plcd.instr = LCD_SET_DDRAM_ADDR | BATTERYZ_DADDR;
    send_JOB(PLCD, &this.info.plcd);
}

PRIVATE void get_request(void)
{
    this.state = ENSLAVED;
    this.sm.taskid = ANY;    
    sae2_TWI_GCSR(this.info.twi, BATTERY_NOTIFY, this.sm);
}

/* end code */
