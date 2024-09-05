#!/bin/sh
# bt_tee_sender.sh
# creates a temp file to capture both sides of the exchange
. ./defs.sh
stamp=`date "+%C%y%m%d%H%M%S"`
touch .history
stty sane <$port
#stty 115200 -echo igncr -icrnl <$port
stty 115200 -echo igncr -icrnl -icanon onocr ocrnl -onlcr <$port
#stty 115200 raw <$port
rlcat -a $tmpdir/$logfileprefix.$stamp.sentf
