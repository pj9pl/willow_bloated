#!/bin/bash
# oslo/defs.sh - common definitions.

. ../etc/bluetooth/hc05.sh

export logfileprefix=oslo
export tmpdir=../tmp
export port=/dev/`rfcomm|grep ${CYRIL_HC05ID}|cut -c-7`
export PORT="-P $port"
