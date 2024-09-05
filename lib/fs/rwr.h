/* fs/rwr.h */

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

#ifndef _RWR_H_
#define _RWR_H_

#ifndef _MAIN_

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    inum_t inum;
    off_t offset;
    ushort_t len;
    unsigned whence : 1;   /* SEEK_SET = FALSE, SEEK_END = TRUE */
    unsigned truncate : 1; /* if TRUE set the file size to zero. */
    uchar_t *src;
} rwr_request;

typedef struct {
    ProcNumber taskid;
    jobref_t jobref;
    hostid_t sender_addr;
    inum_t inum;
    off_t fpos;
    ushort_t nbytes;
    uchar_t result;
} rwr_reply;

typedef union {
    rwr_request request;
    rwr_reply reply;
} rwr_msg;                         /* 15 bytes */

#else /* _MAIN_ */

PUBLIC uchar_t receive_rwr(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _RWR_H_ */