/* lcd/plcd.h */

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

#ifndef _PLCD_H_
#define _PLCD_H_

#ifndef _MAIN_

typedef struct _plcd_info {
    struct _plcd_info *nextp;   
    ProcNumber replyTo;
    uchar_t instr;
    uchar_t *p; /* pointer to chars. */
    uint16_t n; /* number of chars. */
} plcd_info;

/* Commands [p.23] */
#define LCD_SET_DDRAM_ADDR         _BV(7)
#define LCD_SET_CGRAM_ADDR         _BV(6)
#define LCD_FUNCTION_SET           _BV(5)
#define LCD_CURSOR_SHIFT           _BV(4)
#define LCD_ENTRY_MODE_SET         _BV(2)
#define LCD_DISPLAY_CONTROL        _BV(3)
#define LCD_RETURN_HOME            _BV(1)
#define LCD_CLEAR_DISPLAY          _BV(0)

/* flags for display entry mode */
#define LCD_ENTRY_LEFT             _BV(1)
#define LCD_ENTRY_SHIFT_INCREMENT  _BV(0)

/* flags for display on/off control */
#define LCD_DISPLAY_ON             _BV(2)
#define LCD_CURSOR_ON              _BV(1)
#define LCD_BLINK_ON               _BV(0)

/* flags for display/cursor shift */
#define LCD_DISPLAY_MOVE           _BV(3)
#define LCD_MOVE_RIGHT             _BV(2)

/* flags for function set */
#define LCD_8BIT_MODE              _BV(4)
#define LCD_2LINE                  _BV(3)
#define LCD_5x10_DOTS              _BV(2)

/* [p.24] */
#define CGRAM_ADDR_MASK             0x3F
#define DDRAM_ADDR_MASK             0x7F

#else /* _MAIN_ */

PUBLIC void config_plcd(void);
PUBLIC uchar_t receive_plcd(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _PLCD_H_ */
