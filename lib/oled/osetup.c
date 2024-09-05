/* oled/osetup.c */

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

/* oled setup secretary.
 * 
 * This accepts OSETUP_REQUEST messages containing an mtype byte and an
 * unsigned long. The mtype byte describes the unsigned long.
 *
 */

#include <stdlib.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/memz.h"
#include "oled/common.h"
#include "oled/oled.h"
#include "oled/osetup.h"

/* I am .. */
#define SELF OSETUP
#define this osetup

typedef enum {
    IDLE = 0,
    ENSLAVED,
    SETTING_CONTRAST,
    SETTING_DISPLAY,
    SETTING_ORIGIN,
    SETTING_LINESTART,
    RESETTING_CONSOLE,
    FETCHING_DATA,
    DRAWING_TEXT,
    DRAWING_RECT,
    DRAWING_LINE,
    SENDING_REPLY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    char *rbuf;
    osetup_msg sm;  /* service message */
    union {
        memz_msg memz;
    } msg;
    union {
        twi_info twi;
        oled_info oled;
    } info;
} osetup_t;

/* I have .. */
PRIVATE osetup_t this;

/* I can .. */
PRIVATE void exec_command(void);
PRIVATE void resume(message *m_ptr);
PRIVATE void get_request(void);
PRIVATE void send_reply(uchar_t result);

PUBLIC uchar_t receive_osetup(message *m_ptr)
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
            resume(m_ptr);
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
    switch (this.sm.request.op) {

    case SET_CONTRAST:
        this.state = SETTING_CONTRAST;
        this.info.oled.op = SET_CONTRAST;
        this.info.oled.u.contrast.value = this.sm.request.u.contrast.value;
        send_JOB(OLED, &this.info.oled);
        break;

    case SET_DISPLAY:
        this.state = SETTING_DISPLAY;
        this.info.oled.op = SET_DISPLAY;
        this.info.oled.u.display.value = this.sm.request.u.display.value;
        send_JOB(OLED, &this.info.oled);
        break;

    case SET_ORIGIN:
        this.state = SETTING_ORIGIN;
        this.info.oled.op = SET_ORIGIN;
        this.info.oled.u.origin.value = this.sm.request.u.origin.value;
        send_JOB(OLED, &this.info.oled);
        break;

    case SET_LINESTART:
        this.state = SETTING_LINESTART;
        this.info.oled.op = SET_LINESTART;
        this.info.oled.u.linestart.value = this.sm.request.u.linestart.value;
        send_JOB(OLED, &this.info.oled);
        break;

    case DRAW_TEXT:
        if ((this.rbuf = malloc(this.sm.request.u.text.len)) == NULL) {
            send_reply(ENOMEM);
        } else {
            this.state = FETCHING_DATA;
            this.msg.memz.request.src = this.sm.request.u.text.cp;
            this.msg.memz.request.len = this.sm.request.u.text.len; 
            sae1_TWI_MTMR(this.info.twi, this.sm.request.sender_addr,
                     MEMZ_REQUEST,
                    &this.msg.memz.request, sizeof(this.msg.memz.request),
                     this.rbuf, this.sm.request.u.text.len);
        }
        break;

    case DRAW_RECT:
        this.state = DRAWING_RECT;
        this.info.oled.op = DRAW_RECT;
        this.info.oled.rop = this.sm.request.rop;
        this.info.oled.inhibit = this.sm.request.inh;
        this.info.oled.u.rect.x = this.sm.request.u.rect.x;
        this.info.oled.u.rect.y = this.sm.request.u.rect.y;
        this.info.oled.u.rect.w = this.sm.request.u.rect.w;
        this.info.oled.u.rect.h = this.sm.request.u.rect.h;
        send_JOB(OLED, &this.info.oled);
        break;

    case DRAW_LINE:
        this.state = DRAWING_LINE;
        this.info.oled.op = DRAW_LINE;
        this.info.oled.rop = this.sm.request.rop;
        this.info.oled.inhibit = this.sm.request.inh;
        this.info.oled.u.line.x1 = this.sm.request.u.line.x1;
        this.info.oled.u.line.y1 = this.sm.request.u.line.y1;
        this.info.oled.u.line.x2 = this.sm.request.u.line.x2;
        this.info.oled.u.line.y2 = this.sm.request.u.line.y2;
        send_JOB(OLED, &this.info.oled);
        break;

    default:
        send_reply(EINVAL);
        break;
    }
}

PRIVATE void resume(message *m_ptr)
{
    switch (this.state) {
    case IDLE:
    case ENSLAVED:
        break;

    case FETCHING_DATA:
        this.state = DRAWING_TEXT;
        this.info.oled.op = DRAW_TEXT;
        this.info.oled.rop = this.sm.request.rop;
        this.info.oled.inhibit = this.sm.request.inh;
        this.info.oled.u.text.x = this.sm.request.u.text.x;
        this.info.oled.u.text.y = this.sm.request.u.text.y;
        this.info.oled.u.text.cp = this.rbuf;
        this.info.oled.u.text.len = this.sm.request.u.text.len;
        this.info.oled.u.text.font = SMALLFONT;
        send_JOB(OLED, &this.info.oled);
        break;

    case SETTING_LINESTART:
        this.state = RESETTING_CONSOLE;
        send_SET_IOCTL(CONSOLE, SIOC_CURSOR_POSITION, 0);
        break;

    case RESETTING_CONSOLE:
    case DRAWING_TEXT:
    case DRAWING_RECT:
    case DRAWING_LINE:
    case SETTING_CONTRAST:
    case SETTING_DISPLAY:
    case SETTING_ORIGIN:
        send_reply(m_ptr->RESULT);
        break;

    case SENDING_REPLY:
        get_request();
        break;
    }
}

PRIVATE void get_request(void)
{
    if (this.rbuf) {
        free(this.rbuf);
        this.rbuf = NULL;
    }
    this.state = ENSLAVED;
    this.sm.request.taskid = ANY;
    sae2_TWI_SR(this.info.twi, OSETUP_REQUEST, this.sm.request);
}

PRIVATE void send_reply(uchar_t result)
{
    this.state = SENDING_REPLY;
    hostid_t reply_address = this.sm.request.sender_addr;
    this.sm.reply.sender_addr = HOST_ADDRESS;
    this.sm.reply.result = result;
    sae2_TWI_MT(this.info.twi, reply_address, OSETUP_REPLY, this.sm.reply);
}

/* end code */
