/* fs/map.c */

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

/* A bitmap server.
 * Accepts alloc and free requests for inode and zone bitmaps.
 *
 * This task always uses sd_admin.buf.
 */
#include <avr/io.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "fs/ssd.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/map.h"

/* I am .. */
#define SELF MAP
#define this map

typedef enum {
    IDLE = 0,
    SCANNING_BITMAP,
    PERUSING_BITMAP,
    FREEING_BIT,
    READING_BIT,
    FREEING_CHUNK,
    WRITING_FREED_CHUNK
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    map_info *headp;
    ushort_t cur_sector;
    ushort_t sector_ofs;
    ushort_t span;
    union {
        ssd_info ssd;
    } info;
} map_t;

/* I have .. */
static map_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE uchar_t lowest_zero_idx(uchar_t x);

PUBLIC uchar_t receive_map(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
    case REPLY_RESULT:
        /* Reply to the headp->replyTo.
         * Point headp to headp->nextp, releasing the caller's resource.
         * If headp is not null, start the job.
         */
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL)
                    start_job();
            }
        }
        break;

    case JOB:
        {
            map_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                map_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    /* Set the preconditions for the scanning loop.
     * The first sector of the selected bitmap has to be requested.
     */
    if (this.headp->type == IMAP) {
        this.cur_sector = IMAP_SECTOR_NUMBER;
    } else if (this.headp->type == ZMAP) {
        this.cur_sector = ZMAP_SECTOR_NUMBER;
    }
    switch (this.headp->op) {
    case ALLOC_BIT:
        if (this.headp->nr_bits == 1) {
            this.state = SCANNING_BITMAP;
            this.sector_ofs = 0;
            sae_READ_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        } else {
            this.state = PERUSING_BITMAP;
            this.span = (this.headp->nr_bits >> BITS_PER_BYTE_SHIFT) +
                   ((this.headp->nr_bits & BITS_PER_BYTE_MASK) ? 1 : 0);
            this.headp->nr_bits = this.span << BITS_PER_BYTE_SHIFT;
            this.sector_ofs = 0;
            sae_READ_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        }
        break;

    case FREE_BIT:
        if (this.headp->nr_bits == 1) {
            this.state = FREEING_BIT;
            this.cur_sector += this.headp->bit_number >> BITS_PER_BLOCK_SHIFT;
            sae_READ_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        } else {
            this.state = FREEING_CHUNK;
            this.cur_sector += this.headp->bit_number >> BITS_PER_BLOCK_SHIFT;
            this.sector_ofs = (this.headp->bit_number & BLOCK_SIZE_MASK) >>
                                                     BITS_PER_BYTE_SHIFT;
            this.span = (this.headp->nr_bits >> BITS_PER_BYTE_SHIFT) +
                          ((this.headp->nr_bits & BITS_PER_BYTE_MASK) ? 1 : 0); 
            sae_READ_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        }
        break;

    default:
        send_REPLY_RESULT(SELF, EINVAL);
        break;
    }
}

PRIVATE void resume(void)
{
    int i;
    ushort_t j;
    int k;
    int start;
    uchar_t shift;

    switch (this.state) {
    case IDLE:
        break;

    case SCANNING_BITMAP:
        /* to set a single bit */
        for (i = 0; i < BLOCK_SIZE; i++) {
            if (sd_admin.buf[i] != 0xFF) {
                break;
            }
        }
        if (i < BLOCK_SIZE) {
            /* Work out which bit is the lowest, and add that to the
             * (bytecount * 8) to yield a bit number.
             * Set the bit and write it back to disk.
             * set the state to IDLE so the reply from SSD resumes
             * to send a reply to the headp->replyTo.
             */
            this.state = IDLE;
            uchar_t n = lowest_zero_idx(sd_admin.buf[i]);
            sd_admin.buf[i] |= 1 << n;
            this.headp->bit_number =
                          ((this.sector_ofs + i) << BITS_PER_BYTE_SHIFT) + n;
            this.headp->nr_bits = 1;
            sae_WRITE_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        } else {
            /* We reached the end of the sector without finding an unset bit.
             * If there were any further sectors, as indicated by the
             * downCounter then request the next sector.
             *  - Add a sector's worth of bits (4096) to an internal tally.
             *  - Keep state as SCANNING_BITMAP.
             *
             * As there is only one bitmap sector, there is no space available.
             */
            send_REPLY_RESULT(SELF, ENOSPC);
        }
        break;

    case PERUSING_BITMAP:
        j = 0;
        for (i = 0; i < BLOCK_SIZE; i++) {
            if (sd_admin.buf[i] == 0x00) {
                if (j == 0) {
                    start = i;
                }
                j++;
                if (j == this.span) {
                    this.state = IDLE;
                    for (k = start; k <= i; k++) {
                        sd_admin.buf[k] = 0xFF;
                    }
                    this.headp->bit_number =
                               (this.sector_ofs + start) << BITS_PER_BYTE_SHIFT;
                    sae_WRITE_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
                    break;
                }
            } else {
                j = 0;
            }
        }
        if (this.state == PERUSING_BITMAP) {
            this.cur_sector++;
            this.sector_ofs += BLOCK_SIZE;
            sae_READ_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        }
        break;

    case FREEING_BIT:
        /* After reading the sector containing the bit to be freed. */
        this.state = IDLE;
        i = (this.headp->bit_number & BITS_PER_BLOCK_MASK) >>
                                                   BITS_PER_BYTE_SHIFT;
        shift = this.headp->bit_number & BITS_PER_BYTE_MASK;
        if (sd_admin.buf[i] & _BV(shift)) {
            sd_admin.buf[i] &= ~_BV(shift);
            sae_WRITE_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        } else {
            send_REPLY_RESULT(SELF, EBADSLT);
        }
        break;

    case READING_BIT:
        /* After reading the sector containing the bit to be read. */
        i = (this.headp->bit_number & BITS_PER_BLOCK_MASK) >>
                                                   BITS_PER_BYTE_SHIFT;
        shift = this.headp->bit_number & BITS_PER_BYTE_MASK;
        this.headp->isset = (sd_admin.buf[i] & _BV(shift)) ? TRUE : FALSE;
        /* after writing the modified bitmap sector out to disk. */
        this.state = IDLE;
        send_REPLY_RESULT(SELF, EOK);
        break;

    case FREEING_CHUNK:
        /* After reading the sector containing the chunk to be freed.
         * Zero the bytes from sector_ofs to the minimum of end-of-sector
         * and end of span.
         * Save the sector that has just been modified.
         * If the are more bytes to zero, request the next sector;
         * otherwise set the state to IDLE and sent a note to self.
         */
        for (i = this.sector_ofs; i < BLOCK_SIZE; i++) {
            sd_admin.buf[i] = 0;
            if (--this.span == 0) {
                break;
            }
        }

        this.state = this.span ? WRITING_FREED_CHUNK : IDLE;
        sae_WRITE_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        break;

    case WRITING_FREED_CHUNK:
        this.state = FREEING_CHUNK;
        this.sector_ofs = 0;
        this.cur_sector++;
        sae_READ_SSD(this.info.ssd, this.cur_sector, sd_admin.buf);
        break;
    }
}

/* fxtbook.pdf section 1.3.2 Computing the index of the lowest one */
PRIVATE uchar_t lowest_zero_idx(uchar_t x)
{
    uchar_t r = 0;
    x = ~x;
    x &= -x;
    if (x & 0b11110000) r += 4;
    if (x & 0b11001100) r += 2;
    if (x & 0b10101010) r += 1;
    return r;
}

/* convenience functions */

PUBLIC void send_ALLOC_MAP(ProcNumber sender, map_info *cp, uchar_t type,
                                                          ushort_t nr_bits)
{
    cp->op = ALLOC_BIT;
    cp->type = type;
    cp->nr_bits = nr_bits;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_FREE_MAP(ProcNumber sender, map_info *cp, uchar_t type,
                                        ushort_t bit_nr, ushort_t nr_bits)
{
    cp->op = FREE_BIT;
    cp->type = type;
    cp->bit_number = bit_nr;
    cp->nr_bits = nr_bits;
    send_m3(sender, SELF, JOB, cp);
}
/* end code */
