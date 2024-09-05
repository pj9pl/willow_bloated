#!/bin/bash
# lima/defs.sh - common definitions.

. ../etc/bluetooth/hc05.sh

export logfileprefix=lima
export tmpdir=../tmp
export port=/dev/`rfcomm|grep ${LIMA_HC05ID}|cut -c-7`
export PORT="-P $port"
