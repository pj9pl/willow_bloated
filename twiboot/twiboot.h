/* twiboot/twiboot.h */

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

#ifndef _TWIBOOT_H_
#define _TWIBOOT_H_

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
 *   2byte eeprom size
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


/* SLA+R */
#define CMD_WAIT                0x00
#define CMD_READ_VERSION        0x01
#define CMD_ACCESS_MEMORY       0x02
/* internal mappings */
#define CMD_ACCESS_CHIPINFO     (0x10 | CMD_ACCESS_MEMORY)
#define CMD_ACCESS_FLASH        (0x20 | CMD_ACCESS_MEMORY)
#define CMD_ACCESS_EEPROM       (0x30 | CMD_ACCESS_MEMORY)
#define CMD_WRITE_FLASH_PAGE    (0x40 | CMD_ACCESS_MEMORY)
#define CMD_WRITE_EEPROM_PAGE   (0x50 | CMD_ACCESS_MEMORY)

/* SLA+W */
#define CMD_SWITCH_APPLICATION  CMD_READ_VERSION
/* internal mappings */
#define CMD_BOOT_BOOTLOADER     (0x10 | CMD_SWITCH_APPLICATION) /* APP only */
#define CMD_BOOT_APPLICATION    (0x20 | CMD_SWITCH_APPLICATION)

/* CMD_SWITCH_APPLICATION parameter */
#define BOOTTYPE_BOOTLOADER     0x00    /* APP only */
#define BOOTTYPE_APPLICATION    0x80

/* CMD_{READ|WRITE}_* parameter */
#define MEMTYPE_CHIPINFO        0x00
#define MEMTYPE_FLASH           0x01
#define MEMTYPE_EEPROM          0x02

#define VERSION_LEN             16
#define CHIPINFO_LEN            8

#endif /* _TWIBOOT_H_ */
