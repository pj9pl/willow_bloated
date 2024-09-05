/* oled/sh1106.h */

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

#ifndef _SH1106_H_
#define _SH1106_H_

/* ------------------- SH1106 specifics --------------------
 * The control byte
 */
#define CONTINUATION_BIT                   _BV(7)
#define DATA_REGISTER_BIT                  _BV(6)
/* neither CONTINUATION_BIT nor DATA_REGISTER_BIT set */
#define NULL_CONTROL_BYTE                   0x00

/* --------------------------------------------
 * Command #1 [p.19] 
 */
#define SET_LOWER_COLUMN_ADDRESS           0x00
#define   COLUMN_ADDRESS_MASK              0x0f

/* --------------------------------------------
 * Command #2 [p.19]
 */
#define SET_HIGHER_COLUMN_ADDRESS          0x10

/* --------------------------------------------
 *  Command #3 [p.19] Set Pump voltage value.
 */

/* ------------------------------------------
 * Command #4 [p.20] Set Display line start.
 * A 6-bit line number is OR'd into the command byte.
 */
#define SET_DISPLAY_LINE_START             0x40
#define   DISPLAY_LINE_MASK                0x3F

/* -------------------------------------------------
 * Command #5 [p.20] Set Contrast Control Register.
 * Two-byte command.
 * A 8-bit contrast value is provided in the second byte.
 */
#define SET_CONTRAST_CONTROL_MODE          0x81

/* ---------------------------------------------------
 * Command #6 [p.21] Set Segment Re-map.
 * Reverse the ram column - segment correspondance.
 * Horizontally flip the screen.
 */
#define SET_SEGMENT_REMAP                  0xA0
#define   NORMAL_ROTATE_DIRECTION          0x00 /* power-on default */
#define   REVERSE_ROTATE_DIRECTION         0x01

/* --------------------------------------------
 * Command #7 [p.21] Set Entire Display OFF/ON
 */
#define SET_ENTIRE_DISPLAY_OFFON           0xA4
#define   ENTIRE_DISPLAY_NORMAL            0x00
#define   ENTIRE_DISPLAY_ON                0x01
 
/* --------------------------------------------
 * Command #8 [p.21] Set Normal/Reverse Display
 */
#define SET_NORMAL_REVERSE_DISPLAY         0xA6
#define   NORMAL_DISPLAY                   0x00
#define   REVERSE_DISPLAY                  0x01

/* --------------------------------------------
 * Command #9 [p.22] Set Mulltiplex Ration
 */

/* --------------------------------------------
 * Command #10 [p.22] Set DC-DC OFF/ON
 */

/* --------------------------------------------
 * Command #11 [p.23] Display OFF/ON
 */
#define DISPLAY_OFFON                      0xAE
#define   DISPLAY_OFF                      0x00
#define   DISPLAY_ON                       0x01

/* ------------------------------------------------
 * Command #12 [p.23] Set Page Address
 * A 3-bit page address is OR'd into the command byte
 */
#define SET_PAGE_ADDRESS                   0xB0
#define   PAGE_ADDRESS_MASK                0x07

/* ----------------------------------------------
 * Command #13 [p.24] Set Common Output Scan Direction
 * reverse the scan direction. Vertically flip the display
 */
#define SET_COMMON_OUTPUT_SCAN             0xC0
#define   NORMAL_SCAN_DIRECTION            0x00 /* power-on default */
#define   REVERSE_SCAN_DIRECTION           0x08

/* ------------------------------------------------
 * Command #14 [p.24] Set Display Offset
 */
#define SET_DISPLAY_OFFSET_MODE            0xD3

/* -------------------------------------------------------------------------
 * Command #15 [p.25] Set Display Clock Divide Ratio / Oscillator Frequency
 */

/* -----------------------------------------------------
 * Command #16 [p.26] Set Dis-charge / Pre-charge Period
 */

/* ---------------------------------------------------------
 * Command #17 [p.26] Set Common pads hardware configuration
 */
#define SET_COMMON_PADS_HW_CONFIG_MODE     0xDA
#define   SEQUENTIAL                       0x02
#define   ALTERNATIVE                      0x12 /* power-on default */
/* ------------------------------------------
 * Command #18 [p.27] Set VCOM Deselct Level
 */

/* ------------------------------------------
 * Command #19 [p.28] Read-Modify-Write
 */

/* ------------------------------------------
 * Command #20 [p.28] End (Read-Modify-Write)
 */
#define END_READ_MODIFY_WRITE              0xEE

/* ------------------------------------------
 * Command #21 [p.29] NOP
 */
#define NOP                                0xE3

/* ------------------------------------------
 * Command #22 [p.29] Write Display Data
 * This uses a control byte of DATA_REGISTER_BIT
 * followed by an unspecified number of data bytes
 * in a TWI_MT transaction.
 */

/* ------------------------------------------
 * Command #23 [p.29] Read Status
 */
#define BUSY                               0x80
#define ONOFF                              0x40

/* ------------------------------------------
 * Command #24 [p.29] Read Display Data
 */

#endif /* _SH1106_H_ */
