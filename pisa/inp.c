/* pisa/inp.c */

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

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/tty.h"
#include "sys/ser.h"
#include "sys/eex.h"
#include "fs/sfa.h"
#include "alba/ad7124.h"
#include "alba/alba.h"
#include "alba/mdac.h"
#include "alba/stairs.h"
#include "alba/patch.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "hc05/bc4.h"
#include "sys/ver.h"
#include "sys/inp.h"

/* escape sequence */
#define MAX_ESCAPE_SEQUENCE 16
#define MAX_HEX_BUF (MAX_ESCAPE_SEQUENCE >> 1)
#define ESC_CHAR        ':' /* not 0x1b */

/* I am .. */
#define SELF INP
#define this inp

#define MAX_ARGS 9

typedef enum {
    IDLE = 0,
    WRITING_ALBA_STRING,
    READING_MDAC,
    SELECTING_EGOR_OUTPUT,
    INFORMING_EGOR,
    STARTING_EGOR,
    STOPPING_EGOR,
    SETTING_EXT_CALIBRATION,
    FETCHING_EXT_CALIBRATION,
    SETTING_EGOR_LOGGING,
    WRITING_MDAC_CHANNEL,
    SETTING_EGOR_LOOP_COUNT,
    SELECTING_TTY_OUTPUT,
    SETTING_SER_BAUDRATE,
    SETTING_SER_CONSUMER,
    SETTING_DISPLAY_MODE,
    SETTING_LOGGING,
    RESETTING_ALBA
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned in_comment : 1;
    unsigned in_escape : 1;
    unsigned in_esc_y : 1;
    unsigned insign : 1;
    unsigned stairs_is_running : 1;
    unsigned first : 1;
    unsigned second : 1;
    unsigned third : 1;
    uchar_t incount;
    long inval;
    short args[MAX_ARGS];
    uchar_t narg;
    uchar_t err;
    char hex_buf[MAX_HEX_BUF];
    uchar_t esc_index; /* escape sequence index */
    uchar_t patchno;
    CharProc vptr;
    ulong_t ext_cal;
    stairs_info stairs;
    union {
        alba_info alba;
        mdac_info mdac;
        eex_info eex;
        patch_info patch;
    } info;
} inp_t;

/* I have .. */
static inp_t this;

/* I can .. */
PRIVATE void resume(void);
PRIVATE void consume(void);
PRIVATE void drain(void);

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

    case REPLY_DATA:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.stairs_is_running && m_ptr->sender == STAIRS) {
            this.stairs_is_running = FALSE;
        } else if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                this.err = m_ptr->RESULT;
                this.third = TRUE;
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

    case WRITING_ALBA_STRING:
        if (this.info.alba.mode == READ_MODE) {
            tty_puts_P(PSTR("alba 0x"));
            switch (this.esc_index) {
            case 4:
                tty_puthex(this.info.alba.rb.val >> 24);
                /* fallthrough */
            case 3:
                tty_puthex(this.info.alba.rb.val >> 16);
                /* fallthrough */
            case 2:
                tty_puthex(this.info.alba.rb.val >> 8);
                tty_puthex(this.info.alba.rb.val);
                /* fallthrough */
            }
        }
        tty_putc('\n');
        break;

    case READING_MDAC:
        if (this.info.mdac.read_flag == TRUE) {
            char sbuf[23];
            sprintf_P(sbuf, PSTR("mdac:%d,%d,%d,%d,%d,%d,%d\n"),
                                 this.info.mdac.channel,
                                 this.info.mdac.access_eeprom,
                                 this.info.mdac.val,
                                 this.info.mdac.inhibit_update,
                                 this.info.mdac.reference,
                                 this.info.mdac.powermode,
                                 this.info.mdac.gain);
            tty_puts(sbuf);
        }
        break;

    case SELECTING_EGOR_OUTPUT:
        tty_puts_P(PSTR("eo: "));
        ok = TRUE;
        break;

    case INFORMING_EGOR:
        tty_puts_P(PSTR("ie: "));
        ok = TRUE;
        break;

    case STARTING_EGOR:
        tty_puts_P(PSTR("ee: "));
        ok = TRUE;
        break;

    case STOPPING_EGOR:
        tty_puts_P(PSTR("de: "));
        ok = TRUE;
        break;

    case SETTING_EXT_CALIBRATION:
        tty_puts_P(PSTR("sk: "));
        this.ext_cal = 0;
        ok = TRUE;
        break;

    case FETCHING_EXT_CALIBRATION:
        {
            char sbuf[18];
            sprintf_P(sbuf, PSTR("gk: %lu\n"), this.ext_cal);
            tty_puts(sbuf);
        }
        break;

    case SETTING_EGOR_LOGGING:
        tty_puts_P(PSTR("le: "));
        ok = TRUE;
        break;

    case WRITING_MDAC_CHANNEL:
        tty_puts_P(PSTR("mc: "));
        ok = TRUE;
        break;

    case SETTING_EGOR_LOOP_COUNT:
        tty_puts_P(PSTR("el: "));
        ok = TRUE;
        break;

    case SELECTING_TTY_OUTPUT:
        tty_puts_P(PSTR("ou: "));
        ok = TRUE;
        break;

    case SETTING_SER_BAUDRATE:
        tty_puts_P(PSTR("sb: "));
        ok = TRUE;
        break;

    case SETTING_SER_CONSUMER:
        tty_puts_P(PSTR("sc: "));
        ok = TRUE;
        break;

    case SETTING_DISPLAY_MODE:
        tty_puts_P(PSTR("sd: "));
        ok = TRUE;
        break;

    case SETTING_LOGGING:
        tty_puts_P(PSTR("sl: "));
        ok = TRUE;
        break;

    case RESETTING_ALBA:
        tty_puts_P(PSTR("reset alba: "));
        ok = TRUE;
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
            } else if (this.in_escape) {
                switch (ch) {
                case 'y':
                    this.in_esc_y = TRUE;
                    this.in_escape = FALSE;
                    break;

                default:
                    this.in_escape = FALSE;
                    break;
                }
            } else if (this.in_esc_y) {
                if (ch == ';') {
                    this.in_esc_y = FALSE;
                    if (!isodd(this.esc_index)) {
                        /* reject odd number of digits */
                        this.state = WRITING_ALBA_STRING;
                        /* this.hex_buf[] contains a big-endian value which is
                         * what the ad7124 expects.
                         */
                        this.esc_index >>= 1;
                        this.info.alba.mode = (this.hex_buf[0] & 0x40) ?
                                                        READ_MODE : WRITE_MODE;
                        this.info.alba.regno = this.hex_buf[0] & ~0x40;
                        switch (this.esc_index) {
                        case 4:
                            this.info.alba.rb.val =
                                             (ulong_t)this.hex_buf[1] << 16 |
                                             (ulong_t)this.hex_buf[2] << 8 |
                                                               this.hex_buf[3];
                            break;
                        case 3:
                            this.info.alba.rb.val =
                                             (ulong_t)this.hex_buf[1] << 8 |
                                                               this.hex_buf[2];
                            break;
                        case 2:
                            this.info.alba.rb.val = this.hex_buf[1];
                            break;
                        }
                        send_JOB(ALBA, &this.info.alba);
                        return;
                    }
                } else if (this.esc_index < MAX_ESCAPE_SEQUENCE) {
                    uchar_t v = this.esc_index >> 1;
                    if ('a' <= ch && ch <= 'f')
                        ch -= 'a' - 10;
                    else if ('A' <= ch && ch <= 'F')
                        ch -= 'A' - 10;
                    else if ('0' <= ch && ch <= '9')
                        ch -= '0';
                    else
                        this.in_esc_y = FALSE;

                    if (this.in_esc_y == TRUE) {
                        if (isodd(this.esc_index))
                            this.hex_buf[v] |= ch;
                        else
                            this.hex_buf[v] = ch << 4;
                        this.esc_index++;
                    }
                } else {
                    this.in_esc_y = FALSE;
                }
            } else {
                switch (ch) {
                case ESC_CHAR: /* Escape char ':' e.g. :y2180001F; */
                    this.in_escape = TRUE;
                    this.esc_index = 0;
                    break;

                case 'B':
                    /* set baud rate
                     * 1B   9600
                     * 2B   19200
                     * 3B   38400
                     * 4B   57600
                     * 5B   115200
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case B9600:
                        case B19200:
                        case B38400:
                        case B57600:
                        case B115200:
                            this.state = SETTING_SER_BAUDRATE;
                            send_SET_IOCTL(SER, SIOC_BAUDRATE, this.inval);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
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
                    /* set SER consumer i.e. who receives NOT_EMPTY message */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = SETTING_SER_CONSUMER;
                            send_SET_IOCTL(SER, SIOC_CONSUMER, INP);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = SETTING_SER_CONSUMER;
                            send_SET_IOCTL(SER, SIOC_CONSUMER, SERIN);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;
                    
                case 'd':
                    if (this.incount) {
                        send_SET_IOCTL(EGOR, SIOC_DISPLAY_MODE, this.inval);
                        this.state = SETTING_DISPLAY_MODE;
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'e':
                    /* print the build ident */
                    tty_puts_P(version);
                    break;

                case 'h':
                    /* redirect EGOR output to a destination
                     * 0h OFF. No output.
                     * 1h send measurement to VOLTAGEZ @ SUMO (lcd)
                     * 2h send measurement to VOLTAGEP @ LIMA (i2c oled) 
                     * 3h send measurement to VOLTAGEP @ PERU (spi oled)  
                     * 4h send measurement to OSTREAM @ BALI (uart)
                     * 5h send measurement to OSTREAM @ LIMA
                     * 6h send measurement to OSTREAM @ PERU
                     * 7h send measurement to VOLTAGEx GENERAL CALL
                     */
                    if (this.incount) {
                        this.state = SELECTING_EGOR_OUTPUT;
                        send_SET_IOCTL(EGOR, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'j':
                    /* '<n> j' EGOR start/stop
                     * 0j EGOR send STOP 
                     * 1j EGOR send START
                     */
                    if (this.incount) {
                        switch (this.inval) {
                        case 0:
                            this.state = STOPPING_EGOR;
                            send_STOP(EGOR);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;

                        case 1:
                            this.state = STARTING_EGOR;
                            send_START(EGOR);
                            this.incount = 0;
                            this.narg = 0;
                            this.insign = FALSE;
                            return;
                        }
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'k':
                   /* 'k' read calibration value from EEPROM */
                    this.state = FETCHING_EXT_CALIBRATION;
                    this.info.eex.sptr = (uchar_t *)&this.ext_cal;
                    this.info.eex.eptr = 0;
                    this.info.eex.cnt = sizeof(this.ext_cal);
                    this.info.eex.mode = EEX_READ;
                    send_JOB(EEX, &this.info.eex);
                    return;
 
                case 'K':
                    /* '<nnn> K' write calibration value 'nnn' to EEPROM */
                    if (this.incount) {
                        this.state = SETTING_EXT_CALIBRATION;
                        this.ext_cal = this.inval;
                        this.info.eex.sptr = (uchar_t *)&this.ext_cal;
                        this.info.eex.eptr = 0;
                        this.info.eex.cnt = sizeof(this.ext_cal);
                        this.info.eex.mode = EEX_WRITE;
                        send_JOB(EEX, &this.info.eex);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;
 
                case 'l':
                    /* '<n> l' turn EGOR logging on/off
                     * 0l turn logging off
                     * 1l turn logging on
                     */
                    if (this.incount) {
                        this.state = SETTING_EGOR_LOGGING;
                        send_SET_IOCTL(EGOR, SIOC_LOGGING, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'm':
                    /* '<ch,ee> m' read MDAC channel parameters */
                    /* 0,0 m - read channel 0 output register
                     * 0,1 m - read channel 0 eeprom register
                     * 1,0 m - read channel 1 output register
                     * 2,0 m - read channel 2 output register
                     * 3,0 m - read channel 3 output register
                     */
                    if (this.narg == 1 && this.incount == 1 &&
                          this.args[0] >= 0 && this.args[0] < 4 &&
                          this.inval >= 0 && this.inval < 2) {
                        this.state = READING_MDAC;
                        sae_MDAC_READ(this.info.mdac, this.args[0], this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    tty_puts_P(PSTR("usage: <ch,ee> m\n"));
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'M':
                    /* '<ch,ee,val,inh,ref,pm,ga> M'
                     * write MDAC channel parameters
                     */
                    if (this.narg == 6 && this.incount == 1 &&
                          this.args[0] >= 0 && this.args[0] < 4 &&
                          this.args[1] >= 0 && this.args[1] < 2 &&
                          this.args[2] >= 0 && this.args[2] < 4096 &&
                          this.args[3] >= 0 && this.args[3] < 2 &&
                          this.args[4] >= 0 && this.args[4] < 2 &&
                          this.args[5] >= 0 && this.args[5] < 4 &&
                          this.inval >= 0 && this.inval < 2) {
                        this.state = WRITING_MDAC_CHANNEL;
                        sae_MDAC_WRITE(this.info.mdac, this.args[0],
                                                     this.args[1],
                                                     this.args[2],
                                                     this.args[3],
                                                     this.args[4],
                                                     this.args[5],
                                                     this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    tty_puts_P(PSTR("usage: <ch,ee,val,inh,ref,pm,ga> M\n"));
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'r':
                    /* '<nn> r' EGOR set the number of iterations */ 
                    if (this.incount) {
                        this.state = SETTING_EGOR_LOOP_COUNT;
                        send_SET_IOCTL(EGOR, SIOC_LOOP_COUNT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'R':
                    /* '1R' Reset the AD7124 */
                    if (this.incount == 1 && this.narg == 0 &&
                               this.inval == 1 && this.insign == FALSE) {
                        this.state = RESETTING_ALBA;
                        this.info.alba.mode = RESET_MODE;
                        send_JOB(ALBA, &this.info.alba);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;
                    
                case 's':
                    /* run stairs command
                     * '<ch,val,stepsize,nr_samples> s'
                     * If either stepsize or nr_samples is zero,
                     * an endless loop results.
                     */
                    if (this.stairs_is_running == FALSE &&
                          this.narg == 3 && this.incount > 0 &&
                          this.args[0] >= 0 && this.args[0] < 4 &&
                          this.args[1] >= 0 && this.args[1] < 4096 &&
                          this.args[2] != 0 && this.args[2] < 256 &&
                          this.inval != 0 && this.inval < 256) {
                        this.stairs_is_running = TRUE;
                        this.stairs.channel = this.args[0];
                        this.stairs.val = this.args[1];
                        this.stairs.stepsize = this.args[2];
                        this.stairs.nr_samples = this.inval;
                        send_JOB(STAIRS, &this.stairs);
                    } else {
                        tty_puts_P(PSTR(
                                   "usage: <ch,val,stepsize,nr_samples> s\n"));
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
                    break;

                case 'S':
                    /* cancel stairs command */
                    if (this.stairs_is_running) {
                        send_CANCEL(STAIRS, &this.stairs);
                    }
                    break;

                case 'y':
                   /* 'y' Update the MDAC output registers */
                    send_SYNC(MDAC);
                    break;

                case 'z':
                    /* set the tty output destination
                     * 0z  local SER device
                     * 1z  GATEWAY OSTREAM device
                     * 2z  TWI_OLED_ADDRESS OSTREAM device
                     * 3z  SPI_OLED_ADDRESS OSTREAM device
                     */
                    if (this.incount) {
                        this.state = SELECTING_TTY_OUTPUT;
                        send_SET_IOCTL(TTY, SIOC_SELECT_OUTPUT, this.inval);
                        this.incount = 0;
                        this.narg = 0;
                        this.insign = FALSE;
                        return;
                    }
                    this.incount = 0;
                    this.narg = 0;
                    this.insign = FALSE;
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

                case '\n': /* line terminator */
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

/* end code */
