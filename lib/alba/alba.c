/* alba/alba.c */

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

/*
 * AD7124-8 SPI driver
 *
 * See also Analog Devices AD7124-8 Revision E datasheet.
 */

/*
 * Reading the data register whilst the /RDY pin is high is deferred
 * until the pin goes low.
 */
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "alba/ad7124.h"
#include "alba/alba.h"

/* I am .. */
#define SELF ALBA
#define this alba

/* Sending 8 * 0xFF bytes resets the AD7124-8, and
 * produces an 8-byte response in the receive buffer.
 */
#define RESET_LENGTH 8
#define ALBA_BUFLEN RESET_LENGTH

#define SPI_DDR   DDRB
#define SPI_PINS  PINB
#define SPI_PORT  PORTB

#define SPI_SCK  _BV(PORTB5)
#define SPI_MISO _BV(PORTB4)
#define SPI_MOSI _BV(PORTB3)
#define SPI_SS   _BV(PORTB2)
#define SPI_SYNC _BV(PORTB1)

#define RDY_PINS SPI_PINS
#define RDY_PORT SPI_PORT
#define RDY_BIT  SPI_MISO 

typedef enum {
    IDLE = 0,
    BUSY
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned armed : 1;
    alba_info *headp;
    ProcNumber rdy_replyTo;
    uchar_t buf[ALBA_BUFLEN];
    uchar_t rcnt;
    uchar_t tcnt;
} alba_t;

/* I have .. */
static alba_t this;

static const uchar_t __flash reg_width[] = {
    [AD7124_Status] = 1,
    [AD7124_ADC_Control] = 2,
    [AD7124_Data] = 3,
    [AD7124_IOCon1] = 3,
    [AD7124_IOCon2] = 2,
    [AD7124_ID] = 1,
    [AD7124_Error] = 3,
    [AD7124_Error_En] = 3,
    [AD7124_Mclk_Count] = 1,
    [AD7124_Channel_0] = 2,
    [AD7124_Channel_1] = 2,
    [AD7124_Channel_2] = 2,
    [AD7124_Channel_3] = 2,
    [AD7124_Channel_4] = 2,
    [AD7124_Channel_5] = 2,
    [AD7124_Channel_6] = 2,
    [AD7124_Channel_7] = 2,
    [AD7124_Channel_8] = 2,
    [AD7124_Channel_9] = 2,
    [AD7124_Channel_10] = 2,
    [AD7124_Channel_11] = 2,
    [AD7124_Channel_12] = 2,
    [AD7124_Channel_13] = 2,
    [AD7124_Channel_14] = 2,
    [AD7124_Channel_15] = 2,
    [AD7124_Config_0] = 2,
    [AD7124_Config_1] = 2,
    [AD7124_Config_2] = 2,
    [AD7124_Config_3] = 2,
    [AD7124_Config_4] = 2,
    [AD7124_Config_5] = 2,
    [AD7124_Config_6] = 2,
    [AD7124_Config_7] = 2,
    [AD7124_Filter_0] = 3,
    [AD7124_Filter_1] = 3,
    [AD7124_Filter_2] = 3,
    [AD7124_Filter_3] = 3,
    [AD7124_Filter_4] = 3,
    [AD7124_Filter_5] = 3,
    [AD7124_Filter_6] = 3,
    [AD7124_Filter_7] = 3,
    [AD7124_Offset_0] = 3,
    [AD7124_Offset_1] = 3,
    [AD7124_Offset_2] = 3,
    [AD7124_Offset_3] = 3,
    [AD7124_Offset_4] = 3,
    [AD7124_Offset_5] = 3,
    [AD7124_Offset_6] = 3,
    [AD7124_Offset_7] = 3,
    [AD7124_Gain_0] = 3,
    [AD7124_Gain_1] = 3,
    [AD7124_Gain_2] = 3,
    [AD7124_Gain_3] = 3,
    [AD7124_Gain_4] = 3,
    [AD7124_Gain_5] = 3,
    [AD7124_Gain_6] = 3,
    [AD7124_Gain_7] = 3
};

/* I can .. */
PRIVATE void start_job(void);
PRIVATE void detect_rdy(ProcNumber replyTo);
PRIVATE void enable_PC0_interrupt(void);
PRIVATE void clear_PC0_interrupt(void);
PRIVATE void enable_rdy_interrupt(void);
PRIVATE void disable_rdy_interrupt(void);
PRIVATE void select_slave(void);
PRIVATE void deselect_slave(void);
PRIVATE uchar_t get_width(uchar_t regno);

/* configure the SPI */
PUBLIC void config_alba(void)
{
    /* Enable pinchange 0 interrupt, to allow RDY_BIT */
    enable_PC0_interrupt();

    /* Activate pullups on SYNC, SS and MISO/RDY.
     *
     * SYNC and SS get pulled up whilst they are inputs
     * so that they continue to generate a high level when
     * they are reconfigured as outputs.
     *
     * MISO/RDY remains configured as an input and needs a
     * soft pullup as the pin will float when the AD7124 is
     * deselected.
     *
     * MOSI and SCK don't warrant pullups.
     */
    SPI_PORT |= SPI_SYNC | SPI_SS | SPI_MISO;

    /* Configure SYNC, SS, MOSI and SCK as outputs.
     * MISO/RDY stays as a pulled-up input.
     */
    SPI_DDR |= SPI_SYNC | SPI_SS | SPI_MOSI | SPI_SCK;

    /* Enable SPI in the power reduction register after setting the pullups */
    PRR &= ~_BV(PRSPI);

    /* Configure the SPI control and status registers [p.174-7]
     * SPIE = 1 Enable STC interrupt
     * SPE  = 1 Enable SPI
     * DORD = 0 Data Order: MSB first
     * MSTR = 1 Master
     * CPOL = 1 SCK high when idle         \___ SPI Data Mode 3
     * CPHA = 1 sampled on trailing edge   /
     * SPR1 = 0 \
     * SPR0 = 1  >-- clock rate == F_CPU / 8 == 1MHz 
     * SPI2X =1 / 
     */
    SPCR = _BV(SPIE) | _BV(SPE) | _BV(MSTR) | _BV(CPOL) | _BV(CPHA) | _BV(SPR0);
    SPSR = _BV(SPI2X);
}

PUBLIC uchar_t receive_alba(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case NOT_BUSY:
        switch (this.state) {
        case IDLE:
            break;

        case BUSY:
            this.state = IDLE;
            if (this.headp->mode == READ_MODE) {
               /* extract 3 bytes big-endian into a little-endian ulong_t
                * then copy the status byte to the most significant byte
                * of the ulong_t.
                *    [0,1,2,3,4]  -->  [3,2,1,4]
                *       ^ ^ ^         lsb     msb
                */
                switch (this.rcnt) {
                case 5:
                    this.headp->rb.ch[0] = this.buf[3]; 
                    this.headp->rb.ch[1] = this.buf[2];
                    this.headp->rb.ch[2] = this.buf[1];
                    this.headp->rb.ch[3] = this.buf[4];
                    break;

                case 4:
                    this.headp->rb.ch[0] = this.buf[3]; 
                    this.headp->rb.ch[1] = this.buf[2];
                    this.headp->rb.ch[2] = this.buf[1];
                    this.headp->rb.ch[3] = 0;
                    break;

                case 3:
                    this.headp->rb.ch[0] = this.buf[2]; 
                    this.headp->rb.ch[1] = this.buf[1];
                    this.headp->rb.ch[2] = 0;
                    this.headp->rb.ch[3] = 0;
                    break;

                case 2:
                    this.headp->rb.ch[0] = this.buf[1];
                    this.headp->rb.ch[1] = 0;
                    this.headp->rb.ch[2] = 0;
                    this.headp->rb.ch[3] = 0;
                    break;
                }
            }
            send_REPLY_INFO(this.headp->replyTo, EOK, this.headp);
            if ((this.headp = this.headp->nextp) != NULL) 
                start_job();
            break;
        }
        break;

    case JOB:
        {
            alba_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                alba_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case RDY_REQUEST:
        if (this.armed && this.rdy_replyTo == 0) {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        } else {
            detect_rdy(m_ptr->sender);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

PRIVATE void start_job(void)
{
    /* Start the transaction.
     * The whole message is contained in the transmit buffer.
     * Transfer the first byte to the SPI hardware.
     *
     * Suspend any /RDY detection for the duration of the
     * transaction. The fact that this.armed is TRUE
     * is all that is required in order to restore it.
     */
    disable_rdy_interrupt();

    uchar_t len = get_width(this.headp->regno);
    switch (this.headp->mode) {
    case RESET_MODE:
        this.tcnt = RESET_LENGTH;
        memset(this.buf, 0xFF, this.tcnt);
        break;

    case READ_MODE:
        this.tcnt = len + 1;
        if (this.headp->regno == AD7124_Data && this.headp->data_status)
            this.tcnt++;
        this.buf[0] = AD7124_COMM_REG_RD | this.headp->regno;
        if (this.tcnt && this.headp->regno == AD7124_Data) {
            select_slave();
            if (RDY_PINS & RDY_BIT) {
                /* deselect the slave until the interrupt is enabled */
                deselect_slave();
                detect_rdy(0);
                return;
            }
        }
        break;

    case WRITE_MODE:
        this.tcnt = len + 1;
        this.buf[0] = AD7124_COMM_REG_WR | this.headp->regno;
        switch (len) {
        case 1:
            this.buf[1] = this.headp->rb.ch[0];
            break;

        case 2:
            this.buf[1] = this.headp->rb.ch[1];
            this.buf[2] = this.headp->rb.ch[0];
            break;

        case 3:
            this.buf[1] = this.headp->rb.ch[2];
            this.buf[2] = this.headp->rb.ch[1];
            this.buf[3] = this.headp->rb.ch[0];
            break;
        }
        break;
    }

    if (this.tcnt) {
        select_slave();
        this.state = BUSY;
        this.tcnt--;
        this.rcnt = 0;
        SPDR = this.buf[this.rcnt];
    }
}

/*-------------------------------------------------------
  Handle a PCINT0 interrupt.
  This occurs when the AD7124 has signaled /RDY. 
  Mask the RDY_BIT interrupt.

  This appears as <__vector_3>: in the .lst file.
-------------------------------------------------------*/
ISR(PCINT0_vect)
{
    /* MISO/RDY has produced a low level. */
    /* Disable PCINT4 and register the event. */
    disable_rdy_interrupt();
    this.armed = FALSE;
    if (this.rdy_replyTo) {
        deselect_slave();
        send_ADC_RDY(this.rdy_replyTo, EOK);
        this.rdy_replyTo = 0;
    } else {
        /* start a deferred read */
        this.state = BUSY;
        this.tcnt--;
        this.rcnt = 0;
        SPDR = this.buf[this.rcnt];
    }
}

/* -----------------------------------------------------
   Handle an SPI Serial Transfer Complete interrupt.

   This appears as <__vector_17>: in the .lst file.
   -----------------------------------------------------*/
ISR(SPI_STC_vect)
{
    this.buf[this.rcnt++] = SPDR;
    if (this.tcnt) {
        this.tcnt--;
        SPDR = this.buf[this.rcnt];
    } else {
        /* We arrive here on the last interrupt of the transaction.
         * The final byte has been received and there are no more bytes
         * to transmit.
         */
        deselect_slave();
        send_NOT_BUSY(SELF);

        /* Restore the /RDY detection if it was suspended. */
        if (this.armed) {
            detect_rdy(this.rdy_replyTo);
        }
    }
}

PRIVATE void detect_rdy(ProcNumber replyTo)
{
    if (this.rdy_replyTo && this.rdy_replyTo != replyTo)
        send_ADC_RDY(this.rdy_replyTo, ENODEV);
    this.rdy_replyTo = replyTo;
    this.armed = TRUE;
    clear_PC0_interrupt();
    enable_rdy_interrupt();
    select_slave();
}

PRIVATE void enable_PC0_interrupt(void)
{
    PCICR |= _BV(PCIE0);
}

PRIVATE void clear_PC0_interrupt(void)
{
    PCIFR |= _BV(PCIF0);
}

PRIVATE void enable_rdy_interrupt(void)
{
    PCMSK0 |= RDY_BIT;
}

PRIVATE void disable_rdy_interrupt(void)
{
    PCMSK0 &= ~RDY_BIT;
}

PRIVATE void select_slave(void)
{
    SPI_PORT &= ~SPI_SS;
}

PRIVATE void deselect_slave(void)
{
    SPI_PORT |= SPI_SS;
}

PRIVATE uchar_t get_width(uchar_t regno)
{
    if (regno < AD7124_REG_NO)
        return pgm_read_byte_near(reg_width + regno);
    return 0;
}

/* end code */
