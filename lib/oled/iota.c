/* oled/iota.c */

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

/* This task provides a message interface to a generic 128x64 white OLED
 * 128x64 display containing an SH1106. The board is unbranded.
 *
 * It uses the I2C interface as described in SH1106.pdf, page 11.
 *
 * The two TWI transactions to write the addresses and then write the page
 * fragment cannot be concatenated into a single transaction because tbe
 * command preamble must be contiguous with the page fragment. This would
 * place the preamble within the page, overwriting the display data.
 */

#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "sys/font.h"
#include "oled/sh1106.h"
#include "oled/common.h"
#include "oled/oled.h"
#include "oled/iota.h"

/* I am .. */
#define SELF OLED
#define this iota

#define ONE_HUNDRED_MILLISECONDS  100
#define INITIAL_DELAY             ONE_HUNDRED_MILLISECONDS

/* The column and page addresses can be set as a three command continuation.
 * This requires a five-byte buffer.
 */
#define SET_ADDRESS_COMMAND_LEN   FIVE_BYTES
#define CBUF_LEN                  SET_ADDRESS_COMMAND_LEN

typedef enum {
    IDLE = 0,
    INITIALIZING,
    SETTING_ORIGIN,
    SETTING_ADDRESSES,
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned ison : 1;         /* 0x01 */
    unsigned refreshing : 1;   /* 0x02 */
    unsigned ginhibit : 1;     /* 0x04 */
    oled_info *headp;
    ProcNumber replyTo;
    uchar_t page;
    uchar_t cbuf[CBUF_LEN];
    uchar_t cache[NR_PAGES][NR_COLUMNS];
    uchar_t left_calipers[NR_PAGES];
    uchar_t right_calipers[NR_PAGES];
    union {
        twi_info twi;
        clk_info clk;
    } info;
} iota_t;

/* I have .. */
static iota_t this;

/* I can .. */
PRIVATE void put_pixel(uchar_t x, uchar_t y);
PRIVATE void draw_line(uchar_t x1, uchar_t y1, uchar_t x2, uchar_t y2);
PRIVATE void put_char_array(void);
PRIVATE void put_bigchar_array(void);
PRIVATE void check_for_dirty(void);
PRIVATE void refresh(uchar_t page);
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void set_addresses(uchar_t column, uchar_t page);
PRIVATE void write(ushort_t count, uchar_t *ptr, uchar_t cmd);
PRIVATE void read(ushort_t count, uchar_t *ptr, uchar_t cmd);

PUBLIC uchar_t receive_iota(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            check_for_dirty();
            if (!this.refreshing) {
                if (this.headp) {
                    send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT,
                                                               this.headp);
                    if ((this.headp = this.headp->nextp) != NULL)
                        start_job();
                } else if (this.replyTo) {
                    send_REPLY_RESULT(this.replyTo, m_ptr->RESULT);
                    this.replyTo = 0;
                }
            }
        }
        break;

    case JOB:
        {
            oled_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                oled_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case INIT:
        if (this.state == IDLE) {
            this.state = INITIALIZING;
            this.replyTo = m_ptr->sender;
            /* open the calipers wide in order to clear the Display ram */
            for (uchar_t i = 0; i < NR_PAGES; i++) {
                memset(this.cache[i], '\0', NR_COLUMNS);
                this.left_calipers[i] = 0;
                this.right_calipers[i] = NR_COLUMNS -1;
            }
            read(ONE_BYTE, this.cbuf, NULL_CONTROL_BYTE);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    if (this.refreshing)
        return;
    this.ginhibit = this.headp->inhibit;
    switch (this.headp->op) {
    case DRAW_TEXT:
        switch (this.headp->u.text.font) {
        case SMALLFONT:
            put_char_array();
            break;
        case BIGFONT:
            put_bigchar_array();
            break;
        }
        send_REPLY_RESULT(SELF, EOK);
        break;

    case DRAW_LINE:
        if (this.headp->u.line.x1 < NR_COLUMNS &&
               this.headp->u.line.y1 < NR_ROWS &&
               this.headp->u.line.x2 < NR_COLUMNS &&
               this.headp->u.line.y2 < NR_ROWS) {
            draw_line(this.headp->u.line.x1,
                      this.headp->u.line.y1,
                      this.headp->u.line.x2,
                      this.headp->u.line.y2);
            send_REPLY_RESULT(SELF, EOK);
        } else {
            send_REPLY_RESULT(SELF, EINVAL);
        }
        break;

    case DRAW_RECT:
        if (this.headp->u.rect.x < NR_COLUMNS &&
               this.headp->u.rect.y < NR_ROWS &&
               this.headp->u.rect.x + this.headp->u.rect.w < NR_COLUMNS &&
               this.headp->u.rect.y + this.headp->u.rect.h < NR_ROWS) {
            uchar_t x = this.headp->u.rect.x;
            uchar_t y = this.headp->u.rect.y;
            uchar_t xx = x + this.headp->u.rect.w;
            uchar_t yy = y + this.headp->u.rect.h;

            for (uchar_t i = x; i <= xx; i++) {
                put_pixel(i, y);
                put_pixel(i, yy);
            }

            for (uchar_t i = y + 1; i < yy; i++) {
                put_pixel(x, i);
                put_pixel(xx, i);
            }
            send_REPLY_RESULT(SELF, EOK);
        } else {
            send_REPLY_RESULT(SELF, EINVAL);
        }
        break;

    case SET_CONTRAST:
        this.cbuf[0] = SET_CONTRAST_CONTROL_MODE;
        this.cbuf[1] = this.headp->u.contrast.value;
        write(TWO_BYTES, this.cbuf, NULL_CONTROL_BYTE);
        break;

    case SET_ORIGIN:
        this.cbuf[0] = 0;
        switch (this.headp->u.origin.value) {
        case ORIGIN_BOTTOM: /*  place the y origin at bottom */
            this.cbuf[0] = SET_COMMON_OUTPUT_SCAN | NORMAL_SCAN_DIRECTION;
            break;

        case ORIGIN_TOP: /*  place the y origin at top */
            this.cbuf[0] = SET_COMMON_OUTPUT_SCAN | REVERSE_SCAN_DIRECTION;
            break;

        case ORIGIN_RIGHT: /*  place the x origin at right */
            this.cbuf[0] = SET_SEGMENT_REMAP | NORMAL_ROTATE_DIRECTION;
            break;

        case ORIGIN_LEFT: /*  place the x origin at left */
            this.cbuf[0] = SET_SEGMENT_REMAP | REVERSE_ROTATE_DIRECTION;
            break;
        }
        if (this.cbuf[0])
            write(ONE_BYTE, this.cbuf, NULL_CONTROL_BYTE);
        else
            send_REPLY_RESULT(SELF, EINVAL);
        break;

    case SET_LINESTART:
        this.cbuf[0] = SET_DISPLAY_LINE_START
                        | (this.headp->u.linestart.value & DISPLAY_LINE_MASK);
        write(ONE_BYTE, this.cbuf, NULL_CONTROL_BYTE);
        break;

    case SET_DISPLAY:
        {
            uchar_t ret = EINVAL;
            this.cbuf[0] = 0;
            switch (this.headp->u.display.value) {
            case 0:
                this.cbuf[0] = DISPLAY_OFFON | DISPLAY_OFF;
                this.ison = FALSE;
                break;

            case 1:
                this.cbuf[0] = DISPLAY_OFFON | DISPLAY_ON;
                this.ison = TRUE;
                break;

            case 2: /* clear the screen */
                for (uchar_t i = 0; i < NR_PAGES; i++) {
                    memset(this.cache[i], '\0', NR_COLUMNS);
                    this.left_calipers[i] = 0;
                    this.right_calipers[i] = NR_COLUMNS -1;
                }
                ret = EOK;
                break;

            case 3:
                this.cbuf[0] = SET_ENTIRE_DISPLAY_OFFON | ENTIRE_DISPLAY_ON;
                break;

            case 4:
                this.cbuf[0] = SET_ENTIRE_DISPLAY_OFFON | ENTIRE_DISPLAY_NORMAL;
                break;

            case 5:
                this.cbuf[0] = SET_NORMAL_REVERSE_DISPLAY | NORMAL_DISPLAY;
                break;

            case 6:
                this.cbuf[0] = SET_NORMAL_REVERSE_DISPLAY | REVERSE_DISPLAY;
                break;
            }
            if (this.cbuf[0])
                write(ONE_BYTE, this.cbuf, NULL_CONTROL_BYTE);
            else
                send_REPLY_RESULT(SELF, ret);
        }
        break;

    default:
        send_REPLY_RESULT(SELF, EINVAL);
        break;
    }
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case INITIALIZING:
        /* We've just read the status register into cbuf[0] */
        if (this.cbuf[0] & BUSY) {
            /* take a nap */
            sae_CLK_SET_ALARM(this.info.clk, INITIAL_DELAY);
        } else {
            this.ison = (this.cbuf[0] & ONOFF) ? FALSE : TRUE;
            /* set the origin to top,left */
            this.state = SETTING_ORIGIN;
            this.cbuf[0] = SET_COMMON_OUTPUT_SCAN | REVERSE_SCAN_DIRECTION;
            this.cbuf[1] = NULL_CONTROL_BYTE;
            this.cbuf[2] = SET_SEGMENT_REMAP | REVERSE_ROTATE_DIRECTION;
            write(THREE_BYTES, this.cbuf, CONTINUATION_BIT); 
        }
        break;

    case SETTING_ORIGIN:
        this.state = IDLE;
        this.cbuf[0] = SET_CONTRAST_CONTROL_MODE;
        this.cbuf[1] = INITIAL_CONTRAST_VALUE;
        write(TWO_BYTES, this.cbuf, NULL_CONTROL_BYTE);
        break;

    case SETTING_ADDRESSES:
        this.state = IDLE;
        write(this.right_calipers[this.page] -
                   this.left_calipers[this.page] + 1,
                  &this.cache[this.page][this.left_calipers[this.page]],
                  DATA_REGISTER_BIT);
        this.left_calipers[this.page] = NR_COLUMNS -1;
        this.right_calipers[this.page] = 0;
        break;
    }
}

PRIVATE void put_pixel(uchar_t x, uchar_t y)
{
    if (x >= NR_COLUMNS || y >= NR_ROWS)
        return;

    uchar_t page = y >> PAGE_SHIFT;
    uchar_t *cp = &this.cache[page][x];
    uchar_t bit = _BV(y & PAGE_MASK);

    switch (this.headp->rop) {
    case SET:
        *cp |= bit;
        break;

    case XOR:
        *cp ^= bit;
        break;

    default:
        return;
    }

    if (this.left_calipers[page] > x)
        this.left_calipers[page] = x;
    if (this.right_calipers[page] < x)
        this.right_calipers[page] = x;
}

PRIVATE void draw_line(uchar_t x1, uchar_t y1, uchar_t x2, uchar_t y2)
{
    /* adapted from Arduino/libraries/U8g2/src/clib/u8g2_line.c
     *
     * Universal 8bit Graphics Library (https://github.com/olikraus/u8g2/)
     *
     * Copyright (c) 2016, olikraus@gmail.com
     * All rights reserved.
     */
    uchar_t tmp;
    uchar_t dx;
    uchar_t dy;

    uchar_t swapxy = FALSE;
  
    dx = (x1 > x2) ? x1 - x2 : x2 - x1;
    dy = (y1 > y2) ? y1 - y2 : y2 - y1;

    if (dy > dx) {
        swapxy = TRUE;
        tmp = dx; dx = dy; dy = tmp;
        tmp = x1; x1 = y1; y1 = tmp;
        tmp = x2; x2 = y2; y2 = tmp;
    }

    if (x1 > x2) {
        tmp = x1; x1 = x2; x2 = tmp;
        tmp = y1; y1 = y2; y2 = tmp;
    }

    if (x2 == 255)
      x2--;

    char err = dx >> 1;
    char ystep = (y2 > y1) ? 1 : -1;
    uchar_t y = y1;
    uchar_t x;

    for (x = x1; x <= x2; x++) {
        if (swapxy == FALSE) 
            put_pixel(x, y); 
        else 
            put_pixel(y, x); 
        if ((err -= dy) < 0) {
            y += ystep;
            err += dx;
        }
    }
}

PRIVATE void put_char_array(void)
{
    uchar_t x = this.headp->u.text.x;
    uchar_t y = this.headp->u.text.y;

    if (x >= NR_COLUMNS || y >= NR_ROWS)
        return;

    uchar_t page = y >> PAGE_SHIFT;
    uchar_t shift = y & PAGE_MASK;
    uchar_t mask = 0xFF << shift;

    if (this.left_calipers[page] > x) {
        this.left_calipers[page] = x;
        if (shift) {
            if (this.left_calipers[(page + 1) & PAGE_MASK] > x) {
                this.left_calipers[(page + 1) & PAGE_MASK] = x;
            }
        }
    }

    for (uchar_t n = 0; n < this.headp->u.text.len; n++) {
        uchar_t ch = this.headp->u.text.cp[n];
        if (ch < NR_SMALL_CHARS && x + SMALL_FONT_WIDTH < NR_COLUMNS) {
            for (uchar_t i = 0; i < SMALL_FONT_WIDTH -1; i++) {
                uchar_t val = pgm_read_byte_near(smallfont +
                                           (ch * (SMALL_FONT_WIDTH -1)) + i);
                switch (this.headp->rop) {
                case SET:
                    if (shift) {
                        this.cache[page][x + i] = (val << shift) |
                                        (this.cache[page][x + i] & ~mask);
                        this.cache[(page + 1) & PAGE_MASK][x + i] =
                                        (val >> (BITS_PER_BYTE - shift)) |
                                        (this.cache[(page + 1) &
                                         PAGE_MASK][x + i] & mask);
                    } else {
                        this.cache[page][x + i] = val;
                    }
                    break;

                case XOR:
                    if (shift) { 
                        this.cache[page][x + i] = ((val << shift) ^
                                        (this.cache[page][x + i] & mask)) |
                                        (this.cache[page][x + i] & ~mask);
                        this.cache[(page + 1) & PAGE_MASK][x + i] =
                                        ((val >> (BITS_PER_BYTE - shift)) ^
                                        (this.cache[(page + 1) &
                                         PAGE_MASK][x + i] & ~mask)) |
                                        (this.cache[(page + 1) &
                                         PAGE_MASK][x + i] & mask);
                    } else {
                        this.cache[page][x + i] ^= val;
                    }
                    break;
                }
            }
            x += SMALL_FONT_WIDTH;
        }
    }
    if (this.right_calipers[page] < x)
        this.right_calipers[page] = x;
    if (shift && this.right_calipers[(page + 1) & PAGE_MASK] < x)
        this.right_calipers[(page + 1) & PAGE_MASK] = x;
}

PRIVATE void put_bigchar_array(void)
{
    uchar_t x = this.headp->u.text.x;
    uchar_t y = this.headp->u.text.y;

    uchar_t page = y >> PAGE_SHIFT;
    uchar_t shift = y & PAGE_MASK;
    uchar_t mask = 0xFF << shift;

    if (this.left_calipers[page] > x)
        this.left_calipers[page] = x;
    if (this.left_calipers[(page + 1) & PAGE_MASK] > x)
        this.left_calipers[(page + 1) & PAGE_MASK] = x;
    if (shift && this.left_calipers[(page + 2) & PAGE_MASK] > x)
        this.left_calipers[(page + 2) & PAGE_MASK] = x;

    for (uchar_t n = 0; n < this.headp->u.text.len; n++) {
        uchar_t ch = this.headp->u.text.cp[n];
        if (ch >= '0' && ch <= '9') {
            ch -= '0';
        } else if (ch == '.') {
            ch = 10;
        } else if (ch == ' ') {
            ch = 11;
        } else if (ch == '-') {
            ch = 12;
        } else {
            ch = 13;
        }
        for (uchar_t i = 0; i < BIG_FONT_WIDTH -2; i++) {
            for (uchar_t j = 0; j < BIG_FONT_HEIGHT / BITS_PER_BYTE; j++) {
                uchar_t val = pgm_read_byte_near(((char *)bigfont) +
                              (ch * ((BIG_FONT_WIDTH - BIG_MARGIN) *
                               sizeof(ushort_t))) + i * sizeof(ushort_t) + j);
                switch (this.headp->rop) {
                case SET:
                    if (shift) {
                        this.cache[(page + j) & PAGE_MASK][x + i] =
                                 (val << shift) | (this.cache[(page + j) &
                                  PAGE_MASK][x + i] & ~mask);
                        this.cache[(page + j + 1) & PAGE_MASK][x + i] =
                                 (val >> (BITS_PER_BYTE - shift)) |
                                 (this.cache[(page + j + 1) &
                                  PAGE_MASK][x + i] & mask);
                    } else {
                        this.cache[(page + j) & PAGE_MASK][x + i] = val;
                    }
                    break;

                case XOR:
                    if (shift) { 
                        this.cache[(page + j) & PAGE_MASK][x + i] =
                          ((val << shift) ^ (this.cache[(page + j) &
                            PAGE_MASK][x + i] & mask)) |
                           (this.cache[(page + j) & PAGE_MASK][x + i] & ~mask);
                        this.cache[(page + j + 1) & PAGE_MASK][x + i] =
                          ((val >> (BITS_PER_BYTE - shift)) ^
                           (this.cache[(page + j + 1) &
                            PAGE_MASK][x + i] & ~mask)) |
                           (this.cache[(page + j + 1) &
                            PAGE_MASK][x + i] & mask);
                    } else {
                        this.cache[(page + j) & PAGE_MASK][x + i] ^= val;
                    }
                    break;
                }
            }
        }
        x += BIG_FONT_WIDTH;
    }
    if (this.right_calipers[page] < x)
        this.right_calipers[page] = x;
    if (this.right_calipers[(page + 1) & PAGE_MASK] < x)
        this.right_calipers[(page + 1) & PAGE_MASK] = x;
    if (shift && this.right_calipers[(page + 2) & PAGE_MASK] < x)
        this.right_calipers[(page + 2) & PAGE_MASK] = x;
}

PRIVATE void check_for_dirty(void)
{
    this.refreshing = TRUE;
    if (TRUE || this.ginhibit == FALSE) {
        for (uchar_t i = 0; i < NR_PAGES; i++) {
            if (this.left_calipers[i] <= this.right_calipers[i]) {
                refresh(i); 
                return;
            }
        }
    }
    this.refreshing = FALSE;
}

PRIVATE void refresh(uchar_t page)
{
    this.state = SETTING_ADDRESSES;
    this.page = page;
    set_addresses(this.left_calipers[page] + 2, page);
    write(SET_ADDRESS_COMMAND_LEN, this.cbuf, CONTINUATION_BIT);
}

PRIVATE void set_addresses(uchar_t column, uchar_t page)
{
    this.cbuf[0] = SET_LOWER_COLUMN_ADDRESS | (column & COLUMN_MASK);
    this.cbuf[1] = CONTINUATION_BIT;
    this.cbuf[2] = SET_HIGHER_COLUMN_ADDRESS |
                          ((column >> COLUMN_SHIFT) & COLUMN_MASK);
    this.cbuf[3] = NULL_CONTROL_BYTE;
    this.cbuf[4] = SET_PAGE_ADDRESS | (page & PAGE_MASK);
}

PRIVATE void write(ushort_t count, uchar_t *ptr, uchar_t cmd)
{
    sae1_TWI_MT(this.info.twi, IOTA_I2C_ADDRESS, cmd, ptr, count);
}

PRIVATE void read(ushort_t count, uchar_t *ptr, uchar_t cmd)
{
    sae1_TWI_MR(this.info.twi, IOTA_I2C_ADDRESS, cmd, ptr, count);
}

/* end code */
