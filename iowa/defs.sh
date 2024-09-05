#!/bin/bash
# iowa/defs.sh - common definitions.

. ../etc/bluetooth/hc05.sh

export logfileprefix=iowa
export tmpdir=../tmp
export port=/dev/`rfcomm|grep ${IOWA_HC05ID}|cut -c-7`
export PORT="-P $port"
