/* isp/hvpp.c */

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

/* High Voltage Parallel Programmer for the 28 pin DIP ATmega328P-PU device.
 *
 * Handle incoming characters as an INTEL HEX file.
 *
 * See also:-
 * Atmel datasheet DS40002061A [p.294-303,320-321]
 * Atmel App Note AVR061: STK500 Communication Protocol [doc2525.pdf].
 * Intel Hexadecimal Object File Format Specification Rev.A 1/6/88 [Hexfrmt.pdf]
 * Philips P89C51RC+ Product Specification [P89C51RC+IN.pdf, p.43-4].
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "isp/ihex.h"
#include "isp/hvpp.h"

/* I am .. */
#define SELF HVPP
#define this hvpp

#define UNPROGRAMMED  0xFF

/* These are the controller pins */
#define XA0_BIT       _BV(PORTC0)
#define XA1_BIT       _BV(PORTC1)
#define BS1_BIT       _BV(PORTC2)
#define BS2_BIT       _BV(PORTC3)
#define WR_BIT        _BV(DDC4)
#define OE_BIT        _BV(DDC5)
#define RDY_BIT       _BV(PORTD2)
#define VCC_BIT       _BV(PORTD3)
#define VPP_BIT       _BV(PORTD4)
#define XTAL1_BIT     _BV(PORTD5)
#define PAGEL_BIT     _BV(PORTD7)

/* data bus command values [p.297..303] */
#define NOP_CMD                   0x00
#define CHIP_ERASE_CMD            0x80
#define WRITE_FLASH_CMD           0x10
#define WRITE_EEPROM_CMD          0x11
#define READ_FLASH_CMD            0x02
#define READ_EEPROM_CMD           0x03
#define WRITE_FUSE_CMD            0x40
#define WRITE_LOCK_CMD            0x20
#define READ_FUSE_LOCK_CMD        0x04
#define READ_SIG_CALIB_CMD        0x08

/* Parallel programming characteristics [p.320]
 * microsecond delays are in floating point to satisfy
 * _delay_us() in <util/delay.h>
 */
#define ONE_MICROSECOND               1.0
#define FIVE_MICROSECONDS             5.0
#define SIXTY_MICROSECONDS           60.0
#define THREE_HUNDRED_MICROSECONDS  300.0
#define THREE_SIXTY_MICROSECONDS    360.0

#define RESET_DELAY                 SIXTY_MICROSECONDS
#define START_PROGRAMMING_DELAY     THREE_HUNDRED_MICROSECONDS
#define END_PAGE_PROGRAMMING_DELAY  THREE_SIXTY_MICROSECONDS
#define PULSE_WIDTH                 ONE_MICROSECOND
#define SETTLING_TIME               ONE_MICROSECOND
#define HOLD_TIME                   ONE_MICROSECOND

/* Pulse timing specifications [p.320-1] */

/* XTAL1 pulse operates on leading and trailing edges, as in a 74HC373 */
#define tDVXH  SETTLING_TIME
#define tXHXL  PULSE_WIDTH
#define tXLDX  HOLD_TIME

/* PAGEL pulse operates on leading and trailing edges, as in a 74HC373 */
#define tBVPH  SETTLING_TIME
#define tPHPL  PULSE_WIDTH
#define tPLBX  HOLD_TIME

/* WR pulse operates on the leading edge, as in a 74HC374 */
#define tBVWL  SETTLING_TIME
#define tWLWH  PULSE_WIDTH
#define tWLBX  HOLD_TIME

/* PCPAGE,PCWORD [p.294]
 * These two masks separate the offset address
 * into an 8-bit page and 7-bit offset.
 *
 * Note that these masks operate on byte values with the result 
 * shifted right one more bit to represent a word value before
 * writing it as a page number.
 */
#define PROGRAM_PAGE_NUMBER_MASK 0x7F80
#define PROGRAM_PAGE_OFFSET_MASK 0x007F

/* EEPROM has 10-bit value 2-bit pagesize */
#define EEPROM_PAGE_NUMBER_MASK 0x03FC 
#define EEPROM_PAGE_OFFSET_MASK 0x0003

typedef enum {
    IDLE = 0,
    REDIRECTING_TO_SELF,
    READY,
    WRITING_PROGRAM_MEMORY_PAGE,
    WRITING_EEPROM_MEMORY,
    PRINTING_MEMORY,
    ABORTING,
    FINISHED
} __attribute__ ((packed)) state_t;

#define LINE_LEN 50 /* output buffer */

typedef struct {
    state_t state;    
    unsigned device_power : 1; /* the socket is powered */
    unsigned in_record : 1;    /* between ':' and '\n' */
    unsigned dirty : 1;        /* pagebuf has been written */
    unsigned seen_eof : 1;     /* TRUE from EOF record to POWER_OFF */
    unsigned page_programmed : 1; /* issue a no-op prior to client reply */
    unsigned in_eeprom : 1;    /* eeprom data */
    hvpp_info *headp;
    ushort_t ofs_address;
    uchar_t error;
    ushort_t hcount;           /* incoming hex char count */
    ushort_t bcount;           /* translated binary record bytes */
    ushort_t end_loc;          /* read memory end address */
    uchar_t n_bytes;           /* number of bytes contained within pagebuf */
    uchar_t pindex;            /* iterative loop hex record start point */
    uchar_t subfunction;
    uchar_t selection;
    ushort_t lindex;           /* index into linebuf output buffer */
    /* eeprom */
    uchar_t low_addr;
    uchar_t *bp;
    uchar_t high_addr;
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
    union {
        ser_info ser;
    } info;
    uchar_t linebuf[LINE_LEN];
    /* ATmega328P page size in bytes = 128 [p.294] */
    uchar_t pagebuf[SPM_PAGESIZE];
    uchar_t readbuf[SPM_PAGESIZE];
} hvpp_t;

/* I have .. */
static hvpp_t this;

/* I can .. */

PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void consume(CharProc vp);
PRIVATE void parse(void);
PRIVATE void proc_record(void);
PRIVATE void fetch_buffer(void);
PRIVATE void load_program_memory_page(void);
PRIVATE void print_data_record(void);
PRIVATE void print_eof_record(void);
PRIVATE void print_misc_write_record(uchar_t val);
PRIVATE void puthex(uchar_t ch);
PRIVATE void bputc(uchar_t c);
PRIVATE void bputs_P(PGM_P str);
PRIVATE void put_nibble(uchar_t v);
PRIVATE uchar_t get_nibble(uchar_t c);
PRIVATE void enable_rdy_detection(void);
PRIVATE void print_prompt(uchar_t c);
PRIVATE void power_off(void);

PRIVATE uchar_t read_pinb(void);
PRIVATE void set_portb_output(void);
PRIVATE void set_portb_input(void);
PRIVATE void enter_programming_mode(void);
PRIVATE void exit_programming_mode(void);
PRIVATE void pulse_xtal1(void);
PRIVATE void pulse_pagel(void);
PRIVATE void pulse_wr(void);
PRIVATE void load_command(uchar_t cmd);
PRIVATE void load_address_low_byte(uchar_t adr);
PRIVATE void load_address_high_byte(uchar_t adr);
PRIVATE void load_data_low_byte(uchar_t val);
PRIVATE void load_data_high_byte(uchar_t val);
PRIVATE void latch_data(void);
PRIVATE void program_flash_page(void);
PRIVATE void chip_erase(void);
PRIVATE void program_flash(void);
PRIVATE void end_page_programming(void);
PRIVATE void read_flash(void);

PRIVATE void load_eeprom_data_byte(uchar_t val);
PRIVATE void program_eeprom(void);
PRIVATE void read_eeprom(void);

/* encapsulated bitops */
PRIVATE void set_xa0(void);
PRIVATE void clear_xa0(void);
PRIVATE void set_xa1(void);
PRIVATE void clear_xa1(void);
PRIVATE void set_bs1(void);
PRIVATE void clear_bs1(void);
PRIVATE void set_bs2(void);
PRIVATE void clear_bs2(void);
PRIVATE void set_wr(void);
PRIVATE void clear_wr(void);
PRIVATE void set_oe(void);
PRIVATE void clear_oe(void);
PRIVATE void set_vcc(void);
PRIVATE void clear_vcc(void);
PRIVATE void set_vpp(void);
PRIVATE void clear_vpp(void);
PRIVATE void set_pagel(void);
PRIVATE void clear_pagel(void);
PRIVATE void set_xtal1(void);
PRIVATE void clear_xtal1(void);
PRIVATE void turn_vcc_on(void);
PRIVATE void turn_vcc_off(void);
PRIVATE void turn_vpp_on(void);
PRIVATE void turn_vpp_off(void);

PUBLIC uchar_t receive_hvpp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        consume(m_ptr->VPTR);
        break;

    case NOT_BUSY:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            this.in_eeprom = FALSE;
            this.seen_eof = FALSE;
            if (this.device_power) {
                power_off();
            }

            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL)
                    start_job(); 
            }
        }
        break;

    case JOB:
        {
            hvpp_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                hvpp_info *tp;
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
    enter_programming_mode();
    this.device_power = TRUE;
    this.state = REDIRECTING_TO_SELF;
    this.headp->lineno = 1;
    send_SET_IOCTL(SER, SIOC_CONSUMER, SELF);
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case REDIRECTING_TO_SELF:
        this.state = READY;
        print_prompt('.');
        break;

    case READY:
        break;

    case WRITING_PROGRAM_MEMORY_PAGE:
        read_flash();
        this.dirty = FALSE;
        if (memcmp(this.pagebuf, this.readbuf, sizeof(this.readbuf))) {
            this.state = ABORTING;
            print_prompt('R');
            break;
        }
        this.state = READY;
        memset(this.pagebuf, UNPROGRAMMED, sizeof(this.pagebuf));
        if (this.bcount) {
            parse();
        } else if (this.seen_eof) {
            this.seen_eof = FALSE;
        } else {
            print_prompt('.');
        }
        break;

    case WRITING_EEPROM_MEMORY:
        program_eeprom();
        break;

    case PRINTING_MEMORY:
        /* We arrive here after printing a record.
         * The number of bytes in readbuf is this.n_bytes.
         */

        if (this.pindex < this.n_bytes) {
            print_data_record();
        } else if (this.ofs_address + this.pindex < this.end_loc) {
            fetch_buffer();
            print_data_record();
        } else {
            this.state = FINISHED;
            print_eof_record();
        }
        break;

    case ABORTING:
    case FINISHED:
        power_off();
        break;
    }
}

PRIVATE void consume(CharProc vp)
{
    char ch;

    while ((vp) (&ch) == EOK) {
        switch (ch) {
        case '\n': /* 0x0a */
            if (this.in_record) {
                this.in_record = FALSE;
                parse();
            }
            this.hcount = 0;
            this.headp->lineno++;
            break;

        case ':':
            if (this.hcount == 0) {
                this.bcount = 0;
                this.in_record = TRUE;
            } else {
                /* colon within record means file is corrupt */
                this.state = ABORTING;
                print_prompt('Z');
                return;
            }
            break;

        case '\r': /* 0x0d */
            break;

        default:
            if (this.in_record && this.bcount < LINE_LEN) {
                if (isxdigit(ch)) {
                    this.hcount++;
                    uchar_t hex = get_nibble(toupper(ch));
                    if (isodd(this.hcount)) {
                        this.r.recbuf[this.bcount] = hex << 4;
                    } else {
                        this.r.recbuf[this.bcount++] |= hex;
                    }
                } else {
                    /* non-hex character means file is corrupt */
                    this.state = ABORTING;
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
    for (ushort_t i = 0; i < this.bcount; i++) 
        sum += this.r.recbuf[i];
    if (sum) {
        this.state = ABORTING;
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
    uchar_t val = 0;
    uchar_t err = EOK;

    switch (this.r.data.record_type) {
    case IHEX_DATA_RECORD:
        /* :nnaaaa00dd....ddcc
         *
         */
        addr = this.r.data.offset_high << 8 | this.r.data.offset_low;
        if (this.in_eeprom) {
           /* Up to 16 bytes in a record.
            * 4 bytes per eeprom page.
            * Data may span upto 5 pages, 2 partially.
            */
            this.state = WRITING_EEPROM_MEMORY;
            this.bp = this.r.data.buf;
            this.n_bytes = this.r.data.datalen;
            this.high_addr = (addr & EEPROM_PAGE_NUMBER_MASK) >> 2;
            this.low_addr = addr & EEPROM_PAGE_OFFSET_MASK; 
            program_eeprom();
        } else {
            page = addr & PROGRAM_PAGE_NUMBER_MASK;
            offset = addr & PROGRAM_PAGE_OFFSET_MASK;

            if (this.dirty && (this.ofs_address != page ||
                        offset + this.r.data.datalen > SPM_PAGESIZE)) {
                load_program_memory_page();
            } else {
                this.ofs_address = page;
                memcpy(this.pagebuf + offset, this.r.data.buf,
                                this.r.data.datalen);
                this.dirty = TRUE;
                print_prompt('.');
            }
        }
        break;

    case IHEX_END_OF_FILE_RECORD:
        /* :00xxxx01cc
         *
         */
        this.seen_eof = TRUE;
        if (this.dirty) {
            /* pagebuf residual data */
            load_program_memory_page();
        } else {
            power_off();
        }
        break;

    case IHEX_EXTENDED_LINEAR_ADDRESS_RECORD:
        /* :02xxxx04aaaacc
         * 0x0081 equates to eeprom segment
         * :02000004008179
         */
        if (this.r.extended_linear_address.ulba_high == 0x00 && 
                this.r.extended_linear_address.ulba_low == 0x81) {
            this.in_eeprom = TRUE;
        } else {
            this.in_eeprom = FALSE;
        }
        print_prompt('.');
        break;

    case IHEX_MISC_WRITE_RECORD:
        /* :nnxxxx06ffssddcc
         * lock bits, LFuse, HFuse, EFuse
         */
        if (this.r.misc_write.subfunction == IHEX_MISC_WRITE_FUSES) {
            this.state = FINISHED;
            switch (this.r.misc_write.selection) {
            case IHEX_LOCKBITS:
                load_command(WRITE_LOCK_CMD);
                load_data_low_byte(this.r.misc_write.data);
                clear_bs1();
                clear_bs2();
                pulse_wr();
                enable_rdy_detection();
                break;

            case IHEX_LOW_FUSE:
                load_command(WRITE_FUSE_CMD);
                load_data_low_byte(this.r.misc_write.data);
                clear_bs1();
                clear_bs2();
                pulse_wr();
                enable_rdy_detection();
                break;

            case IHEX_HIGH_FUSE:
                load_command(WRITE_FUSE_CMD);
                load_data_low_byte(this.r.misc_write.data);
                set_bs1();
                clear_bs2();
                pulse_wr();
                clear_bs1();
                enable_rdy_detection();
                break;

            case IHEX_EXTENDED_FUSE:
                load_command(WRITE_FUSE_CMD);
                load_data_low_byte(this.r.misc_write.data);
                clear_bs1();
                set_bs2();
                pulse_wr();
                clear_bs2();
                enable_rdy_detection();
                break;

            default:
                err = EINVAL;
                break;
            }
        } else {
            err = EINVAL;
        }
        break;

    case IHEX_READ_DATA_RECORD:
        /* :05xxxx07sssseeeeffcc
         * read data from ssss to eeee.
         * ff = 0 == display 
         *      1 == blank check
         */
        this.ofs_address = this.r.read_data.start_high << 8
                              | this.r.read_data.start_low;
        this.end_loc = (this.r.read_data.end_high << 8
                            | this.r.read_data.end_low);
        this.subfunction = this.r.read_data.subfunction;

        if (this.ofs_address < this.end_loc) {
            this.pindex = 0;
            switch (this.r.read_data.subfunction) {
            case IHEX_DISPLAY_DATA:
                fetch_buffer();
                this.state = PRINTING_MEMORY;
                print_data_record();
                break;

            case IHEX_BLANK_CHECK:
                {
                    uchar_t failed = FALSE;
                    /* We arrive here after fetching a buffer.
                     * this.pindex is zero after fetching a buffer,
                     * non-zero after printing a record.
                     * The number of bytes in readbuf is this.n_bytes.
                     */
                    /* Disable the watchdog as checking the whole 32k takes
                     * longer than it will allow. It will get reenabled later
                     * when it wakes up in receive() - not necessarily on the
                     * next message.
                     */
                    wdt_disable();
                    while (!failed && this.ofs_address < this.end_loc) {
                        fetch_buffer();
                        for ( ;this.pindex < this.n_bytes; this.pindex++) {
                            if (this.readbuf[this.pindex] != UNPROGRAMMED) {
                                failed = TRUE;
                                break;
                            }
                        }
                    }
                    this.state = FINISHED;
                    this.lindex = 0;
                    if (failed) {
                        ushort_t location = this.ofs_address + this.pindex;
                        bputs_P(PSTR("0x"));
                        puthex(location >> 8);
                        puthex(location & 0xFF);
                        bputc('\n');
                    } else {
                        bputs_P(PSTR("blank\n"));
                    }
                    sae_SER(this.info.ser, this.linebuf, this.lindex);
                }
                break;

            default:
                err = EINVAL;
                break;
            }
        } else {
            err = EINVAL;
        }
        break;

    case IHEX_MISC_READ_RECORD:
        /* :02xxxx08ffsscc
         * lock bits, LFuse, HFuse, EFuse, signature, calibration byte
         */
        this.subfunction = this.r.misc_read.subfunction;
        this.selection = this.r.misc_read.selection;
        switch (this.r.misc_read.subfunction) {
        case IHEX_MISC_READ_SIGNATURE:
            switch (this.r.misc_read.selection) {
            case IHEX_SIGNATURE0:
                load_command(READ_SIG_CALIB_CMD);
                load_address_low_byte(0);
                set_portb_input();
                clear_bs1();
                clear_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            case IHEX_SIGNATURE1:
                load_command(READ_SIG_CALIB_CMD);
                load_address_low_byte(1);
                set_portb_input();
                clear_bs1();
                clear_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            case IHEX_SIGNATURE2:
                load_command(READ_SIG_CALIB_CMD);
                load_address_low_byte(2);
                set_portb_input();
                clear_bs1();
                clear_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            case IHEX_CALIBRATION_BYTE:
                load_command(READ_SIG_CALIB_CMD);
                load_address_low_byte(0);
                set_portb_input();
                set_bs1();
                clear_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            default:
                err = EINVAL;
                break;
            }
            break;

        case IHEX_MISC_READ_FUSES:
            switch (this.r.misc_read.selection) {
            case IHEX_LOCKBITS:
                load_command(READ_FUSE_LOCK_CMD);
                set_portb_input();
                set_bs1();
                clear_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            case IHEX_LOW_FUSE:
                load_command(READ_FUSE_LOCK_CMD);
                set_portb_input();
                clear_bs1();
                clear_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            case IHEX_HIGH_FUSE:
                load_command(READ_FUSE_LOCK_CMD);
                set_portb_input();
                set_bs1();
                set_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            case IHEX_EXTENDED_FUSE:
                load_command(READ_FUSE_LOCK_CMD);
                set_portb_input();
                clear_bs1();
                set_bs2();
                val = read_pinb();
                set_portb_output();
                break;

            default:
                err = EINVAL;
                break;
            }
            break;

        default:
            err = EINVAL;
            break;
        }
        if (err == EOK) {
            this.state = FINISHED;
            print_misc_write_record(val);
        }
        break;

    case IHEX_ERASE_RECORD:
        /* :02xxxx09ffsscc
         * Example to erase the chip:-
         * :020000090302F0
         */
        if (this.r.erase.subfunction == 3 && this.r.erase.selection == 2) {
            this.state = FINISHED;
            chip_erase();
        } else {
            err = EINVAL;
        }
        break;

    default:
        err = EINVAL;
        break;
    }
    if (err != EOK) {
        send_REPLY_RESULT(SELF, err);
    }
}

PRIVATE void load_program_memory_page(void)
{
    /* Load pagebuf into the Program Memory Page. */
    this.state = WRITING_PROGRAM_MEMORY_PAGE;
    program_flash();         
}

PRIVATE void fetch_buffer(void)
{
    /* Increment the ofs_address on subsequent calls when pindex is at the
     * full extent of the pagebuf. Then erase pindex for the next buffer.
     */
     
    this.ofs_address += this.pindex;
    this.pindex = 0;
    ushort_t len = this.end_loc - this.ofs_address + 1;
    this.n_bytes = MIN(SPM_PAGESIZE, len);
    
    if (this.in_eeprom) {
        read_eeprom();
    } else {
        read_flash();
    }
}

/* Construct a data record in linebuf encoding pagebuf from this.pindex onwards.
 * This should be the minimum of 16 and the bytes remaining in the buffer.
 * Send the hex record to the serial port.
 */
PRIVATE void print_data_record(void)
{
    uchar_t len = this.n_bytes - this.pindex;
    len = MIN(IHEX_MAX_DATA_BYTES, len);
    ushort_t ofs = this.ofs_address + this.pindex;

    this.lindex = 0;

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
        puthex(this.readbuf[this.pindex + i]);
        sum += this.readbuf[this.pindex + i];
    }

    puthex(-sum);

    bputc('\n');

    this.pindex += len;

    sae_SER(this.info.ser, this.linebuf, this.lindex);
}

PRIVATE void print_eof_record(void)
{
    uchar_t len = 0;
    uchar_t csum = 0xFF;
    this.lindex = 0;

    bputc(':');
    puthex(len);
    puthex(0x00);
    puthex(0x00);
    puthex(IHEX_END_OF_FILE_RECORD);
    puthex(csum);
    bputc('\n');

    sae_SER(this.info.ser, this.linebuf, this.lindex);
}

/* Print the result of a misc read as the corresponding misc write record.
 * This produces bogus write records for read-only values.
 */
PRIVATE void print_misc_write_record(uchar_t val)
{
    this.lindex = 0;

    bputc(':');
    puthex(0x03);
    uchar_t sum = 0x03;
    puthex(0x00);
    sum += 0x00;
    puthex(0x00);
    sum += 0x00;
    puthex(IHEX_MISC_WRITE_RECORD);
    sum += IHEX_MISC_WRITE_RECORD;
    puthex(this.subfunction);
    sum += this.subfunction;
    puthex(this.selection);
    sum += this.selection;
    puthex(val);
    sum += val;
    puthex(-sum);
    bputc('\n');

    sae_SER(this.info.ser, this.linebuf, this.lindex);
}

PRIVATE void bputc(uchar_t c)
{
    if (this.lindex < LINE_LEN) {
        this.linebuf[this.lindex++] = c;
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
#define HIGH_NIBBLE(c)     ((c) >> 4 & 0x0f)
#define LOW_NIBBLE(c)      ((c) & 0x0f)

    put_nibble(HIGH_NIBBLE(ch));
    put_nibble(LOW_NIBBLE(ch));
}

PRIVATE uchar_t get_nibble(uchar_t c)
{
    return(c > '9' ? c - 'A' + 10 : c - '0');
}

/* -----------------------------------------------------
   Handle an INT0 interrupt.
   This appears as <__vector_1>: in the .lst file.
   -----------------------------------------------------*/
ISR(INT0_vect)
{
    /* disable the interrupt [p.80] */
    EIMSK &= ~_BV(INT0);
    send_NOT_BUSY(SELF);
}

PRIVATE void enable_rdy_detection(void)
{
    /* enable the interrupt [p.80] */
    EIFR |= _BV(INTF0); /* set it to clear it [p.81] */
    EIMSK |= _BV(INT0);
}

PRIVATE uchar_t read_pinb(void)
{
    _delay_us(SETTLING_TIME);
    return PINB;
}

PRIVATE void set_portb_output(void)
{
    set_oe();     /* disable DUT output */
    DDRB = 0xFF;  /* config all pins as output */
}

PRIVATE void set_portb_input(void)
{
    DDRB =  0x00; /* config all pins as input */
    PORTB = 0x00; /* disable any soft pullup */
    clear_oe();   /* enable DUT output */
}

PRIVATE void enter_programming_mode(void)
{
    /* Configure INT0 to trigger on a low level [p.80]
     */
    EICRA &= ~(_BV(ISC01) | _BV(ISC00));
    
    /* VCC_BIT and VPP_BIT must be maintained at a low level to keep them
     * switched off.
     * The pins that connect directly with the ZIF should generate a zero
     * level output until after it is energised.
     */
    PORTD |= RDY_BIT;
    
    DDRD |= VPP_BIT | VCC_BIT | PAGEL_BIT | XTAL1_BIT;
    DDRC |= BS2_BIT | BS1_BIT | XA1_BIT | XA0_BIT;
    DDRB = 0xFF; /* All the DATA pins (B[7..0]) set to low-level output. */

    clear_xa0();
    clear_xa1();
    clear_pagel();
    clear_bs1();
    clear_bs2();
    clear_xtal1();

    set_wr();
    set_oe();

    turn_vcc_on();
    _delay_us(RESET_DELAY);

    turn_vpp_on();
    _delay_us(START_PROGRAMMING_DELAY);
}

PRIVATE void exit_programming_mode(void)
{
    if (this.page_programmed) {
        end_page_programming();
        _delay_us(END_PAGE_PROGRAMMING_DELAY);
        this.page_programmed = FALSE;
    }
    turn_vpp_off();
    clear_xa0();
    clear_xa1();
    clear_bs1();
    clear_bs2();
    clear_oe();
    clear_wr();
    clear_pagel();
    clear_xtal1();
    turn_vcc_off();

    PORTD &= ~RDY_BIT;
    DDRD &= ~(VPP_BIT | VCC_BIT | PAGEL_BIT | XTAL1_BIT);
    DDRC &= ~(OE_BIT | WR_BIT | BS2_BIT | BS1_BIT | XA1_BIT | XA0_BIT);
    DDRB = 0; /* All the DATA pins (B[7..0]) set to high-Z input. */

}

PRIVATE void pulse_xtal1()
{
    _delay_us(tDVXH);
    set_xtal1();
    _delay_us(tXHXL);
    clear_xtal1();
    _delay_us(tXLDX);
}

PRIVATE void pulse_pagel()
{
    _delay_us(tBVPH);
    set_pagel();
    _delay_us(tPHPL);
    clear_pagel();
    _delay_us(tPLBX);
}

PRIVATE void pulse_wr()
{
    _delay_us(tBVWL);
    clear_wr();
    _delay_us(tWLWH);
    set_wr();
    _delay_us(tWLBX); /* tWLBX is actually specified from when wr is cleared
                       * so this is being rather generous.
                       */
}

PRIVATE void load_command(uchar_t cmd)
{
    set_xa1();
    clear_xa0();
    clear_bs1();
    clear_bs2();
    set_portb_output();
    PORTB = cmd;
    pulse_xtal1();
}

PRIVATE void load_address_low_byte(uchar_t adr)
{
    /* [p.298] */
    clear_xa1();
    clear_xa0();
    clear_bs1();
    set_portb_output();
    PORTB = adr;
    pulse_xtal1();
}

PRIVATE void load_address_high_byte(uchar_t adr)
{
    /* [p.298] */
    clear_xa1();
    clear_xa0();
    set_bs1();
    set_portb_output();
    PORTB = adr;
    pulse_xtal1();
}

PRIVATE void load_data_low_byte(uchar_t val)
{
    /* [p.297] */
    clear_xa1();
    set_xa0();
    set_portb_output();
    PORTB = val;
    pulse_xtal1();
}

PRIVATE void load_data_high_byte(uchar_t val)
{
    /* [p.297] */
    set_bs1();
    clear_xa1();
    set_xa0();
    set_portb_output();
    PORTB = val;
    pulse_xtal1();
}

PRIVATE void latch_data(void)
{
   clear_xa1();
   set_xa0();
   set_bs1();
   pulse_pagel();
}

PRIVATE void program_flash_page(void)
{
    pulse_wr();
    enable_rdy_detection();
}

PRIVATE void chip_erase(void)
{
    /* [p.297] */
    load_command(CHIP_ERASE_CMD);
    pulse_wr();
    enable_rdy_detection();
}

PRIVATE void program_flash(void)
{
    uchar_t *bp = this.pagebuf;
    uchar_t high_addr = (this.ofs_address & PROGRAM_PAGE_NUMBER_MASK) >> 9; 
    uchar_t low_addr = (this.ofs_address & PROGRAM_PAGE_NUMBER_MASK) >> 1; 

    load_command(WRITE_FLASH_CMD);
    for (uchar_t i = 0; i < (sizeof(this.pagebuf) / sizeof(ushort_t)); i++) {
        uchar_t low = *bp++;
        uchar_t high = *bp++;
        if (low == UNPROGRAMMED && high == UNPROGRAMMED)
            continue;
        load_address_low_byte(low_addr + i);
        load_data_low_byte(low);
        load_data_high_byte(high);
        latch_data();
    }
    load_address_high_byte(high_addr);
    program_flash_page();
    this.page_programmed = TRUE;
}

PRIVATE void end_page_programming(void)
{
    /* [p.298] */
    load_command(NOP_CMD);
}

PRIVATE void read_flash(void)
{
    /* [p.300] */
    uchar_t *bp = this.readbuf;
    uchar_t high_addr = (this.ofs_address & PROGRAM_PAGE_NUMBER_MASK) >> 9; 
    uchar_t low_addr = (this.ofs_address & PROGRAM_PAGE_NUMBER_MASK) >> 1; 

    load_command(READ_FLASH_CMD);
    for (uchar_t i = 0; i < (sizeof(this.readbuf) / sizeof(ushort_t)); i++) {
        load_address_high_byte(high_addr);
        load_address_low_byte(low_addr + i);
        set_portb_input();
        clear_bs1();
        *bp++ = read_pinb();
        set_bs1();
        *bp++ = read_pinb();
        set_portb_output();
    }
}

PRIVATE void load_eeprom_data_byte(uchar_t val)
{
    /* [p.299-300] */
    clear_bs1();
    clear_xa1();
    set_xa0();
    set_portb_output();
    PORTB = val;
    pulse_xtal1();
}

PRIVATE void program_eeprom_page(void)
{
    clear_bs1();
    pulse_wr();
    enable_rdy_detection();
}

PRIVATE void program_eeprom(void)
{
   /* preconditions:-
    *  this.bp points into this.r.data.buf[]
    *  this.n_bytes holds the number of bytes remaining.
    *  this.high_addr holds the eeprom page number.
    *  this.low_addr holds the 0..3 offset within the page
    */
    uchar_t len = MIN(this.n_bytes, E2PAGESIZE);

    load_command(WRITE_EEPROM_CMD);
    load_address_high_byte(this.high_addr);
    for (; this.low_addr < len; this.low_addr++, this.n_bytes--) {
        uchar_t val = *this.bp++;
        if (val == UNPROGRAMMED)
            continue;
        load_address_low_byte(this.low_addr);
        load_eeprom_data_byte(val);
        latch_data();
    }
    this.high_addr++;
    this.low_addr = 0;
    if (this.n_bytes == 0)
        this.state = READY;
    program_eeprom_page();
    this.page_programmed = TRUE;
}

PRIVATE void read_eeprom(void)
{
    /* [p.300] */
    uchar_t *bp = this.readbuf;
    ushort_t n = this.ofs_address;

    load_command(READ_EEPROM_CMD);
    for (uchar_t i = 0; i < sizeof(this.readbuf); i++, n++) {
        load_address_high_byte(n >> 8);
        load_address_low_byte(n & 0xFF);
        set_portb_input();
        clear_bs1();
        *bp++ = read_pinb();
        set_portb_output();
    }
}

PRIVATE void print_prompt(uchar_t c)
{
    this.lindex = 0;
    bputc(c);
    sae_SER(this.info.ser, this.linebuf, this.lindex);
}

PRIVATE void power_off(void)
{
    exit_programming_mode();
    this.device_power = FALSE;
    this.state = IDLE;
    print_prompt('$');
}

/* encapsulated bitops */
PRIVATE void set_xa0(void)
{
    PORTC |=  XA0_BIT;
}

PRIVATE void clear_xa0(void)
{
    PORTC &= ~XA0_BIT;
}

PRIVATE void set_xa1(void)
{
    PORTC |=  XA1_BIT;
}

PRIVATE void clear_xa1(void)
{
    PORTC &= ~XA1_BIT;
}

PRIVATE void set_bs1(void)
{
    PORTC |=  BS1_BIT;
}

PRIVATE void clear_bs1(void)
{
    PORTC &= ~BS1_BIT;
}

PRIVATE void set_bs2(void)
{
    PORTC |=  BS2_BIT;
}

PRIVATE void clear_bs2(void)
{
    PORTC &= ~BS2_BIT;
}

PRIVATE void set_wr(void)
{
 /* high-Z input */
    DDRC &=  ~WR_BIT;
}

PRIVATE void clear_wr(void)
{
   /* low-Z zero output */
    DDRC |= WR_BIT;
}

PRIVATE void set_oe(void)
{
   /* high-Z input */
    DDRC &= ~OE_BIT;
}

PRIVATE void clear_oe(void)
{
   /* low-Z zero output */
    DDRC |= OE_BIT;
}

PRIVATE void set_vcc(void)
{
    PORTD |=  VCC_BIT;
}

PRIVATE void clear_vcc(void)
{
    PORTD &= ~VCC_BIT;
}

PRIVATE void set_vpp(void)
{
    PORTD |=  VPP_BIT;
}

PRIVATE void clear_vpp(void)
{
    PORTD &= ~VPP_BIT;
}

PRIVATE void set_pagel(void)
{
    PORTD |= PAGEL_BIT;
}

PRIVATE void clear_pagel(void)
{
    PORTD &= ~PAGEL_BIT;
}

PRIVATE void set_xtal1(void)
{
    PORTD |= XTAL1_BIT;
}

PRIVATE void clear_xtal1(void)
{
    PORTD &= ~XTAL1_BIT;
}

PRIVATE void turn_vcc_on(void)
{
    set_vcc();
}

PRIVATE void turn_vcc_off(void)
{
    clear_vcc();
}

PRIVATE void turn_vpp_on(void)
{
    set_vpp();
}

PRIVATE void turn_vpp_off(void)
{
    clear_vpp();
}

/* end code */
