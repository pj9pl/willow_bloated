/* alba/mdac.h */

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

#ifndef _MDAC_H_
#define _MDAC_H_

#ifndef _MAIN_

#define MDAC_NR_CHANNELS 4
#define MDAC_MAX_VALUE 4096

enum {
    ZERO_DB = 0,
    SIX_DB
};

enum {
    NORMAL = 0,
    OFF_1K,
    OFF_100K,
    OFF_500K
};

enum {
    VDD_REFERENCE = 0,
    INTERNAL_REFERENCE
};

typedef struct _mdac_info {
    struct _mdac_info *nextp;
    ProcNumber replyTo;
    unsigned val : 12; /* align the bitfield: 4th and 3rd bytes  */
    unsigned gain : 1;
    unsigned powermode : 2;
    unsigned reference : 1;
    unsigned inhibit_update : 1; /* 2nd byte */
    unsigned channel : 2;

    unsigned read_flag : 1;      /* driver flags, not device flags */
    unsigned access_eeprom : 1;
} mdac_info;

/* convenience functions */
PUBLIC void send_MDAC_READ (
    ProcNumber sender,
    mdac_info *cp,
    uchar_t channel,
    uchar_t eeprom
);

PUBLIC void send_MDAC_WRITE (
    ProcNumber sender,
    mdac_info *cp,
    uchar_t channel,
    uchar_t eeprom,
    ushort_t value,
    uchar_t inhibit,
    uchar_t reference,
    uchar_t powermode,
    uchar_t gain
);

/* convenience macros */
#define sae_MDAC_READ(a,b,c)            send_MDAC_READ(SELF, &(a),(b),(c))
#define sae_MDAC_WRITE(a,b,c,d,e,f,g,h) send_MDAC_WRITE(SELF, &(a),(b),(c), \
                                                        (d),(e),(f),(g),(h))

#else /* _MAIN_ */

PUBLIC void config_mdac(void);
PUBLIC uchar_t receive_mdac(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _MDAC_H_ */
