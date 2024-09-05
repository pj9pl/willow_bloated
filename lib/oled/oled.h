/* oled/oled.h */

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

#ifndef _OLED_H_
#define _OLED_H_

typedef enum {
    SET = 1,
    XOR
} __attribute__((packed))rop_t;

typedef enum {
    DRAW_TEXT = 1,
    DRAW_RECT,
    DRAW_LINE,
    SET_CONTRAST,
    SET_ORIGIN,
    SET_LINESTART,
    SET_DISPLAY
}  __attribute__((packed))oled_op;

/* character array */
typedef struct {
   uchar_t x;
   uchar_t y;
   char *cp;
   uchar_t len;
   uchar_t font;
} text_t;

typedef struct {
   uchar_t x;
   uchar_t y;
   uchar_t w;
   uchar_t h;
   uchar_t filled;
} rect_t;

typedef struct {
   uchar_t x1;
   uchar_t y1;
   uchar_t x2;
   uchar_t y2;
   uchar_t dashed;
} line_t;

typedef struct {
    uchar_t value;
} contrast_t;

typedef struct {
    uchar_t value;
} origin_t;

typedef struct {
    uchar_t value;
} linestart_t;

typedef struct {
    uchar_t value;
} display_t;

typedef struct _oled_info {
    struct _oled_info *nextp;   
    ProcNumber replyTo;
    oled_op op;
    union {
        text_t text;
        rect_t rect;
        line_t line;
        contrast_t contrast;
        origin_t origin;
        linestart_t linestart;
        display_t display;
    } u;
    rop_t   rop;
    unsigned inhibit : 1;
} oled_info;

#define ORIGIN_BOTTOM 1
#define ORIGIN_TOP    2
#define ORIGIN_RIGHT  3
#define ORIGIN_LEFT   4

#define SMALLFONT     0
#define BIGFONT       1

#endif /* _OLED_H_ */
