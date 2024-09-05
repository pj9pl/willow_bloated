/* fs/ssd.c */

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

/* SDCard SPI driver.
 *
 * see also [AT p.xx]     ATmega328P datasheet.
 *          [SD p.xx]     Physical Layer Simplified Specification v4.10
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "fs/sdc.h"
#include "fs/ssd.h"

/* I am .. */
#define SELF SSD
#define this ssd

#define TWO_SECONDS 2000 /* milliseconds */
#define CARD_NOT_READY_DELAY TWO_SECONDS

#define FF_BYTE 0xff        /* return byte in many cases */
#define PRE_INIT 0xBF

#define TRANSMISSION_BIT 0x40 /* [SD p.161] */
#define MAX_NCR 20
#define CMD0 0
#define CMD8 8
#define CMD17 17
#define CMD24 24
#define ACMD41 41
#define CMD55 55
#define CMD58 58
#define START_BLOCK_TOKEN 0xfe
#define DATA_ERROR_TOKEN_MASK 0xf0

/* R1 Response Format [SD p.169] */
#define PARAMETER_ERROR      _BV(6)
#define ADDRESS_ERROR        _BV(5)
#define ERASE_SEQUENCE_ERROR _BV(4)
#define COM_CRC_ERROR        _BV(3)
#define ILLEGAL_COMMAND      _BV(2)
#define ERASE_ERROR          _BV(1)
#define IN_IDLE_STATE        _BV(0)

/* Data Response Token [SD p.172] */
#define DATA_RESPONSE_MASK 0x1F
#define DATA_ACCEPTED      0x05
#define DATA_CRC_ERROR     0x0B
#define DATA_WRITE_ERROR   0x0D

#define SPI_DDR  DDRB
#define SPI_PINS PINB
#define SPI_PORT PORTB
#define SPI_SCK  _BV(PORTB5)
#define SPI_MISO _BV(PORTB4)
#define SPI_MOSI _BV(PORTB3)
#define SPI_SS   _BV(PORTB2)
#define SPI_CD   _BV(PORTB1)      /* Card Detect: 0 = present, 1 = absent. */

typedef enum {
    UNSET = 0,
    INITIALIZING,
    INITIALIZED
} __attribute__ ((packed)) init_status_t;

/* these are the various stages that the SSD can encounter
 * during an SPI transaction.
 */
typedef enum {
    IN_PRE_INIT,
    IN_COMMAND,
    AWAITING_FLAGS,
    IN_RESPONSE,
    AWAITING_READ_TOKEN,
    IN_READ_DATA,
    IN_READ_CRC,
    IN_WRITE_DATA,
    IN_WRITE_CRC,
    AWAITING_DATA_RESPONSE,
    BUSY,
    DONE
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned sdhc : 1;
    init_status_t init_status : 2;
    unsigned read_token_expected : 1;
    unsigned write_token_available : 1;
    ssd_info *headp;
    uchar_t checksum[2];
    uchar_t cmd_buf[6];
    uchar_t response_buf[4];
    uchar_t *cmd;
    uchar_t *src;
    uchar_t *dst;
    uchar_t *crc;
    uchar_t crc_cnt;
    uchar_t cmd_cnt;
    ushort_t src_cnt;
    ushort_t dst_cnt;
    uchar_t flags; /* R1 Response Format [SD p.169] */
    uchar_t Ncr;
    union {
        clk_info clk;
    } info;
} ssd_t;

/* I have .. */
static ssd_t this;

/* I can .. */
PRIVATE void deselect_card(void) { SPI_PORT |= SPI_SS; }
PRIVATE void select_card(void) { SPI_PORT &= ~SPI_SS; }

PRIVATE void start_job(void);
PRIVATE void resume(void);
PRIVATE void do_pre_init(void);
PRIVATE void do_cmd0(void);
PRIVATE void do_cmd8(void);
PRIVATE void do_acmd41(void);
PRIVATE void do_cmd55(void);
PRIVATE void do_cmd58(void);
PRIVATE void do_read_block(void);
PRIVATE void do_write_block(void);
PRIVATE void do_cmd_common(void);

/* initialize the SPI */
PUBLIC void config_ssd(void)
{
    /* Activate pullup SS while it is an input so that it remains high
     * when it is configured as an output without any glitches. [AT p.85]
     */
    deselect_card();

    /* Configure SS, MOSI and SCK as outputs
     * MISO stays as an input with external pullup.
     */
    SPI_DDR |= SPI_SS | SPI_MOSI | SPI_SCK;

    /* Enable SPI in the power reduction register after setting the pullups */
    PRR &= ~_BV(PRSPI);

    /* Configure the SPI control and status registers [AT p.174-7]
     * SPIE=1                  Enable STC interrupt
     * SPE=1                   Enable SPI
     * DORD=0                  MSB first
     * MSTR=1                  Master
     * CPOL=0 CPHA=0           Mode 0
     * SPR1=0 SPR0=1 SPI2X=1   Clock Rate = F_CPU / 8 == 1 MHz
     */
    SPCR = _BV(SPIE) | _BV(SPE) | _BV(MSTR) | _BV(SPR0);
    SPSR |= _BV(SPI2X);
}

PUBLIC uchar_t receive_ssd(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case MEDIA_CHANGE:
        this.init_status = UNSET;
        break;

    case REPLY_RESULT:
        deselect_card();
        if (this.init_status == INITIALIZING) {
            resume();
        } else if (this.headp) {
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            if ((this.headp = this.headp->nextp) != NULL)
                start_job();
        }
        break;

    case INIT_OK:
        deselect_card();
        if (this.headp)
            start_job();
        break;

    case ALARM:
        if (this.init_status == INITIALIZING)
            do_cmd55();
        break;

    case JOB:
        {
            ssd_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                ssd_info *tp;
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
    if (this.init_status != INITIALIZED) {
        if (this.init_status == UNSET) {
            do_pre_init();
        }
        return;
    }

    switch (this.headp->op) {
    case READ_SECTOR:
        do_read_block();
        break;
    case WRITE_SECTOR:
        do_write_block();
        break;
    }
}

/* This function processes the result of the
 * command just performed and acts accordingly.
 */
PRIVATE void resume(void)
{
    uchar_t cmd = this.cmd_buf[0] & ~TRANSMISSION_BIT;
    
    switch (cmd) {
    case PRE_INIT:
        do_cmd0();
        break;

    case CMD0:
        if (this.flags == IN_IDLE_STATE)
            do_cmd8();
        break;

    case CMD8:
        if (this.flags == IN_IDLE_STATE)
            do_cmd55();
        break;

    case CMD55:
        if (this.flags == IN_IDLE_STATE)
            do_acmd41();
        break;

    case ACMD41:
        if (this.flags == 0x00) {
            do_cmd58();
        } else if (this.flags == IN_IDLE_STATE) {
            sae_CLK_SET_ALARM(this.info.clk, CARD_NOT_READY_DELAY);
        }
        break;

    case CMD58:
        if (this.flags == 0x00) {
            /* Test the Card power up status bit [p.112] */
            if (this.response_buf[0] & 0x80)
                /* Test the Card Capacity Status bit (CCS) [p.112] */ 
                this.sdhc = (this.response_buf[0] & 0x40) ? TRUE : FALSE;
            this.init_status = INITIALIZED;
            /* Send an irregular reply to the main loop to start
             * any pending job. Initialization is an irregular operation,
             * and warrents some special provision.
             * The regular reply would remove the job from the headp, in
             * the belief that the job has been done, and that the reply
             * is to announce that event.
             */
            send_INIT_OK(SELF);
        }
        break;
    }
}

PRIVATE void do_pre_init(void)
{
    /* provide 78 clock cycles with /CS high. [SD p.149] 
     * assign PRE_INIT (0xBF) to this.cmd_buf[0] so that
     * resume() can identify this cmd.
     */
    this.init_status = INITIALIZING;
    this.cmd_buf[0] = PRE_INIT;
    this.cmd_cnt = 10;
    this.state = IN_PRE_INIT;    
    SPDR = FF_BYTE;
}

/* CMD0: GO_IDLE_STATE [SD p.163]
 * Resets the SD Memory Card.
 */
PRIVATE void do_cmd0(void)
{
    this.cmd_buf[0] = CMD0 | TRANSMISSION_BIT;
    this.cmd_buf[1] = 0x00;
    this.cmd_buf[2] = 0x00;
    this.cmd_buf[3] = 0x00;
    this.cmd_buf[4] = 0x00;
    this.cmd_buf[5] = 0x95;
    this.src = 0;
    this.src_cnt = 0;
    this.dst = 0;
    this.dst_cnt = 0;
    this.read_token_expected = FALSE;
    this.write_token_available = FALSE;
    do_cmd_common();
}

/* CMD8: SEND_IF_COND [SD p.164]
 * SD Memory Card interface condition that includes the host supply
 * voltage information and asks the accessed card whether card can
 * operate in supplied voltage range. Reserved bits shall be set to '0'.
 */
PRIVATE void do_cmd8(void)
{
    this.cmd_buf[0] = CMD8 | TRANSMISSION_BIT;
    this.cmd_buf[1] = 0x00;
    this.cmd_buf[2] = 0x00;
    this.cmd_buf[3] = 0x01;
    this.cmd_buf[4] = 0xAA;
    this.cmd_buf[5] = 0x87;
    this.src = 0;
    this.src_cnt = 0;
    this.dst = this.response_buf;
    this.dst_cnt = 4;
    this.read_token_expected = FALSE;
    this.write_token_available = FALSE;
    do_cmd_common();
}

/* CMD17: READ_SINGLE_BLOCK [SD p.164]
 * Reads a block of the size selected by the SET_BLOCKLEN command.
 */
PRIVATE void do_read_block(void)
{
    this.cmd_buf[0] = CMD17 | TRANSMISSION_BIT;
    this.cmd_buf[1] = (this.headp->phys_sector >> 24);
    this.cmd_buf[2] = (this.headp->phys_sector >> 16);
    this.cmd_buf[3] = (this.headp->phys_sector >> 8);
    this.cmd_buf[4] = this.headp->phys_sector;
    this.cmd_buf[5] = 0xff;
    this.src = 0;
    this.src_cnt = 0;
    this.dst = this.headp->buf;
    this.dst_cnt = 512;
    this.read_token_expected = TRUE;
    this.write_token_available = FALSE;
    do_cmd_common();
}

/* CMD24: WRITE_BLOCK [SD p.164]
 * Writes a block of the size selected by the SET_BLOCKLEN command.
 * n.b. SDHC have fixed size blocklen of 512 bytes.
 */
PRIVATE void do_write_block(void)
{
    this.cmd_buf[0] = CMD24 | TRANSMISSION_BIT;
    this.cmd_buf[1] = (this.headp->phys_sector >> 24);
    this.cmd_buf[2] = (this.headp->phys_sector >> 16);
    this.cmd_buf[3] = (this.headp->phys_sector >> 8);
    this.cmd_buf[4] = this.headp->phys_sector;
    this.cmd_buf[5] = 0xff;
    this.src = this.headp->buf;
    this.src_cnt = 512;
    this.dst = 0;
    this.dst_cnt = 0;
    this.read_token_expected = FALSE;
    this.write_token_available = TRUE;
    do_cmd_common();
}

/* ACMD41: SD_SEND_OP_COND [SD p.167]
 * Sends host capacity support information and activates
 * the card's initialization process.
 * Reserved bits shall be set to '0'.
 */
PRIVATE void do_acmd41(void)
{
    this.cmd_buf[0] = ACMD41 | TRANSMISSION_BIT;
    this.cmd_buf[1] = 0x40;
    this.cmd_buf[2] = 0x00;
    this.cmd_buf[3] = 0x00;
    this.cmd_buf[4] = 0x00;
    this.cmd_buf[5] = 0xFF;
    this.src = 0;
    this.src_cnt = 0;
    this.dst = 0;
    this.dst_cnt = 0;
    this.read_token_expected = FALSE;
    this.write_token_available = FALSE;
    do_cmd_common();
}

/* CMD55: APP_CMD [SD p.166]
 * Defines to the card that the next command is an application
 * specific command rather than a standard command.
 */
PRIVATE void do_cmd55(void)
{
    this.cmd_buf[0] = CMD55 | TRANSMISSION_BIT;
    this.cmd_buf[1] = 0x00;
    this.cmd_buf[2] = 0x00;
    this.cmd_buf[3] = 0x00;
    this.cmd_buf[4] = 0x00;
    this.cmd_buf[5] = 0xFF;
    this.src = 0;
    this.src_cnt = 0;
    this.dst = 0;
    this.dst_cnt = 0;
    this.read_token_expected = FALSE;
    this.write_token_available = FALSE;
    do_cmd_common();
}

/* CMD58: READ_OCR. [SD p.166]
 * Reads the OCR register of a card.
 * CCS bit is assigned to OCR[32].
 */
PRIVATE void do_cmd58(void)
{
    this.cmd_buf[0] = CMD58 | TRANSMISSION_BIT;
    this.cmd_buf[1] = 0x00;
    this.cmd_buf[2] = 0x00;
    this.cmd_buf[3] = 0x00;
    this.cmd_buf[4] = 0x00;
    this.cmd_buf[5] = 0xFF;
    this.src = 0;
    this.src_cnt = 0;
    this.dst = this.response_buf;
    this.dst_cnt = 4;
    this.read_token_expected = FALSE;
    this.write_token_available = FALSE;
    do_cmd_common();
}

PRIVATE void do_cmd_common(void)
{
    this.cmd = this.cmd_buf;
    this.cmd_cnt = 6;
    this.Ncr = MAX_NCR;
    this.flags = 0;
    this.crc = this.checksum;
    this.crc_cnt = 2;
    this.state = IN_COMMAND;
    select_card();
    this.cmd_cnt--;
    SPDR = *this.cmd++;
}

/* -----------------------------------------------------
   Handle an SPI Serial Transfer Complete interrupt.
   This appears as <__vector_17>: in the .lst file.
   -----------------------------------------------------*/
ISR(SPI_STC_vect)
{
    uchar_t data;

    switch (this.state) {
    case IN_PRE_INIT:
        if (this.cmd_cnt) {
            SPDR = FF_BYTE;
            this.cmd_cnt--;
        } else {
            this.state = DONE;
            SPDR = FF_BYTE;
        }
        break;

    case IN_COMMAND:
        if (this.cmd_cnt) {
            SPDR = *this.cmd++;
            this.cmd_cnt--;
        } else {
            this.state = AWAITING_FLAGS;
            SPDR = FF_BYTE;
        }
        break;

    case AWAITING_FLAGS:
        data = SPDR;
        if ((data & 0x80) == 0) { /* R1 Response has been received */
            this.flags = data;
            if (this.write_token_available) {
                SPDR = START_BLOCK_TOKEN;
                this.state = IN_WRITE_DATA;
            } else if (this.dst_cnt) {
                if (this.read_token_expected)
                    this.state = AWAITING_READ_TOKEN;
                else
                    this.state = IN_RESPONSE;
                SPDR = FF_BYTE;
            } else {
                this.state = DONE;
                SPDR = FF_BYTE;
            }
        } else if (this.Ncr-- == 0) {         /* timed out */
            send_REPLY_RESULT(SELF, ENODEV);
        } else {            /* try again, up to MAX_NCR attempts */
            SPDR = FF_BYTE;
        }
        break;

    case IN_RESPONSE:
        *this.dst++ = SPDR;
        SPDR = FF_BYTE;
        if (--this.dst_cnt == 0)
            this.state = DONE;
        break;

    case AWAITING_READ_TOKEN:
        data = SPDR;
        if ((data & DATA_ERROR_TOKEN_MASK) == 0) {
            send_REPLY_RESULT(SELF, data);
        } else {
            if (data == START_BLOCK_TOKEN)
                this.state = IN_READ_DATA;
            SPDR = FF_BYTE;
        }
        break;

    case IN_READ_DATA:
        *this.dst++ = SPDR;
        SPDR = FF_BYTE;
        if (--this.dst_cnt == 0)
            this.state = IN_READ_CRC;
        break;

    case IN_READ_CRC:
        *this.crc++ = SPDR;
        SPDR = FF_BYTE;
        if (--this.crc_cnt == 0)
            this.state = DONE;
        break;

    case IN_WRITE_DATA:
        if (this.src_cnt) {
            SPDR = *this.src++;
            this.src_cnt--;
        } else {
            this.state = IN_WRITE_CRC;
            SPDR = FF_BYTE;
        }
        break;

    case IN_WRITE_CRC:
        this.state = AWAITING_DATA_RESPONSE;
        SPDR = FF_BYTE;
        break;

    case AWAITING_DATA_RESPONSE:
        /* Data Response Token [SD p.172] */
        data = SPDR;
        if (data != 0xff) {
            switch (data & DATA_RESPONSE_MASK) {
            case DATA_ACCEPTED:
               /* exiting state. 
                * does not cause any further interrupts.
                */
                this.state = BUSY;
                SPDR = FF_BYTE;
                break;

            case DATA_CRC_ERROR:
                send_REPLY_RESULT(SELF, EFAULT);
                break;

            case DATA_WRITE_ERROR:
                send_REPLY_RESULT(SELF, EACCES);
                break;
            }
        } else {
            SPDR = FF_BYTE;
        }
        break;

    case BUSY:
        data = SPDR;
        if (data)
            this.state = DONE;
        SPDR = FF_BYTE;
        break;

    case DONE:
        send_REPLY_RESULT(SELF, EOK);
        break;
    }
}

/* convenience function */

PUBLIC void send_SSD_JOB(ProcNumber sender, ssd_info *cp, uchar_t op,
                                              ushort_t sector, void *bp)
{
    cp->op = op;
    cp->phys_sector = sd_meta.firstSector + sector;
    cp->buf = bp;
    send_m3(sender, SELF, JOB, cp);
}

/* end code */
