/* alba/osetup.h */

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

#ifndef _OSETUP_H_
#define _OSETUP_H_

#ifndef _MAIN_

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    oled_op  op;
    union {
        text_t text;
        rect_t rect;
        line_t line;
        contrast_t contrast;
        origin_t origin;
        linestart_t linestart;
        display_t display;
    } u;
    unsigned rop : 2;
    unsigned inh : 1;
} osetup_request;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t result;
} osetup_reply;

typedef union {
    osetup_request request;
    osetup_reply reply;
} osetup_msg;

#else /* _MAIN_ */

PUBLIC uchar_t receive_osetup(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _OSETUP_H_ */
