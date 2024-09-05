#!/bin/bash
# bali/defs.sh - common definitions.

. ../etc/bluetooth/hc05.sh

export logfileprefix=bali
export tmpdir=../tmp
export port=/dev/`rfcomm|grep ${ZARA_HC05ID}|cut -c-7`
export PORT="-P $port"
