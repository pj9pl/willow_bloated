/* bmp/bmp.c */

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

/* A message interface to a BOSCH BMP180 pressure sensor.
 * The breakout board has 4k7 pullups on SDA and SCL.
 * Refer to BOSCH datasheet BST-BMP180-DS000-09.pdf r2.5 April 2015.
 *
 */

#include <avr/io.h>

#include "sys/ioctl.h"
#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/twi.h"
#include "bmp/bmp.h"

/* I am .. */
#define SELF BMP
#define this bmp

#define BMP180_I2C_ADDRESS        0xEE
#define BMP180_ID                 0x55
#define OSS                       3
#define SCO                       5
#define ONE_BYTE                  1
#define TWO_BYTES                 2
#define THREE_BYTES               3
#define TEN_MILLISECONDS          10
#define FIFTEEN_MILLISECONDS      15
#define TWENTYSIX_MILLISECONDS    26

#define TEMPERATURE_WAIT_DELAY    TEN_MILLISECONDS
#define PRESSURE_WAIT_DELAY       TWENTYSIX_MILLISECONDS
#define POLL_WAIT_DELAY           FIFTEEN_MILLISECONDS

#define BMP_ID_ADDR               0xD0
#define BMP_CONTROL_ADDR          0xF4
#define BMP_ADC_OUT_ADDR          0xF6
#define BMP_CONV_TEMP_CMD         0x2E
#define BMP_CONV_PRESS_CMD        0x34
#define BMP_RESET_ADDR            0xE0
#define BMP_CALIB_ADDR            0xAA
#define BMP_CALIB_LENGTH          22
#define BMP_RESET_VALUE           0xB6

typedef enum {
    IDLE = 0,
    READING_ID,
    READING_CALIBRATION_DATA,
    MEASURING_TEMPERATURE,
    IN_TEMPERATURE_WAIT_STATE,
    READING_TEMPERATURE_DATA,
    MEASURING_PRESSURE,
    IN_PRESSURE_WAIT_STATE,
    POLLING_EOC,
    READING_PRESSURE_DATA
} __attribute__ ((packed)) state_t;

typedef struct {
    short AC1;
    short AC2;
    short AC3;
    unsigned short AC4;
    unsigned short AC5;
    unsigned short AC6;
    short B1;
    short B2;
    short MB;
    short MC;
    short MD;
} cal_data;

typedef struct {
    state_t state;
    bmp_info *headp;
    cal_data cal;
    uchar_t io;
    union {
        twi_info twi;
        clk_info clk;
    } info;
} bmp_t;

/* I have .. */
static bmp_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void calc_readings(void);
PRIVATE void resume(void);
PRIVATE void write(ushort_t len, void *ptr, uchar_t cmd);
PRIVATE void read(ushort_t len, void *ptr, uchar_t cmd);

PUBLIC uchar_t receive_bmp(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case REPLY_INFO:
    case REPLY_RESULT:
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
            bmp_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            ip->T = 0;
            ip->p = 0;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                bmp_info *tp;
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
    if (this.headp->mode == READ_BMP) {
        if (this.cal.AC1 == 0) {
            this.state = READING_ID;
            read(ONE_BYTE, &this.io, BMP_ID_ADDR);
        } else {
            this.state = MEASURING_TEMPERATURE;
            this.io = BMP_CONV_TEMP_CMD;
            write(ONE_BYTE, &this.io, BMP_CONTROL_ADDR);
        }
    } else {
        this.io = BMP_RESET_VALUE;
        write(ONE_BYTE, &this.io, BMP_RESET_ADDR);
    }
}

/* function adapted from that given in the BMP180 datasheet p.15 */
PRIVATE void calc_readings(void)
{
    /* calculate true temperature */
    long X1 = ((long)this.headp->T - this.cal.AC6) * this.cal.AC5 >> 15;
    long X2 = ((long)this.cal.MC << 11) / (X1 + this.cal.MD);
    long B5 = X1 + X2;
    this.headp->T = (B5 * 10 + 8) >> 4;

    /* calculate true pressure */
    long B6 = B5 - 4000;
    X1 = this.cal.B2 * (B6 * B6 >> 12) >> 11;
    X2 = this.cal.AC2 * B6 >> 11;
    long X3 = X1 + X2;
    long B3 = ((((this.cal.AC1 << 2) + X3) << OSS) + 2) >> 2; 
    X1 = this.cal.AC3 * B6 >> 13;
    X2 = this.cal.B1 * (B6 * B6 >> 12) >> 16;
    X3 = (X1 + X2 + 2) >> 2;
    unsigned long B4 = this.cal.AC4 * (unsigned long)(X3 + 32768) >> 15;
    unsigned long B7 = ((unsigned long)this.headp->p - B3) * (50000 >> OSS);
    this.headp->p = (B7 < 0x80000000) ? (B7 << 1) / B4 : (B7 / B4) << 1;
    X1 = (this.headp->p >> 8) * (this.headp->p >> 8);
    X1 = X1 * 3038 >> 16;
    X2 = -7357 * this.headp->p >> 16;
    this.headp->p = this.headp->p + ((X1 + X2 + 3791) >> 4);
}

PRIVATE void resume(void)
{
    uchar_t *j;
    uchar_t k;

    switch (this.state) {

    case IDLE:
        break;

    case READING_ID:
        if (this.io == BMP180_ID) {
            this.state = READING_CALIBRATION_DATA;
            read(BMP_CALIB_LENGTH, &this.cal, BMP_CALIB_ADDR);
        } else {
            send_REPLY_RESULT(SELF, ENODEV);
        }
        break;

    case READING_CALIBRATION_DATA:
        this.state = MEASURING_TEMPERATURE;
        /* the cal data has been written into this.cal
         * as 11 big-endian 16-bit integers, 8 signed, 3 unsigned.
         * Step through swapping the pairs of bytes as we go.
         */
        j = (uchar_t *)&this.cal;
        for (int i = 0; i < (BMP_CALIB_LENGTH >> 1); i++, j += 2) {
            k = *j;
            *j = *(j+1);
            *(j+1) = k;
        }
        this.io = BMP_CONV_TEMP_CMD;
        write(ONE_BYTE, &this.io, BMP_CONTROL_ADDR);
        break;

    case MEASURING_TEMPERATURE:
        this.state = IN_TEMPERATURE_WAIT_STATE; 
        sae_CLK_SET_ALARM(this.info.clk, TEMPERATURE_WAIT_DELAY);
        break;

    case IN_TEMPERATURE_WAIT_STATE:
        this.state = READING_TEMPERATURE_DATA; 
        read(TWO_BYTES, &this.headp->T, BMP_ADC_OUT_ADDR);
        break;

    case READING_TEMPERATURE_DATA:
        this.state = MEASURING_PRESSURE;
        j = (uchar_t *) &this.headp->T;
        k = j[0];
        j[0] = j[1];
        j[1] = k;

        this.io = BMP_CONV_PRESS_CMD | OSS << 6;
        write(ONE_BYTE, &this.io, BMP_CONTROL_ADDR);
        break;

    case MEASURING_PRESSURE:
        this.state = IN_PRESSURE_WAIT_STATE; 
        sae_CLK_SET_ALARM(this.info.clk, PRESSURE_WAIT_DELAY);
        break;

    case IN_PRESSURE_WAIT_STATE:
        this.state = POLLING_EOC;
        read(ONE_BYTE, &this.io, BMP_CONTROL_ADDR);
        break;
     
    case POLLING_EOC:
        if (this.io & _BV(SCO)) {
            this.state = IN_PRESSURE_WAIT_STATE;
            sae_CLK_SET_ALARM(this.info.clk, POLL_WAIT_DELAY);
        } else {
            this.state = READING_PRESSURE_DATA; 
            read(THREE_BYTES, &this.headp->p, BMP_ADC_OUT_ADDR);
        }
        break;

    case READING_PRESSURE_DATA:
        /* pressure is a 3-byte variable,
         * so only swap first and third bytes.
         */
        this.state = IDLE;
        j = (uchar_t *) &this.headp->p;
        k = j[0];
        j[0] = j[2];
        j[2] = k;

        this.headp->p >>= 8 - OSS;
        calc_readings();
        send_REPLY_RESULT(SELF, EOK);
        break;
    }
}

PRIVATE void write(ushort_t len, void *ptr, uchar_t cmd)
{
    sae1_TWI_MT(this.info.twi, BMP180_I2C_ADDRESS, cmd, ptr, len);
}

PRIVATE void read(ushort_t len, void *ptr, uchar_t cmd)
{
    sae1_TWI_MR(this.info.twi, BMP180_I2C_ADDRESS, cmd, ptr, len);
}

/* end code */
