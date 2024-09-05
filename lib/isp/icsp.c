/* isp/icsp.c */

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

/* In-circuit serial programmer. Handle incoming characters as an
 * INTEL hex file and reprogram a device attached to the SPI interface.
 *
 * See also:-
 * Atmel datasheet [DS40002061A.pdf p.303-316]
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
#include "isp/ihex.h"
#include "isp/icsd.h"
#include "isp/icsp.h"

/* I am .. */
#define SELF ICSP
#define this icsp

#define ATTEMPTS_TO_ENABLE_PROGRAMMING       3
#define SERIAL_PROGRAMMING                0x53 /* 'S' */
#define UNPROGRAMMED                      0xFF

/* delays [p.305] */
#define FOUR_MILLISECONDS                    4
#define FIVE_MILLISECONDS                    5
#define TEN_MILLISECONDS                    10
#define tWD_FLASH            FIVE_MILLISECONDS /* 4.5ms */
#define tWD_EEPROM           FOUR_MILLISECONDS /* 3.6ms */
#define tWD_ERASE             TEN_MILLISECONDS /* 9.0ms */
#define tWD_FUSE             FIVE_MILLISECONDS /* 4.5ms */

/* These two masks separate the offset address
 * into an 8-bit page number and 7-bit page offset.
 */

#define PROGRAM_PAGE_NUMBER_MASK                0x7F80
#define PROGRAM_PAGE_OFFSET_MASK                0x007F

typedef enum {
    IDLE = 0,
    POWERING_ON,
    ENABLING_PROGRAMMING,
    REDIRECTING_TO_SELF,
    READY,
    ERASING_CHIP,
    LOADING_PROGRAM_MEMORY_PAGE,
    WRITING_MEMORY_PAGE,
    IN_FLASH_WRITE_DELAY,
    READING_BACK,
    WRITING_EEPROM_BYTE,
    IN_EEPROM_WRITE_DELAY,
    PRINTING_PROGRAM_MEMORY,
    CHECKING_PROGRAM_MEMORY,
    PRINTING_MISC_DATA,
    ABORTING,
    FINISHED,
    POWERING_OFF
} __attribute__ ((packed)) state_t;

#define LINE_LEN 50 /* output buffer */

typedef struct {
    state_t state;    
    unsigned icsd_power : 1; /* the socket is powered */
    unsigned in_record : 1; /* between ':' and '\n' */
    unsigned dirty : 1;     /* pagebuf has been written */
    unsigned seen_eof : 1;  /* TRUE from EOF record to POWER_OFF */
    unsigned in_eeprom : 1; /* eeprom data */
    icsp_info *headp;
    ushort_t ofs_address;
    uchar_t error;
    uchar_t attempts;       /* number of attempts to enable programming */
    ushort_t bcount;        /* translated binary record bytes */
    ushort_t hcount;        /* incoming hex char count */
    ushort_t lindex;        /* index into linebuf */
    ulong_t start_loc;      /* read memory start address */
    ulong_t end_loc;        /* read memory end address */
    uchar_t n_bytes;        /* number of bytes contained within pagebuf */
    uchar_t pindex;         /* iterative loop hex record start point */
    uchar_t subfunction;
    uchar_t selection;
    uchar_t *bp;            /* page buffer pointer */
    union {
        icsd_info icsd;
        ser_info ser;
        clk_info clk;
    } info;
    union {
        uchar_t recbuf[RECORD_LEN];
        data_record_t data;
        eof_record_t eof;
        extended_linear_address_record_t extended_linear_address;
        misc_write_record_t misc_write;
        read_data_record_t read_data;
        misc_read_record_t misc_read;
        erase_record_t erase;
    } r;
    uchar_t linebuf[LINE_LEN];
    /* ATmega328P page size in bytes = 128 [p.294] */
    uchar_t pagebuf[SPM_PAGESIZE];
    uchar_t readbuf[SPM_PAGESIZE];
} icsp_t;

/* I have .. */
static icsp_t *this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void consume(CharProc vp);
PRIVATE void parse(void);
PRIVATE void proc_record(void);
PRIVATE void fetch_buffer(void);
PRIVATE void write_eeprom(void);
PRIVATE void read_eeprom(void);
PRIVATE void read_flash(void);
PRIVATE void load_program_memory_page(void);
PRIVATE void print_data_record(void);
PRIVATE void print_eof_record(void);
PRIVATE void print_misc_write_record(uchar_t val);
PRIVATE void bputc(uchar_t c);
PRIVATE void bputs_P(PGM_P str);
PRIVATE void put_nibble(uchar_t v);
PRIVATE void puthex(uchar_t ch);
PRIVATE uchar_t get_nibble(uchar_t c);
PRIVATE void print_prompt(uchar_t c);
PRIVATE void power_off(void);

PUBLIC uchar_t receive_icsp(message *m_ptr)
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
            if (this->icsd_power) {
                power_off();
            } else {
                this->state = IDLE;
                if (this->headp) {
                    this->seen_eof = FALSE;
                    this->in_eeprom = FALSE;
                    if (m_ptr->RESULT == EOK) {
                        m_ptr->RESULT = this->error;
                    }
                    send_REPLY_INFO(this->headp->replyTo, m_ptr->RESULT,
                                                                this->headp);
                    if ((this->headp = this->headp->nextp) != NULL)
                        start_job(); 
                }
                if (this->headp == NULL) {
                    free(this);
                    this = NULL;
                }
            }
        }
        break;

    case JOB:
        if (this == NULL && (this = calloc(1, sizeof(*this))) == NULL) {
            send_REPLY_INFO(m_ptr->sender, ENOMEM, m_ptr->INFO);
        } else {
            icsp_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this->headp) {
                this->headp = ip;
                start_job();
            } else {
                icsp_info *tp;
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
    this->state = POWERING_ON;
    this->attempts = ATTEMPTS_TO_ENABLE_PROGRAMMING;
    send_SET_IOCTL(ICSD, SIOC_DEVICE_POWER, POWER_ON);
}

PRIVATE void resume(void)
{
    switch (this->state) {
    case IDLE:
        break;

    case POWERING_ON:
        this->icsd_power = TRUE;
        this->state = ENABLING_PROGRAMMING;
        this->info.icsd.bp = NULL;
        this->info.icsd.bcnt = 0;
        this->info.icsd.waddr = 0;
        this->info.icsd.txbuf[0] = PROGRAMMING_ENABLE_1;
        this->info.icsd.txbuf[1] = PROGRAMMING_ENABLE_2;
        this->info.icsd.txbuf[2] = 0;
        this->info.icsd.txbuf[3] = 0;
        this->info.icsd.tcnt = ICSD_BUFLEN;
        send_JOB(ICSD, &this->info.icsd);
        break;

    case ENABLING_PROGRAMMING:
        if (this->info.icsd.rxbuf[2] == SERIAL_PROGRAMMING) {
            this->state = REDIRECTING_TO_SELF;
            send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
        } else {
            if (this->attempts--) {
                this->state = POWERING_ON;
                send_SET_IOCTL(ICSD, SIOC_ICSD_COMMAND, PULSE_RESET);
            } else {
                this->state = ABORTING;
                this->error = ENODEV;
                send_SET_IOCTL(ICSD, SIOC_DEVICE_POWER, POWER_OFF);
            }
        }
        break;

    case REDIRECTING_TO_SELF:
        this->state = READY;
        print_prompt('.');
        break;

    case READY:
        break;

    case ERASING_CHIP:
        this->state = FINISHED;
        sae_CLK_SET_ALARM(this->info.clk, tWD_ERASE);
        break;

    case LOADING_PROGRAM_MEMORY_PAGE:
        /* Finished loading the pagebuf into the Program Memory Page
         * Write the program memory page.  
         */
        this->state = WRITING_MEMORY_PAGE;
        this->info.icsd.bp = NULL;
        this->info.icsd.bcnt = 0;
        this->info.icsd.waddr =
                       (this->ofs_address & PROGRAM_PAGE_NUMBER_MASK) >> 1;
        this->info.icsd.txbuf[0] = WRITE_PROGRAM_MEMORY_PAGE;
        this->info.icsd.txbuf[1] = this->info.icsd.waddr >> 8 & 0xFF;
        this->info.icsd.txbuf[2] = this->info.icsd.waddr & 0xFF;
        this->info.icsd.txbuf[3] = 0;
        this->info.icsd.tcnt = ICSD_BUFLEN;
        send_JOB(ICSD, &this->info.icsd);
        break;

    case WRITING_MEMORY_PAGE:
        /* wait tWD_FLASH */
        this->state = IN_FLASH_WRITE_DELAY;
        sae_CLK_SET_ALARM(this->info.clk, tWD_FLASH);
        break;

    case IN_FLASH_WRITE_DELAY:
        this->state = READING_BACK;
        this->info.icsd.bp = this->readbuf;
        this->info.icsd.bcnt = sizeof(this->readbuf);
        this->info.icsd.waddr =
                       (this->ofs_address & PROGRAM_PAGE_NUMBER_MASK) >> 1;
        this->info.icsd.txbuf[0] = READ_PROGRAM_MEMORY;
        this->info.icsd.txbuf[1] = this->info.icsd.waddr >> 8 & 0xFF;
        this->info.icsd.txbuf[2] = this->info.icsd.waddr & 0xFF;
        this->info.icsd.txbuf[3] = 0;
        this->info.icsd.tcnt = ICSD_BUFLEN;
        send_JOB(ICSD, &this->info.icsd);
        break;

    case READING_BACK:
        if (memcmp(this->pagebuf, this->readbuf, sizeof(this->readbuf))) {
            this->state = ABORTING;
            print_prompt('R');
            break;
        }
        this->state = READY;
        this->dirty = FALSE;
        memset(this->pagebuf, '\0', sizeof(this->pagebuf));
        if (this->bcount) {
            parse();
        } else if (this->seen_eof) {
            this->seen_eof = FALSE;
        } else {
            print_prompt('.');
        }
        break;

    case WRITING_EEPROM_BYTE:
        /* wait tWD_EEPROM */
        this->state = this->n_bytes ? IN_EEPROM_WRITE_DELAY : READY;
        sae_CLK_SET_ALARM(this->info.clk, tWD_EEPROM);
        break;

    case IN_EEPROM_WRITE_DELAY:
        write_eeprom();
        break;

    case PRINTING_PROGRAM_MEMORY:
        /* We arrive here after either fetching a buffer or printing a record.
         * this->pindex is zero after fetching a buffer, non-zero after
         * printing a record.
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

    case CHECKING_PROGRAM_MEMORY:
        {
            /* We arrive here after fetching a buffer.
             * The number of bytes in the pagebuffer is this->n_bytes.
             */

            uchar_t failed = FALSE;
            for ( ; this->pindex < this->n_bytes; this->pindex++) {
                if (this->pagebuf[this->pindex] != UNPROGRAMMED) {
                    failed = TRUE;
                    break;
                }
            }
            if (!failed && this->start_loc < this->end_loc) {
                fetch_buffer();
            } else {
                this->state = FINISHED;
                this->lindex = 0;
                if (failed) {
                    bputs_P(PSTR("0x"));
                    puthex(this->start_loc >> 8);
                    puthex(this->start_loc | this->pindex);
                    bputc('\n');
                } else {
                    bputs_P(PSTR("blank\n"));
                }
                sae_SER(this->info.ser, this->linebuf, this->lindex);
            }
        }
        break;

    case PRINTING_MISC_DATA:
        /* The READ_MISC_DATA operation has completed.
         * Print a hex record corresponding to the character in the third byte.
         */
        this->state = FINISHED;
        print_misc_write_record(this->info.icsd.rxbuf[3]);
        break;

    case ABORTING:
    case FINISHED:
        power_off();
        break;

    case POWERING_OFF:
        this->state = IDLE;
        print_prompt('$');
        break;
    }
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
                    this->hcount++;
                    uchar_t hex = get_nibble(toupper(ch));
                    if (isodd(this->hcount)) {
                        this->r.recbuf[this->bcount] = hex << 4;
                    } else {
                        this->r.recbuf[this->bcount++] |= hex;
                    }
                } else {
                    /* non-hex character means file is corrupt */
                    this->state = ABORTING;
                    print_prompt('Z');
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
    uchar_t cmd = 0;
    uchar_t val1 = 0;
    uchar_t val2 = 0;

    switch (this->r.data.record_type) {
    case IHEX_DATA_RECORD:
        /* :nnaaaa00dd....ddcc
         *
         */
        addr = this->r.data.offset_high << 8 | this->r.data.offset_low;
        if (this->in_eeprom) {
           /* Up to 16 bytes in a record.
            * Write the bytes individually.
            */
            this->state = WRITING_EEPROM_BYTE;
            this->bp = this->r.data.buf;
            this->n_bytes = this->r.data.datalen;
            this->ofs_address = addr;
            write_eeprom();
        } else {
            page = addr & PROGRAM_PAGE_NUMBER_MASK;
            offset = addr & PROGRAM_PAGE_OFFSET_MASK;

            if (this->dirty && (this->ofs_address != page ||
                         offset + this->r.data.datalen > SPM_PAGESIZE)) {
                load_program_memory_page();
            } else {
                this->ofs_address = page;
                memcpy(this->pagebuf + offset, this->r.data.buf,
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
            power_off();
        }
        break;

    case IHEX_EXTENDED_LINEAR_ADDRESS_RECORD:
        /* :02xxxx04aaaacc
         * 0x0081 equates to eeprom segment
         */
        if (this->r.extended_linear_address.ulba_high == 0x00 && 
                this->r.extended_linear_address.ulba_low == 0x81) {
            this->in_eeprom = TRUE;
        } else {
            this->in_eeprom = FALSE;
        }
        print_prompt('.');
        break;

    case IHEX_MISC_WRITE_RECORD:
        /* :nnxxxx06ffssddcc
         * lock bits, LFuse, HFuse, EFuse
         */
        if (this->r.misc_write.subfunction == IHEX_MISC_WRITE_FUSES) {
            switch (this->r.misc_write.selection) {
            case IHEX_LOCKBITS:
                cmd = WRITE_LOCK_BITS_1;
                val1 = WRITE_LOCK_BITS_2;
                break;

            case IHEX_LOW_FUSE:
                cmd = WRITE_FUSE_BITS_1;
                val1 = WRITE_FUSE_BITS_2;
                break;

            case IHEX_HIGH_FUSE:
                cmd = WRITE_FUSE_HIGH_BITS_1;
                val1 = WRITE_FUSE_HIGH_BITS_2;
                break;

            case IHEX_EXTENDED_FUSE:
                cmd = WRITE_EXTENDED_FUSE_BITS_1;
                val1 = WRITE_EXTENDED_FUSE_BITS_2;
                break;
            }
        }
        if (cmd) {
            this->info.icsd.txbuf[0] = cmd;
            this->info.icsd.txbuf[1] = val1;
            this->info.icsd.txbuf[2] = val2;
            this->info.icsd.txbuf[3] = this->r.misc_write.data;
            this->info.icsd.tcnt = ICSD_BUFLEN;
            this->state = FINISHED;
            send_JOB(ICSD, &this->info.icsd);
        }
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

        if (this->start_loc < this->end_loc) {
            this->pindex = 0;
            if (this->r.read_data.subfunction == IHEX_DISPLAY_DATA) {
                this->state = PRINTING_PROGRAM_MEMORY;
            } else if (this->r.read_data.subfunction == IHEX_BLANK_CHECK) {
                this->state = CHECKING_PROGRAM_MEMORY;
            }
            fetch_buffer();
        }
        break;

    case IHEX_MISC_READ_RECORD:
        /* :02xxxx08ffsscc
         * lock bits, LFuse, HFuse, EFuse, signature, calibration byte
         */
        this->subfunction = this->r.misc_read.subfunction;
        this->selection = this->r.misc_read.selection;
        if (this->r.misc_read.subfunction == IHEX_MISC_READ_SIGNATURE) {
            cmd = READ_SIGNATURE_BYTE;
            switch (this->r.misc_read.selection) {
            case IHEX_SIGNATURE0:
                val2 = 0;
                break;

            case IHEX_SIGNATURE1:
                val2 = 1;
                break;

            case IHEX_SIGNATURE2:
                val2 = 2;
                break;

            case IHEX_CALIBRATION_BYTE:
                cmd = READ_CALIBRATION_BYTE;
                break;
            }
        } else if (this->r.misc_read.subfunction == IHEX_MISC_READ_FUSES) {
            switch (this->r.misc_read.selection) {
            case IHEX_LOCKBITS:
                cmd = READ_LOCK_BITS;
                break;

            case IHEX_LOW_FUSE:
                cmd = READ_FUSE_BITS;
                break;

            case IHEX_HIGH_FUSE:
                cmd = READ_FUSE_HIGH_BITS_1;
                val1 = READ_FUSE_HIGH_BITS_2;
                break;

            case IHEX_EXTENDED_FUSE:
                cmd = READ_EXTENDED_FUSE_BITS_1;
                val1 = READ_EXTENDED_FUSE_BITS_2;
                break;
            }
        }
        if (cmd) {
            this->info.icsd.txbuf[0] = cmd;
            this->info.icsd.txbuf[1] = val1;
            this->info.icsd.txbuf[2] = val2;
            this->info.icsd.txbuf[3] = 0;
            this->info.icsd.tcnt = ICSD_BUFLEN;
            this->state = PRINTING_MISC_DATA;
            send_JOB(ICSD, &this->info.icsd);
        }
        break;

    case IHEX_ERASE_RECORD:
        /* :02xxxx09ffsscc
         * :020000090302F0
         */
        if (this->r.erase.subfunction == 3 && this->r.erase.selection == 2) {
            this->info.icsd.txbuf[0] = CHIP_ERASE_1;
            this->info.icsd.txbuf[1] = CHIP_ERASE_2;
            this->info.icsd.txbuf[2] = 0;
            this->info.icsd.txbuf[3] = 0;
            this->info.icsd.tcnt = ICSD_BUFLEN;
            this->state = ERASING_CHIP;
            send_JOB(ICSD, &this->info.icsd);
        }
        break;
    }
}

PRIVATE void load_program_memory_page(void)
{
    /* Load pagebuf into the Program Memory Page.
     * Preload the first byte into txbuf[3] and decrement bcnt. 
     */
             
    this->state = LOADING_PROGRAM_MEMORY_PAGE;
    this->info.icsd.bp = this->pagebuf;
    this->info.icsd.bcnt = sizeof(this->pagebuf) -1;
    this->info.icsd.waddr = 0;
    this->info.icsd.txbuf[0] = LOAD_PROGRAM_MEMORY_PAGE;
    this->info.icsd.txbuf[1] = 0;
    this->info.icsd.txbuf[2] = 0;
    this->info.icsd.txbuf[3] = *(this->info.icsd.bp)++;
    this->info.icsd.tcnt = ICSD_BUFLEN;
    send_JOB(ICSD, &this->info.icsd);
}

PRIVATE void fetch_buffer(void)
{
    /* Increment the start_loc on subsequent calls when pindex is at the
     * full extent of the pagebuf. Then erase pindex for the next buffer.
     */
     
    this->start_loc += this->pindex;
    this->pindex = 0;
    ushort_t len = this->end_loc - this->start_loc + 1;
    this->n_bytes = MIN(SPM_PAGESIZE, len);
    if (this->in_eeprom) {
        read_eeprom();
    } else {
        read_flash();
    }
}

PRIVATE void write_eeprom(void)
{
    while (this->n_bytes && *this->bp == UNPROGRAMMED) {
        this->ofs_address++;
        this->n_bytes--;
        this->bp++;
    }
    if (this->n_bytes) {
        this->state = WRITING_EEPROM_BYTE;
        this->info.icsd.txbuf[0] = WRITE_EEPROM_MEMORY_BYTE;
        this->info.icsd.txbuf[1] = this->ofs_address >> 8;
        this->info.icsd.txbuf[2] = this->ofs_address;
        this->info.icsd.txbuf[3] = *this->bp;
        this->info.icsd.tcnt = ICSD_BUFLEN;
        send_JOB(ICSD, &this->info.icsd);
        this->ofs_address++;
        this->n_bytes--;
        this->bp++;
    } else {
        this->state = READY;
        print_prompt('.');
    }
}

PRIVATE void read_eeprom(void)
{
    this->info.icsd.bp = this->pagebuf;
    this->info.icsd.bcnt = this->n_bytes;
    this->info.icsd.waddr = this->start_loc; /* byte address */
    this->info.icsd.txbuf[0] = READ_EEPROM_MEMORY;
    this->info.icsd.txbuf[1] = this->info.icsd.waddr >> 8 & 0x03;
    this->info.icsd.txbuf[2] = this->info.icsd.waddr & 0xFF;
    this->info.icsd.txbuf[3] = 0;
    this->info.icsd.tcnt = ICSD_BUFLEN;
    send_JOB(ICSD, &this->info.icsd);
}

PRIVATE void read_flash(void)
{
    this->info.icsd.bp = this->pagebuf;
    this->info.icsd.bcnt = this->n_bytes;
    this->info.icsd.waddr = this->start_loc >> 1; /* word address */
    this->info.icsd.txbuf[0] = READ_PROGRAM_MEMORY;
    this->info.icsd.txbuf[1] = this->info.icsd.waddr >> 8 & 0xFF;
    this->info.icsd.txbuf[2] = this->info.icsd.waddr & 0xFF;
    this->info.icsd.txbuf[3] = 0;
    this->info.icsd.tcnt = ICSD_BUFLEN;
    send_JOB(ICSD, &this->info.icsd);
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
        puthex(this->pagebuf[this->pindex + i]);
        sum += this->pagebuf[this->pindex + i];
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

/* Print the result of a misc read as the corresponding misc write record.
 * This produces bogus write records for read-only values.
 */
PRIVATE void print_misc_write_record(uchar_t val)
{
    this->lindex = 0;

    bputc(':');
    puthex(0x03);
    uchar_t sum = 0x03;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_MISC_WRITE_RECORD);
    sum += IHEX_MISC_WRITE_RECORD;
    puthex(this->subfunction);
    sum += this->subfunction;
    puthex(this->selection);
    sum += this->selection;
    puthex(val);
    sum += val;
    puthex(-sum);

    bputc('\n');

    sae_SER(this->info.ser, this->linebuf, this->lindex);
}

PRIVATE void bputc(uchar_t c)
{
    if (this->lindex < LINE_LEN) {
        this->linebuf[this->lindex++] = c;
    }
}

PRIVATE void bputs_P(PGM_P str)
{
    char ch;
    while ((ch = pgm_read_byte_near(str++)) != 0) {
        bputc(ch);
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

PRIVATE void power_off(void)
{
    this->state = POWERING_OFF;
    this->icsd_power = FALSE;
    send_SET_IOCTL(ICSD, SIOC_DEVICE_POWER, POWER_OFF);
}

/* end code */
