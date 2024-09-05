/* net/i2c.h */

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

#ifndef _I2C_H_
#define _I2C_H_

/* i2c 8-bit addresses. Even numbers for write mode, odd (+1) for read mode */

/* Fixed addresses, defined elsewhere:-
 *   OLED  IOTA_I2C_ADDRESS       0x78 (120)  oled screen
 *   RTC   RV3028C7_I2C_ADDRESS   0xA4 (164)  real time clock
 *   MDAC  MCP4728_I2C_ADDRESS    0xC0 (192)  digital to analog convertor
 *   BMP   BMP180_I2C_ADDRESS     0xEE (238)  barometer
 *   NLCD  LCD_I2C_ADDRESS        0x4E (78)   16 x 2 lcd character display
 */

/* host addresses */
#define OSLO_I2C_ADDRESS 0x34 /* 52 */
#define PISA_I2C_ADDRESS 0x36 /* 54 */
#define SUMO_I2C_ADDRESS 0x38 /* 56 */
#define IOWA_I2C_ADDRESS 0x3A /* 58 */
#define BALI_I2C_ADDRESS 0x3C /* 60 */
#define PERU_I2C_ADDRESS 0x3E /* 62 */
#define FIDO_I2C_ADDRESS 0x40 /* 64 */
#define LIMA_I2C_ADDRESS 0x42 /* 66 */
#define GOAT_I2C_ADDRESS 0x44 /* 68 - GOAT isn't actually on the network.. */

#define GCALL_I2C_ADDRESS 0x00 /* 0 - general call shouldn't use read mode */

/* resource aliases */
#define UTC_ADDRESS            OSLO_I2C_ADDRESS
#define LCD_ADDRESS            SUMO_I2C_ADDRESS
#define SPI_OLED_ADDRESS       PERU_I2C_ADDRESS
#define TWI_OLED_ADDRESS       LIMA_I2C_ADDRESS
#define BAT_ADDRESS            OSLO_I2C_ADDRESS
#define LOG_ADDRESS            OSLO_I2C_ADDRESS
#define FS_ADDRESS             OSLO_I2C_ADDRESS
#define BAROMETER_ADDRESS      FIDO_I2C_ADDRESS
#define GATEWAY_ADDRESS        BALI_I2C_ADDRESS

#endif /* _I2C_H_ */
