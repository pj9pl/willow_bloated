# optiboot/Makefile

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

TOOLS = /home/pjw/opt/arduino-1.8.10/hardware/tools/avr/bin
CFLAGS = -Wall -Wextra -Os -DF_CPU=$(F_CPU) -mmcu=$(MCU) \
            -DBAUD_RATE=$(BAUDRATE)
LDSECTIONS = -Wl,--section-start=.text=$(BOOTLOADER_START) \
             -Wl,--section-start=.version=0x7ffe
LFLAGS = $(LDSECTIONS) -Wl,--relax -Wl,--gc-sections -nostartfiles -nostdlib

optiboot.hex: optiboot.c Makefile
	$(TOOLS)/avr-gcc $(CFLAGS) -c -o optiboot.o optiboot.c
	$(TOOLS)/avr-gcc $(CFLAGS) $(LFLAGS) -o optiboot.elf optiboot.o
	$(TOOLS)/avr-objdump -h -S optiboot.elf >optiboot.lst
	$(TOOLS)/avr-objcopy -j .text -j .data -j .version \
             --set-section-flags .version=alloc,load -O srec optiboot.elf \
             optiboot.srec
	srec_cat optiboot.srec -output optiboot.hex -Intel -address-length=2 \
            -line-length=44

clean:
	rm -f *.o *.elf *.srec *.lst *.hex
