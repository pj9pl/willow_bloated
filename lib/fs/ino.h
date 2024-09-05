/* fs/ino.h */

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

#ifndef _INO_H_
#define _INO_H_

#ifndef _MAIN_

#define GET_INODE 1
#define PUT_INODE 2
 
typedef struct _ino_info {
    struct _ino_info *nextp;   
    ProcNumber replyTo;
    uchar_t op;
    inum_t inum;
    inode_t *ip;
    uchar_t *buf;
} ino_info;

/* convenience function */
PUBLIC void send_INO_JOB (
    ProcNumber sender,
    ino_info *cp,
    uchar_t op,
    inum_t inum,
    inode_t *ip,
    void *bp
);

/* convenience macros insert SELF in the sender arg. */

#define sae_PUT_INODE(a,b,c,d)  send_INO_JOB(SELF, &(a),PUT_INODE,(b),(c),(d))
#define sae_GET_INODE(a,b,c,d)  send_INO_JOB(SELF, &(a),GET_INODE,(b),(c),(d))

#else /* _MAIN_ */

PUBLIC uchar_t receive_ino(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _INO_H_ */
