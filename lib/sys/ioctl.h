/* sys/ioctl.h */

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

#ifndef _IOCTL_H_
#define _IOCTL_H_

#define  SIOC_LOOP_COUNT        2  /* egor: long value for iteration */
#define  SIOC_CARD_DETECT       7  /* ssd: read the sdcard detect pin */
#define  SIOC_HC05_COMMAND     10  /* value selects the command */
#define  SIOC_MKFS_COMMAND     12  /* mkfs: value selects the command */
#define  SIOC_START            24  /* not used */
#define  SIOC_LENGTH           25  /* not used */
#define  SIOC_SAMPLING_RATE    26  /* LTP, BATZ, TEMPEST */
#define  SIOC_BACKLIGHT        27  /* plcd: 0=off, 1=on */
#define  SIOC_SELECT_OUTPUT    28  /* tty, others select i2c destination */
#define  SIOC_DEVICE_POWER     29  /* 0 = power off, 1 = power on */
#define  SIOC_PLCD_COMMAND     30  /* plcd: value selects the command */
#define  SIOC_OMODE            32  /* TTY: 1 = RAW, 0 = COOKED */
#define  SIOC_CONSUMER         36  /* set dest of serial input NOT_EMPTY */
#define  SIOC_LVSD_COMMAND     37  /* lvsd: value selects the command */
#define  SIOC_ICSD_COMMAND     38  /* icsd: value selects the command */
#define  SIOC_BAUDRATE         39  /* ser: set speed */
#define  SIOC_STEPSIZE         40  /* stairs: */
#define  SIOC_CHANNEL          41  /* stairs: */
#define  SIOC_DELAY            43  /* stairs: */
#define  SIOC_DISPLAY_MODE     44  /* alba/patch: configure the AD7124 */
#define  SIOC_LOGGING          45  /* alba/egor: switch logging on/off */
#define  SIOC_STANDARD         46  /* metric or imperial */
#define  SIOC_PERIODIC_TIME_INTERRUPT 47 /* sys/rtc.c */
#define  SIOC_START_VALUE      48
#define  SIOC_END_VALUE        49
#define  SIOC_BOOTTIME         50
#define  SIOC_BUTTONVAL        51
#define  SIOC_CURSOR_POSITION  52  /* oled/console.c reset cursor position */

#endif /* _IOCTL_H_ */
