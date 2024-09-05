#!/bin/sh
# bt_tee_receiver.sh
# appends to the file with the most recent timestamp (using ls), not the most
# recently written file (using ls -rt), that was created by bt_tee_sender.sh
. ./defs.sh
sentf=`ls $tmpdir/$logfileprefix*sentf|tail -1`
ucat <$port |tee -a $sentf
