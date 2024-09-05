/* fs/sdc.c */

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

/* Owner of two 512 byte buffers, each with a sector address and flags.
 * Common global functions are also defined here.
 */

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/sfa.h"
#include "fs/mbr.h"
#include "fs/map.h"
#include "fs/ino.h"
#include "fs/ssd.h"
#include "fs/sdc.h"

/* I have .. */
sd_buffer sd_admin;
sd_buffer sd_datum;
sd_metadata sd_meta;

/* I can .. */
PUBLIC uchar_t read_partition_table(void)
{
    mbr_t *mbr = (mbr_t *)sd_admin.buf;

    if (mbr->mbrSig0 == 0x55 && mbr->mbrSig1 == 0xAA) {
        part_t *part = mbr->part;
        int i;
        for (i = 0; 1 < 4; i++, part++) {
            if (part->type == LFS_PARTITION_TYPE)
                break;
        }

        if (i == 4) {
            return ENODEV;
        } else {
            sd_meta.firstSector = part->firstSector;
            sd_meta.totalSectors = part->totalSectors;
        }
    } else {
        return ENXIO;
    }
    return EOK;
}

/* end code */
