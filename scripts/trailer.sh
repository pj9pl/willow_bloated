#!/bin/sh
# trailer.sh
. ./defs.sh
file=`ls -rt $tmpdir/${logfileprefix}*|tail -1`
tail -f $file 
