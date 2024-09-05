/* isp/isp.c */

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

/* In-system programmer. Handle incoming characters as an
 * INTEL hex file and act as proxy for a remote twiboot device.
 *
 * See also:-
 * willow/twiboot/twiboot.c
 * Atmel App Note AVR061: STK500 Communication Protocol [doc2525.pdf].
 * Intel Hexadecimal Object File Format Specification Rev.A 1/6/88 [Hexfrmt.pdf]
 * Philips P89C51RC+ Product Specification [P89C51RC+IN.pdf p.43-4].
 */
 
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "sys/ser.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "isp/ihex.h"
#include "../twiboot/twiboot.h"
#include "isp/isp.h"

/* I am .. */
#define SELF ISP
#define this isp

/* These two masks separate the offset address
 * into an 8-bit page and 7-bit offset.
 */

#define PROGRAM_PAGE_NUMBER_MASK 0x7F80
#define PROGRAM_PAGE_OFFSET_MASK 0x007F

#define TWENTY_MILLISECONDS    20
#define READBACK_PAUSE         TWENTY_MILLISECONDS

typedef enum {
    IDLE = 0,
    FETCHING_VERSION,
    PRINTING_VERSION,
    FETCHING_CHIPINFO,
    PRINTING_CHIPINFO,
    REDIRECTING_TO_SELF,
    READY,
    LOADING_PROGRAM_MEMORY_PAGE,
    LOADING_EEPROM_PAGE,
    PAUSING_BEFORE_READBACK,
    READING_BACK,
    PRINTING_PROGRAM_MEMORY,
    ABORTING,
    FINISHED
} __attribute__ ((packed)) state_t;

#define LINE_LEN 50 /* output buffer */

/* cmdbuf and pagebuf must be contiguous for TWI transmission */
typedef struct {
    uchar_t cmd[3];
    uchar_t page[SPM_PAGESIZE];
} cbuf_t;

typedef struct {
    state_t state;    
    unsigned in_record : 1; /* TRUE if consuming between ':' and '\n' */
    unsigned dirty : 1;     /* TRUE if pagebuf has been written */
    unsigned seen_eof : 1;  /* TRUE from EOF record to POWER_OFF */
    unsigned in_eeprom : 1; /* FALSE for flash, TRUE for eeprom data */
    isp_info *headp;
    ushort_t page_address;
    ushort_t hcount;        /* incoming hex char count */
    ushort_t bcount;        /* translated binary record bytes */
    ulong_t start_loc;      /* read memory start address */
    ulong_t end_loc;        /* read memory end address */
    uchar_t n_bytes;        /* number of bytes contained within pagebuf */
    uchar_t pindex;         /* iterative loop hex record start point */
    uchar_t subfunction;
    ushort_t lindex;        /* index into linebuf output buffer */
    union {
        uchar_t recbuf[RECORD_LEN];
        data_record_t data;
        eof_record_t eof;
        extended_linear_address_record_t extended_linear_address;
        read_data_record_t read_data;
    } r;
    union {
        twi_info twi;
        ser_info ser;
        clk_info clk;
    } info;
    uchar_t linebuf[LINE_LEN];
    cbuf_t  cbuf;
    uchar_t readbuf[SPM_PAGESIZE];
} isp_t;

/* I have .. */
static isp_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void consume(CharProc vp);
PRIVATE void parse(void);
PRIVATE void proc_record(void);
PRIVATE void fetch_version(void);
PRIVATE void fetch_chipinfo(void);
PRIVATE void start_application(void);
PRIVATE void load_program_memory_page(void);
PRIVATE void fetch_buffer(void);
PRIVATE void print_data_record(void);
PRIVATE void print_eof_record(void);
PRIVATE void bputc(uchar_t c);
PRIVATE void put_nibble(uchar_t v);
PRIVATE void puthex(uchar_t ch);
PRIVATE uchar_t get_nibble(uchar_t c);
PRIVATE void print_prompt(uchar_t c);

PUBLIC uchar_t receive_isp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        consume(m_ptr->VPTR);
        break;

    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this->state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this->state = IDLE;
            if (this->headp) {
                send_REPLY_INFO(this->headp->replyTo, m_ptr->RESULT,
                                                            this->headp);
                if ((this->headp = this->headp->nextp) != NULL) {
                    this->in_eeprom = FALSE;
                    start_job();
                }
            }
            if (this->headp == NULL) {
                free(this);
                this = NULL;
            }
        }
        break;

    case JOB:
        if (this == NULL && (this = calloc(1, sizeof(*this))) == NULL) {
            send_REPLY_INFO(m_ptr->sender, ENOMEM, m_ptr->INFO);
        } else {
            isp_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                isp_info *tp;
                for (tp = this->headp; tp->nextp; tp = tp->nextp)
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
    this->state = FETCHING_VERSION;
    fetch_version();
}

PRIVATE void resume(void)
{
    uchar_t *s;
    switch (this->state) {
    case IDLE:
        break;

    case FETCHING_VERSION:
        this->state = PRINTING_VERSION;
        this->lindex = 0;
        s = this->readbuf;
        for (uchar_t i = 0; i < VERSION_LEN && *s > 0; i++)
            bputc(*s++);
        bputc('\n');
        sae_SER(this->info.ser, this->linebuf, this->lindex);
        break;

    case PRINTING_VERSION:
        this->state = FETCHING_CHIPINFO;
        fetch_chipinfo();
        break;

    case FETCHING_CHIPINFO:
        this->state = PRINTING_CHIPINFO;
        this->lindex = 0;
        for (uchar_t i = 0; i < CHIPINFO_LEN; i++)
            puthex(this->readbuf[i]);
        bputc('\n');
        sae_SER(this->info.ser, this->linebuf, this->lindex);
        break;

    case PRINTING_CHIPINFO:
        this->state = REDIRECTING_TO_SELF;
        send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
        break;

    case REDIRECTING_TO_SELF:
        this->state = READY;
        print_prompt('.');
        break;

    case READY:
        break;

    case LOADING_PROGRAM_MEMORY_PAGE:
        this->state = PAUSING_BEFORE_READBACK;
        sae_CLK_SET_ALARM(this->info.clk, READBACK_PAUSE);
        break;

    case PAUSING_BEFORE_READBACK:
        this->state = READING_BACK;
        this->start_loc = this->page_address;
        fetch_buffer();
        break;

    case LOADING_EEPROM_PAGE:
        print_prompt('.');
        break;

    case READING_BACK:
        if (memcmp(this->cbuf.page, this->readbuf, sizeof(this->cbuf.page))) {
            this->state = ABORTING;
            print_prompt('R');
            break;
        }
        this->state = READY;
        this->dirty = FALSE;
        memset(this->cbuf.page, '\0', sizeof(this->cbuf.page));
        if (this->bcount) {
            parse();
        } else if (this->seen_eof) {
            this->seen_eof = FALSE;
        } else {
            print_prompt('.');
        }
        break;
    
    case PRINTING_PROGRAM_MEMORY:
        /* We arrive here both after fetching a buffer and after printing
         * a record. this->pindex is zero after fetching a buffer, non-zero
         * after printing a record.
         * The number of bytes in the pagebuffer is this->n_bytes.
         */

        if (this->pindex < this->n_bytes) {
            print_data_record();
        } else if (this->start_loc + this->pindex < this->end_loc) {
            fetch_buffer();
        } else {
            this->state = FINISHED;
            print_eof_record();
        }

        break;
    
    case ABORTING:
    case FINISHED:
        this->state = IDLE;
        this->seen_eof = FALSE;
        print_prompt('$');
        break;
    }
}

PRIVATE void fetch_version(void)
{
    sae1_TWI_MR(this->info.twi, this->headp->target,
              CMD_READ_VERSION, this->readbuf, VERSION_LEN);
}

PRIVATE void fetch_chipinfo(void)
{
    this->cbuf.cmd[0] = MEMTYPE_CHIPINFO;
    this->cbuf.cmd[1] = 0;
    this->cbuf.cmd[2] = 0;
    sae1_TWI_MTMR(this->info.twi, this->headp->target, CMD_ACCESS_MEMORY,
                 this->cbuf.cmd, sizeof(this->cbuf.cmd),
                 this->readbuf, CHIPINFO_LEN);
}

PRIVATE void start_application(void)
{
    this->cbuf.cmd[0] = BOOTTYPE_APPLICATION;
    sae1_TWI_MT(this->info.twi, this->headp->target,
              CMD_SWITCH_APPLICATION, this->cbuf.cmd, 1);
}

PRIVATE void load_program_memory_page(void)
{
    this->state = LOADING_PROGRAM_MEMORY_PAGE;
    this->cbuf.cmd[0] = MEMTYPE_FLASH;
    this->cbuf.cmd[1] = this->page_address >> 8 & 0xFF;
    this->cbuf.cmd[2] = this->page_address & 0xFF;
    sae2_TWI_MT(this->info.twi, this->headp->target,
              CMD_ACCESS_MEMORY, this->cbuf);
}

PRIVATE void load_eeprom_page(void)
{
    this->state = LOADING_EEPROM_PAGE;
    memcpy(this->cbuf.page, this->r.data.buf, this->r.data.datalen);   
    this->cbuf.cmd[0] = MEMTYPE_EEPROM;
    this->cbuf.cmd[1] = this->r.data.offset_high;
    this->cbuf.cmd[2] = this->r.data.offset_low;
    sae1_TWI_MT(this->info.twi, this->headp->target,
              CMD_ACCESS_MEMORY, &this->cbuf, this->r.data.datalen +3);
}

PRIVATE void fetch_buffer(void)
{
    this->start_loc += this->pindex;
    this->pindex = 0;
    ushort_t len = this->end_loc - this->start_loc + 1;
    this->n_bytes = MIN(SPM_PAGESIZE, len);

    this->cbuf.cmd[0] = MEMTYPE_FLASH;
    this->cbuf.cmd[1] = this->start_loc >> 8 & 0xFF;
    this->cbuf.cmd[2] = this->start_loc & 0xFF;
    sae2_TWI_MTMR(this->info.twi, this->headp->target, CMD_ACCESS_MEMORY,
                  this->cbuf.cmd, this->readbuf);
}

PRIVATE void consume(CharProc vp)
{
    char ch;

    while ((vp) (&ch) == EOK) {

        switch (ch) {
        case '\n': /* 0x0a */
            if (this->in_record) {
                this->in_record = FALSE;
                parse();
            }
            this->hcount = 0;
            break;

        case ':':
            if (this->hcount == 0) {
                this->bcount = 0;
                this->in_record = TRUE;
            } else {
                /* colon within record means file is corrupt */
                this->state = ABORTING;
                print_prompt('Z');
                return;
            }
            break;

        case '\r': /* 0x0d */
            break;

        default:
            if (this->in_record && this->bcount < LINE_LEN) {
                if (isxdigit(ch)) {
                    uchar_t hex = get_nibble(toupper(ch));
                    if (isodd(++this->hcount)) {
                        this->r.recbuf[this->bcount] = hex << 4;
                    } else {
                        this->r.recbuf[this->bcount++] |= hex;
                    }
                } else {
                    /* non-hex character means file is corrupt */
                    this->state = ABORTING;
                    print_prompt('Z');
                    return;
                }
            }
            break;
        }
    }
}

/* Assume the input buffer to contain a valid Intel hex format record. */
PRIVATE void parse(void)
{
    uchar_t sum = 0;
    for (ushort_t i = 0; i < this->bcount; i++) 
        sum += this->r.recbuf[i];
    if (sum) {
        this->state = ABORTING;
        print_prompt('X');
    } else {
        proc_record();
    }
}

PRIVATE void proc_record(void)
{
    ushort_t addr;
    ushort_t page;
    ushort_t offset;

    switch (this->r.data.record_type) {
    case IHEX_DATA_RECORD:
        /* :nnaaaa00dd....ddcc
         *
         */
        if (this->in_eeprom) {
            load_eeprom_page();
        } else {
            addr = this->r.data.offset_high << 8 | this->r.data.offset_low;
            page = addr & PROGRAM_PAGE_NUMBER_MASK;
            offset = addr & PROGRAM_PAGE_OFFSET_MASK;

            if (this->dirty && (this->page_address != page ||
                             offset + this->r.data.datalen > SPM_PAGESIZE)) {
                load_program_memory_page();
            } else {
                this->page_address = page;
                memcpy(this->cbuf.page + offset, this->r.data.buf,
                                                    this->r.data.datalen);
                this->dirty = TRUE;
                print_prompt('.');
            }
        }
        break;

    case IHEX_END_OF_FILE_RECORD:
        /* :00xxxx01cc
         *
         */
        this->seen_eof = TRUE;
        if (this->dirty) {
            /* pagebuf residual data */
            load_program_memory_page();
        } else {
            this->state = FINISHED;
            start_application();
        }
        break;

    case IHEX_EXTENDED_LINEAR_ADDRESS_RECORD:
        /* :02xxxx04aaaacc
         * 0x0081 equates to eeprom segment
         * :02000004008179
         */
        if (this->r.extended_linear_address.ulba_high == 0x00 && 
                this->r.extended_linear_address.ulba_low == 0x81) {
            this->in_eeprom = TRUE;
        } else {
            this->in_eeprom = FALSE;
        }
        print_prompt('.');
        break;

    case IHEX_READ_DATA_RECORD:
        /* :05xxxx07sssseeeeffcc
         * read data from ssss to eeee.
         * ff = 0 == display 
         *      1 == blank check
         */
        this->start_loc = this->r.read_data.start_high << 8
                              | this->r.read_data.start_low;
        this->end_loc = (this->r.read_data.end_high << 8
                            | this->r.read_data.end_low);
        this->subfunction = this->r.read_data.subfunction;

        this->pindex = 0;
        if (this->r.read_data.subfunction == IHEX_DISPLAY_DATA) {
            this->state = PRINTING_PROGRAM_MEMORY;
        }
        fetch_buffer();
        break;
    }
}

/* Construct a data record in linebuf encoding pagebuf from this->pindex
 * onwards. This should be the minimum of 16 and the bytes remaining in
 * the buffer. Send the hex record to the serial port.
 */
PRIVATE void print_data_record(void)
{
    uchar_t len = this->n_bytes - this->pindex;
    len = MIN(IHEX_MAX_DATA_BYTES, len);
    ushort_t ofs = this->start_loc + this->pindex;

    this->lindex = 0;

    bputc(':');
    puthex(len);
    uchar_t sum = len;
    puthex(ofs >> 8 & 0xFF);
    sum += ofs >> 8 & 0xFF;
    puthex(ofs & 0xFF);
    sum += ofs & 0xFF;
    puthex(IHEX_DATA_RECORD);
    sum += IHEX_DATA_RECORD;
    for (uchar_t i = 0; i < len; i++) {
        puthex(this->cbuf.page[this->pindex + i]);
        sum += this->cbuf.page[this->pindex + i];
    }

    puthex(-sum);

    bputc('\n');

    this->pindex += len;

    sae_SER(this->info.ser, this->linebuf, this->lindex);
}

PRIVATE void print_eof_record(void)
{
    this->lindex = 0;

    bputc(':');
    puthex(0x00);
    puthex(0x00);
    puthex(0x00);
    puthex(0x01);
    puthex(0xFF);
    bputc('\n');

    sae_SER(this->info.ser, this->linebuf, this->lindex);
}

PRIVATE void bputc(uchar_t c)
{
    if (this->lindex < LINE_LEN) {
        this->linebuf[this->lindex++] = c;
    }
}

PRIVATE void put_nibble(uchar_t v)
{
    bputc((v < 10 ? '0' : '7') + v);
}

PRIVATE void puthex(uchar_t ch)
{
#define HIGH_NIBBLE(c)         ((c) >> 4 & 0x0f)
#define LOW_NIBBLE(c)          ((c) & 0x0f)

    put_nibble(HIGH_NIBBLE(ch));
    put_nibble(LOW_NIBBLE(ch));
}

PRIVATE uchar_t get_nibble(uchar_t c)
{
    return(c > '9' ? c - 'A' + 10 : c - '0');
}

PRIVATE void print_prompt(uchar_t c)
{
    this->lindex = 0;
    bputc(c);
    sae_SER(this->info.ser, this->linebuf, this->lindex);
}

/* end code */
