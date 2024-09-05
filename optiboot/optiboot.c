/**********************************************************/
/* optiboot.c - Optiboot bootloader for an Atmega328P     */
/*              derived from arduino-1.8.10               */
/*              this version modified to suit willow/bali */
/*                                                        */
/* Optiboot bootloader for Arduino                        */
/* http://optiboot.googlecode.com                         */
/*                                                        */
/* Arduino-maintained version : See README.TXT            */
/* http://code.google.com/p/arduino/                      */
/*                                                        */
/* Heavily optimised bootloader that is faster and        */
/* smaller than the Arduino standard bootloader           */
/*                                                        */
/* Enhancements:                                          */
/*   Fits in 512 bytes, saving 1.5K of code space         */
/*   Background page erasing speeds up programming        */
/*   Higher baud rate speeds up programming               */
/*   Written almost entirely in C                         */
/*   Customisable timeout with accurate timeconstant      */
/*   Optional virtual UART. No hardware UART required.    */
/*   Optional virtual boot partition for devices without. */
/*                                                        */
/* What you lose:                                         */
/*   Implements a skeleton STK500 protocol which is       */
/*     missing several features including EEPROM          */
/*     programming and non-page-aligned writes            */
/*   High baud rate breaks compatibility with standard    */
/*     Arduino flash settings                             */
/*                                                        */
/* Fully supported:                                       */
/*   ATmega328P based devices                             */
/*                                                        */
/* Assumptions:                                           */
/*   The code makes several assumptions that reduce the   */
/*   code size. They are all true after a hardware reset, */
/*   but may not be true if the bootloader is called by   */
/*   other means or on other hardware.                    */
/*     No interrupts can occur                            */
/*     UART and Timer 1 are set to their reset state      */
/*     SP points to RAMEND                                */
/*                                                        */
/* Code builds on code, libraries and optimisations from: */
/*   stk500boot.c          by Jason P. Kyle               */
/*   Arduino bootloader    http://www.arduino.cc          */
/*   Spiff's 1K bootloader
         http://spiffie.org/know/arduino_1k_bootloader/bootloader.shtml */
/*   avr-libc project      http://nongnu.org/avr-libc     */
/*   Adaboot
                 http://www.ladyada.net/library/arduino/bootloader.html */
/*   AVR305                Atmel Application Note         */
/*                                                        */
/* This program is free software; you can redistribute it */
/* and/or modify it under the terms of the GNU General    */
/* Public License as published by the Free Software       */
/* Foundation; either version 2 of the License, or        */
/* (at your option) any later version.                    */
/*                                                        */
/* This program is distributed in the hope that it will   */
/* be useful, but WITHOUT ANY WARRANTY; without even the  */
/* implied warranty of MERCHANTABILITY or FITNESS FOR A   */
/* PARTICULAR PURPOSE.  See the GNU General Public        */
/* License for more details.                              */
/*                                                        */
/* You should have received a copy of the GNU General     */
/* Public License along with this program; if not, write  */
/* to the Free Software Foundation, Inc.,                 */
/* 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA */
/*                                                        */
/* Licence can be viewed at                               */
/* http://www.fsf.org/licenses/gpl.txt                    */
/*                                                        */
/**********************************************************/


/**********************************************************/
/*                                                        */
/* Optional defines:                                      */
/*                                                        */
/**********************************************************/
/*                                                        */
/* BAUD_RATE:                                             */
/* Set bootloader baud rate.                              */
/*                                                        */
/**********************************************************/

/**********************************************************/
/* Version Numbers!                                       */
/*                                                        */
/* Arduino Optiboot now includes this Version number in   */
/* the source and object code.                            */
/*                                                        */
/* Version 3 was released as zip from the optiboot        */
/*  repository and was distributed with Arduino 0022.     */
/* Version 4 starts with the arduino repository commit    */
/*  that brought the arduino repository up-to-date with   */
/* the optiboot source tree changes since v3.             */
/*                                                        */
/**********************************************************/

/**********************************************************/
/* Edit History:					  */
/*							  */
/* 4.4 WestfW: add initialization of address to keep      */
/*             the compiler happy.  Change SC'ed targets. */
/*             Return the SW version via READ PARAM       */
/* 4.3 WestfW: catch framing errors in getch(), so that   */
/*             AVRISP works without HW kludges.           */
/*  http://code.google.com/p/arduino/issues/detail?id=368n*/
/* 4.2 WestfW: reduce code size, fix timeouts, change     */
/*             verifySpace to use WDT instead of appstart */
/* 4.1 WestfW: put version number in binary.		  */
/**********************************************************/

#define OPTIBOOT_MAJVER 4
#define OPTIBOOT_MINVER 4

#define MAKESTR(a) #a
#define MAKEVER(a, b) MAKESTR(a*256+b)

asm("  .section .version\n"
    "optiboot_version:  .word " MAKEVER(OPTIBOOT_MAJVER, OPTIBOOT_MINVER) "\n"
    "  .section .text\n");

#include <inttypes.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

/* Pin D6 (#12) is configured as an input with a soft pullup and connected
 * to the the bootloader switch, which provides an open circuit or a short
 * to 0v. The pin must be low during an external reset for the bootloader
 * to be entered.
 */
#define BL_PORT PORTD
#define BL_PIN  PIND
#define BL      PIND6

// <avr/boot.h> uses sts instructions, but this version uses out instructions
// This saves cycles and program memory.
#include "boot.h"


/* We don't use <avr/wdt.h> as those routines have interrupt overhead
 * we don't need.
 */

#include "stk500.h"

/* Watchdog settings */
#define WATCHDOG_OFF    (0)
#define WATCHDOG_16MS   (_BV(WDE))

/* Function Prototypes */
/* The main function is in init9, which removes the interrupt vector table */
/* we don't need. It is also 'naked', which means the compiler does not    */
/* generate any entry or exit code itself. */
int main(void) __attribute__ ((naked)) __attribute__ ((section (".init9")));
void putch(char);
uint8_t getch(void);
/* "static inline" is a compiler hint to reduce code size */
static inline void getNch(uint8_t);
void verifySpace(void);
uint8_t getLen(void);
static inline void watchdogReset();
void watchdogConfig(uint8_t x);
void appStart() __attribute__ ((naked));

#define RAMSTART (0x100)
#define NRWWSTART (0x7000)

/* C zero initialises all global variables. However, that requires */
/* These definitions are NOT zero initialised, but that doesn't matter */
/* This allows us to drop the zero init code, saving us memory */
#define buff    ((uint8_t*)(RAMSTART))

/* main program starts here */
int main(void)
{
    uint8_t ch;

    /*
     * Making these local and in registers prevents the need for initializing
     * them, and also saves space because code no longer stores to memory.
     * (initializing address keeps the compiler happy, but isn't really
     *  necessary, and uses 4 bytes of flash.)
     */
    register uint16_t address = 0;
    register uint8_t  length;

    // After the zero init loop, this is the first code to run.
    //
    // This code makes the following assumptions:
    //  No interrupts will execute
    //  SP points to RAMEND
    //  r1 contains zero
    //
    // If not, uncomment the following instructions:
    // cli();
    asm volatile ("clr __zero_reg__");

    // Adaboot no-wait mod
    ch = MCUSR;
    MCUSR = 0;


    BL_PORT |= _BV(BL);

    if (!(ch & _BV(EXTRF)) || (BL_PIN & _BV(BL)))
        appStart();

    UCSR0A = _BV(U2X0); //Double speed mode USART0
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
    UCSR0C = _BV(UCSZ00) | _BV(UCSZ01);
    UBRR0L = (uint8_t)( (F_CPU + BAUD_RATE * 4L) / (BAUD_RATE * 8L) - 1 );

    /* Forever loop */
    for (;;) {
        /* get character from UART */
        ch = getch();

        if (ch == STK_GET_PARAMETER) {
            unsigned char which = getch();
            verifySpace();
            if (which == 0x82) {
                /*
                 * Send optiboot version as "minor SW version"
                 */
                putch(OPTIBOOT_MINVER);
            } else if (which == 0x81) {
                putch(OPTIBOOT_MAJVER);
            } else {
                /*
                 * GET PARAMETER returns a generic 0x03 reply for
                 * other parameters - enough to keep Avrdude happy
                 */
                putch(0x03);
            }
        } else if (ch == STK_SET_DEVICE) {
            // SET DEVICE is ignored
            getNch(20);
        } else if (ch == STK_SET_DEVICE_EXT) {
            // SET DEVICE EXT is ignored
            getNch(5);
        } else if (ch == STK_LOAD_ADDRESS) {
            // LOAD ADDRESS
            uint16_t newAddress;
            newAddress = getch();
            newAddress = (newAddress & 0xff) | (getch() << 8);
            // Convert from word address to byte address
            newAddress += newAddress;
            address = newAddress;
            verifySpace();
        } else if (ch == STK_UNIVERSAL) {
            // UNIVERSAL command is ignored
            getNch(4);
            putch(0x00);
        } else if (ch == STK_PROG_PAGE) {
            /* Write memory, length is big endian and is in bytes */
            // PROGRAM PAGE - we support flash programming only, not EEPROM
            uint8_t *bufPtr;
            uint16_t addrPtr;

            getch();               /* getlen() */
            length = getch();
            getch();

            // If we are in RWW section, immediately start page erase
            if (address < NRWWSTART)
                __boot_page_erase_short((uint16_t)(void*)address);

            // While that is going on, read in page contents
            bufPtr = buff;
            do
                *bufPtr++ = getch();
            while (--length);

            /* If we are in NRWW section,
             * page erase has to be delayed until now.
             */
            // Todo: Take RAMPZ into account
            if (address >= NRWWSTART)
                __boot_page_erase_short((uint16_t)(void*)address);

            // Read command terminator, start reply
            verifySpace();

            // If only a partial page is to be programmed,
            // the erase might not be complete.
            // So check that here
            boot_spm_busy_wait();

            // Copy buffer into programming buffer
            bufPtr = buff;
            addrPtr = (uint16_t)(void*)address;
            ch = SPM_PAGESIZE / 2;
            do {
                uint16_t a;
                a = *bufPtr++;
                a |= (*bufPtr++) << 8;
                __boot_page_fill_short((uint16_t)(void*)addrPtr,a);
                addrPtr += 2;
            } while (--ch);

            // Write from programming buffer
            __boot_page_write_short((uint16_t)(void*)address);
            boot_spm_busy_wait();

            // Reenable read access to flash
            boot_rww_enable();
        } else if (ch == STK_READ_PAGE) {
            /* Read memory block mode, length is big endian.  */
            // READ PAGE - we only read flash
            getch();        /* getlen() */
            length = getch();
            getch();

            verifySpace();
            do
                putch(pgm_read_byte_near(address++));
            while (--length);
        } else if (ch == STK_READ_SIGN) {
            /* Get device signature bytes  */
            // READ SIGN - return what Avrdude wants to hear
            /* avrdude needs the programmer option to be set
             * to '-c arduino' instead of '-c stk500v1' for
             * these signature bytes to be read.
             */ 
            verifySpace();
            putch(SIGNATURE_0);
            putch(SIGNATURE_1);
            putch(SIGNATURE_2);
        } else if (ch == STK_LEAVE_PROGMODE) {
            // Adaboot no-wait mod
            watchdogConfig(WATCHDOG_16MS);
            verifySpace();
        } else {
            // This covers the response to commands like STK_ENTER_PROGMODE
            verifySpace();
        }
        putch(STK_OK);
    }
}

void putch(char ch)
{
    while (!(UCSR0A & _BV(UDRE0)))
        ;
    UDR0 = ch;
}

uint8_t getch(void)
{
    uint8_t ch;

    while(!(UCSR0A & _BV(RXC0)))
        ;
    ch = UDR0;
    return ch;
}

void getNch(uint8_t count)
{
    do
        getch();
    while (--count);
    verifySpace();
}

void verifySpace(void)
{
    if (getch() != CRC_EOP) {
        watchdogConfig(WATCHDOG_16MS);  // shorten WD timeout
        while (1)                       // and busy-loop so that WD causes
            ;                           //  a reset and app start.
    }
    putch(STK_INSYNC);
}

// Watchdog functions. These are only safe with interrupts turned off.
void watchdogReset()
{
    __asm__ __volatile__ (
        "wdr\n"
    );
}

void watchdogConfig(uint8_t x)
{
    WDTCSR = _BV(WDCE) | _BV(WDE);
    WDTCSR = x;
}

void appStart(void)
{
    watchdogConfig(WATCHDOG_OFF);
    __asm__ __volatile__ (
        // Jump to RST vector
        "clr r30\n"
        "clr r31\n"
        "ijmp\n"
    );
}

/* end code */
