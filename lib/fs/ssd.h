/* fs/ssd.h */

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

#ifndef _SSD_H_
#define _SSD_H_

#ifndef _MAIN_

/* The SSD uses an external buffer.
 * The client provides an info pointer
 * to the SSD in READ_BLOCK and WRITE_BLOCK messages.
 * All blocks are 512 bytes.
 */

#define READ_SECTOR  0x01
#define WRITE_SECTOR 0x02

typedef struct _ssd_info {
    struct _ssd_info *nextp;
    ProcNumber replyTo;
    uchar_t *buf;        /* pointer to the 512 byte buffer */
    ulong_t phys_sector; /* disk sector number to be read/written */
    uchar_t op;          /* read=1, write=2 */
} ssd_info;

/* convenience function */
PUBLIC void send_SSD_JOB (
    ProcNumber sender,
    ssd_info *cp,
    uchar_t op,
    ushort_t sector,
    void *bp
);

/* convenience macros insert SELF in the sender arg. */

#define sae_SSD_JOB(a,b,c,d)     send_SSD_JOB(SELF, &(a),(b),(c),(d))
#define sae_READ_SSD(a,b,c)      send_SSD_JOB(SELF, &(a),READ_SECTOR,(b),(c))
#define sae_WRITE_SSD(a,b,c)     send_SSD_JOB(SELF, &(a),WRITE_SECTOR,(b),(c))

#else /* _MAIN_ */

PUBLIC void config_ssd(void);
PUBLIC uchar_t receive_ssd(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _SSD_H_ */
