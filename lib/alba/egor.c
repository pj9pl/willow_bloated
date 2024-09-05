/* alba/egor.c */

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

 /* An iterative process director that reads the AD7124 data register and writes
 * the value to a destination.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/ser.h"
#include "sys/rv3028c7.h"
#include "alba/ad7124.h"
#include "alba/alba.h"
#include "alba/egor.h"
#include "net/i2c.h"
#include "net/twi.h"
#include "net/ostream.h"
#include "fs/sfa.h"
#include "fs/sdc.h"
#include "fs/fsd.h"
#include "fs/rwr.h"

/* I am .. */
#define SELF EGOR
#define this egor

#define LINE_MAX 18 /* 'egor:23,016664F7' + '\n' + '\0' */

#define NR_FILES 4

#define NR_READINGS 8
#define RECORD_LEN 24

#define BUFSIZE (RECORD_LEN * NR_READINGS)

typedef struct {
    char sp[RECORD_LEN];
} reading_t;

typedef struct {
    inum_t i_inum;
    ushort_t i_nzones;
} nbuf_t;

typedef enum {
    IDLE = 0,
    FETCHING_INODES,
    READING_CONTROL_REG,
    WRITING_CONTROL_REG,
    READING_DATA,
    FETCHING_UNIXTIME,
    SKIPPING_OUTPUT,
    WRITING_DATA
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned running : 1;
    unsigned next_file : 1;
    unsigned logging : 1;
    unsigned stop_logging : 1;
    unsigned no_logging : 1;
    unsigned voltage_notify : 1;
    unsigned gen_output : 1;
    time_t ts;
    ulong_t jcount;
    ulong_t val;
    ProcNumber replyTo;
    char lbuf[LINE_MAX];
    uchar_t destination;
    uchar_t err;
    ushort_t saved_ctrl_reg;
    uchar_t display_mode;
    rwr_msg rwr;
    twi_info logf;
    reading_t *bufa;
    reading_t *bufb;
    reading_t *wp;
    reading_t *rp;
    uchar_t wr_index; /* current index */
    uchar_t rd_index; /* previous index */
    uchar_t whence;
    char *basename;
    uchar_t idx;
    nbuf_t nbuf[NR_FILES];
    inode_t *ibuf;
    dbuf_t dbuf;
    union {
        ostream_msg ostream;
        fsd_msg fsd;
    } msg;
    union {
        twi_info twi;
        alba_info alba;
        ser_info ser;
    } info;
} egor_t;

/* I have .. */
static egor_t this;

/* I can .. */
PRIVATE void resume(message *m_ptr);
PRIVATE void restore_ctrl_reg(void);
PRIVATE void write_buffer(void);
PRIVATE void flush_buffer(void);
PRIVATE void finish(void);

PUBLIC uchar_t receive_egor(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case REPLY_INFO:
        if (this.rp && (twi_info *) m_ptr->INFO == &this.logf) {
            if (this.rwr.reply.result == EOK) {
                this.rp = NULL;
                /* check space remaining */
                if (BYTE_ZONE(this.rwr.reply.fpos + sizeof(this.bufa)) >=
                                              this.nbuf[this.idx].i_nzones) {
                    /* insufficient space for a whole buffer */
                    this.next_file = TRUE;
                }
            } else if (this.rwr.reply.result == EXFULL) {
                this.next_file = TRUE;
                write_buffer();
            } else {
                this.logging = FALSE;
            }
        } else if (this.state) {
            resume(m_ptr);
        } else {
            this.running = FALSE;
            if (this.replyTo) {
                send_REPLY_RESULT(this.replyTo, this.err);
                this.replyTo = 0;
            }
            this.err = 0;
            if (this.bufa && this.rp == NULL) {
                free(this.bufa);
                this.bufa = NULL;
            }
        }
        break;

    case START:
        if (this.state == IDLE && this.basename == NULL) {
            if ((this.basename = calloc(1, NAME_SIZE +1 +
                                NR_FILES * sizeof(inode_t))) == NULL) {
                send_REPLY_RESULT(m_ptr->sender, ENOMEM);
                break;
            }
            this.ibuf = (inode_t *)(this.basename + NAME_SIZE +1);

            this.idx = 0;
            this.no_logging = FALSE;
            this.replyTo = m_ptr->sender;
            this.state = FETCHING_INODES;
            resume(m_ptr);
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    case STOP:
        if (this.state && this.running) {
            this.running = FALSE;
            if (this.replyTo == 0) {
                this.replyTo = m_ptr->sender;
            } else if (this.replyTo != m_ptr->sender) {
                send_REPLY_RESULT(this.replyTo, EINTR);
                this.replyTo = m_ptr->sender;
            }
        } else {
            send_REPLY_RESULT(m_ptr->sender, EOK);
        }
        break;

    case SET_IOCTL:
        {
            uchar_t ret = EOK;
            
            switch (m_ptr->IOCTL) {
            case SIOC_LOOP_COUNT:
                this.jcount = m_ptr->LCOUNT;
                break;

            case SIOC_LOGGING:
                if (this.no_logging == TRUE && this.running == TRUE) {
                    this.logging = FALSE;
                } else {
                    if (this.logging && m_ptr->LCOUNT == 0) {
                        this.stop_logging = TRUE;
                        flush_buffer();
                    } else {
                        this.logging = (m_ptr->LCOUNT == 1);
                    }
                }
                break;

            case SIOC_SELECT_OUTPUT:
                switch (m_ptr->LCOUNT) {
                case 0: /* OFF */
                    this.gen_output = FALSE;
                    break;

                case 1: /* VOLTAGEZ @ LCD_ADDRESS */
                    this.destination = LCD_ADDRESS;
                    this.voltage_notify = TRUE;
                    this.gen_output = TRUE;
                    break;

                case 2: /* VOLTAGEP @ TWI_OLED_ADDRESS */
                    this.destination = TWI_OLED_ADDRESS;
                    this.voltage_notify = TRUE;
                    this.gen_output = TRUE;
                    break;

                case 3: /* VOLTAGEP @ SPI_OLED_ADDRESS */
                    this.destination = SPI_OLED_ADDRESS;
                    this.voltage_notify = TRUE;
                    this.gen_output = TRUE;
                    break;

                case 4: /* OSTREAM @ GATEWAY */
                    this.destination = GATEWAY_ADDRESS;
                    this.voltage_notify = FALSE;
                    this.gen_output = TRUE;
                    break;

                case 5: /* OSTREAM @ lima */
                    this.destination = TWI_OLED_ADDRESS;
                    this.voltage_notify = FALSE;
                    this.gen_output = TRUE;
                    break;

                case 6: /* OSTREAM @ peru */
                    this.destination = SPI_OLED_ADDRESS;
                    this.voltage_notify = FALSE;
                    this.gen_output = TRUE;
                    break;

                case 7:  /* VOLTAGEx @ [any/some/none] */
                    this.destination = GCALL_I2C_ADDRESS;
                    this.voltage_notify = TRUE;
                    this.gen_output = TRUE;
                    break;

                default:
                    ret = EINVAL;
                    break;
                }
                break;

            case SIOC_DISPLAY_MODE:
                this.display_mode = m_ptr->LCOUNT | VOLTAGE_TYPE;
                break;

            default:
                ret = EINVAL;
                break;
            }
            send_REPLY_RESULT(m_ptr->sender, ret);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void resume(message *m_ptr)
{
    uchar_t *vp;

    switch (this.state) {
    case IDLE:
        break;

    case FETCHING_INODES:
        if (this.idx && this.msg.fsd.reply.result) {
            this.no_logging = TRUE;
        }
        if (this.idx < NR_FILES) {
            sprintf_P(this.basename, PSTR("egor%d"), this.idx + 1);
            this.msg.fsd.request.taskid = SELF;
            this.msg.fsd.request.jobref = &this.info.twi;
            this.msg.fsd.request.sender_addr = HOST_ADDRESS;
            this.msg.fsd.request.op = OP_PATH;
            this.msg.fsd.request.p.path.src = this.basename;
            this.msg.fsd.request.p.path.len = strlen(this.basename);
            this.msg.fsd.request.p.path.ip = this.ibuf + this.idx;
            this.msg.fsd.request.p.path.cwd = ROOT_INODE_NR;
            sae2_TWI_MTSR(this.info.twi, FS_ADDRESS,
                    FSD_REQUEST, this.msg.fsd.request,
                    FSD_REPLY, this.msg.fsd.reply);
            this.idx++;
        } else {
            time_t f_time = this.ibuf[0].i_mtime;
            this.idx = 0;
            if (this.no_logging == FALSE) {
                /* compare #0 with #1, #2 and #3 */
                for (uchar_t i = 1; i < NR_FILES; i++) {
                    if (f_time < this.ibuf[i].i_mtime) {
                        f_time = this.ibuf[i].i_mtime;
                        this.idx = i;
                    }
                }
            
                for (uchar_t i = 0; i < NR_FILES; i++) {
                    this.nbuf[i].i_inum = this.ibuf[i].i_inum;
                    this.nbuf[i].i_nzones = this.ibuf[i].i_nzones;
                }
            }

            free(this.basename);
            this.basename = NULL;

            if (this.no_logging == FALSE && this.bufa == NULL) {
                if ((this.bufa = calloc(2, BUFSIZE)) == NULL) {
                    this.no_logging = TRUE;
                } else {
                    this.bufb = this.bufa + NR_READINGS;
                }
            }

            if (this.jcount == 0 && this.replyTo) {
                send_REPLY_RESULT(this.replyTo, EOK);
                this.replyTo = 0;
            }

            if (this.no_logging == FALSE)
                this.logging = TRUE;

            this.running = TRUE;
            this.wp = this.bufa;
            this.wr_index = 0;
            this.whence = SEEK_END;
            this.state = READING_CONTROL_REG;
            this.info.alba.mode = READ_MODE;
            this.info.alba.regno = AD7124_ADC_Control;
            send_JOB(ALBA, &this.info.alba);
        }
        break;

    case READING_CONTROL_REG:
        /* Ensure DATA_STATUS (0x0400) is set and /CS_EN (0x0200) is clear
         * and that the MODE is set to CONTINUOUS CONVERSION.
         */
        if (this.info.alba.rb.adc_control.data_status != TRUE ||
                this.info.alba.rb.adc_control.cs_en != FALSE ||
                this.info.alba.rb.adc_control.mode != CONTINUOUS_MODE) {
            this.state = WRITING_CONTROL_REG;
            this.saved_ctrl_reg = this.info.alba.rb.val;
            this.info.alba.mode = WRITE_MODE;
            this.info.alba.regno = AD7124_ADC_Control;
            this.info.alba.rb.adc_control.data_status = TRUE;
            this.info.alba.rb.adc_control.cs_en = FALSE;
            this.info.alba.rb.adc_control.mode = CONTINUOUS_MODE;
            send_JOB(ALBA, &this.info.alba);
            break;
        }
        /* else fallthrough */

    case WRITING_CONTROL_REG:
        this.state = IDLE;
        if (m_ptr->RESULT == EOK && this.running) {
            this.state = this.gen_output ? READING_DATA : SKIPPING_OUTPUT;
            this.info.alba.mode = READ_MODE;
            this.info.alba.regno = AD7124_Data;
            this.info.alba.data_status = TRUE;
            send_JOB(ALBA, &this.info.alba);
        } else {
            finish();
        }
        break;

    case READING_DATA:
        vp = (uchar_t *)&this.val;
        this.val = this.info.alba.rb.val;
        if (vp[3] & AD7124_STATUS_REG_ERROR_FLAG) {
            /* The error flag is set. Bail out. */
            finish();
            break;
        }

        /* get the unixtime from the RV3028C7 */
        this.state = FETCHING_UNIXTIME;
        sae2_TWI_MR(this.info.twi, RV3028C7_I2C_ADDRESS,
                        RV_UNIX_TIME_0, this.ts);
        break;

    case FETCHING_UNIXTIME:
        if (this.no_logging == FALSE && this.logging) {
            char sbuf[RECORD_LEN +1]; /* sprintf(3) adds a nil byte */
            sprintf_P(sbuf, PSTR("eg,%08lX,%02X,%08lX\n"), this.ts,
                                                this.display_mode, this.val);
            memcpy(this.wp[this.wr_index].sp, sbuf, RECORD_LEN);
            this.wr_index++;

            if (this.wr_index == NR_READINGS || this.stop_logging) {
                if (this.logging && this.rp == NULL) {
                    this.rp = this.wp;
                    this.wp = (this.wp == this.bufa) ? this.bufb : this.bufa;
                    this.rd_index = this.wr_index;
                    write_buffer();
                } else {
                    this.stop_logging = FALSE;
                    this.logging = FALSE;
                }
                this.wr_index = 0;
            }
        }

        this.state = WRITING_DATA;
        if (this.gen_output) {
            if (this.voltage_notify) {
                switch (this.destination) {
                case LCD_ADDRESS:
                case TWI_OLED_ADDRESS:
                case SPI_OLED_ADDRESS:
                case GCALL_I2C_ADDRESS:
                    this.dbuf.taskid = SELF;
                    this.dbuf.jobref = &this.info.twi;
                    this.dbuf.sender_addr = HOST_ADDRESS;
                    this.dbuf.mtype = this.display_mode;
                    this.dbuf.res = this.val;
                    sae2_TWI_MT(this.info.twi, this.destination,
                                  VOLTAGE_NOTIFY, this.dbuf);
                    return; /* a break here would fallthrough below */
                }
            } else {
                switch (this.destination) {
                case TWI_OLED_ADDRESS:
                case SPI_OLED_ADDRESS:
                case GATEWAY_ADDRESS:
                    /* print to an OSTREAM sink */
                    sprintf_P(this.lbuf, PSTR("egor:%02X,%08lX\n"),
                                                this.display_mode, this.val);
                    this.msg.ostream.request.taskid = SELF;
                    this.msg.ostream.request.jobref = &this.info.twi;
                    this.msg.ostream.request.sender_addr = HOST_ADDRESS;
                    this.msg.ostream.request.src = this.lbuf;
                    this.msg.ostream.request.len = strlen(this.lbuf);
                    sae2_TWI_MTSR(this.info.twi, this.destination,
                        OSTREAM_REQUEST, this.msg.ostream.request,
                        OSTREAM_REPLY, this.msg.ostream.reply);
                    return; /* a break here would fallthrough below */
                }
            }
        }
        /* fallthrough */

    case SKIPPING_OUTPUT:
    case WRITING_DATA:
        if (this.running) {
            if (this.jcount > 0) {
                if (--this.jcount == 0) {
                    this.running = FALSE;
                    finish();
                }
            }
            if (this.running) {
                this.state = READING_DATA;
                this.info.alba.mode = READ_MODE;
                this.info.alba.regno = AD7124_Data;
                this.info.alba.data_status = TRUE;
                send_JOB(ALBA, &this.info.alba);
            }
        } else {
            finish();
        }
        break;
    }
}

PRIVATE void restore_ctrl_reg(void)
{
    this.state = IDLE;
    this.running = FALSE;
    this.info.alba.mode = WRITE_MODE;
    this.info.alba.regno = AD7124_ADC_Control;
    this.info.alba.rb.val = this.saved_ctrl_reg;
    send_JOB(ALBA, &this.info.alba);
}

PRIVATE void write_buffer(void)
{
    if (this.no_logging == TRUE)
        return;

    /* Write rp to the file */
    if (this.next_file) {
        if (++this.idx >= NR_FILES)
            this.idx = 0;
    }    
    if (this.stop_logging) {
        this.stop_logging = FALSE;
        this.logging = FALSE;
    }
    this.rwr.request.taskid = SELF;
    this.rwr.request.jobref = &this.logf;
    this.rwr.request.sender_addr = HOST_ADDRESS;
    this.rwr.request.inum = this.nbuf[this.idx].i_inum;
    this.rwr.request.src = (uchar_t *)this.rp;
    this.rwr.request.len = this.rd_index * RECORD_LEN;
    this.rwr.request.offset = 0;
    this.rwr.request.whence = this.whence;
    this.rwr.request.truncate = this.next_file ? TRUE : FALSE;
    sae2_TWI_MTSR(this.logf, FS_ADDRESS,
              RWR_REQUEST, this.rwr.request,
              RWR_REPLY, this.rwr.reply);
    this.whence = SEEK_END;
    this.next_file = FALSE;
}

PRIVATE void flush_buffer(void)
{
    if (this.logging && this.wp && this.rp == NULL && this.wr_index) {
        this.rp = this.wp;
        this.rd_index = this.wr_index;
        write_buffer();
    }
}

PRIVATE void finish(void)
{
    if (this.wp) {
        flush_buffer();
        restore_ctrl_reg();
        this.wp = NULL;
    }
}

/* end code */
