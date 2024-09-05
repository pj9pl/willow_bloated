/* cli/cli.c */

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

/* command line interpreter. */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "sys/tty.h"
#include "sys/syscon.h"
#include "sys/utc.h"
#include "sys/rv3028c7.h"
#include "net/twi.h"
#include "net/i2c.h"
#include "net/memz.h"
#include "net/istream.h"
#include "net/ostream.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/fsd.h"
#include "fs/rwr.h"
#include "oled/common.h"
#include "oled/oled.h"
#include "oled/viola.h"
#include "hc05/hc05.h"
#include "alba/setupd.h"
#include "isp/isp.h"
#include "isp/icsp.h"
#include "key/keysec.h"
#include "cli/cli.h"
#include "cli/dump.h"
#include "cli/cat.h"
#include "cli/put.h"
#include "cli/fsu.h"

/* I am .. */
#define SELF CLI
#define this cli

typedef enum {
    IDLE = 0,
    FETCHING_UPTIME,
    FETCHING_BOOTTIME,
    FETCHING_CURTIME,
    DUMPING_DATA_MEMORY,
    IN_ISP,
    IN_ICSP,
    REDIRECTING_TO_SELF,
    REBOOTING_TARGET,
    MAKING_FILESYS,
    READING_SECTOR,
    SENDING_ISTREAM,
    SENDING_BAR_MESSAGE,
    SENDING_HC05_COMMAND,
    SENDING_HC05_ENQUIRY,
    PINGING_HOST,
    READING_BLSWITCH,
    READING_MDAC,
    SENDING_PRINT_COMMAND,
    FETCHING_CYCLES,
    FETCHING_LASTRESET,
    SHOWING_ELAPSED,
    PUTTING_FILE,
    LISTING_ITEMS,
    CHANGING_DIR,
    PRINTING_CWD,
    MOVING_ITEMS,
    REMOVING_ITEMS,
    MAKING_ITEM,
    CATTING_FILE,
    RESOLVING_PATCHFILE,
    PATCHING_ALBA,
    RESOLVING_KEYFILE,
    CONFIGURING_KEY
} __attribute__ ((packed)) state_t;

/* www.avrfreaks.net/forum/array-strings-flash-1 #24 LabZDjee */
typedef const __flash char ProgmemStringLiteral[];
typedef void (*CliProc) (char *bp);

typedef struct {
    char const __flash *s;
    CliProc f;
} ProgmemStringFuncRef;

#define NR_CMDS   (sizeof(cmds_) / sizeof(ProgmemStringFuncRef))

typedef struct {
    char const __flash *s;
    uchar_t a;
} ProgmemStringHostRef;

#define NR_HOSTNAMES   (sizeof(hostnames_) / sizeof(ProgmemStringHostRef))

#define RBUF_LEN 8

typedef struct {
    state_t state;
    unsigned exiting : 1; 
    cli_info *headp;
    dbuf_t dbuf;            /* cannot be incorporated into msg union */
    uchar_t target;
    uchar_t readbuf[RBUF_LEN];
    void *start_loc;        /* read memory start address */
    void *end_loc;          /* read memory end address */
    uchar_t n_bytes;        /* number of bytes contained within readbuf */
    uchar_t pindex;         /* iterative loop hex record start point */
    uchar_t *src;
    char opt;
    uchar_t *epp;
    inode_t myno;
    inum_t cwd;
    cat_info cat;
    char *catpath;
    char *printbuf;
    memz_msg memz;
    union {
        syscon_msg syscon;
        hc05_msg hc05;
        fsd_msg fsd;
        fsu_msg fsu;
        istream_msg istream;
        ostream_msg ostream;
        rwr_msg rwr;
        setupd_msg setupd;
        key_msg key;
        utc_msg utc;
    } msg;
    union {
        twi_info twi;
        dump_info dump;
        put_info put;
        isp_info isp;
        icsp_info icsp;
    } info;
} cli_t;

/* I have .. */
static cli_t this;

PRIVATE void exit_func(char *bp);
PRIVATE void ping_func(char *bp);
PRIVATE void blswitch_func(char *bp);
PRIVATE void cycles_func(char *bp);
PRIVATE void uptime_func(char *bp);
PRIVATE void curtime_func(char *bp);
PRIVATE void dump_func(char *bp);
PRIVATE void isp_func(char *bp);
PRIVATE void icsp_func(char *bp);
PRIVATE void reboot_func(char *bp);
PRIVATE void hc05_func(char *bp);
PRIVATE void mkfs_func(char *bp);
PRIVATE void sector_func(char *bp);
PRIVATE void inp_func(char *bp);
PRIVATE void cat_func(char *bp);
PRIVATE void print_func(char *bp);
PRIVATE void last_func(char *bp);
PRIVATE void put_func(char *bp);
PRIVATE void ls_func(char *bp);
PRIVATE void cd_func(char *bp);
PRIVATE void pwd_func(char *bp);
PRIVATE void mv_func(char *bp);
PRIVATE void rm_func(char *bp);
PRIVATE void mk_func(char *bp);
PRIVATE void alba_func(char *bp);
PRIVATE void key_func(char *bp);

ProgmemStringFuncRef const __flash cmds_[] = {
    {(ProgmemStringLiteral){"exit"},     exit_func},
    {(ProgmemStringLiteral){"ping"},     ping_func},
    {(ProgmemStringLiteral){"blswitch"}, blswitch_func},
    {(ProgmemStringLiteral){"cycles"},   cycles_func},
    {(ProgmemStringLiteral){"up"},       uptime_func},
    {(ProgmemStringLiteral){"date"},     curtime_func},
    {(ProgmemStringLiteral){"dump"},     dump_func},
    {(ProgmemStringLiteral){"isp"},      isp_func},
    {(ProgmemStringLiteral){"icsp"},     icsp_func},
    {(ProgmemStringLiteral){"reboot"},   reboot_func},
    {(ProgmemStringLiteral){"hc05"},     hc05_func},
    {(ProgmemStringLiteral){"mk"},       mk_func},
    {(ProgmemStringLiteral){"rm"},       rm_func},
    {(ProgmemStringLiteral){"mkdir"},    mk_func},
    {(ProgmemStringLiteral){"rmdir"},    rm_func},
    {(ProgmemStringLiteral){"mkfs"},     mkfs_func},
    {(ProgmemStringLiteral){"sector"},   sector_func},
    {(ProgmemStringLiteral){"inp"},      inp_func},
    {(ProgmemStringLiteral){"cat"},      cat_func},
    {(ProgmemStringLiteral){"print"},    print_func},
    {(ProgmemStringLiteral){"last"},     last_func},
    {(ProgmemStringLiteral){"put"},      put_func},
    {(ProgmemStringLiteral){"ls"},       ls_func},
    {(ProgmemStringLiteral){"cd"},       cd_func},
    {(ProgmemStringLiteral){"pwd"},      pwd_func},
    {(ProgmemStringLiteral){"mv"},       mv_func},
    {(ProgmemStringLiteral){"alba"},     alba_func},
    {(ProgmemStringLiteral){"key"},      key_func}
};

ProgmemStringHostRef const __flash hostnames_[] = {
    {(ProgmemStringLiteral){"bali"}, BALI_I2C_ADDRESS},
    {(ProgmemStringLiteral){"oslo"}, OSLO_I2C_ADDRESS},
    {(ProgmemStringLiteral){"pisa"}, PISA_I2C_ADDRESS},
    {(ProgmemStringLiteral){"sumo"}, SUMO_I2C_ADDRESS},
    {(ProgmemStringLiteral){"lima"}, LIMA_I2C_ADDRESS},
    {(ProgmemStringLiteral){"iowa"}, IOWA_I2C_ADDRESS},
    {(ProgmemStringLiteral){"peru"}, PERU_I2C_ADDRESS},
    {(ProgmemStringLiteral){"fido"}, FIDO_I2C_ADDRESS},
    {(ProgmemStringLiteral){"self"}, HOST_ADDRESS}
};

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void lookup_cmd(char *);
PRIVATE uchar_t lookup_host(char *bp, uchar_t *valp);
PRIVATE void fetch_buffer(void);
PRIVATE void send_fsu(void);
PRIVATE void send_fsd(void);
PRIVATE void send_syscon(void);

PUBLIC uchar_t receive_cli(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_DATA:
    case REPLY_INFO:
    case REPLY_RESULT:
        if (this.printbuf) {
            free(this.printbuf);
            this.printbuf = NULL;
        }

        if (m_ptr->sender == CAT && this.catpath) {
            free(this.catpath);
            this.catpath = NULL;
        }

        if (this.state) {
            if (m_ptr->RESULT == EOK) {
                resume();
            } else {
                if (this.state == IN_ISP || this.state == IN_ICSP ||
                                              this.state == PUTTING_FILE) {
                    this.state = REDIRECTING_TO_SELF;
                    send_SET_IOCTL(SER, SIOC_CONSUMER, CANON);
                    break;
                }
                this.state = IDLE;
                tty_puts_P(PSTR("cli: "));
                tty_printl(m_ptr->RESULT);
                tty_putc('\n');
                if (this.headp) {
                    send_REPLY_INFO(this.headp->replyTo,
                              m_ptr->RESULT, this.headp);
                    if ((this.headp = this.headp->nextp) != NULL)
                        start_job();
                }
            }
        } else if (this.headp) {
            switch (m_ptr->RESULT) {
            case ENOENT:
                tty_puts_P(PSTR("host not found\n"));
                break;

            case EINVAL:
                tty_puts_P(PSTR("bad value\n"));
                break;
            }
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL) {
                start_job();
            }
        }

        if (this.exiting) {
            this.exiting = FALSE;
            send_REPLY_RESULT(INP, EOK);
        }

        break;

    case JOB:
        {
            cli_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                cli_info *tp;
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
    if (this.cwd == 0)
        this.cwd = ROOT_INODE_NR;
    this.opt = '\0';
    lookup_cmd(this.headp->bp);
}

PRIVATE void resume(void)
{
    ulong_t val;
    uchar_t ret = EOK;
    uchar_t ok = FALSE;

    switch (this.state) {
    case IDLE:
        return;

    case PINGING_HOST:
        tty_puts_P(PSTR("ready"));
        break;

    case READING_BLSWITCH:
        /* check that
         *   - PIND6 is low    (being presented with a low level)
         *   - DDRD6 is low    (configured to be an input)
         *   - PORTD6 is high  (configured to apply a weak pullup)
         */
        if (!(this.readbuf[0] & 0x40) && !(this.readbuf[1] & 0x40) &&
                                               (this.readbuf[2] & 0x40))
            tty_puts_P(PSTR("closed"));
        else
            tty_puts_P(PSTR("open"));
        break;

    case FETCHING_UPTIME:
        {
            tty_printl(this.msg.utc.reply.val);
            tty_putc(' ');
            val = this.msg.utc.reply.val / (60L * 60L * 24L);
            tty_printl(val);
            tty_putc(',');
            val = this.msg.utc.reply.val % (60L * 60L * 24L);
            tty_printl(val / (60L * 60L));
            tty_putc(',');
            val = val % (60L * 60L);
            tty_printl(val / 60L);
            tty_putc(',');
            tty_printl(val % 60L);
        }
        break;

    case FETCHING_BOOTTIME:
    case FETCHING_CURTIME:
        {
            char s[26];
            this.msg.utc.reply.val -= UNIX_OFFSET;
            ctime_r(&this.msg.utc.reply.val, s);
            tty_puts(s);
        }
        break;

    case LISTING_ITEMS:
        if (this.msg.fsu.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsu.reply.result);
            tty_putc(')');
        } else {
            this.state = IDLE;
            send_REPLY_RESULT(SELF, ret);
            return;
        }
        break;

    case DUMPING_DATA_MEMORY:
    case SENDING_PRINT_COMMAND:
        ok = TRUE;
        break;

    case READING_SECTOR:
    case MAKING_FILESYS:
        if (this.msg.fsd.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsd.reply.result);
            tty_putc(')');
        } else {
            ok = TRUE;
        }
        break;

    case IN_ISP:
    case IN_ICSP:
    case PUTTING_FILE:
        this.state = REDIRECTING_TO_SELF;
        send_SET_IOCTL(SER, SIOC_CONSUMER, CANON);
        return;

    case REDIRECTING_TO_SELF:
        /* This was where final tty_putc('$'); was being called.
         * Now the '$' is printed in isp.c, icsp.c, hvpp.c
         * as it is an intrinsic part of the hexfile protocol.
         * Only the newline is generated here. See below.
         */
        break;

    case REBOOTING_TARGET:
        tty_puts_P(PSTR("rebooting "));
        tty_printl(this.target);
        break;
        
    case SENDING_ISTREAM:
        this.state = IDLE;
        send_REPLY_RESULT(SELF, ret);
        return;

    case SENDING_BAR_MESSAGE:
    case SENDING_HC05_COMMAND:
        if (this.msg.hc05.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.hc05.reply.result);
            tty_putc(')');
        } else {
            ok = TRUE;
        }
        break;

    case SENDING_HC05_ENQUIRY:
        if (this.msg.hc05.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.hc05.reply.result);
            tty_putc(')');
        } else {
            tty_puts_P(PSTR("hc05:"));
            tty_printl(this.msg.hc05.reply.val);
        }
        break;

    case READING_MDAC:
        tty_printl(this.dbuf.mtype);
        tty_putc(' ');
        tty_printl(this.dbuf.res);
        break;

    case FETCHING_CYCLES:
        tty_printl(this.msg.syscon.reply.p.cycles.count);
        tty_putc(',');
        tty_printl(this.msg.syscon.reply.p.cycles.depth);
        tty_putc(',');
        tty_printl(this.msg.syscon.reply.p.cycles.lost);
        break;

    case FETCHING_LASTRESET:
        if (this.opt == 'c') {
            this.msg.syscon.reply.p.lastreset.boottime -= UNIX_OFFSET;
            char store[26];
            ctime_r(&this.msg.syscon.reply.p.lastreset.boottime, store); 
            tty_puts(store);
        } else {
            this.state = SHOWING_ELAPSED;
            sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
              RV_UNIX_TIME_0, this.dbuf.res);
            return;
        }
        break;

    case SHOWING_ELAPSED:
        this.dbuf.res -= this.msg.syscon.reply.p.lastreset.boottime;
        tty_printl(this.dbuf.res);
        tty_putc(' ');
        val = this.dbuf.res / (60L * 60L * 24L);
        tty_printl(val);
        tty_putc(',');
        val = this.dbuf.res % (60L * 60L * 24L);
        tty_printl(val / (60L * 60L));
        tty_putc(',');
        val = val % (60L * 60L);
        tty_printl(val / 60L);
        tty_putc(',');
        tty_printl(val % 60L);
        break;

    case CHANGING_DIR:
        if (this.msg.fsd.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsd.reply.result);
            tty_putc(')');
        } else {
            if ((this.myno.i_mode & I_TYPE) == I_DIRECTORY) {
                this.cwd = this.myno.i_inum;
                ok = TRUE;
            } else {
                tty_puts_P(PSTR("not a directory"));
            }
        }
        break;

    case PRINTING_CWD:
        if (this.msg.fsu.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsu.reply.result);
            tty_putc(')');
        }
        break;

    case MOVING_ITEMS:
    case REMOVING_ITEMS:
    case MAKING_ITEM:
        if (this.msg.fsu.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsu.reply.result);
            tty_putc(')');
        } else {
            ok = TRUE;
        }
        break;

    case CATTING_FILE:
        tty_puts_P(PSTR("cat:"));
        tty_printl(this.cat.fpos);
        break;

    case RESOLVING_PATCHFILE:
        if (this.msg.fsd.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsd.reply.result);
            tty_putc(')');
        } else if ((this.myno.i_mode & I_TYPE) == I_REGULAR) {
            this.state = PATCHING_ALBA;
            inum_t nr = this.msg.fsd.reply.p.path.base_inum;
            this.msg.setupd.request.val = nr;
            this.msg.setupd.request.op = OP_LOAD;
            this.msg.setupd.request.taskid = SELF;
            this.msg.setupd.request.jobref = &this.info.twi;
            this.msg.setupd.request.sender_addr = HOST_ADDRESS;
            sae2_TWI_MTSR(this.info.twi, PISA_I2C_ADDRESS,
               SETUPD_REQUEST, this.msg.setupd.request,
               SETUPD_REPLY, this.msg.setupd.reply);
            return;
        } else {
            tty_puts_P(PSTR("not a regular file"));
        }
        break;
    
    case PATCHING_ALBA:
        if (this.msg.setupd.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.setupd.reply.result);
            tty_putc(',');
            tty_printl(this.msg.setupd.reply.val);
            tty_putc(')');
        } else {
            ok = TRUE;
        }
        break;

    case RESOLVING_KEYFILE:
        if (this.msg.fsd.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.fsd.reply.result);
            tty_putc(')');
        } else if ((this.myno.i_mode & I_TYPE) == I_REGULAR) {
            this.state = CONFIGURING_KEY;
            inum_t nr = this.msg.fsd.reply.p.path.base_inum;
            this.msg.key.request.val = nr;
            this.msg.key.request.op = OP_LOADKEY;
            this.msg.key.request.taskid = SELF;
            this.msg.key.request.jobref = &this.info.twi;
            this.msg.key.request.sender_addr = HOST_ADDRESS;
            sae2_TWI_MTSR(this.info.twi, FIDO_I2C_ADDRESS,
               KEY_REQUEST, this.msg.key.request,
               KEY_REPLY, this.msg.key.reply);
            return;
        } else {
            tty_puts_P(PSTR("not a regular file"));
        }
        break;

    case CONFIGURING_KEY:
        if (this.msg.key.reply.result) {
            tty_putc('(');
            tty_printl(this.msg.key.reply.result);
            tty_putc(',');
            tty_printl(this.msg.key.reply.val);
            tty_putc(')');
        } else {
            ok = TRUE;
        }
        break;
    }

    if (ok)
        tty_puts_P(PSTR("ok"));
    tty_putc('\n');
    this.state = IDLE;
    send_REPLY_RESULT(SELF, ret);
}

PRIVATE void lookup_cmd(char *bp)
{
    char *cp;
    CliProc fp;

    while (*bp == ' ')
        bp++;
 
    if (strlen(bp) > 0) {
        cp = bp;
        while (*cp && *cp != ' ')
            cp++;
        uchar_t clen = cp - bp;
        for (uchar_t i = 0; i < NR_CMDS; i++) {
            if ((cp = (char *) pgm_read_word_near(&cmds_[i].s)) != NULL) {
                uchar_t len = strlen_P(cp);
                if (clen == len && strncmp_P(bp, cp, len) == 0) {
                    if ((fp = (CliProc)
                                 pgm_read_word_near(&cmds_[i].f)) != NULL) {
                        bp += len;
                        while (*bp == ' ')
                            bp++;
                        (fp) (bp);
                        return;
                    }
                }
            }
        }
        tty_puts(bp);
        tty_puts_P(PSTR(" : bad command\n"));
    }
    send_REPLY_RESULT(SELF, ESRCH);
}

PRIVATE uchar_t lookup_host(char *bp, uchar_t *val)
{
    char *cp;

    for (uchar_t i = 0; i < NR_HOSTNAMES; i++) {
        if ((cp = (char *) pgm_read_word_near(&hostnames_[i].s)) != NULL) {
            uchar_t len = strlen_P(cp);
            if (strncmp_P(bp, cp, len) == 0) {
                *val = pgm_read_byte_near(&hostnames_[i].a);
                return EOK;
            }
        }
    }
    return ENXIO;
}

/* command functions */

PRIVATE void exit_func( __attribute__ ((unused)) char *bp)
{
    this.state = IDLE;
    this.exiting = TRUE;
    send_SET_IOCTL(SER, SIOC_CONSUMER, INP);
}

PRIVATE void ping_func(char *bp)
{
    /* ping <host>
     * test the presence and the status of the bootloader switch of <host>
     */

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        this.pindex = 0;
        /* request the contents of the PIND, DDRD and PORTD registers
         * these are situated at 0x29, 0x2A and 0x2B, respectively.
         */
        this.start_loc = (void *)0x29;
        this.end_loc = (void *)(0x2B + 1);
        this.state = PINGING_HOST;
        fetch_buffer();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void blswitch_func(char *bp)
{
    /* blswitch <host>
     * test the presence and the status of the bootloader switch of <host>
     */ 

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        this.pindex = 0;
        /* request the contents of the PIND, DDRD and PORTD registers
         * these are situated at 0x29, 0x2A and 0x2B, respectively.
         */
        this.start_loc = (void *)0x29;
        this.end_loc = (void *)(0x2B + 1);
        this.state = READING_BLSWITCH;
        fetch_buffer();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void cycles_func( __attribute__ ((unused)) char *bp)
{
    /* print cycle count */

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        this.state = FETCHING_CYCLES;
        this.msg.syscon.request.op = OP_CYCLES;
        send_syscon();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}


/* --------------------------- UTC ------------------------ */

PRIVATE void uptime_func(char *bp)
{
    if (*bp == '-') {
        this.opt = *++bp;
        while (*bp && *bp != ' ')
            bp++;
        while (*bp == ' ')
            bp++;
    }

    if (this.opt == 'c') {
        this.state = FETCHING_BOOTTIME;
        this.msg.utc.request.op = GET_BOOTTIME;
    } else {
        this.state = FETCHING_UPTIME;
        this.msg.utc.request.op = GET_UPTIME;
    }

    this.msg.utc.request.taskid = SELF;
    sae2_TWI_MTMR(this.info.twi, UTC_ADDRESS,
            UTC_REQUEST, this.msg.utc, this.msg.utc);
}

PRIVATE void curtime_func( __attribute__ ((unused)) char *bp)
{
    this.state = FETCHING_CURTIME;
    this.msg.utc.request.taskid = SELF;
    this.msg.utc.request.op = GET_TIME;
    sae2_TWI_MTMR(this.info.twi, UTC_ADDRESS,
            UTC_REQUEST, this.msg.utc, this.msg.utc);
}

PRIVATE uchar_t get_nibble(char c)
{
    return(c > '9' ? c - 'A' + 10 : c - '0');
}

PRIVATE void dump_func(char *bp)
{
    /* dump <host> [[start] [[+length] | [end]]]
     * e.g. 
     *     dump fido 800 
     *     dump pisa 100 +100
     *     dump self 200 2FF
     */
    uchar_t plus = FALSE;
    ushort_t addr = 0;
    this.info.dump.start_loc = 0x0000;
    this.info.dump.end_loc = (void *)(RAMEND + 1);

    if (*bp && lookup_host(bp, &this.info.dump.target) == EOK) {

        while (*bp && *bp != ' ')
            bp++;

        while (*bp == ' ')
            bp++;

        while (*bp && isxdigit(*bp)) {
            addr = (addr << 4) + get_nibble(toupper(*bp));
            bp++;
        }

        this.info.dump.start_loc = (void *)addr;
        addr = 0;

        while (*bp == ' ')
            bp++;

        if (*bp) {
            this.info.dump.end_loc = 0;
            if (*bp == '+') {
                plus = TRUE;
                bp++;
                while (*bp == ' ')
                    bp++;
            }

            while (*bp && isxdigit(*bp)) {
                addr = (addr << 4) + get_nibble(toupper(*bp));
                bp++;
            }
            this.info.dump.end_loc = plus ? (this.info.dump.start_loc + addr)
                                          : (void *)(addr +1);
        }

        if ((this.info.dump.end_loc > (void *)(RAMEND +1)) ||
               (this.info.dump.start_loc >= this.info.dump.end_loc)) {
            send_REPLY_RESULT(SELF, EINVAL);
        } else {
            this.state = DUMPING_DATA_MEMORY;
            send_JOB(DUMP, &this.info.dump);
        }
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void fetch_buffer(void)
{
    this.start_loc += this.pindex;
    this.pindex = 0;
    ushort_t len = this.end_loc - this.start_loc;
    this.n_bytes = MIN(RBUF_LEN, len);

    if (this.n_bytes) {
        this.memz.request.taskid = SELF;
        this.memz.request.src = this.start_loc;
        this.memz.request.len = this.n_bytes;
        sae2_TWI_MTMR(this.info.twi, this.target, MEMZ_REQUEST,
                      this.memz.request, this.readbuf);
    } else {
        send_REPLY_RESULT(SELF, EOK);
    }
}

PRIVATE void isp_func(char *bp)
{
    /* isp <host> */

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        this.state = IN_ISP;
        this.info.isp.target = this.target;
        send_JOB(ISP, &this.info.isp);
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void icsp_func( __attribute__ ((unused)) char *bp)
{
    /* icsp */
    this.state = IN_ICSP;
    send_JOB(ICSP, &this.info.icsp);
}

PRIVATE void reboot_func(char *bp)
{
    /* reboot <host> */

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        this.state = REBOOTING_TARGET;
        this.msg.syscon.request.taskid = SELF;
        this.msg.syscon.request.jobref = &this.info.twi;
        this.msg.syscon.request.sender_addr = HOST_ADDRESS;
        this.msg.syscon.request.op = OP_REBOOT;
        this.msg.syscon.request.p.reboot.host = HOST_ADDRESS;
        sae2_TWI_MT(this.info.twi, this.target,
              SYSCON_REQUEST, this.msg.syscon.request);
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void hc05_func(char *bp)
{
    /* hc05 <host> <on|off> */

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        while (*bp && *bp != ' ')
            bp++;
        while (*bp == ' ')
            bp++;
        this.state = SENDING_HC05_COMMAND;
        if (strncmp_P(bp, PSTR("on"), 2) == 0) {
            this.msg.hc05.request.op = HC05_POWERON;
        } else if (strncmp_P(bp, PSTR("off"), 3) == 0) {
            this.msg.hc05.request.op = HC05_POWEROFF;
        } else {
            this.msg.hc05.request.op = HC05_ENQUIRE;
            this.state = SENDING_HC05_ENQUIRY;
        }
        this.msg.hc05.request.taskid = SELF;
        this.msg.hc05.request.jobref = &this.info.twi;
        this.msg.hc05.request.sender_addr = HOST_ADDRESS;
        sae2_TWI_MTSR(this.info.twi, this.target,
               HC05_REQUEST, this.msg.hc05.request,
               HC05_REPLY, this.msg.hc05.reply);
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void mkfs_func( __attribute__ ((unused)) char *bp)
{
    /* mkfs */

    this.state = MAKING_FILESYS;
    this.msg.fsd.request.op = OP_MKFS;
    send_fsd();
}

PRIVATE void sector_func(char *bp)
{
    /* sector <number> */

    ushort_t tval = 0;

    while (*bp && isdigit(*bp)) {
        tval = tval * 10 + *bp - '0';
        bp++;
    }

    this.state = READING_SECTOR;
    this.msg.fsd.request.op = OP_SECTOR;
    this.msg.fsd.request.p.sectf.num = tval;
    send_fsd();
}

PRIVATE void inp_func(char *bp)
{
    /* inp <host> <string> */

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        while (*bp && *bp != ' ')
            bp++;
        while (*bp == ' ')
            bp++;
        if (*bp) {
            char *sp = bp + strlen(bp);
            *sp++ = '\n';
            *sp = '\0';
            uchar_t len = strlen(bp);
            this.state = SENDING_ISTREAM;
            this.msg.istream.request.taskid = SELF;
            this.msg.istream.request.jobref = &this.info.twi;
            this.msg.istream.request.sender_addr = HOST_ADDRESS;
            this.msg.istream.request.src = bp;
            this.msg.istream.request.len = len +1;
            sae2_TWI_MTSR(this.info.twi, this.target,
                    ISTREAM_REQUEST, this.msg.istream.request,
                    ISTREAM_REPLY, this.msg.istream.reply);
        } else {
            send_REPLY_RESULT(SELF, EINVAL);
        }
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void print_func(char *bp)
{
    /* Construct a print command and send it to an OSTREAM host.
     * or construct an AT command string and send it to an HC-05 host.
     *
     * print <host> <string>
     *
     * e.g.
     *      print peru mercury rising
     *      print lima temp falling
     *      print iowa AT+UART=9600,0,0
     */

    if (*bp && lookup_host(bp, &this.target) == EOK) {

        while (*bp && *bp != ' ')
            bp++;
        while (*bp == ' ')
            bp++;

        uchar_t len = strlen(bp);
        if ((this.printbuf = calloc(len + 3, sizeof(char))) == NULL) {
            send_REPLY_RESULT(SELF, ENOMEM);
        } else {
            strcpy(this.printbuf, bp);
            char *sp = this.printbuf + len;
            *sp++ = '\r'; /* for HC-05 AT commands */
            *sp++ = '\n';
            this.state = SENDING_PRINT_COMMAND;
            this.msg.ostream.request.taskid = SELF;
            this.msg.ostream.request.jobref = &this.info.twi;
            this.msg.ostream.request.sender_addr = HOST_ADDRESS;
            this.msg.ostream.request.src = this.printbuf;
            this.msg.ostream.request.len = strlen(this.printbuf);
            sae2_TWI_MTSR(this.info.twi, this.target,
                OSTREAM_REQUEST, this.msg.ostream.request,
                OSTREAM_REPLY, this.msg.ostream.reply);
        }
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void last_func(char *bp)
{
    /* last [-c] <host> */

    if (*bp == '-') {
        this.opt = *++bp;
        while (*bp && *bp != ' ')
            bp++;
        while (*bp == ' ')
            bp++;
    }

    if (*bp && lookup_host(bp, &this.target) == EOK) {
        this.state = FETCHING_LASTRESET;
        this.msg.syscon.request.op = OP_BOOTTIME;
        send_syscon();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void put_func(char *bp)
{
    /* put -t <filename> <EOF>
     *
     * put lines of text to a file
     */

    this.state = PUTTING_FILE;
    this.info.put.bp = bp;
    this.info.put.cwd = this.cwd;
    send_JOB(PUT, &this.info.put);
}

PRIVATE void ls_func( __attribute__ ((unused)) char *bp)
{
    /* ls [-opt] [name]
     *
     * list files
     *
     * note that the whole command line is sent in the FSU request. 
     */
    this.state = LISTING_ITEMS;
    this.msg.fsu.request.p.ls.dest = GATEWAY_ADDRESS;
    this.msg.fsu.request.op = OP_LS;
    send_fsu();
}

PRIVATE void cd_func(char *bp)
{
    if (*bp) {
        this.state = CHANGING_DIR;
        this.msg.fsd.request.op = OP_PATH;
        this.msg.fsd.request.p.path.src = bp;
        this.msg.fsd.request.p.path.len = strlen(bp);
        this.msg.fsd.request.p.path.cwd = this.cwd;
        this.msg.fsd.request.p.path.ip = &this.myno;
        send_fsd();
    } else {
        this.cwd = ROOT_INODE_NR;
        tty_puts_P(PSTR("ok\n"));
        send_REPLY_RESULT(SELF, EOK);
    }
}

PRIVATE void pwd_func( __attribute__ ((unused)) char *bp)
{
    /* pwd 
     *
     * print the full path of the working directory
     */
    this.state = PRINTING_CWD;
    this.msg.fsu.request.op = OP_PWD;
    this.msg.fsu.request.p.pwd.dest = GATEWAY_ADDRESS;
    send_fsu();
}

PRIVATE void mv_func( __attribute__ ((unused)) char *bp)
{
    /* mv old ...  new 
     *
     * move a file or directory 
     * if the new name exists and is a directory, move the old name
     * into that directory.
     * note that the whole command line is sent in the FSU request. 
     */
    this.state = MOVING_ITEMS;
    this.msg.fsu.request.op = OP_MV;
    send_fsu();
}

PRIVATE void rm_func( __attribute__ ((unused)) char *bp)
{
    /* rm [-rf] item ...
     * rmdir [-f] item ...
     *
     * remove a file or directory
     */
    this.state = REMOVING_ITEMS;
    this.msg.fsu.request.op = OP_RM;
    send_fsu();
}

PRIVATE void mk_func( __attribute__ ((unused)) char *bp)
{
    /* mk [nzones] path
     * mkdir path
     *
     * create a file or make a directory
     */
    this.state = MAKING_ITEM;
    this.msg.fsu.request.op = OP_MK;
    send_fsu();
}

PRIVATE void cat_func(char *bp)
{
    /* cat [fpos] filepath */
    if (*bp) {
        off_t fpos = 0;
        char *tp = bp;

        while (*tp && *tp != ' ')
            tp++;
        while (*tp == ' ')
            tp++;

        /* if *tp is not a NIL character, there are two args, the first
         * being the file position to start reading from, and the second
         * being the path. glean the numeric value from bp and then
         * set bp to the path that tp is currently pointing to.
         *
         * if *tp is a NIL character, there is only the file path, and bp
         * should be used as it stands.
         */
        if (*tp) {
            while (isdigit(*bp)) {
                fpos = (fpos * 10) + (*bp++ - '0');
            }
            bp = tp;
        }

        if ((this.catpath = malloc(strlen(bp) +1)) == NULL) {
            send_REPLY_RESULT(SELF, ENOMEM);
        } else {
            this.state = CATTING_FILE;
            strcpy(this.catpath, bp);
            this.cat.path = this.catpath;
            this.cat.cwd = this.cwd;
            this.cat.dest = GATEWAY_ADDRESS;
            this.cat.fpos = fpos;
            send_JOB(CAT, &this.cat);
        }
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void send_fsu(void)
{
    /* common fsu instructions
     * note that the whole command line is sent in the FSU request.
     */
    this.msg.fsu.request.taskid = SELF;
    this.msg.fsu.request.jobref = &this.info.twi;
    this.msg.fsu.request.sender_addr = HOST_ADDRESS;
    this.msg.fsu.request.argstr = this.headp->bp;
    this.msg.fsu.request.arglen = strlen(this.headp->bp);
    this.msg.fsu.request.cwd = this.cwd;
    sae2_TWI_MTSR(this.info.twi, IOWA_I2C_ADDRESS,
      FSU_REQUEST, this.msg.fsu.request,
      FSU_REPLY, this.msg.fsu.reply);
}

PRIVATE void alba_func(char *bp)
{
    /* execute an ALBA config file.
     *
     */
    if (*bp) {
        this.state = RESOLVING_PATCHFILE;
        this.msg.fsd.request.op = OP_PATH;
        this.msg.fsd.request.p.path.src = bp;
        this.msg.fsd.request.p.path.len = strlen(bp);
        this.msg.fsd.request.p.path.cwd = this.cwd;
        this.msg.fsd.request.p.path.ip = &this.myno;
        send_fsd();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void key_func(char *bp)
{
    /* execute a KEY config file.
     *
     */
    if (*bp) {
        this.state = RESOLVING_KEYFILE;
        this.msg.fsd.request.op = OP_PATH;
        this.msg.fsd.request.p.path.src = bp;
        this.msg.fsd.request.p.path.len = strlen(bp);
        this.msg.fsd.request.p.path.cwd = this.cwd;
        this.msg.fsd.request.p.path.ip = &this.myno;
        send_fsd();
    } else {
        send_REPLY_RESULT(SELF, EINVAL);
    }
}

PRIVATE void send_fsd(void)
{
    /* common fsd instructions */

    this.msg.fsd.request.taskid = SELF;
    this.msg.fsd.request.jobref = &this.info.twi;
    this.msg.fsd.request.sender_addr = HOST_ADDRESS;
    sae2_TWI_MTSR(this.info.twi, FS_ADDRESS,
           FSD_REQUEST, this.msg.fsd.request,
           FSD_REPLY, this.msg.fsd.reply);
}

PRIVATE void send_syscon(void)
{
    /* common syscon instructions */

    this.msg.syscon.request.taskid = SELF;
    this.msg.syscon.request.jobref = &this.info.twi;
    this.msg.syscon.request.sender_addr = HOST_ADDRESS;
    sae2_TWI_MTSR(this.info.twi, this.target,
           SYSCON_REQUEST, this.msg.syscon.request,
           SYSCON_REPLY, this.msg.syscon.reply);
}

/* end code */
