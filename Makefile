# willow/Makefile

# Copyright (c) 2024, Peter Welch
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

# To build everything, type 'make clean && make'
PACKAGES = bali fido goat iowa lima oslo peru pisa sumo
BOOTLOADERS = optiboot twiboot
STAMP = willow-`date "+%C%y%m%d%H%M%S"`

all: apps tools

apps:
	for i in $(PACKAGES) ;\
        do (cd $$i ; echo "Making in $$i..."; make); done

tools:
	(cd hal ; make clean && make install)

clean:
	-rm -f *.ps *.ps~ *.pdf
	for i in $(PACKAGES) $(BOOTLOADERS);\
	do (cd $$i ; echo "Making clean in $$i..."; make clean); done
	(cd hal ; make clean)

dist:
	git archive --format=tar --prefix=$(STAMP)/ -o $(STAMP).tar HEAD
	gzip willow-*.tar
	mv willow-*.tar.gz ../willow-dailys	

daily: dist

listing:
	a2ps --toc -1 -E --borders=no \
                 --output=stuff.ps \
                README.md \
                LICENSE \
                Makefile \
                bali/README* \
                bali/defs.sh \
                bali/Makefile \
                bali/host.h \
                bali/main.c \
                bali/inp.c \
                bali/sysinit.c \
                fido/README* \
                fido/Makefile \
                fido/host.h \
                fido/main.c \
                fido/inp.c \
                fido/sysinit.c \
                goat/README* \
                goat/Makefile \
                goat/host.h \
                goat/main.c \
                goat/inp.c \
                goat/sysinit.c \
                iowa/README* \
                iowa/defs.sh \
                iowa/Makefile \
                iowa/host.h \
                iowa/main.c \
                iowa/inp.c \
                iowa/sysinit.c \
                lima/README* \
                lima/defs.sh \
                lima/Makefile \
                lima/host.h \
                lima/main.c \
                lima/inp.c \
                lima/sysinit.c \
                oslo/README* \
                oslo/defs.sh \
                oslo/Makefile \
                oslo/host.h \
                oslo/main.c \
                oslo/inp.c \
                oslo/sysinit.c \
                peru/README* \
                peru/Makefile \
                peru/host.h \
                peru/main.c \
                peru/inp.c \
                peru/sysinit.c \
                pisa/README* \
                pisa/Makefile \
                pisa/host.h \
                pisa/main.c \
                pisa/inp.c \
                pisa/sysinit.c \
                sumo/README* \
                sumo/Makefile \
                sumo/host.h \
                sumo/main.c \
                sumo/inp.c \
                sumo/sysinit.c \
                lib/alba/ad7124.h \
                lib/alba/alba.h \
                lib/alba/alba.c \
                lib/alba/egor.h \
                lib/alba/egor.c \
                lib/alba/fritz.h \
                lib/alba/fritz.c \
                lib/alba/mdac.h \
                lib/alba/mdac.c \
                lib/alba/patch.h \
                lib/alba/patch.c \
                lib/alba/ramp.h \
                lib/alba/ramp.c \
                lib/alba/setupd.h \
                lib/alba/setupd.c \
                lib/alba/stairs.h \
                lib/alba/stairs.c \
                lib/bmp/bmp.h \
                lib/bmp/bmp.c \
                lib/bmp/tempest.h \
                lib/bmp/tempest.c \
                lib/bmp/tplog.h \
                lib/bmp/tplog.c \
                lib/cli/canon.h \
                lib/cli/canon.c \
                lib/cli/cat.h \
                lib/cli/cat.c \
                lib/cli/cli.h \
                lib/cli/cli.c \
                lib/cli/dump.h \
                lib/cli/dump.c \
                lib/cli/fsu.h \
                lib/cli/fsu.c \
                lib/cli/ls.h \
                lib/cli/ls.c \
                lib/cli/mk.h \
                lib/cli/mk.c \
                lib/cli/mv.h \
                lib/cli/mv.c \
                lib/cli/put.h \
                lib/cli/put.c \
                lib/cli/pwd.h \
                lib/cli/pwd.c \
                lib/cli/rm.h \
                lib/cli/rm.c \
                lib/fs/fsd.h \
                lib/fs/fsd.c \
                lib/fs/indir.h \
                lib/fs/indir.c \
                lib/fs/ino.h \
                lib/fs/ino.c \
                lib/fs/link.h \
                lib/fs/link.c \
                lib/fs/map.h\
                lib/fs/map.c \
                lib/fs/mbr.h \
                lib/fs/mkfs.h \
                lib/fs/mkfs.c \
                lib/fs/mknod.h \
                lib/fs/mknod.c \
                lib/fs/mount.h \
                lib/fs/mount.c \
                lib/fs/path.h \
                lib/fs/path.c \
                lib/fs/readf.h \
                lib/fs/readf.c \
                lib/fs/rwr.h \
                lib/fs/rwr.c \
                lib/fs/scan.h \
                lib/fs/scan.c \
                lib/fs/sdc.h \
                lib/fs/sdc.c \
                lib/fs/sfa.h \
                lib/fs/ssd.h \
                lib/fs/ssd.c \
                lib/fs/unlink.h \
                lib/fs/unlink.c \
                lib/hc05/hc05.h \
                lib/hc05/bc3.h \
                lib/hc05/bc3.c \
                lib/hc05/bc4.h \
                lib/hc05/bc4.c \
                lib/hc05/bcx.c \
                lib/hc05/bcy.h \
                lib/hc05/bcy.c \
                lib/isp/ihex.h \
                lib/isp/hvpp.h \
                lib/isp/hvpp.c \
                lib/isp/icsd.h \
                lib/isp/icsd.c \
                lib/isp/icsp.h \
                lib/isp/icsp.c \
                lib/isp/isp.h \
                lib/isp/isp.c \
                lib/key/keyconf.h \
                lib/key/keyconf.c \
                lib/key/keyexec.h \
                lib/key/keyexec.c \
                lib/key/keypad.h \
                lib/key/keypad.c \
                lib/key/keysec.h \
                lib/key/keysec.c \
                lib/lcd/barz.h \
                lib/lcd/barz.c \
                lib/lcd/batteryz.h \
                lib/lcd/batteryz.c \
                lib/lcd/datez.h \
                lib/lcd/datez.c \
                lib/lcd/glyph.h \
                lib/lcd/glyph.c \
                lib/lcd/lcache.h \
                lib/lcd/lcache.c \
                lib/lcd/nlcd.h \
                lib/lcd/nlcd.c \
                lib/lcd/plcd.h \
                lib/lcd/plcd.c \
                lib/lcd/pressurez.h \
                lib/lcd/pressurez.c \
                lib/lcd/temperaturez.h \
                lib/lcd/temperaturez.c \
                lib/lcd/voltagez.h \
                lib/lcd/voltagez.c \
                lib/net/i2c.h \
                lib/net/services.h \
                lib/net/istream.h \
                lib/net/istream.c \
                lib/net/memp.h \
                lib/net/memp.c \
                lib/net/memz.h \
                lib/net/memz.c \
                lib/net/ostream.h \
                lib/net/ostream.c \
                lib/net/twi.h \
                lib/net/twi.c \
                lib/oled/oled.h \
                lib/oled/common.h \
                lib/oled/sh1106.h \
                lib/oled/barp.h \
                lib/oled/barp.c \
                lib/oled/console.h \
                lib/oled/console.c \
                lib/oled/datep.h \
                lib/oled/datep.c \
                lib/oled/iota.h \
                lib/oled/iota.c \
                lib/oled/osetup.h \
                lib/oled/osetup.c \
                lib/oled/vespa.h \
                lib/oled/vespa.c \
                lib/oled/viola.h \
                lib/oled/viola.c \
                lib/oled/vitp.h \
                lib/oled/vitp.c \
                lib/oled/voltagep.h \
                lib/oled/voltagep.c \
                lib/sys/defs.h \
                lib/sys/en_dst.h \
                lib/sys/errno.h \
                lib/sys/ioctl.h \
                lib/sys/adcn.h \
                lib/sys/adcn.c \
                lib/sys/batz.h \
                lib/sys/batz.c \
                lib/sys/clk.h \
                lib/sys/clk.c \
                lib/sys/dmp.h \
                lib/sys/dmp.c \
                lib/sys/eex.h \
                lib/sys/eex.c \
                lib/sys/font.h \
                lib/sys/font.c \
                lib/sys/inp.h \
                lib/sys/msg.h \
                lib/sys/msg.c \
                lib/sys/rv3028c7.h \
                lib/sys/rtc.h \
                lib/sys/rtc.c \
                lib/sys/ser.h \
                lib/sys/ser.c \
                lib/sys/serin.h \
                lib/sys/serin.c \
                lib/sys/stw.h \
                lib/sys/stw.c \
                lib/sys/syscon.h \
                lib/sys/syscon.c \
                lib/sys/sysinit.h \
                lib/sys/timz.h \
                lib/sys/timz.c \
                lib/sys/tty.h \
                lib/sys/tty.c \
                lib/sys/ttynn.c \
                lib/sys/utc.h \
                lib/sys/utc.c \
                lib/sys/ver.h \
                lib/sys/ver.c \
                lib/sys/vitz.h \
                lib/sys/vitz.c \
                optiboot/Makefile \
                optiboot/README.TXT \
                optiboot/boot.h \
                optiboot/stk500.h \
                optiboot/optiboot.c \
                twiboot/Makefile \
                twiboot/README \
                twiboot/twiboot.h \
                twiboot/twiboot.c \
                hal/Makefile \
                hal/*.[ch] \
                etc/alba/* \
                etc/bali/* \
                etc/bluetooth/* \
                etc/key/* \
                regress/* \
                scripts/* \
                bookmaker/*/* \
                doc/*/*
	ps2pdf stuff.ps $(STAMP).pdf
	rm -f stuff.ps
