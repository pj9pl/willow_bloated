#!/bin/bash
# goat/defs.sh - common definitions.

. ../etc/bluetooth/hc05.sh

export logfileprefix=goat
export tmpdir=../tmp
export baud=9600
export port=/dev/`rfcomm|grep ${GOAT_HC05ID}|cut -c-7`
export PORT="-P $port"
