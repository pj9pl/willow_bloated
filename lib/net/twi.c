/* net/twi.c */
 
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

/* Two wire interface.
 *
 * see also ATmega328P datasheet DS40002061A [p.215-242]
 * and Philips/NXP I2C specification and user manual. UM10204.pdf
 *
 * The device has two principal modes of operation in Master and Slave,
 * each able to receive and transmit.
 *
 * Slave mode is the quiescent condition when a client has registered to
 * receive an incoming message. A secretary typically registers a Service
 * Number at boot, and re-register after each subsequent reply.
 *
 * A secretary typically receives from ANY host:task, and replies to the
 * host:task:jobref that the client provides in the request. This allows
 * each task to perform any operation.
 *
 * Where there are no clients registered to receive incoming messages the
 * address acknowledgement is disabled. A remote master sending to this
 * address would generate an ENODEV (19) error.
 *
 * Servicing a call requires that an scmd matches that which the remote master
 * sent as the mcmd. If no match is found within the pool the transaction is
 * terminated and the remote master generates an EACCES (13) error.
 *
 *
 * Master mode is an active transitory condition caused by an internal
 * JOB request message being received, where the mode includes MT or MR.
 * Entering Master mode may involve several attempts. Slave mode is
 * suspended during Master mode.
 *
 * ---------------------------------------------------------------------------
 *
 * [Errata p.654] describes a 'TWI Data setup time can be too short' problem,
 * suggesting a workaround by inserting a delay between setting TWDR and
 * TWCR. [p.317-8] lists the twi electrical characteristics.
 *
 * Waiting for a quiet bus is key to a successful transmission as this ensures
 * a clean STOP condition that can persist beyond the minimum bus free period
 * of 4.7us.
 *
 * Any competing master will also start at approximately the same time, since
 * they would have detected traffic had they been much later, and vice-versa.
 * This makes arbitration more likely, with the losing master generating a
 * TW_MT_ARB_LOST status which signifies that the transmission can be retried.
 *
 * Hosts and services are not always available. A TW_MT_SLA_NACK status is
 * generated when a host does not acknowledge its slave address.
 * A TW_MT_DATA_NACK status is generated when a service is unavailable.
 * This may be spurious or persistant. The number of attempts to send is
 * limited to MAX_NACK_RETRIES, before either ENODEV where the host was
 * unavailable or EACCES where the service was unavailable is returned to
 * the client.
 */

#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/twi.h>
#include <util/delay.h>

#include "sys/defs.h"
#include "sys/msg.h"
#include "sys/clk.h"
#include "net/i2c.h"
#include "net/twi.h"

/* I am .. */
#define SELF TWI
#define this twi

#define BASIC_COMMAND          (_BV(TWINT) | _BV(TWEN) | _BV(TWIE))

/* for the master */
#define START_COMMAND          (BASIC_COMMAND | _BV(TWSTA))
#define STOP_COMMAND           (BASIC_COMMAND | _BV(TWSTO))

/* for the slave */
#define CONTINUE_COMMAND       (BASIC_COMMAND | _BV(TWEA))
#define DISCONTINUE_COMMAND    BASIC_COMMAND
#define DISCONNECT_COMMAND     _BV(TWINT)

/* set the bus clock frequency for master mode transactions */
#define TWI_FREQ               100000  /* normal speed */

/* Identify the sda pins. Use it for PORTC and DDRC */
#define SDA_PIN                PINC4
#define SCL_PIN                PINC5

/* The number of times to check that the bus is quiet.
 * A quiet bus has both SCL and SDA consistantly high.
 * A value of 3 has been deemed insufficient.
 * 10 times equates to ~10us @ 8MHz.
 */
#define QUIESCENT_CHECKS         ((uchar_t)((F_CPU / 800000UL) & 0xFF))
#define TWI_PINS                 (_BV(SCL_PIN) | _BV(SDA_PIN))

/* SET_ALARM uval constants */
#define ONE_HUNDRED_MILLISECONDS 100

#define TRANSMIT_DELAY           ONE_HUNDRED_MILLISECONDS
#define RETRY_DELAY              ONE_HUNDRED_MILLISECONDS
#define ARBITRATION_DELAY        ONE_HUNDRED_MILLISECONDS

/* _delay_us() constants */
#define TWO_HUNDRED_NANOSECONDS  0.2

#define DATA_SETUP_TIME          TWO_HUNDRED_NANOSECONDS 

/* The R/W bit in the SLA-R/W byte */
#define WRITE_MODE               0
#define READ_MODE                1

#define MAX_NACK_RETRIES         7

/* bus busy */
#define MAX_TRANSMIT_ATTEMPTS    50 /* 50 x 100ms = 5s */

/* four byte command */
#define FBC    (sizeof(Service) + sizeof(ProcNumber) + sizeof(jobref_t))

typedef void (*PTF_void) (void);

typedef enum {
    PRESCALE_ONE = 0,
    PRESCALE_FOUR,
    PRESCALE_SIXTEEN,
    PRESCALE_SIXTYFOUR
} prescalevalues;

typedef enum {
    IDLE = 0,
    STARTING,
    MASTERING,
    SLAVING
} __attribute__ ((packed)) state_t;

typedef struct {
    state_t state;
    unsigned alarm_pending : 1;
    twi_info *headp;
    twi_info *pool;
    twi_info *slavep;
    uchar_t *tptr;
    ushort_t tcnt;
    uchar_t gc_tally;
    clk_info clk;
    uchar_t nack_retries;
    uchar_t transmit_attempts;
    uchar_t fbc_buf[FBC];
    uchar_t fbc_count;
} twi_t;

/* I have .. */
static twi_t this;

/* I can .. */
PRIVATE void start_job(void);
PRIVATE uchar_t cancel_job(twi_info *ip);
PRIVATE twi_info *scan_pool(Service num);

PRIVATE void tw_bus_error(void);
PRIVATE void tw_start(void);
PRIVATE void tw_rep_start(void);
PRIVATE void tw_mt_sla_ack(void);
PRIVATE void tw_mt_sla_nack(void);
PRIVATE void tw_mt_data_ack(void);
PRIVATE void tw_mt_data_nack(void);
PRIVATE void tw_mt_arb_lost(void);
PRIVATE void tw_mr_sla_ack(void);
PRIVATE void tw_mr_sla_nack(void);
PRIVATE void tw_mr_data_ack(void);
PRIVATE void tw_mr_data_nack(void);
PRIVATE void tw_sr_sla_ack(void);
PRIVATE void tw_sr_arb_lost_sla_ack(void);
PRIVATE void tw_sr_gcall_ack(void);
PRIVATE void tw_sr_arb_lost_gcall_ack(void);
PRIVATE void tw_sr_data_ack(void);
PRIVATE void tw_sr_data_nack(void);
PRIVATE void tw_sr_gcall_data_ack(void);
PRIVATE void tw_sr_gcall_data_nack(void);
PRIVATE void tw_sr_stop(void);
PRIVATE void tw_st_sla_ack(void);
PRIVATE void tw_st_arb_lost_sla_ack(void);
PRIVATE void tw_st_data_ack(void);
PRIVATE void tw_st_data_nack(void);
PRIVATE void tw_st_last_data(void);
PRIVATE void tw_no_info(void);

PUBLIC void config_twi(void)
{
    PRR &= ~_BV(PRTWI);
    /* Initialize twi prescaler and bit rate register [p.222,239,241]
     * SCL Freq = F_CPU / (16 + 2 * TWBR * PrescalerValue))
     * 100000 = 8000000 / (16 + 2 * 32 * 1)
     * 32 = (8000000 / 100000 - 16) / 2
     *
     * For 11.0592 MHz clock:-
     * 47 = (11059200 / 100538 - 16) / 2
     */
    TWSR = PRESCALE_ONE;
    TWBR = (F_CPU / TWI_FREQ - 16) / 2;
    TWAR = HOST_ADDRESS;
}

PUBLIC uchar_t receive_twi(message *m_ptr)
{
    switch (m_ptr->opcode) {
    case ALARM:
        this.alarm_pending = FALSE;
        if (this.headp)
            start_job();
        break;

    case MASTER_COMPLETE:
        switch (m_ptr->RESULT) {
        case EOK:
            /* Remove the info from the job queue.
             * If the mode specifies TWI_SR and the m_ptr->RESULT is EOK
             * then it is appended to the slave list, else notify the
             * caller that the job has completed.
             */
            this.nack_retries = 0;
            if (this.headp->mode & TWI_SR) {
                twi_info *ip = this.headp;
                this.headp = this.headp->nextp;
                ip->nextp = NULL;

                if (!this.pool) {
                    this.pool = ip;
                } else {
                    twi_info *tp;
                    for (tp = this.pool; tp->nextp; tp = tp->nextp) {
                        ;
                    }
                    tp->nextp = ip;
                }

                if (ip->mode & TWI_GC) {
                    this.gc_tally++;
                    TWAR |= _BV(TWGCE);
                }

            } else {
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                this.headp = this.headp->nextp;
            }
            break;

        case ENODEV: /* TW_MT_SLA_NACK: slave didn't acknowledge */
        case EACCES: /* TW_MT_DATA_NACK: service not available */
            if (this.nack_retries++ < MAX_NACK_RETRIES) {
                if (this.alarm_pending == FALSE) {
                    this.alarm_pending = TRUE;
                    sae_CLK_SET_ALARM(this.clk, RETRY_DELAY);
                }
            } else {
                this.nack_retries = 0;
                send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
                this.headp = this.headp->nextp;
            }
            break;

        case EAGAIN: /* TW_MT_ARB_LOST: try again */
            if (this.alarm_pending == FALSE) {
                this.alarm_pending = TRUE;
                sae_CLK_SET_ALARM(this.clk, ARBITRATION_DELAY); 
            }
            break;

        default:     /* EINVAL,ENXIO */
            send_REPLY_INFO(this.headp->replyTo, m_ptr->RESULT, this.headp);
            this.headp = this.headp->nextp;
            break;
        }
        this.state = IDLE;

        if (this.alarm_pending == FALSE && this.headp) {
            start_job();
        } else {
            TWCR = this.pool ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
        }
        break;

    case SLAVE_COMPLETE:
        /* We arrive here after a slave transaction has completed. */
        if (this.slavep) {
            if (this.slavep->mode & TWI_GC && --this.gc_tally == 0) {
                TWAR &= ~_BV(TWGCE);
            }
            if (this.pool == this.slavep) {
                /* It's at the head of the list. */
                this.pool = this.slavep->nextp;
            } else {
                for (twi_info *ip = this.pool; ip; ip = ip->nextp) {
                    if (ip->nextp == this.slavep) {
                        ip->nextp = this.slavep->nextp;
                        break;
                    }
                }
            }

            send_REPLY_INFO(this.slavep->replyTo, m_ptr->RESULT, this.slavep);
            this.slavep = NULL;
        }

        this.state = IDLE;

        if (this.alarm_pending == FALSE && this.headp) {
            start_job();
        } else {
            TWCR = this.pool ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
        }
        break;

    case JOB:
        {
            twi_info *ip = m_ptr->INFO;
            ip->replyTo = m_ptr->sender;
            ip->nextp = NULL;
            if (ip->mode & TWI_MT) {
                if (!this.headp) {
                    this.headp = ip;
                    start_job();
                } else {
                    twi_info *tp;
                    for (tp = this.headp; tp->nextp; tp = tp->nextp)
                        ;
                    tp->nextp = ip;
                }
            } else if (ip->mode & TWI_SR) {
                if (!this.pool) {
                    this.pool = ip;
                } else {
                    twi_info *tp;
                    for (tp = this.pool; tp->nextp; tp = tp->nextp) {
                        ;
                    }
                    tp->nextp = ip;
                }

                if (ip->mode & TWI_GC) {
                    this.gc_tally++;
                    TWAR |= _BV(TWGCE);
                }

                if (this.state == IDLE) {
                    TWCR = this.pool ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
                }
            } else {
                /* where there is neither MT nor SR phase */
                send_REPLY_INFO(ip->replyTo, EINVAL, ip);
            }
        }
        break;

    case CANCEL:
        {
            uchar_t ret;
            /* Extract this job from whichever queue it is in. */
            cli();
            ret = cancel_job(m_ptr->INFO);
            sei();
            send_REPLY_INFO(m_ptr->sender, ret, m_ptr->INFO);
        }
        break;

    default:
        return ENOMSG;
    }
    return EOK;
}

/*
 * The master must not proceed until the slave is idle and address
 * acknowledgement is disabled and the SCL and SDA pins have been
 * read as high QUIESCENT_CHECKS in succession, indicating that the
 * bus is idle.
 */
PRIVATE void start_job(void)
{
    /* Turn interrupts off to prevent an incoming transmission from
     * setting the state from IDLE to SLAVING before we get to set
     * it from IDLE to STARTING.
     */
    cli();
    if (this.state == IDLE) {
        /* stop acknowledging the slave address */
        TWCR &= ~_BV(TWEA);
        this.state = STARTING;
    }
    sei();

    if (this.state == STARTING) {
        if (this.headp->dest_addr == HOST_ADDRESS) {
            /* loopback */
            if ((this.slavep = scan_pool(this.headp->mcmd)) != NULL) {
                ushort_t len = MIN(this.slavep->rcnt, this.headp->tcnt);
                memcpy(this.slavep->rptr, this.headp->tptr, len);
                this.slavep->rcnt -= len;
                if (this.headp->mode & TWI_MR && this.slavep->mode & TWI_ST) {
                    if (this.slavep->st_callback)
                        (this.slavep->st_callback) (this.slavep);
                    len = MIN(this.slavep->tcnt, this.headp->rcnt);
                    memcpy(this.headp->rptr, this.slavep->tptr, len);
                    this.headp->rcnt -= len;
                }
                send_MASTER_COMPLETE(EOK);
                send_SLAVE_COMPLETE(EOK);
            } else {
                this.nack_retries = MAX_NACK_RETRIES;
                send_MASTER_COMPLETE(EACCES);
            }
        } else {
            /* Preserve the client values to allow
             * the transfer to be reattempted if need be.
             */
            this.tptr = this.headp->tptr;
            this.tcnt = this.headp->tcnt;

            /* Sample the pins a number of times to detect any traffic.
             * Relinquish at first sign of activity.
             */
            cli();
            for (uchar_t i = QUIESCENT_CHECKS; i; i--) {
                if ((PINC & TWI_PINS) != TWI_PINS) {
                    /* acknowledge the slave address whilst we wait. */
                    this.state = IDLE;
                    TWCR = this.pool ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
                    sei();
                    if (++this.transmit_attempts == MAX_TRANSMIT_ATTEMPTS) {
                        this.transmit_attempts = 0;
                        send_MASTER_COMPLETE(EHOSTDOWN);
                    } else if (this.alarm_pending == FALSE) {
                        this.alarm_pending = TRUE;
                        sae_CLK_SET_ALARM(this.clk, TRANSMIT_DELAY);
                    }
                    return;
                }
            }
            /* proceed */
            TWCR = START_COMMAND;
            sei();
            this.transmit_attempts = 0;
        }
    }
}

/* cancel job
 *
 * Disengage a client's twi_info from either the job queue or the pool.
 *
 * sae_TWI_CANCEL() should reference the same twi_info that send_JOB()
 * referenced previously, as it is the addresses that are being compared.
 *
 * @return EOK if it has been found and released.
 *         EBUSY if it is in use.
 *         ESRCH if it is not found.
 *
 */
PRIVATE uchar_t cancel_job(twi_info *ip)
{
    if (this.headp && (ip->mode & TWI_MT)) {
        if (this.headp == ip) {
            if (this.state != IDLE) {
                return EBUSY;
            } else {
                this.headp = ip->nextp;
                return EOK;
            }
        } else {
            for (twi_info *tp = this.headp; tp->nextp; tp = tp->nextp) {
                if (tp->nextp == ip) {
                    tp->nextp = ip->nextp;
                    return EOK;
                }
            }
        }
    }

    if (this.pool && (ip->mode & TWI_SR)) {
        if (this.slavep == ip) {
            return EBUSY;
        } else {
            if (this.pool == ip) {
                this.pool = ip->nextp;
                return EOK;
            } else {
                for (twi_info *tp = this.pool; tp->nextp; tp = tp->nextp) {
                    if (tp->nextp == ip) {
                        tp->nextp = ip->nextp;
                        return EOK;
                    }
                }
            }
        }
    }
    return ESRCH;
}

PRIVATE twi_info *scan_pool(Service num)
{
    twi_info *ip = this.pool; 
    for (; ip; ip = ip->nextp) {
        if (ip->scmd == num)
            return ip;
    }
    return NULL;
}

/* -----------------------------------------------------
   Handle a TWI interrupt.
   This appears as <__vector_24>: in the .lst file.
   -----------------------------------------------------*/

ISR(TWI_vect)
{
#define _IV(c) ((c) >> 3)
    PRIVATE const PTF_void __flash functab_[] = {
        [_IV(TW_BUS_ERROR)]             = tw_bus_error,
        [_IV(TW_START)]                 = tw_start,
        [_IV(TW_REP_START)]             = tw_rep_start,
        [_IV(TW_MT_SLA_ACK)]            = tw_mt_sla_ack,
        [_IV(TW_MT_SLA_NACK)]           = tw_mt_sla_nack,
        [_IV(TW_MT_DATA_ACK)]           = tw_mt_data_ack,
        [_IV(TW_MT_DATA_NACK)]          = tw_mt_data_nack,
        [_IV(TW_MT_ARB_LOST)]           = tw_mt_arb_lost,
        [_IV(TW_MR_SLA_ACK)]            = tw_mr_sla_ack,
        [_IV(TW_MR_SLA_NACK)]           = tw_mr_sla_nack,
        [_IV(TW_MR_DATA_ACK)]           = tw_mr_data_ack,
        [_IV(TW_MR_DATA_NACK)]          = tw_mr_data_nack,
        [_IV(TW_SR_SLA_ACK)]            = tw_sr_sla_ack,
        [_IV(TW_SR_ARB_LOST_SLA_ACK)]   = tw_sr_arb_lost_sla_ack,
        [_IV(TW_SR_GCALL_ACK)]          = tw_sr_gcall_ack,
        [_IV(TW_SR_ARB_LOST_GCALL_ACK)] = tw_sr_arb_lost_gcall_ack,
        [_IV(TW_SR_DATA_ACK)]           = tw_sr_data_ack,
        [_IV(TW_SR_DATA_NACK)]          = tw_sr_data_nack,
        [_IV(TW_SR_GCALL_DATA_ACK)]     = tw_sr_gcall_data_ack,
        [_IV(TW_SR_GCALL_DATA_NACK)]    = tw_sr_gcall_data_nack,
        [_IV(TW_SR_STOP)]               = tw_sr_stop,
        [_IV(TW_ST_SLA_ACK)]            = tw_st_sla_ack,
        [_IV(TW_ST_ARB_LOST_SLA_ACK)]   = tw_st_arb_lost_sla_ack,
        [_IV(TW_ST_DATA_ACK)]           = tw_st_data_ack,
        [_IV(TW_ST_DATA_NACK)]          = tw_st_data_nack,
        [_IV(TW_ST_LAST_DATA)]          = tw_st_last_data,
        [_IV(TW_NO_INFO)]               = tw_no_info
    };
    PTF_void f_ptr = (PTF_void) pgm_read_word_near(functab_ + _IV(TW_STATUS));
    if (f_ptr)
        (f_ptr) ();
}

PRIVATE void tw_bus_error(void)
{
    /* A: illegal start or stop condition [0x00] */
    TWCR = STOP_COMMAND;
    if (this.state == SLAVING)
        send_SLAVE_COMPLETE(ECONNABORTED);
    else
        send_MASTER_COMPLETE(ECONNREFUSED);
}

/* -----------------------------------------------------
           Master Transmitter and Receiver
   ----------------------------------------------------- */

PRIVATE void tw_start(void)
{
    /* B: start condition transmitted [0x08] */
    this.state = MASTERING;
    TWDR = this.headp->dest_addr | WRITE_MODE;
    _delay_us(DATA_SETUP_TIME);
    TWCR = CONTINUE_COMMAND;
}

PRIVATE void tw_rep_start(void)
{
    /* C: repeated start condition transmitted [0x10] */
    TWDR = this.headp->dest_addr | READ_MODE;
    _delay_us(DATA_SETUP_TIME);
    TWCR = CONTINUE_COMMAND;
}

/* -----------------------------------------------------
         Master Transmitter -[p.227-8]
   ----------------------------------------------------- */
PRIVATE void tw_mt_sla_ack(void)
{
    /* D: SLA+W transmitted, ACK received [0x18]
     * Slave has acknowledged its existance.
     */
    TWDR = this.headp->mcmd;
    _delay_us(DATA_SETUP_TIME);
    TWCR = CONTINUE_COMMAND;
}

PRIVATE void tw_mt_sla_nack(void)
{
    /* E: SLA+W transmitted, NACK received [0x20]
     * Slave didn't acknowledge its address: No such device.
     * Send STOP condition.
     */
    TWCR = STOP_COMMAND;
    send_MASTER_COMPLETE(ENODEV); /* No such device: 19 */
}

PRIVATE void tw_mt_data_ack(void)
{
    /* F: data transmitted, ACK received [0x28]
     * Slave acknowledges the previous byte and invites another.
     */
    if (this.tcnt) {
        this.tcnt--;
        TWDR = *this.tptr++;
        _delay_us(DATA_SETUP_TIME);
        TWCR = CONTINUE_COMMAND;
    } else {
        /* The slave (subroutine U:) cannot differentiate
         * between STOP and REPEATED START
         */
        if (this.headp->mode & TWI_MR && this.headp->rcnt) {
            /* send repeated start */
            TWCR = START_COMMAND;
        } else {
            /* send stop condition */
            TWCR = STOP_COMMAND;
            send_MASTER_COMPLETE(EOK);
        }
    }
}

PRIVATE void tw_mt_data_nack(void)
{
    /* G: Data transmitted, NACK received [0x30]
     * Slave cannot accept data at this time.
     * Send stop condition.
     */
    TWCR = STOP_COMMAND;
    send_MASTER_COMPLETE(EACCES);  /* Permission denied: 13 */
}

PRIVATE void tw_mt_arb_lost(void)
{
    /* H: Arbitration lost in SLA+W or data bytes [0x38] */
    TWCR = DISCONTINUE_COMMAND;
    send_MASTER_COMPLETE(EAGAIN);  /* Try again: 11 */
}

/* -----------------------------------------------------
        Master Receiver -[p.230]
   ----------------------------------------------------- */
PRIVATE void tw_mr_sla_ack(void)
{
    /* I: SLA+R transmitted, ACK received [0x40] */
    TWCR = CONTINUE_COMMAND;
}

PRIVATE void tw_mr_sla_nack(void)
{
    /* J: SLA+R transmitted, NACK received [0x48]
     * Slave not found.
     */
    TWCR = STOP_COMMAND;
    send_MASTER_COMPLETE(ENODEV); /* No such device: 19 */
}

PRIVATE void tw_mr_data_ack(void)
{
    /* K: data received, ACK returned [0x50] */
    if (this.headp->rcnt) {
        this.headp->rcnt--;
        *this.headp->rptr++ = TWDR;
        TWCR = CONTINUE_COMMAND;
    } else {
        TWCR = DISCONTINUE_COMMAND;
    }
}

PRIVATE void tw_mr_data_nack(void)
{
     /* L: data received, NACK returned [0x58] */
    if (this.headp->rcnt) {
        this.headp->rcnt--;
        *this.headp->rptr++ = TWDR;
    }
    /* slave empty */
    /* this.headp->rcnt indicates by how much the request falls short. */
    TWCR = STOP_COMMAND;
    send_MASTER_COMPLETE(EOK);
}

/* -----------------------------------------------------
             Slave Receiver -[p.233]
   ----------------------------------------------------- */
PRIVATE void tw_sr_sla_ack(void)
{
    /* M: SLA+W received, ACK returned [0x60] */
    if (this.state == IDLE) {
        this.state = SLAVING;
        TWCR = CONTINUE_COMMAND;
    } else {
        TWCR = DISCONTINUE_COMMAND;
    }
}

PRIVATE void tw_sr_arb_lost_sla_ack(void)
{
    /* N: arbitration lost in SLA+RW, SLA+W received, ACK returned [0x68] */
    if (this.state == IDLE) {
        this.state = SLAVING;
        TWCR = CONTINUE_COMMAND;
    } else {
        TWCR = DISCONTINUE_COMMAND;
    }
}

PRIVATE void tw_sr_gcall_ack(void)
{
    /* O: general call received, ACK returned [0x70] */
    if (this.state == IDLE) {
        this.state = SLAVING;
        TWCR = CONTINUE_COMMAND;
    } else {
        TWCR = DISCONTINUE_COMMAND;
    }
}

PRIVATE void tw_sr_arb_lost_gcall_ack(void)
{
    /* P: arbitration lost in SLA+RW,
     * general call received, ACK returned [0x78]
     */
    if (this.state == IDLE) {
        this.state = SLAVING;
        TWCR = CONTINUE_COMMAND;
    } else {
        TWCR = DISCONTINUE_COMMAND;
    }
}

PRIVATE void tw_sr_data_ack(void)
{
    /* Q: data received, ACK returned [0x80] */
    if (!this.slavep) {
        if (this.fbc_count < FBC) {
            this.fbc_buf[this.fbc_count++] = TWDR;
            TWCR = CONTINUE_COMMAND;
            if (this.fbc_count < FBC)
                return;
            /* Find a slave that matches the received four byte command. */
            /* search for a specific listener */
            for (twi_info *ip = this.pool; ip; ip = ip->nextp) {
                if (ip->scmd == this.fbc_buf[0] &&
                       memcmp(ip->rptr, this.fbc_buf +1, FBC -1) == 0) {
                    ip->rptr += FBC -1;
                    ip->rcnt -= FBC -1;
                    this.slavep = ip;
                    TWCR = CONTINUE_COMMAND;
                    break;
                }
            }
            if (!this.slavep) {
                /* search for ANY listener */
                for (twi_info *ip = this.pool; ip; ip = ip->nextp) {
                    if (ip->scmd == this.fbc_buf[0] && ip->rptr[0] == ANY) {
                        memcpy(ip->rptr, this.fbc_buf +1, FBC -1);
                        ip->rptr += FBC -1;
                        ip->rcnt -= FBC -1;
                        this.slavep = ip;
                        TWCR = CONTINUE_COMMAND;
                        break;
                    }
                }
            }
        }
        this.fbc_count = 0;
        if (!this.slavep) {
            /* A counterpart slave was not found.
             * Terminate the transaction.
             * Send a completion message to the process context
             * to issue a CONTINUE_COMMAND.
             */
            TWCR = DISCONNECT_COMMAND;
            send_SLAVE_COMPLETE(EBADRQC); /* Invalid request code: 56 */
        }
    } else {
        if (this.slavep->rcnt) {
            this.slavep->rcnt--;
            *this.slavep->rptr++ = TWDR;
            TWCR = this.slavep->rcnt ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
        } else {
            TWCR = DISCONTINUE_COMMAND;
            send_SLAVE_COMPLETE(EBADE); /* Invalid exchange: 52 */
        }
    }
}

PRIVATE void tw_sr_data_nack(void)
{
    /* R: data received, NACK returned [0x88] */
    TWCR = DISCONTINUE_COMMAND;
    send_SLAVE_COMPLETE(EBUSY); /* Device or resource busy: 16 */
}

PRIVATE void tw_sr_gcall_data_ack(void)
{
    /* S: general call data received, ACK returned [0x90] */
    if (!this.slavep) {
        if (this.fbc_count < FBC) {
            this.fbc_buf[this.fbc_count++] = TWDR;
            TWCR = CONTINUE_COMMAND;
            if (this.fbc_count < FBC)
                return;
            /* Find a slave that matches the received four byte command. */
            /* search for a specific GC listener */
            for (twi_info *ip = this.pool; ip; ip = ip->nextp) {
                if ((ip->mode & TWI_GC) && ip->scmd == this.fbc_buf[0] &&
                       memcmp(ip->rptr, this.fbc_buf +1, FBC -1) == 0) {
                    ip->rptr += FBC -1;
                    ip->rcnt -= FBC -1;
                    this.slavep = ip;
                    TWCR = CONTINUE_COMMAND;
                    break;
                }
            }
            if (!this.slavep) {
                /* search for ANY GC listener */
                for (twi_info *ip = this.pool; ip; ip = ip->nextp) {
                    if ((ip->mode & TWI_GC) && ip->scmd == this.fbc_buf[0] &&
                           ip->rptr[0] == ANY) {
                        memcpy(ip->rptr, this.fbc_buf +1, FBC -1);
                        ip->rptr += FBC -1;
                        ip->rcnt -= FBC -1;
                        this.slavep = ip;
                        TWCR = CONTINUE_COMMAND;
                        break;
                    }
                }
            }
        }
        this.fbc_count = 0;
        if (!this.slavep) {
            /* An appropriate slave was not found.
             * Terminate the transaction.
             * Send a message to the process context to reset the conditions.
             */
            TWCR = DISCONNECT_COMMAND;
            send_SLAVE_COMPLETE(EBADRQC);
        }
    } else {
        if (this.slavep->rcnt) {
            this.slavep->rcnt--;
            *this.slavep->rptr++ = TWDR;
            TWCR = this.slavep->rcnt ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
        } else {
            TWCR = DISCONTINUE_COMMAND;
            send_SLAVE_COMPLETE(EBADE); /* Invalid exchange: 52 */
        }
    }
}

PRIVATE void tw_sr_gcall_data_nack(void)
{
    /* T: general call data received, NACK returned [0x98] */
    TWCR = DISCONTINUE_COMMAND;
    send_SLAVE_COMPLETE(EBUSY); /* Device or resource busy: 16 */
}

PRIVATE void tw_sr_stop(void)
{
    /* U: stop or repeated start condition received while selected [0xA0] */
     if (this.slavep->mode & TWI_ST) {
        TWCR = CONTINUE_COMMAND;
    } else {
        TWCR = DISCONTINUE_COMMAND;
        send_SLAVE_COMPLETE(EOK);
    }
}

/* -----------------------------------------------------
      Slave Transmitter -[p.236]
   ----------------------------------------------------- */
PRIVATE void tw_st_sla_ack(void)
{
    /* V: Own SLA+R has been received; ACK has ben returned [0xA8] */
    tw_st_arb_lost_sla_ack();
}

PRIVATE void tw_st_arb_lost_sla_ack(void)
{
    if (this.slavep->mode & TWI_ST) {
        /* W: arbitration lost in SLA+RW, SLA+R received, ACK returned [0xB0] */
        if (this.slavep->rcnt == 0) {
            /* Refer back to the slave as to how we set the
             * transmit pointer and count from the received data.
             * N.B. rcnt is zero, and rptr is at the end of the data.
             */ 
            if (this.slavep->st_callback) {
                (this.slavep->st_callback) (this.slavep);
            }
        } else {
            TWDR = 0;
            _delay_us(DATA_SETUP_TIME);
            TWCR = DISCONTINUE_COMMAND;
            return;
        }
    } else {
        /* Slave does not have TWI_ST mode set. */
        TWDR = 0;
        _delay_us(DATA_SETUP_TIME);
        TWCR = DISCONTINUE_COMMAND;
        return;
    }
    tw_st_data_ack();
}

PRIVATE void tw_st_data_ack(void)
{
    /* X: data transmitted, ACK received [0xB8] */
    TWDR = *this.slavep->tptr++;
    this.slavep->tcnt--;
    _delay_us(DATA_SETUP_TIME);
    TWCR = this.slavep->tcnt ? CONTINUE_COMMAND : DISCONTINUE_COMMAND;
}

PRIVATE void tw_st_data_nack(void)
{
    /* Y: data transmitted, NACK received [0xC0] */
    TWCR = DISCONTINUE_COMMAND;
    send_SLAVE_COMPLETE(EOK);
}

PRIVATE void tw_st_last_data(void)
{
    /* Z: last data byte transmitted, ACK received [0xC8] */
    TWCR = DISCONTINUE_COMMAND;
    send_SLAVE_COMPLETE(EOK);
}

/* -----------------------------------------------------
        Miscellaneous States  -[p.237]
   ----------------------------------------------------- */

PRIVATE void tw_no_info(void)
{
    TWCR = (1 << TWINT) | (1 << TWSTO);
}

/* -----------------------------------------------------
            convenience functions
   ----------------------------------------------------- */

/* These functions are an alternative to the send_JOB() macro in
 * msg.h and assume that send_m3() is used to convey JOB requests.
 */

PUBLIC void send_TWI_MT(ProcNumber sender, twi_info *cp, hostid_t dest_addr,
                              uchar_t mcmd, void *tptr, ushort_t tcnt)
{
    cp->dest_addr = dest_addr;
    cp->mcmd = mcmd;
    cp->tptr = tptr;
    cp->tcnt = tcnt;
    cp->mode = TWI_MT;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_MTMR(ProcNumber sender, twi_info *cp, hostid_t dest_addr,
                              uchar_t mcmd, void *tptr, ushort_t tcnt,
                              void *rptr, ushort_t rcnt)
{
    cp->dest_addr = dest_addr;
    cp->mcmd = mcmd;
    cp->tptr = tptr;
    cp->tcnt = tcnt;
    cp->rptr = rptr;
    cp->rcnt = rcnt;
    cp->mode = TWI_MT | TWI_MR;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_MR(ProcNumber sender, twi_info *cp, hostid_t dest_addr,
                                    uchar_t mcmd, void *rptr, ushort_t rcnt)
{
    cp->dest_addr = dest_addr;
    cp->mcmd = mcmd;
    cp->tcnt = 0;
    cp->rptr = rptr;
    cp->rcnt = rcnt;
    cp->mode = TWI_MT | TWI_MR;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_MTSR(ProcNumber sender, twi_info *cp, hostid_t dest_addr,
                                    uchar_t mcmd, void *tptr, ushort_t tcnt,
                                    uchar_t scmd, void *rptr, ushort_t rcnt)
{
    cp->dest_addr = dest_addr;
    cp->mcmd = mcmd;
    cp->tptr = tptr;
    cp->tcnt = tcnt;
    cp->scmd = scmd;
    cp->rptr = rptr;
    cp->rcnt = rcnt;
    cp->mode = TWI_MT | TWI_SR;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_GCSR(ProcNumber sender, twi_info *cp, uchar_t scmd,
                                                void *rptr, ushort_t rcnt)
{
    cp->scmd = scmd;
    cp->rptr = rptr;
    cp->rcnt = rcnt;
    cp->mode = TWI_GC | TWI_SR;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_SR(ProcNumber sender, twi_info *cp, uchar_t scmd,
                                               void *rptr, ushort_t rcnt)
{
    cp->scmd = scmd;
    cp->rptr = rptr;
    cp->rcnt = rcnt;
    cp->mode = TWI_SR;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_SRST(ProcNumber sender, twi_info *cp, uchar_t scmd,
                             void *rptr, ushort_t rcnt, Callback callback)
{
    cp->scmd = scmd;
    cp->rptr = rptr;
    cp->rcnt = rcnt;
    cp->st_callback = callback;
    cp->mode = TWI_SR | TWI_ST;
    send_m3(sender, SELF, JOB, cp);
}

PUBLIC void send_TWI_CANCEL(ProcNumber sender, twi_info *cp)
{
    send_m3(sender, SELF, CANCEL, cp);
}

/* end code */
