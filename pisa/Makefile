# Makefile for pisa

# Copyright (c) 2024 Peter Welch
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in
#   the documentation and/or other materials provided with the
#   distribution.
# * Neither the name of the copyright holders nor the names of
#   contributors may be used to endorse or promote products derived
#   from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

#
# usage: make clean && make
#
#-----------------------------------------------------------------------------
# The name of the image to be built.
APP = pisa
I2C_ADDRESS = PISA_I2C_ADDRESS

# The relocatable object files.

CXXFLAGS = -I../lib
vpath %.c ../lib/sys:../lib/net:../lib/alba:../lib/hc05

LIB_OBJS = msg.o \
           ser.o \
           tty.o \
           clk.o \
           twi.o \
           memz.o \
           memp.o \
           syscon.o \
           istream.o \
           ostream.o \
           bc4.o \
           alba.o \
           egor.o \
           fritz.o \
           mdac.o \
           stairs.o \
           patch.o \
           setupd.o \
           eex.o \
           ramp.o \
           serin.o \
           ver.o \

APP_OBJS = sysinit.o \
           inp.o \
           main.o

OBJS = $(LIB_OBJS) $(APP_OBJS)

#-----------------------------------------------------------------------------

# The cpu architecture and clock frequency for determining avr-libc constants.
MCU = atmega328p
F_CPU = 8000000

#-----------------------------------------------------------------------------

# Fuses

# BOD disabled
EXTENDEDFUSES = 0xFF
# 1k bootsize
HIGHFUSES = 0xDC
BOOTLOADER_START = 0x7C00
# internal 8 MHz RC oscillator
LOWFUSES = 0xE2

#-----------------------------------------------------------------------------

#=============================================================================
# nothing should require adjustment beyond here.
#=============================================================================


CC = avr-gcc
CFLAGS = -Os -DF_CPU=$(F_CPU) -mmcu=$(MCU) -Wall -Wextra -I. $(CXXFLAGS)
LD = avr-gcc
LDFLAGS = -DF_CPU=$(F_CPU) -mmcu=$(MCU)
OBJCOPY = avr-objcopy
OBJDUMP = avr-objdump
SIZE = avr-size
SREC_CAT = srec_cat

#-----------------------------------------------------------------------------

.SUFFIXES: .elf .srec .hex .lst .eep .dsm
.elf.srec:
	$(OBJCOPY) --output-target=srec --remove-section=.eeprom $< $@
.srec.hex:
	$(SREC_CAT) $< -output $@ -Intel -address-length=2 -line-length=44
.elf.lst:
	$(OBJDUMP) --disassemble $< >$@
.elf.eep:
	$(OBJCOPY) --output-target=ihex --only-section=.eeprom \
                   --set-section-flags=.eeprom=alloc,load \
                   --no-change-warnings $< $@
.elf.dsm:
	echo -n "# $(APP) " >$@
	echo `git describe` >>$@
	avr-nm -S -n --size-sort $< |grep '^0080'|sort >>$@

#-----------------------------------------------------------------------------

$(APP).hex: $(APP).elf $(APP).lst $(APP).dsm touch

$(APP).elf: description.h .depend $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

avp:    image
	avp -l $(LOWFUSES) -h $(HIGHFUSES) image.hex

image: $(APP).hex
	(cd ../twiboot && make clean && make I2C_ADDRESS=$(I2C_ADDRESS) \
            MCU=$(MCU) F_CPU=$(F_CPU) BOOTLOADER_START=$(BOOTLOADER_START))
	head -n -1 $(APP).hex >$@.hex
	cat ../twiboot/twiboot.hex >>$@.hex

clean:
	-rm -f *.o *.lst *.elf *.srec *.hex .depend *.dsm

touch:
	rm description.h

description.h:
	echo "#define APP \"$(APP)\"" >$@
	echo "#define DESCRIPTION \"`git describe`\"" >>$@

#----------------------------------------------------------------------------

.depend:
	$(CC) -M $(CFLAGS) *.c ../lib/*/*.c > $@
#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
