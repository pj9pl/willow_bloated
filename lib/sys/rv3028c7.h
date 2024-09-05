/* sys/rv3028c7.h */

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

/* refer to RV-3028-C7_App-Manual.pdf */

#ifndef _RV3028C7_H_
#define _RV3028C7_H_

/* Device address */
#define RV3028C7_I2C_ADDRESS     0xA4 /* 164 */

/* Clock registers [p.14] */
#define RV_SECONDS               0x00 /* two BCD digits: 00 to 59 */
#define RV_MINUTES               0x01 /* two BCD digits: 00 to 59 */
#define RV_HOURS                 0x02 /* two BCD digits: 00 to 23 or 12 */
#define   RV_AMPM               _BV(5) /* 0=AM, 1=PM when RV_12_24 is set */ 

/* Calendar registers [p.16] */
#define RV_WEEKDAY               0x03
#define RV_DATE                  0x04
#define RV_MONTH                 0x05
#define RV_YEAR                  0x06

/* Alarm registers [p.18] */
#define RV_MINUTES_ALARM         0x07
#define   RM_AE_M                _BV(7)
#define RV_HOURS_ALARM           0x08
#define   RM_AE_H                _BV(7)
#define RV_WEEKDAY_ALARM         0x09
#define   RM_AE_WD               _BV(7)

/* Periodic countdown timer control registers [p.20] */
#define RV_TIMER_VALUE_0         0x0A
#define RV_TIMER_VALUE_1         0x0B
#define RV_TIMER_STATUS_0        0x0C
#define RV_TIMER_STATUS_1_SHADOW 0x0D

/* Status and control registers [p.22] */
#define RV_STATUS                0x0E
#define   RV_EEBUSY             _BV(7)
#define   RV_CLKF               _BV(6)
#define   RV_BSF                _BV(5)
#define   RV_UF                 _BV(4)
#define   RV_TF                 _BV(3)
#define   RV_AF                 _BV(2)
#define   RV_EVF                _BV(1)
#define   RV_PORF               _BV(0)

/* [p.23] */
#define RV_CONTROL_1             0x0F
#define   RV_TRPT               _BV(7)
#define   RV_WADA               _BV(5)
#define   RV_USEL               _BV(4)
#define   RV_EERD               _BV(3)
#define   RV_TE                 _BV(2)
#define   RV_TD1                _BV(1)
#define   RV_TD0                _BV(0)

/* [p.24] */
#define RV_CONTROL_2             0x10
#define   RV_TSE                _BV(7)
#define   RV_CLKIE              _BV(6)
#define   RV_UIE                _BV(5)
#define   RV_TIE                _BV(4)
#define   RV_AIE                _BV(3)
#define   RV_EIE                _BV(2)
#define   RV_12_24              _BV(1)
#define   RV_RESET              _BV(0)

/* GP Bits register [p.25] */
#define RV_GP_BITS               0x11
#define   RV_GP6                _BV(6)
#define   RV_GP5                _BV(5)
#define   RV_GP4                _BV(4)
#define   RV_GP3                _BV(3)
#define   RV_GP2                _BV(2)
#define   RV_GP1                _BV(1)
#define   RV_GP0                _BV(0)

/* Clock interrupt mask register [p.25] (not PIM449) */
#define RV_CLOCK_INT_MASK        0x12
#define   RV_CEIE               _BV(3)
#define   RV_CAIE               _BV(2)
#define   RV_CTIE               _BV(1)
#define   RV_CUIE               _BV(0)

/* Event control register [p.26] (not PIM449) */
#define RV_EVENT_CONTROL         0x13
#define   RV_EHL                _BV(6)
#define   RV_ET1                _BV(5)
#define   RV_ET0                _BV(4)
#define   RV_TSR                _BV(2)
#define   RV_TSOW               _BV(1)
#define   RV_TSS                _BV(0)

/* Time stamp registers [p.27] */
#define RV_COUNT_TS              0x14
#define RV_SECONDS_TS            0x15
#define RV_MINUTES_TS            0x16
#define RV_HOURS_TS              0x17
#define RV_DATE_TS               0x18
#define RV_MONTH_TS              0x19
#define RV_YEAR_TS               0x1A

/* UNIX time registers [p.29] */
#define RV_UNIX_TIME_0           0x1B
#define RV_UNIX_TIME_1           0x1C
#define RV_UNIX_TIME_2           0x1D
#define RV_UNIX_TIME_3           0x1E

/* Ram registers [p.31] */
#define RV_USER_RAM_1            0x1F
#define RV_USER_RAM_2            0x20

/* Password registers [p.32] */
#define RV_PW_0                  0x21
#define RV_PW_1                  0x22
#define RV_PW_2                  0x23
#define RV_PW_3                  0x24

/* EEPROM memory control registers [p.33] */
#define RV_EEADDR                0x25
#define RV_EEDATA                0x26
#define RV_EECMD                 0x27
#define   RV_FIRST_CMD           0x00
#define   RV_UPDATE_EEPROM       0x11
#define   RV_REFRESH             0x12
#define   RV_WRITE_ONE_EEPROM_BYTE  0x21
#define   RV_READ_ONE_EEPROM_BYTE   0x22
#define RV_ID                    0x28
#define   RV_HID3               _BV(7)
#define   RV_HID2               _BV(6)
#define   RV_HID1               _BV(5)
#define   RV_HID0               _BV(4)
#define   RV_VID3               _BV(3)
#define   RV_VID2               _BV(2)
#define   RV_VID1               _BV(1)
#define   RV_VID0               _BV(0)
#define RV_EEPROM_PW_ENABLE      0x30
#define RV_EEPROM_PW_0           0x31
#define RV_EEPROM_PW_1           0x32
#define RV_EEPROM_PW_2           0x33
#define RV_EEPROM_PW_3           0x34
#define RV_EEPROM_CLKOUT         0x35
#define   RV_CLKOE              _BV(7)
#define   RV_CLKSY              _BV(6)
#define   RV_PORIE              _BV(3)
#define   RV_FD2                _BV(2)
#define   RV_FD1                _BV(1)
#define   RV_FD0                _BV(0)

/* EEPROM Offset register [p.37] */
#define RV_EEPROM_OFFSET         0x36

/* EEPROM Backup register [p.39] */
#define RV_EEPROM_BACKUP         0x37
#define   RV_EEOFFSET_0         _BV(7)
#define   RV_BSIE               _BV(6)
#define   RV_TCE                _BV(5)
#define   RV_FEDE               _BV(4)
#define   RV_BSM1               _BV(3)
#define   RV_BSM0               _BV(2)
#define   RV_TCR1               _BV(1)
#define   RV_TCR0               _BV(0)

#define ENABLE_DIRECT_SWITCHING  RV_BSM0
#define ENABLE_LEVEL_SWITCHING  (RV_BSM1 | RV_BSM0)

#endif /* _RV3028C7_H_ */
