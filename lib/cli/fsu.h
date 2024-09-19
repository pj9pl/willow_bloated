/* cli/fsu.h */

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

#ifndef _FSU_H_
#define _FSU_H_

#ifndef _MAIN_

/* FSU REQUEST operations */
#define  OP_LS  1
#define  OP_MV  2
#define  OP_CAT 3
#define  OP_PWD 4
#define  OP_RM  5
#define  OP_MK  6

typedef struct {
    hostid_t dest;
} ls_request;

typedef struct {
    hostid_t dest;
} cat_request;

typedef struct {
    hostid_t dest;
} pwd_request;

typedef struct {
    hostid_t dest;
} rm_request;

typedef struct {
    hostid_t dest;
} mk_request;

/* replies */

typedef struct {
    ushort_t n_items;
} ls_reply;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t op;
    char *argstr;
    ushort_t arglen;
    inum_t cwd;
    union {
        ls_request ls;
        cat_request cat;
        pwd_request pwd;
        rm_request rm;
        mk_request mk;
    } p;
} fsu_request;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    uchar_t result;
    union {
        ls_reply ls;
    } p;
} fsu_reply;

typedef union {
    fsu_request request;
    fsu_reply reply;
} fsu_msg;

#else /* _MAIN_ */

PUBLIC uchar_t receive_fsu(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _FSU_H_ */
