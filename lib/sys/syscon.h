/* sys/syscon.h */

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

#ifndef _SYSCON_H_
#define _SYSCON_H_

#ifndef _MAIN_

/* SYSCON REQUEST opcodes */
#define OP_REBOOT    1 
#define OP_CYCLES    2
#define OP_RESTART   3
#define OP_BOOTTIME  4

typedef struct {
    hostid_t host;
} reboot_request;

typedef struct {
    hostid_t host;
} restart_request;

/* replies */

typedef struct {
    uchar_t depth;
    uchar_t lost;
    ulong_t count;
} cycles_reply;

typedef struct {
    time_t boottime;
} lastreset_reply;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t op;
    union {
        reboot_request reboot;
        restart_request restart;
    } p;
} syscon_request;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t result;
    union {
        cycles_reply cycles;
        lastreset_reply lastreset;
    } p;
} syscon_reply;

typedef union {
    syscon_request request;
    syscon_reply reply;
} syscon_msg;

#else /* _MAIN_ */

PUBLIC uchar_t receive_syscon(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _SYSCON_H_ */
