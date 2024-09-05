/* isp/ihex.h */

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

/* see also Hexfrmt.pdf:-
 * Intel Hexadecimal Object File Format Specification Rev.A 1/6/88 
 */
#ifndef _IHEX_H_
#define _IHEX_H_

/* Intel Hex Record types */
#define IHEX_DATA_RECORD                     0
#define IHEX_END_OF_FILE_RECORD              1
#define IHEX_EXTENDED_LINEAR_ADDRESS_RECORD  4

/* Adapted Philips extensions */
#define IHEX_MISC_WRITE_RECORD               6
#define IHEX_READ_DATA_RECORD                7
#define IHEX_MISC_READ_RECORD                8
#define IHEX_ERASE_RECORD                    9

#define IHEX_MAX_DATA_BYTES                 16 /* in this implementation */

#define IHEX_MISC_WRITE_FUSES                4 /* subfunction */
#define IHEX_MISC_READ_FUSES                 7 /* subfunction */
#define IHEX_MISC_READ_SIGNATURE             0 /* subfunction */

#define IHEX_LOCKBITS                        0 /* selection */
#define IHEX_LOW_FUSE                        1 /* selection */
#define IHEX_HIGH_FUSE                       2 /* selection */
#define IHEX_EXTENDED_FUSE                   3 /* selection */

#define IHEX_SIGNATURE0                      0 /* selection */
#define IHEX_SIGNATURE1                      1 /* selection */
#define IHEX_SIGNATURE2                      2 /* selection */
#define IHEX_CALIBRATION_BYTE                3 /* selection */

#define IHEX_DISPLAY_DATA                    0 /* subfunction */
#define IHEX_BLANK_CHECK                     1 /* subfunction */

#define IHEX_ERASE_MEMORY                    3 /* subfunction */
#define IHEX_EEPROM_MEMORY                   1 /* selection */
#define IHEX_FLASH_MEMORY                    2 /* selection */

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 00 */
    uchar_t buf[16];
    uchar_t sum;
} data_record_t;

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 01 */
    uchar_t sum;
} eof_record_t;

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 04 */
    uchar_t ulba_high;
    uchar_t ulba_low;
    uchar_t sum;
} extended_linear_address_record_t;

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 06 */
    uchar_t subfunction;
    uchar_t selection;
    uchar_t data;
    uchar_t sum;
} misc_write_record_t;

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 07 */
    uchar_t start_high;
    uchar_t start_low;
    uchar_t end_high;
    uchar_t end_low;
    uchar_t subfunction;
    uchar_t sum;
} read_data_record_t;

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 08 */
    uchar_t subfunction;
    uchar_t selection;
    uchar_t sum;
} misc_read_record_t;

typedef struct {
    uchar_t datalen;
    uchar_t offset_high;
    uchar_t offset_low;
    uchar_t record_type;   /* 09 */
    uchar_t subfunction;
    uchar_t selection;
    uchar_t sum;
} erase_record_t;

#define RECORD_LEN 21 /* binary input buffer */

#endif /* _IHEX_H_ */
