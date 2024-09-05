/* alba/patch.h */

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

#ifndef _PATCH_H_
#define _PATCH_H_

#ifndef _MAIN_

#define RESET_AD7124            'I'
#define READ_AD7124             'R'
#define WRITE_AD7124            'W'
#define WAIT_AD7124_READY       'C'
#define WRITE_MCP4728           'M'
#define SET_EGOR_COUNT          'L'
#define SET_EGOR_DISPLAY_MODE   'V'
#define SET_EGOR_OUTPUT         'O'
#define START_STOP_EGOR         'J'  /* J,1 == START, J,0 == STOP */

/* oled control */
#define SET_OLED_CONTRAST       'A' /* [0..255] */
#define SET_OLED_DISPLAY        'B' /* [0..2] */
#define SET_OLED_ORIGIN         'P' /* [0..3] */
#define SET_OLED_LINESTART      'S' /* [0..63] */
#define DRAW_OLED_TEXT          'T'
#define DRAW_OLED_RECT          'E'
#define DRAW_OLED_LINE          'N'

typedef struct _patch_info {
    struct _patch_info *nextp;
    ProcNumber replyTo;
    inum_t inum;              /* inode number of the config file */
    ulong_t nlines;           /* number of lines processed */
} patch_info;

#else /* _MAIN_ */

PUBLIC uchar_t receive_patch(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _PATCH_H_ */
