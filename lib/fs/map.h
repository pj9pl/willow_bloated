/* fs/map.h */

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

#ifndef _MAP_H_
#define _MAP_H_

#ifndef _MAIN_

#define IMAP 1
#define ZMAP 2

#define ALLOC_BIT 1
#define FREE_BIT 2
 
typedef struct _map_info {
    struct _map_info *nextp;   
    ProcNumber replyTo;
    ushort_t bit_number;
    ushort_t nr_bits;
    uchar_t type;
    uchar_t op;
    unsigned isset : 1;
} map_info;

/* convenience functions */
PUBLIC void send_ALLOC_MAP (
    ProcNumber sender,
    map_info *cp,
    uchar_t type,
    ushort_t nr_bits
);

PUBLIC void send_FREE_MAP (
    ProcNumber sender,
    map_info *cp,
    uchar_t type,
    ushort_t bit_nr,
    ushort_t nr_bits
);

/* convenience macros insert SELF in the sender arg. */

#define sae_ALLOC_IMAP(a,b)     send_ALLOC_MAP(SELF, &(a),IMAP,(b))
#define sae_ALLOC_ZMAP(a,b)     send_ALLOC_MAP(SELF, &(a),ZMAP,(b))
#define sae_FREE_IMAP(a,b,c)    send_FREE_MAP(SELF, &(a),IMAP,(b),(c))
#define sae_FREE_ZMAP(a,b,c)    send_FREE_MAP(SELF, &(a),ZMAP,(b),(c))

#else /* _MAIN_ */

PUBLIC uchar_t receive_map(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _MAP_H_ */
