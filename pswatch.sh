#!/bin/sh

## Control variables:
DELAY=5.0
ROOTSU="$(which sudo)"
SRC="$(pwd)/bin"
SYSLOG=/var/log/syslog

## Check system compatibilities:
if [ ! -e "$SRC" ]; then
  echo "Error: Directory not present ($SRC). Abort"
  exit 1
fi

if [ ! -e "$SYSLOG" ]; then
  echo "Error: Need $SYSLOG for observing syslog outputs. Abort"
  exit 1
fi

## Variables:
pids=''
pslist=''
tmp="$($ROOTSU ls $SRC/)"  # attempt first 'sudo'

## Constants:
RED='\e[91m'
GRN='\e[92m'
DFT='\e[0m'

## Decide which executables to follow:
for i in $tmp ; do
  if [ ! -z "$(ls -l $SRC/$i | awk '{printf $1}' | grep 'x' --color=no)" ]; then
    pslist="$i $pslist"
  fi
done

## hardenvc compiled binaries and scripts:
while [ 1 -eq 1 ]; do
  date "+%Y-%m-%d %H:%M:%S"
  echo "$GRN"
  echo "-- User processes -- $DFT"
  ps -au

  echo "$GRN"
  echo "-- PID watch-list -- $DFT"
  for i in $pslist ; do
    pids="$(pidof $i)"
    printf '%s:\t%s\n' $i "$pids"
  done

  echo "$GRN"
  echo "-- Syslog entries -- $DFT"
  tail -n 10 $SYSLOG

  sleep $DELAY
  clear
done

#/// \todo  Implement 'read' from console to reach here:
## Terminate processes:
for i in $pslist ; do
  $ROOTSU kill -s SIGKILL $i
done

## EOF

