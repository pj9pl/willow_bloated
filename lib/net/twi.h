/* net/twi.h */

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

#ifndef _TWI_H_
#define _TWI_H_

#ifndef _MAIN_

#include "net/services.h"

typedef void (*Callback) (void *ptr);

/* modes */
#define TWI_MT 0x01
#define TWI_MR 0x02
#define TWI_SR 0x04
#define TWI_ST 0x08

/* flags */
#define TWI_GC 0x10           /* respond to general call */

typedef uchar_t Service;

typedef struct {
    ProcNumber taskid;        /* task level addressing */
    jobref_t jobref;          /* job level addressing */
    hostid_t sender_addr;     /* sender's host address */
    uchar_t mtype;            /* meta information about res */
    ulong_t res;              /* four bytes */
} dbuf_t;

typedef struct _twi_info {
    struct _twi_info *nextp;  /* job queue pointer */
    ProcNumber replyTo;       /* the client */
    hostid_t dest_addr;       /* destination address */
    Service mcmd;             /* master first byte */
    uchar_t *tptr;            /* transmit buffer pointer */
    ushort_t tcnt;            /* transmit down counter */
    Service scmd;             /* slave first byte */
    uchar_t *rptr;            /* receive buffer pointer */
    ushort_t rcnt;            /* receive down counter */
    uchar_t mode;             /* mode MT|MR|SR|ST and flags GC */
    Callback st_callback;     /* SR-ST changeover function */
} twi_info;

/* convenience functions */

PUBLIC void send_TWI_MT (
    ProcNumber sender,
    twi_info *cp,
    hostid_t dest_addr,
    uchar_t mcmd,
    void *tptr,
    ushort_t tcnt
);

PUBLIC void send_TWI_MTMR (
    ProcNumber sender,
    twi_info *cp,
    hostid_t dest_addr,
    uchar_t mcmd,
    void *tptr,
    ushort_t tcnt,
    void *rptr,
    ushort_t rcnt
);

PUBLIC void send_TWI_MR (
    ProcNumber sender,
    twi_info *cp,
    hostid_t dest_addr,
    uchar_t mcmd,
    void *rptr,
    ushort_t rcnt
);

PUBLIC void send_TWI_MTSR (
    ProcNumber sender,
    twi_info *cp,
    hostid_t dest_addr,
    uchar_t mcmd,
    void *tptr,
    ushort_t tcnt,
    uchar_t scmd,
    void *rptr,
    ushort_t rcnt
);

PUBLIC void send_TWI_GCSR (
    ProcNumber sender,
    twi_info *cp,
    uchar_t scmd,
    void *rptr,
    ushort_t rcnt
);

PUBLIC void send_TWI_SR (
    ProcNumber sender,
    twi_info *cp,
    uchar_t scmd,
    void *rptr,
    ushort_t rcnt
);

PUBLIC void send_TWI_SRST (
    ProcNumber sender,
    twi_info *cp,
    uchar_t scmd,
    void *rptr,
    ushort_t rcnt,
    Callback callback
);

PUBLIC void send_TWI_CANCEL (
    ProcNumber sender,
    twi_info *cp
);

/* convenience macros insert SELF in the sender arg. */

#define sae1_TWI_MT(a,b,c,d,e) \
            send_TWI_MT(SELF, &(a),(b),(c),(d),(e))

#define sae2_TWI_MT(a,b,c,d) \
            send_TWI_MT(SELF, &(a),(b),(c),&(d),sizeof(d))

#define sae1_TWI_MTMR(a,b,c,d,e,f,g) \
            send_TWI_MTMR(SELF, &(a),(b),(c),(d),(e),(f),(g))

#define sae2_TWI_MTMR(a,b,c,d,e) \
            send_TWI_MTMR(SELF, &(a),(b),(c),&(d),sizeof(d),&(e),sizeof(e))

#define sae1_TWI_MR(a,b,c,d,e) \
            send_TWI_MR(SELF, &(a),(b),(c),(d),(e))

#define sae2_TWI_MR(a,b,c,d) \
            send_TWI_MR(SELF, &(a),(b),(c),&(d),sizeof(d))

#define sae1_TWI_MTSR(a,b,c,d,e,f,g,h) \
            send_TWI_MTSR(SELF, &(a),(b),(c),(d),(e),(f),(g),(h))

#define sae2_TWI_MTSR(a,b,c,d,e,f) \
            send_TWI_MTSR(SELF, &(a),(b),(c),&(d),sizeof(d),(e),&(f),sizeof(f))

#define sae1_TWI_GCSR(a,b,c,d) \
            send_TWI_GCSR(SELF, &(a),(b),(c),(d))

#define sae2_TWI_GCSR(a,b,c) \
            send_TWI_GCSR(SELF, &(a),(b),&(c),sizeof(c))

#define sae1_TWI_SR(a,b,c,d) \
            send_TWI_SR(SELF, &(a),(b),(c),(d))

#define sae2_TWI_SR(a,b,c) \
            send_TWI_SR(SELF, &(a),(b),&(c),sizeof(c))

#define sae1_TWI_SRST(a,b,c,d,e) \
            send_TWI_SRST(SELF, &(a),(b),(c),(d),(e))

#define sae2_TWI_SRST(a,b,c,d) \
            send_TWI_SRST(SELF, &(a),(b),&(c),sizeof(c),(d))

#define sae_TWI_CANCEL(a) \
            send_TWI_CANCEL(SELF, &(a))

#else /* _MAIN_ */

PUBLIC void config_twi(void);
PUBLIC uchar_t receive_twi(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _TWI_H_ */
