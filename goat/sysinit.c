/* goat/sysinit.c */

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

/* This module configures the hardware, i.e. switch all the peripherals off
 * and activate soft pullups, prior to allowing each module to apply their own
 * configuration.
 */

#include <avr/io.h>

#include "sys/defs.h"
#include "sys/sysinit.h"

/* I can .. */

PUBLIC void config_sysinit(void)
{
    /* There are no unused pins on goat.

                         -------U-------
            EXT_RST  -> -|RESET      C5|-  -> /OE
     HC05_TX yellow  -> -|D0         C4|-  -> /WR
     HC05_RX green  <-  -|D1         C3|-  -> BS2
             /READY  -> -|D2         C2|-  -> BS1
             VCC_ON <-  -|D3         C1|-  -> XA1
             VPP_ON <-  -|D4         C0|-  -> XA0
                        -|VCC       GND|-
                        -|GND      AREF|-
              DATA6 <-> -|B6       AVCC|-
              DATA7 <-> -|B7         B5|- <-> DATA5
              XTAL1 <-  -|D5         B4|- <-> DATA4
  BOOTLOADER_SWITCH  -> -|D6         B3|- <-> DATA3
              PAGEL <-  -|D7         B2|- <-> DATA2
              DATA0 <-> -|B0         B1|- <-> DATA1
                         ---------------
     */

    /* enable pullup on bootloader switch */
    BL_PORT |= _BV(BL);

    /* Disable peripherals. Enable as required.
     * Set the bit to power-down the peripheral. 
     * Clear the bit to power-up the peripheral. 
     * TWI, TIMER2, TIMER0, -, TIMER1, SPI, USART, ADC.
     */
    PRR |= _BV(PRTWI) | _BV(PRTIM2) | _BV(PRTIM0) | /* - */
           _BV(PRTIM1) | _BV(PRSPI) | _BV(PRUSART0) | _BV(PRADC);
}

/* end code */
