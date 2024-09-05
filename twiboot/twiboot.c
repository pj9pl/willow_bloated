/***************************************************************************
 *   Copyright (C) 10/2020 by Olaf Rempel                                  *
 *   razzor@kopf-tisch.de                                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; version 2 of the License,               *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <util/twi.h>

#include "twiboot.h"
#include "../lib/sys/defs.h"
#include "../lib/net/i2c.h"

#define VERSION_STRING          "TWIBOOT v3.2c"
#define EEPROM_SIZE             (E2END +1)

/* Insist that a bootloader start address is provided.
 * e.g. the Makefile CFLAGS could specify -DBOOTLOADER_START=0x7C00
 */
#ifndef BOOTLOADER_START
#error BOOTLOADER_START not defined.
#endif

/* Insist that a TWI address is provided.
 * e.g. the Makefile CFLAGS could specify -DTWI_ADDRESS=PISA_I2C_ADDRESS
 */
#ifndef TWI_ADDRESS
#error TWI_ADDRESS not defined.
#endif

/*
 * bootloader twi-protocol:
 * - abort boot timeout:
 *   SLA+W, 0x00, STO
 *
 * - show bootloader version
 *   SLA+W, 0x01, SLA+R, {16 bytes}, STO
 *
 * - start application
 *   SLA+W, 0x01, 0x80, STO
 *
 * - read chip info: 3byte signature, 1byte page size, 2byte flash size,
 *                                                           2byte eeprom size
 *   SLA+W, 0x02, 0x00, 0x00, 0x00, SLA+R, {8 bytes}, STO
 *
 * - read one (or more) flash bytes
 *   SLA+W, 0x02, 0x01, addrh, addrl, SLA+R, {* bytes}, STO
 *
 * - read one (or more) eeprom bytes
 *   SLA+W, 0x02, 0x02, addrh, addrl, SLA+R, {* bytes}, STO
 *
 * - write one flash page
 *   SLA+W, 0x02, 0x01, addrh, addrl, {* bytes}, STO
 *
 * - write one (or more) eeprom bytes
 *   SLA+W, 0x02, 0x02, addrh, addrl, {* bytes}, STO
 */

static const uint8_t info[VERSION_LEN] = VERSION_STRING;
static const uint8_t chipinfo[CHIPINFO_LEN] = {
    SIGNATURE_0,
    SIGNATURE_1,
    SIGNATURE_2,
    SPM_PAGESIZE,
    BOOTLOADER_START >> 8 & 0xFF,
    BOOTLOADER_START & 0xFF,
    EEPROM_SIZE >> 8 & 0xFF,
    EEPROM_SIZE & 0xFF,
};

typedef struct {
    uint16_t addr;
    uint8_t buf[SPM_PAGESIZE];
    uint8_t cmd;
} bgr_t;

static bgr_t bgr;

/* *************************************************************************
 * write_flash_page
 * ************************************************************************* */
static void write_flash_page(void)
{
    uint16_t pagestart = bgr.addr;
    uint8_t *p = bgr.buf;

    if (pagestart < BOOTLOADER_START) {
        boot_page_erase(pagestart);
        boot_spm_busy_wait();

        for (uint8_t i = 0; i < SPM_PAGESIZE >> 1; i++, bgr.addr += 2) {
            uint16_t data = *p++;
            data |= *p++ << 8;
            boot_page_fill(bgr.addr, data);
        }

        boot_page_write(pagestart);
        boot_spm_busy_wait();

        /* only required for bootloader section */
        boot_rww_enable();
    }
}

/* *************************************************************************
 * read_eeprom_byte
 * ************************************************************************* */
static uint8_t read_eeprom_byte(uint16_t address)
{
    EEARL = address;
    EEARH = (address >> 8);
    EECR |= _BV(EERE);

    return EEDR;
}


/* *************************************************************************
 * write_eeprom_byte
 * ************************************************************************* */
static void write_eeprom_byte(uint8_t val)
{
    EEARL = bgr.addr;
    EEARH = (bgr.addr >> 8);
    EEDR = val;
    bgr.addr++;

    EECR |= _BV(EEMPE);
    EECR |= _BV(EEPE);

    eeprom_busy_wait();
}


/* *************************************************************************
 * write_eeprom_buffer
 * ************************************************************************* */
static void write_eeprom_buffer(uint8_t size)
{
    uint8_t *p = bgr.buf;

    while (size--)
    {
        write_eeprom_byte(*p++);
    }
}


/* *************************************************************************
 * TWI_data_write
 * ************************************************************************* */
static uint8_t TWI_data_write(uint8_t bcnt, uint8_t data)
{
    uint8_t ack = 0x01;

    switch (bcnt) {
    case 0:
        switch (data) {
        case CMD_SWITCH_APPLICATION:
        case CMD_ACCESS_MEMORY:
        case CMD_WAIT:
            bgr.cmd = data;
            break;

        default:
            /* boot app now */
            bgr.cmd = CMD_BOOT_APPLICATION;
            ack = 0x00;
            break;
        }
        break;

    case 1:
        switch (bgr.cmd) {
        case CMD_SWITCH_APPLICATION:
            if (data == BOOTTYPE_APPLICATION) {
                bgr.cmd = CMD_BOOT_APPLICATION;
            }
            ack = 0x00;
            break;

        case CMD_ACCESS_MEMORY:
            if (data == MEMTYPE_CHIPINFO) {
                bgr.cmd = CMD_ACCESS_CHIPINFO;
            } else if (data == MEMTYPE_FLASH) {
                bgr.cmd = CMD_ACCESS_FLASH;
            } else if (data == MEMTYPE_EEPROM) {
                bgr.cmd = CMD_ACCESS_EEPROM;
            } else {
                ack = 0x00;
            }
            break;

        default:
            ack = 0x00;
            break;
        }
        break;

    case 2:
    case 3:
        bgr.addr <<= 8;
        bgr.addr |= data;
        break;

    default:
        switch (bgr.cmd) {
        case CMD_ACCESS_EEPROM:
            bgr.cmd = CMD_WRITE_EEPROM_PAGE;
            /* fallthrough */
        case CMD_WRITE_EEPROM_PAGE:
        case CMD_ACCESS_FLASH:
            {
                uint8_t pos = bcnt -4;

                bgr.buf[pos] = data;
                if (pos >= (SPM_PAGESIZE -1)) {
                    if (bgr.cmd == CMD_ACCESS_FLASH) {
                        bgr.cmd = CMD_WRITE_FLASH_PAGE;
                    }
                    ack = 0x00;
                }
                break;
            }

        default:
            ack = 0x00;
            break;
        }
        break;
    }
    return ack;
}


/* *************************************************************************
 * TWI_data_read
 * ************************************************************************* */
static uint8_t TWI_data_read(uint8_t bcnt)
{
    uint8_t data;

    switch (bgr.cmd) {
    case CMD_READ_VERSION:
        bcnt %= sizeof(info);
        data = info[bcnt];
        break;

    case CMD_ACCESS_CHIPINFO:
        bcnt %= sizeof(chipinfo);
        data = chipinfo[bcnt];
        break;

    case CMD_ACCESS_FLASH:
        data = pgm_read_byte_near(bgr.addr);
        bgr.addr++;
        break;

    case CMD_ACCESS_EEPROM:
        data = read_eeprom_byte(bgr.addr);
        bgr.addr++;
        break;

    default:
        data = 0xFF;
        break;
    }
    return data;
}


/* *************************************************************************
 * TWI_vect
 * ************************************************************************* */
static void TWI_vect(void)
{
    static uint8_t bcnt;
    uint8_t control = TWCR;

    switch (TW_STATUS) {
    case TW_SR_SLA_ACK:
        /* SLA+W received, ACK returned -> receive data and ACK */
        bcnt = 0;
        break;

    case TW_SR_DATA_ACK:
        /* prev. SLA+W, data received, ACK returned -> receive data and ACK */
        if (TWI_data_write(bcnt++, TWDR) == 0x00) {
            /* the ACK returned by TWI_data_write() is not for the current
             * data in TWDR, but for the next byte received
             */
            control &= ~_BV(TWEA);
        }
        break;

    case TW_ST_SLA_ACK:
        /* SLA+R received, ACK returned -> send data */
        bcnt = 0;
        /* fall through */

    case TW_ST_DATA_ACK:
        /* prev. SLA+R, data sent, ACK returned -> send data */
        TWDR = TWI_data_read(bcnt++);
        break;

    case TW_SR_DATA_NACK:
        /* prev. SLA+W, data received, NACK returned -> IDLE */
        TWI_data_write(bcnt++, TWDR);
        /* fall through */

    case TW_SR_STOP:
        /* STOP or repeated START -> IDLE */
        if (bgr.cmd == CMD_WRITE_FLASH_PAGE ||
                    bgr.cmd == CMD_WRITE_EEPROM_PAGE) {
            /* disable ACK for now, re-enable after page write */
            control &= ~_BV(TWEA);
            TWCR = _BV(TWINT) | control;
            if (bgr.cmd == CMD_WRITE_EEPROM_PAGE) {
                write_eeprom_buffer(bcnt -4);
            } else {
                write_flash_page();
            }
        }
        bcnt = 0;
        /* fall through */

    case TW_ST_DATA_NACK:
        /* prev. SLA+R, data sent, NACK returned -> IDLE */
        control |= _BV(TWEA);
        break;

    default:
        /* illegal state(s) -> reset hardware */
        control |= _BV(TWSTO);
        break;
    }

    TWCR = _BV(TWINT) | control;
}


void (*app_start)(void) = 0x0000;

/* *************************************************************************
 * main
 * ************************************************************************* */
int main(void)
{
    uint8_t ch;

    ch = MCUSR;
    MCUSR = 0;

    wdt_reset();
    WDTCSR |= _BV(WDCE) | _BV(WDE);
    WDTCSR = 0;

    /* enable pullup on the bootloader pin */
    BL_PORT |= _BV(BL);

    /* If the flash is already programmed and either the BL switch is open
     * or it isn't an external reset then start the app.
     */
    if ((pgm_read_byte_near(0x0000) != 0xFF) && 
        (bit_is_set(BL_PIN, BL) || bit_is_clear(ch, EXTRF))) {
        app_start();
    }

    bgr.cmd = CMD_WAIT;
    /* TWI init: set address, auto ACKs */
    TWAR = TWI_ADDRESS;
    TWCR = _BV(TWEA) | _BV(TWEN);

    while (bgr.cmd != CMD_BOOT_APPLICATION) {
        if (TWCR & _BV(TWINT)) {
            TWI_vect();
        }
    }

    /* Disable TWI */
    TWCR = 0x00;

     /* Exit via a watchdog system reset, as the application relies
      * upon the hardware registers to contain their initial values.
      */
    wdt_reset();
    WDTCSR |= _BV(WDCE) | _BV(WDE);
    WDTCSR = _BV(WDE);
    for (;;)
        ;
}

/* end code */
