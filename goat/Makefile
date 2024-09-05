# Makefile for goat

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
# usage: . ./defs.sh && make clean && make && make flash
#
#-----------------------------------------------------------------------------
# The name of the image to be built.
APP = goat

# The relocatable object files.

CXXFLAGS = -I../lib
vpath %.c ../lib/sys:../lib/isp

LIB_OBJS = msg.o \
           ser.o \
           ttynn.o \
           clk.o \
           dmp.o \
           hvpp.o \
           ver.o \

APP_OBJS = sysinit.o \
           inp.o \
           main.o

OBJS = $(LIB_OBJS) $(APP_OBJS)

#-----------------------------------------------------------------------------

# The cpu architecture and clock frequency for determining avr-libc constants.
MCU = atmega328p
F_CPU = 8000000
BAUD = 9600

#-----------------------------------------------------------------------------

# Fuses

# Extended Fuse Byte [p.291]
# BOD disabled
EXTENDEDFUSES = 0xFF

# High Fuse Byte [p.286,292]
# 512 byte bootsize
HIGHFUSES = 0xDE
BOOTLOADER_START = 0x7E00

# Low Fuse Byte [p.43,293]
# internal 8MHz RC oscillator
LOWFUSES = 0xE2

#-----------------------------------------------------------------------------

# The image programming method.
DUDE = /usr/local/bin/avrdude -C /usr/local/etc/avrdude.conf
PARTNUMBER = -p m328p
PROGRAMMER = -c stk500v1
BAUDRATE = -b 9600
# rely on the $PORT definition from the environment, set during . ./defs.sh
#PORT = -P /dev/rfcomm1
WRITE_OPERATION = -U flash:w:$<
READ_OPERATION = -U flash:r:readback.hex:i
DUDEWRITEFLAGS = $(PARTNUMBER) $(PROGRAMMER) $(BAUDRATE) $(PORT) \
                                                    $(WRITE_OPERATION)
DUDEREADFLAGS = -D -v -v -v $(PARTNUMBER) $(PROGRAMMER) $(BAUDRATE) $(PORT) \
                                                    $(READ_OPERATION)

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
                   --no-change-warnings --change-section-lma \
                   .eeprom=0x00180000 $< $@
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
	(cd ../optiboot && make clean && make BAUDRATE=$(BAUD) \
            MCU=$(MCU) F_CPU=$(F_CPU) BOOTLOADER_START=$(BOOTLOADER_START))
	head -n -1 $(APP).hex >$@.hex
	cat ../optiboot/optiboot.hex >>$@.hex

flash: $(APP).hex
	$(DUDE) $(DUDEWRITEFLAGS)

clean:
	-rm -f *.o *.su *.lst *.elf *.srec *.hex *.eep .depend *.dsm

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
