/* isp/icsd.h */

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

#ifndef _ICSD_H_
#define _ICSD_H_

#ifndef _MAIN_

/* Serial programming instruction set. [p.304]
 * The second byte is specified only where it is non-zero.
 */
#define PROGRAMMING_ENABLE_1              0xAC
#define PROGRAMMING_ENABLE_2              0x53
#define CHIP_ERASE_1                      0xAC
#define CHIP_ERASE_2                      0x80
#define POLL_RDY_BSY                      0xF0
#define LOAD_EXTENDED_ADDRESS_BYTE        0x4D
#define LOAD_PROGRAM_MEMORY_PAGE          0x40
#define LOAD_EEPROM_MEMORY_PAGE           0xC1
#define READ_PROGRAM_MEMORY               0x20
#define READ_EEPROM_MEMORY                0xA0
#define READ_LOCK_BITS                    0x58
#define READ_SIGNATURE_BYTE               0x30
#define READ_FUSE_BITS                    0x50
#define READ_FUSE_HIGH_BITS_1             0x58
#define READ_FUSE_HIGH_BITS_2             0x08
#define READ_EXTENDED_FUSE_BITS_1         0x50
#define READ_EXTENDED_FUSE_BITS_2         0x08
#define READ_CALIBRATION_BYTE             0x38
#define WRITE_PROGRAM_MEMORY_PAGE         0x4C
#define WRITE_EEPROM_MEMORY_BYTE          0xC0
#define WRITE_EEPROM_MEMORY_PAGE          0xC2
#define WRITE_LOCK_BITS_1                 0xAC
#define WRITE_LOCK_BITS_2                 0xE0
#define WRITE_FUSE_BITS_1                 0xAC
#define WRITE_FUSE_BITS_2                 0xA0
#define WRITE_FUSE_HIGH_BITS_1            0xAC
#define WRITE_FUSE_HIGH_BITS_2            0xA8
#define WRITE_EXTENDED_FUSE_BITS_1        0xAC
#define WRITE_EXTENDED_FUSE_BITS_2        0xA4
#define HIGH_BYTE                         0x08

#define ICSD_BUFLEN 4

#define PULSE_RESET 1

enum {
    POWER_OFF = 0,
    POWER_ON
};

typedef struct _icsd_info {
    struct _icsd_info *nextp;
    ProcNumber replyTo;
    uchar_t txbuf[ICSD_BUFLEN];
    uchar_t tcnt;
    uchar_t rxbuf[ICSD_BUFLEN];
    uchar_t rcnt;
    
    uchar_t *bp; /* pointer into the client's data buffer */
    ushort_t bcnt; /* number of bytes to be processed  */
    ushort_t waddr; /* word address to accompany bp and bcnt */
} icsd_info;

#else /* _MAIN_ */

PUBLIC uchar_t receive_icsd(message *m_ptr);

#endif /* _MAIN_ */

#endif /* _ICSD_H_ */
