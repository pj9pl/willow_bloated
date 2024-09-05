/* hc05/hc05.h */

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

#ifndef _HC05_H_
#define _HC05_H_

/* valid SIOC_HC05_COMMAND values */
#define HC05_ENQUIRE 1
#define HC05_POWEROFF 2
#define HC05_POWERON 3
#define SET_KEY 4
#define CLEAR_KEY 5
#define HIZ_KEY 6

#define BC352_TYPE 0
#define BC4i7_TYPE 1

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t op;
} hc05_request;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t result;
    uchar_t val;
} hc05_reply;

typedef union {
    hc05_request request;
    hc05_reply reply;
} hc05_msg;

#endif /* _HC05_H_ */
