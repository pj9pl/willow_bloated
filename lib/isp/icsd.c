/* isp/icsd.c */

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

/* in-circuit serial programmer SPI driver. */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "sys/defs.h"
#include "sys/ioctl.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "isp/icsd.h"

/* I am .. */
#define SELF ICSD
#define this icsd

#define TWENTYONE_MILLISECONDS 21
#define POWER_ON_DELAY TWENTYONE_MILLISECONDS /* [p.304, step 2] */

#define FIVE_MICROSECONDS 5.0
#define RESET_POSITIVE_PULSE_DURATION FIVE_MICROSECONDS /* [p.304, step 3] */

#define SPI_DDR  DDRB
#define DD_SS    _BV(DDB2)
#define DD_MOSI  _BV(DDB3)
#define DD_MISO  _BV(DDB4)
#define DD_SCK   _BV(DDB5)

#define SPI_PORT PORTB
#define SPI_SS   _BV(PORTB2)
#define SPI_MOSI _BV(PORTB3)
#define SPI_MISO _BV(PORTB4)
#define SPI_SCK  _BV(PORTB5)

typedef enum {
    IDLE = 0,
    POWERING_ON
} __attribute__ ((packed)) icsd_state;

typedef struct {
    icsd_state state;
    icsd_info *headp;
    ProcNumber replyTo;
    union {
        clk_info clk;
    } info;
} icsd_t;

/* I have .. */
static icsd_t this;

/* I can .. */
PRIVATE void set_target_reset_high(void);
PRIVATE void set_target_reset_low(void);
PRIVATE void set_miso_to_high_Z(void);
PRIVATE void set_mosi_to_low_output(void);
PRIVATE void set_mosi_to_high_Z(void);
PRIVATE void set_sck_to_low_output(void);
PRIVATE void set_sck_to_high_Z(void);

PRIVATE void switch_on(void);
PRIVATE void switch_off(void);

PRIVATE void start_job(void);
PRIVATE void resume(void);

PUBLIC uchar_t receive_icsd(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
    case NOT_BUSY:
    case REPLY_RESULT:
        if (this.state && m_ptr->RESULT == EOK) {
            resume();
        } else {
            this.state = IDLE;
            if (this.headp) {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                if ((this.headp = this.headp->nextp) != NULL) {
                    start_job();
                }
            }
        }
        break;

    case JOB:
        {
            icsd_info *ip = m_ptr->INFO;
            ip->nextp = NULL;
            ip->replyTo = m_ptr->sender;
            ip->rcnt = 0;
            if (!this.headp) {
                this.headp = ip;
                start_job();
            } else {
                icsd_info *tp;
                for (tp = this.headp; tp->nextp; tp = tp->nextp)
                    ;
                tp->nextp = ip;
            }
        }
        break;

    case SET_IOCTL:
        if (this.state == IDLE) {
            switch (m_ptr->IOCTL) {
            case SIOC_ICSD_COMMAND:
                switch (m_ptr->LCOUNT) {
                case PULSE_RESET:         /* [p.304, step 3] */
                    set_target_reset_high();
                    _delay_us(RESET_POSITIVE_PULSE_DURATION);
                    set_target_reset_low();
                    send_REPLY_RESULT(m_ptr->sender, EOK);
                    break;
                }
                break;

            case SIOC_DEVICE_POWER:
                switch (m_ptr->LCOUNT) {
                case POWER_OFF:
                    switch_off();
                    send_REPLY_RESULT(m_ptr->sender, EOK);
                    break;

                case POWER_ON:
                    this.replyTo = m_ptr->sender;
                    switch_on();
                    /* Postpone the reply until the 20ms alarm goes off. */
                    break;
                }
                break;
            }
        } else {
            send_REPLY_RESULT(m_ptr->sender, EBUSY);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/* Start the transaction. */
PRIVATE void start_job(void)
{
    if (this.state == IDLE && this.headp->tcnt) {
        this.headp->tcnt--;
        this.headp->rcnt = 0;
        SPDR = this.headp->txbuf[this.headp->rcnt];
    }
}

PRIVATE void resume(void)
{
    switch (this.state) {
    case IDLE:
        break;

    case POWERING_ON:
        /* The 20ms power-on delay has expired.
         * Inform the client.
         */
        this.state = IDLE;
        send_REPLY_RESULT(this.replyTo, EOK);
        this.replyTo = 0;
        break;
    }
}

/* -----------------------------------------------------
   Handle an SPI Serial Transfer Complete interrupt.

   Although it is listed on [p.74] as VectorNo.18,
   this appears as <__vector_17>: in the .lst file.
   -----------------------------------------------------*/
ISR(SPI_STC_vect)
{
    this.headp->rxbuf[this.headp->rcnt++] = SPDR;

    if (this.headp->tcnt) {
        this.headp->tcnt--;
        /* n.b. The txbuf[] index below is rcnt because
         * rcnt is incremented whilst tcnt is decremented.
         */ 
        SPDR = this.headp->txbuf[this.headp->rcnt];
    } else {
        /* The last interrupt of the transaction. The final byte
         * has been received and there are no more bytes to transmit.
         *
         * For LOAD and READ operations arrange the next iteration.
         * For a single byte operation send a reply.
         */
        switch (this.headp->txbuf[0]) {
        case LOAD_PROGRAM_MEMORY_PAGE:
            if (this.headp->bcnt) {
                this.headp->bcnt--;
                this.headp->txbuf[3] = *(this.headp->bp)++;
                this.headp->tcnt = ICSD_BUFLEN -1;
                this.headp->rcnt = 0;
                if (!isodd(this.headp->bcnt)) {
                    SPDR = this.headp->txbuf[0] | HIGH_BYTE;
                } else {
                    this.headp->waddr++;
                    this.headp->txbuf[2] = this.headp->waddr;
                    SPDR = this.headp->txbuf[0];
                }
            } else {
                send_NOT_BUSY(SELF);
            }
            break;

        case READ_PROGRAM_MEMORY:
            *(this.headp->bp)++ = this.headp->rxbuf[3];
            if (--this.headp->bcnt) {
                this.headp->tcnt = ICSD_BUFLEN -1;
                this.headp->rcnt = 0;
                if (isodd(this.headp->bcnt)) {
                    SPDR = this.headp->txbuf[0] | HIGH_BYTE;
                } else {
                    this.headp->waddr++;
                    this.headp->txbuf[1] = (this.headp->waddr >> 8) & 0xFF;
                    this.headp->txbuf[2] = this.headp->waddr & 0xFF;
                    SPDR = this.headp->txbuf[0];
                }
            } else {
                send_NOT_BUSY(SELF);
            }
            break;

        case READ_EEPROM_MEMORY:
            *(this.headp->bp)++ = this.headp->rxbuf[3];
            if (--this.headp->bcnt) {
                this.headp->tcnt = ICSD_BUFLEN -1;
                this.headp->rcnt = 0;
                this.headp->waddr++;
                this.headp->txbuf[1] = (this.headp->waddr >> 8) & 0xFF;
                this.headp->txbuf[2] = this.headp->waddr & 0xFF;
                SPDR = this.headp->txbuf[0];
            } else {
                send_NOT_BUSY(SELF);
            }
            break;

        default:
           /* A single byte operation is finished
            * after a single transaction.
            */
            this.state = IDLE;
            send_NOT_BUSY(SELF);
            break;
        }
    }
}

PRIVATE void switch_on(void)
{
    if (PRR & _BV(PRSPI)) {
        /* Restore MISO to an input before the SPI is enabled. */
        set_target_reset_low();
        set_miso_to_high_Z();
        set_mosi_to_low_output();
        set_sck_to_low_output();
        /* Vcc has already been applied, provide a high level on the reset pin
         * for at least two cycles after SCK has been set low. [p.304]
         */
        set_target_reset_high();
        set_target_reset_high();
        set_target_reset_high();
        set_target_reset_low();

        /* Configure the SPI [p.174-7]
         * SCK rate set to slowest possible speed: F_CPU/128 (86400 @ 11.06MHz)
         */
        PRR &= ~_BV(PRSPI);
        SPCR = _BV(SPIE) | _BV(SPE) | _BV(MSTR) | _BV(SPR1) | _BV(SPR0);

        /* Request a 20ms delay [p.304] */
        this.state = POWERING_ON;
        sae_CLK_SET_ALARM(this.info.clk, POWER_ON_DELAY);
    } else {
        send_REPLY_RESULT(this.replyTo, EBUSY);
    }
}

PRIVATE void switch_off(void)
{
    /* disable SPI */
    SPCR = 0;
    PRR |= _BV(PRSPI);

    /* set the pins low and release the reset */
    set_mosi_to_high_Z();
    set_sck_to_high_Z();
    set_miso_to_high_Z();
    set_target_reset_high();
}

PRIVATE void set_target_reset_high(void)
{
    /* Soft pullup. */
    SPI_DDR &= ~SPI_SS;
    SPI_PORT |= SPI_SS;
}

PRIVATE void set_target_reset_low(void)
{
    /* Low-Z zero output.
     * Enables the SPI as SS must
     * be configured as an output.
     */
    SPI_PORT &= ~SPI_SS;
    SPI_DDR |= SPI_SS;
}

PRIVATE void set_miso_to_high_Z(void)
{
    SPI_PORT &= ~SPI_MISO;
    SPI_DDR &= ~SPI_MISO;
}

PRIVATE void set_mosi_to_low_output(void)
{
    SPI_PORT &= ~SPI_MOSI;
    SPI_DDR |= SPI_MOSI;
}

PRIVATE void set_mosi_to_high_Z(void)
{
    SPI_PORT &= ~SPI_MOSI;
    SPI_DDR &= ~SPI_MOSI;
}

PRIVATE void set_sck_to_low_output(void)
{
    SPI_PORT &= ~SPI_SCK;
    SPI_DDR |= SPI_SCK;
}

PRIVATE void set_sck_to_high_Z(void)
{
    SPI_PORT &= ~SPI_SCK;
    SPI_DDR &= ~SPI_SCK;
}

/* end code */
