/* fido/inp.c */

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

/* handle incoming characters */
 
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "sys/tty.h"
#include "sys/utc.h"
#include "sys/rv3028c7.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "oled/sh1106.h"
#include "oled/common.h"
#include "sys/ver.h"
#include "sys/inp.h"

/* I am .. */
#define SELF INP
#define this inp

#define MAX_ARGS 1

typedef enum {
    IDLE = 0,
    SELECTING_TEMPEST_OUTPUT,
    STARTING_TEMPEST,
    STOPPING_TEMPEST,
    STARTING_TPLOG,
    STOPPING_TPLOG,
    SELECTING_TIMZ_OUTPUT,
    STARTING_TIMZ,
    STOPPING_TIMZ,
    SELECTING_TTY_OUTPUT,
    SELECTING_VITZ_OUTPUT,
    STARTING_VITZ,
    STOPPING_VITZ,
    FETCHING_UTC,
    FETCHING_UNIXTIME,
    FETCHING_CALENDAR,
    FETCHING_EEPROM_BACKUP_REG,
    FETCHING_EEOFFSET_REG,
    SETTING_EEOFFSET_REG,
    SETTING_EEPROM_BACKUP_REG,
    SETTING_UNIXTIME,
    UPDATING_RTC,
    SETTING_PERIODIC_TIME_INTERRUPT,
    DISABLING_TIMESTAMP,
    ENABLING_TIMESTAMP,
    PRINTING_TIMESTAMP,
    CLEARING_TIMESTAMP,
    FETCHING_TIMESTAMP_STATUS,
    SETTING_EEOFFSET_VALUE_HIGH_BYTE,
    FETCHING_EEOFFSET_VALUE_LOW_BYTE,
    SETTING_EEOFFSET_VALUE_LOW_BYTE,
    FETCHING_EEOFFSET_VALUE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned in_comment : 1;
    unsigned read_utc : 1;
    unsigned update_utc : 1;
    unsigned update_calendar : 1;
    unsigned insign : 1;
    unsigned diff_utc : 1;
    uchar_t incount;
    long inval;
    short args[MAX_ARGS];
    uchar_t narg;
    union {
        ulong_t ubuf;
        ushort_t sbuf;
        uchar_t cbuf;
    } v;
    short eeoffset_value;
    union {
        utc_msg utc;
    } msg;
    CharProc vptr;
    uchar_t rtcbuf[7];
    struct tm time;
    union {
        twi_info twi;
    } info;
} inp_t;

/* I have .. */
static inp_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void consume(void);
PRIVATE void drain(void);
PRIVATE uchar_t tobcd(uchar_t v);
PRIVATE void print_date(void);

PUBLIC uchar_t receive_inp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_EMPTY:
        if (this.vptr && this.vptr != m_ptr->VPTR)
            drain();
        this.vptr = m_ptr->VPTR;
        if (this.state == IDLE && this.vptr)
            consume();
        break;

    case BUTTON_CHANGE:
        {
            char sbuf[10];
            sprintf_P(sbuf, PSTR("btn:%d\n"), m_ptr->mtype);
            tty_puts(sbuf);
        }
        break;
         
    case REPLY_DATA:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                char sbuf[10];
                sprintf_P(sbuf, PSTR("err:%d,%d\n"), this.state, m_ptr->RESULT);
                tty_puts(sbuf);
                this.state = IDLE;
            }
        }

        if (this.state == IDLE && this.vptr) {
            consume();
        }
        break;

    case PERIODIC_ALARM:
        if (this.state == IDLE) {
            if (this.read_utc) {
                /* get the UTC from oslo */
                this.state = FETCHING_UTC;
                this.msg.utc.request.taskid = SELF;
                this.msg.utc.request.op = GET_TIME;

                sae2_TWI_MTMR(this.info.twi, UTC_ADDRESS,
                        UTC_REQUEST, this.msg.utc, this.msg.utc);
            } else {
                /* get the unixtime from the RV3028C7 */
                this.state = FETCHING_UNIXTIME;
                sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                        RV_UNIX_TIME_0, this.v.ubuf);
            }
        } 
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(void)
{
    uchar_t ok = FALSE;

    switch (this.state) {
    case IDLE:
        return;

    case SELECTING_TEMPEST_OUTPUT:
        tty_puts_P(PSTR("so: "));
        ok = TRUE;
        break;

    case STARTING_TEMPEST:
        tty_puts_P(PSTR("es: "));
        ok = TRUE;
        break;

    case STOPPING_TEMPEST:
        tty_puts_P(PSTR("ds: "));
        ok = TRUE;
        break;

    case STARTING_TPLOG:
        tty_puts_P(PSTR("el: "));
        ok = TRUE;
        break;

    case STOPPING_TPLOG:
        tty_puts_P(PSTR("dl: "));
        ok = TRUE;
        break;

    case SELECTING_TIMZ_OUTPUT:
        tty_puts_P(PSTR("to: "));
        ok = TRUE;
        break;

    case STARTING_TIMZ:
        tty_puts_P(PSTR("et: "));
        ok = TRUE;
        break;

    case STOPPING_TIMZ:
        tty_puts_P(PSTR("dt: "));
        ok = TRUE;
        break;

    case SELECTING_TTY_OUTPUT:
        tty_puts_P(PSTR("ou: "));
        ok = TRUE;
        break;

    case SELECTING_VITZ_OUTPUT:
        tty_puts_P(PSTR("ov: "));
        ok = TRUE;
        break;

    case STARTING_VITZ:
        tty_puts_P(PSTR("ev: "));
        ok = TRUE;
        break;

    case STOPPING_VITZ:
        tty_puts_P(PSTR("dv: "));
        ok = TRUE;
        break;

    case FETCHING_UTC:
        {
            if (this.diff_utc) {
                this.state = FETCHING_UNIXTIME;
                sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                        RV_UNIX_TIME_0, this.v.ubuf);
                return;
            }
            char sbuf[18];
            sprintf_P(sbuf, PSTR("utc:%lu.%03ld\n"), this.msg.utc.reply.val,
                                   FRAC_TO_MILLIS(this.msg.utc.reply.frac));
            tty_puts(sbuf);
        }
        break;

    case FETCHING_UNIXTIME:
        if (this.update_utc) {
            this.update_utc = FALSE;
            this.read_utc = TRUE;
            this.msg.utc.request.taskid = SELF;
            this.msg.utc.request.op = SET_TIME;
            this.msg.utc.request.val = this.v.ubuf;
            sae2_TWI_MTMR(this.info.twi, UTC_ADDRESS,
                    UTC_REQUEST, this.msg.utc, this.msg.utc);
        } else if (this.update_calendar) {
            this.update_calendar = FALSE;
            /* avr-libc uses Y2K_EPOCH. subtract the 30 year difference. */
            time_t n = this.v.ubuf - UNIX_OFFSET;
            gmtime_r(&n, &this.time);
            
            this.rtcbuf[RV_SECONDS] = tobcd(this.time.tm_sec);
            this.rtcbuf[RV_MINUTES] = tobcd(this.time.tm_min);
            this.rtcbuf[RV_HOURS] = tobcd(this.time.tm_hour);
            this.rtcbuf[RV_WEEKDAY] = tobcd(this.time.tm_wday);
            this.rtcbuf[RV_DATE] = tobcd(this.time.tm_mday);
            this.rtcbuf[RV_MONTH] = tobcd(this.time.tm_mon +1);
            this.rtcbuf[RV_YEAR] = tobcd(this.time.tm_year - 100);
 
            /* set the calendar registers in the RV3028C7 */
            sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_SECONDS, this.rtcbuf);
        } else if (this.diff_utc) {
            tty_puts_P(PSTR("drift:"));
            char sbuf[10];
            this.diff_utc = FALSE;
            if (this.msg.utc.reply.val < this.v.ubuf) {
                sprintf_P(sbuf, PSTR("-%ld.%03ld\n"),
                                this.v.ubuf - this.msg.utc.reply.val -1,
                                FRAC_TO_MILLIS(256 - this.msg.utc.reply.frac));
            } else {
                sprintf_P(sbuf, PSTR("+%ld.%03ld\n"),
                                this.msg.utc.reply.val - this.v.ubuf,
                                FRAC_TO_MILLIS(this.msg.utc.reply.frac));
            }
            tty_puts(sbuf);
            this.inval = SELF << 8;
            send_SET_IOCTL(RTC, SIOC_PERIODIC_TIME_INTERRUPT, this.inval);
        } else {
            char sbuf[20];
            sprintf_P(sbuf, PSTR("rtc:%ld\n"), this.v.ubuf);
            tty_puts(sbuf);
        }
        break;

    case FETCHING_CALENDAR:
        {
            tty_puts_P(PSTR("cal:"));
            print_date();
        }
        break;

    case FETCHING_EEPROM_BACKUP_REG:
        {
            char sbuf[9];
            sprintf_P(sbuf, PSTR("eb:0x%0X\n"), this.v.cbuf);
            tty_puts(sbuf);
        }
        break;

    case FETCHING_EEOFFSET_REG:
        {
            char sbuf[9];
            sprintf_P(sbuf, PSTR("eo:0x%0X\n"), this.v.cbuf);
            tty_puts(sbuf);
        }
        break;

    case SETTING_EEOFFSET_REG:
        ok = TRUE;
        break;

    case SETTING_EEPROM_BACKUP_REG:
        ok = TRUE;
        break;

    case SETTING_UNIXTIME:
        ok = TRUE;
        break;

    case UPDATING_RTC:
        ok = TRUE;
        break;

    case SETTING_PERIODIC_TIME_INTERRUPT:
        if (this.diff_utc == FALSE)
            ok = TRUE;
        break;

    case DISABLING_TIMESTAMP:
        this.rtcbuf[RV_STATUS - RV_STATUS] &= ~(RV_BSF | RV_EVF);
        this.rtcbuf[RV_CONTROL_2 - RV_STATUS] &= ~RV_TSE;
        this.rtcbuf[RV_EVENT_CONTROL - RV_STATUS] |=  RV_TSR | RV_TSS;
        sae1_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
             RV_STATUS, this.rtcbuf, RV_EVENT_CONTROL - RV_STATUS +1);
        ok = TRUE;
        break;

    case ENABLING_TIMESTAMP:
        this.rtcbuf[RV_STATUS - RV_STATUS] &= ~ (RV_BSF | RV_EVF);
        this.rtcbuf[RV_CONTROL_2 - RV_STATUS] |=  RV_TSE;
        this.rtcbuf[RV_EVENT_CONTROL - RV_STATUS] &= ~ RV_TSOW;
        this.rtcbuf[RV_EVENT_CONTROL - RV_STATUS] |=  RV_TSR | RV_TSS;
        sae1_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
             RV_STATUS, this.rtcbuf, RV_EVENT_CONTROL - RV_STATUS +1);
        ok = TRUE;
        break;

    case PRINTING_TIMESTAMP:
        {
            tty_puts_P(PSTR("TS:"));
            print_date();
        }
        break;

    case CLEARING_TIMESTAMP:
        this.rtcbuf[RV_EVENT_CONTROL - RV_EVENT_CONTROL] |= RV_TSR;
        sae1_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_EVENT_CONTROL, this.rtcbuf, sizeof(uchar_t));
        ok = TRUE;
        break;

    case FETCHING_TIMESTAMP_STATUS:
        tty_puts_P(PSTR("tse:"));
        tty_printl(this.rtcbuf[RV_CONTROL_2 - RV_CONTROL_2] & RV_TSE);
        tty_putc('\n');
        break;

    case SETTING_EEOFFSET_VALUE_HIGH_BYTE:
        this.state = FETCHING_EEOFFSET_VALUE_LOW_BYTE;
        sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_EEPROM_BACKUP, this.v.cbuf);
        return;

    case FETCHING_EEOFFSET_VALUE_LOW_BYTE:
        if (((this.eeoffset_value & 0x01) && (this.v.cbuf & 0x80)) ||
            (!(this.eeoffset_value & 0x01) && !(this.v.cbuf & 0x80))) {
            /* nothing to do */
            ok = TRUE;
        } else {
            this.state = SETTING_EEOFFSET_VALUE_LOW_BYTE;
            if (this.eeoffset_value & 0x01)
                this.v.cbuf |= 0x80;
            else
                this.v.cbuf &= ~0x80;
            sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                    RV_EEPROM_BACKUP, this.v.cbuf);
            this.eeoffset_value = 0;
            return;
        }
        this.eeoffset_value = 0;
        break;

    case SETTING_EEOFFSET_VALUE_LOW_BYTE:
        ok = TRUE;
        break;

    case FETCHING_EEOFFSET_VALUE:
        /* sign extend the char value of the first byte */
        this.eeoffset_value = (short) ((char)this.v.sbuf);
        /* make room for the LSB */
        this.eeoffset_value <<= 1;
        /* OR bit 7 of the second byte into the LSB */
        this.eeoffset_value |= (this.v.sbuf >> 15) & 0x1;
        {
            char sbuf[15];
            sprintf_P(sbuf, PSTR("eeoffset:%d\n"), this.eeoffset_value);
            tty_puts(sbuf);
        }
        this.eeoffset_value = 0;
        break;
    }

    this.state = IDLE;
    if (ok)
        tty_puts_P(PSTR("ok\n"));
}

PRIVATE void consume(void)
{
    char ch;

    while (this.vptr) {
        switch ((this.vptr) (&ch)) {
        case EWOULDBLOCK:
            this.vptr = NULL;
            return;

        case EOK:
            if (ch == '#') {
                this.in_comment = TRUE;
                tty_putc(ch);
            } else if (this.in_comment) {
                if (ch == '\r') {
                    /* do nothing */
                } else if (ch == '\n') {
                    this.in_comment = FALSE;
                    tty_putc('\n');
                } else {
                    tty_putc(ch);
                }
            } else {
                switch (ch) {
                case 'b':
                    /* nnnb : read KEYPAD */
                    if (this.incount) {
                        send_READ(KEYPAD, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'c':
                    /* c : print cycle count */
                    {
                        char sbuf[22];
                        sprintf_P(sbuf, PSTR("cc:%lu,%d\n"), msg_count(),
                                                              msg_depth());
                        tty_puts(sbuf);
                    }
                    break;

                case 'C':
                    /* C : print calendar registers */
                    /* get the calendar registers from the RV3028C7 */
                    this.state = FETCHING_CALENDAR;
                    sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                          RV_SECONDS, this.rtcbuf);
                    return;

                case 'd':
                    /* 0d: disable Time Stamp
                     * 1d: enable Time Stamp
                     * 2d: print Time Stamp
                     * 3d: clear Time Stamp
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = DISABLING_TIMESTAMP;
                            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                                RV_STATUS, this.rtcbuf);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = ENABLING_TIMESTAMP;
                            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                                RV_STATUS, this.rtcbuf);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 2:
                            this.state = PRINTING_TIMESTAMP;
                            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                                RV_COUNT_TS, this.rtcbuf);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 3:
                            this.state = CLEARING_TIMESTAMP;
                            sae1_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                                RV_EVENT_CONTROL, this.rtcbuf, sizeof(uchar_t));
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 4:
                            this.state = FETCHING_TIMESTAMP_STATUS;
                            sae1_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                                RV_CONTROL_2, this.rtcbuf, sizeof(uchar_t));
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'e':
                    /* print the build ident */
                    tty_puts_P(version);
                    break;

                case 'f':
                    /* f : print OSCCAL */
                    {
                        char sbuf[19];
                        unsigned int j = OSCCAL;
                        sprintf_P(sbuf, PSTR("OSCCAL:0x%02X,(%u)\n"), j, j);
                        tty_puts(sbuf);
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'F':
                    /* nnnF : set OSCCAL to nnn */
                    if (this.incount) {
                        OSCCAL = this.inval;
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;
                    
                case 'h':
                    /* 1h : update UTC from UNIX TIME
                     * 2h : update calendar registers from UNIX TIME
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 1:
                            this.update_utc = TRUE;
                            break;

                        case 2:
                            this.update_calendar = TRUE;
                            break;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    this.read_utc = FALSE;
                    break;

                case 'k':
                    /* 0k : read RTC on the periodic time interrupt
                     * 1k : read UTC on the periodic time interrupt
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.read_utc = FALSE;
                            break;

                        case 1:
                            this.read_utc = TRUE;
                            break;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'l':
                    /* 0l : send STOP to TPLOG 
                     * 1l : send START to TPLOG
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = STOPPING_TPLOG;
                            send_STOP(TPLOG);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = STARTING_TPLOG;
                            send_START(TPLOG);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                 case 'M':
                    /* M : copy all configurable RV3028C7 registers to EEPROM
                     * send an UPDATE message to RTC to copy all configurable
                     * registers to EEPROM, since changes are lost when either
                     * power is removed, or when the the auto-refresh occurs
                     * at the beginning of the last second before midnight,
                     * according to the values in the calendar registers.
                     *
                     * See also: [RV-3028-C7: p.54].
                     */
                    if (this.incount) {
                        if (this.inval == 1) {
                            this.state = UPDATING_RTC;
                            send_UPDATE(RTC);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'n':
                    /* n : get the UNIXTIME_REG on the RV3028C7 */
                    this.state = FETCHING_UNIXTIME;
                    sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                      RV_UNIX_TIME_0, this.v.ubuf);
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    return;
  
                case 'N':
                    /* nnnN : set the UNIXTIME_REG on the RV3028C7 */
                    if (this.incount) {
                        this.state = SETTING_UNIXTIME;
                        this.v.ubuf = this.inval;
                        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                          RV_UNIX_TIME_0, this.v.ubuf);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break;

                case 'o':
                    /* o : get the EEOFFSET_REG from the RV3028C7 */
                    this.state = FETCHING_EEOFFSET_REG;
                    sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                      RV_EEPROM_OFFSET, this.v.cbuf);
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    return;

                case 'O':
                    /* nnnO : set the EEOFFSET_REG on the RV3028C7 */
                    if (this.incount) {
                        this.state = SETTING_EEOFFSET_REG;
                        this.v.cbuf = this.inval & 0xFF;
                        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                          RV_EEPROM_OFFSET, this.v.cbuf);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break;

                case 'p':
                    /* p : get the EEPROM_BACKUP_REG from the RV3028C7 */
                    this.state = FETCHING_EEPROM_BACKUP_REG;
                    sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                      RV_EEPROM_BACKUP, this.v.cbuf);
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    return;

                case 'P':
                    /* nnnP : set the EEPROM_BACKUP_REG on the RV3028C7
                     * change the factory default of 0x10 to 0x14 
                     * e.g.: 20P = 0x14, 148P = 0x94
                     *       16P = turn battery backup off (0x10).
                     *       20P = turn battery backup on  (0x14).
                     */
                    if (this.incount) {
                        this.state = SETTING_EEPROM_BACKUP_REG;
                        this.v.cbuf = this.inval;
                        sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                          RV_EEPROM_BACKUP, this.v.cbuf);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break;

                case 'r':
                    /* r : print the EEOFFSET value */
                    this.state = FETCHING_EEOFFSET_VALUE;
                    sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                             RV_EEPROM_OFFSET, this.v.sbuf);
                    return;

                case 'R':
                    /* [-]nnnR : set the EEOFFSET value [RV-3028-C7 p.38]
                     * A value of 255 advances by just under 1 second per hour.
                     * A value of -256 retards similarly.
                     * A value of 1 advances by over 2 seconds per month. 
                     * A value of 0 retards similarly.
                     */
                    if (this.incount) {
                        if (this.inval < -256 || this.inval > 255) {
                            tty_puts_P(PSTR("range\n"));
                        } else {
                            this.eeoffset_value = this.inval;
                            this.state = SETTING_EEOFFSET_VALUE_HIGH_BYTE;
                            this.v.cbuf = (uchar_t) (this.eeoffset_value >> 1);
                            sae2_TWI_MT(this.info.twi, RV3028C7_I2C_ADDRESS,
                                     RV_EEPROM_OFFSET, this.v.cbuf);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 's':
                    /* 0s : TEMPEST send STOP
                     * 1s : TEMPEST send START
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = STOPPING_TEMPEST;
                            send_STOP(TEMPEST);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = STARTING_TEMPEST;
                            send_START(TEMPEST);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'S':
                    /* nS - Select the destination for TEMPEST output data.
                     * 0S : output OFF
                     * 1S : send output to SUMO
                     * 2S : send output to LIMA
                     * 3S : send output to PERU
                     * 7S : send output to GCALL
                     */
                    if (this.incount) {
                        this.state = SELECTING_TEMPEST_OUTPUT;
                        send_SET_IOCTL(TEMPEST, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break; 

                case 't':
                    /* 0t : TIMZ send STOP
                     * 1t : TIMZ send START
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = STOPPING_TIMZ;
                            send_STOP(TIMZ);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = STARTING_TIMZ;
                            send_START(TIMZ);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'T':
                    /* nT - Select the destination for TIMZ output data.
                     * 0T : TIMZ output OFF
                     * 1T : TIMZ send output to SUMO
                     * 2T : TIMZ send output to LIMA
                     * 3T : TIMZ send output to PERU
                     * 7T : TIMZ send output to GCALL
                     */
                    if (this.incount) {
                        this.state = SELECTING_TIMZ_OUTPUT;
                        send_SET_IOCTL(TIMZ, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break;

                case 'u':
                    /* 0u - disable PERIODIC TIME INTERRUPT
                     * 1u - enable PERIODIC TIME INTERRUPT - second
                     * 2u - enable PERIODIC TIME INTERRUPT - minute
                     * 3u - print UTC drift
                     */
                    if (this.incount) {
                        if (this.inval == 3) {
                            this.diff_utc = TRUE;
                            this.inval = 1;
                            this.read_utc = TRUE;
                        } else {
                            this.diff_utc = FALSE; 
                        }
                        this.state = SETTING_PERIODIC_TIME_INTERRUPT;
                        /* encode self as the periodic_replyTo */
                        this.inval |= SELF << 8;
                        send_SET_IOCTL(RTC, SIOC_PERIODIC_TIME_INTERRUPT,
                                                                this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break;

                case 'v':
                    /* 0v : VITZ send STOP message
                     * 1v : VITZ send START message
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = STOPPING_VITZ;
                            send_STOP(VITZ);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = STARTING_VITZ;
                            send_START(VITZ);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                    }
                    break;

                case 'V':
                    /* nV - Select the destination for VITC output data.
                     * 0V = OFF
                     * 1V = LCD (sumo)
                     * 2V = OLED (lima)
                     * 3V = OLED (peru)
                     * 7V = GCALL (any/none)
                     */
                    if (this.incount) {
                        this.state = SELECTING_VITZ_OUTPUT;
                        send_SET_IOCTL(VITZ, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break;

                case 'z':
                    /* set the tty output destination
                     * 0z  local SER device
                     * 1z  GATEWAY OSTREAM device
                     * 2z  SPI_OLED_ADDRESS OSTREAM device
                     */
                    if (this.incount) {
                        this.state = SELECTING_TTY_OUTPUT;
                        send_SET_IOCTL(TTY, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    break; 

                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                    if (this.incount == 0) {
                        this.inval = 0;
                    } else {
                        this.inval *= 10;
                    }
                    this.inval += this.insign ? -(ch - '0') : (ch - '0');
                    this.incount++;
                    break;

                case ',':
                   /* arg separator */
                    if (this.incount && this.narg < MAX_ARGS) {
                        this.args[this.narg++] = (short) this.inval;
                        this.incount = 0;
                        this.insign = FALSE;
                    }
                    break;

                case '-':
                    if (this.incount == 0)
                        this.insign = TRUE;
                    break;

                case '\n':
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;
                }
            }
            break;
        }
    }
}

PRIVATE void drain(void)
{
    char ch;

    while (this.vptr) {
        if ((this.vptr) (&ch) == EWOULDBLOCK) {
            this.vptr = NULL;
            return;
        }
    }
}

PRIVATE uchar_t tobcd(uchar_t v)
{
    return (((v / 10) << 4) | (v % 10));
}

PRIVATE void print_date(void)
{
    char sbuf[25];
    sprintf_P(sbuf, PSTR("20%02X-%02X-%02X %02X:%02X:%02X\n"), 
                              this.rtcbuf[RV_YEAR],
                              this.rtcbuf[RV_MONTH],
                              this.rtcbuf[RV_DATE],
                              this.rtcbuf[RV_HOURS],
                              this.rtcbuf[RV_MINUTES],
                              this.rtcbuf[RV_SECONDS]);
    tty_puts(sbuf);
}

/* end code */
