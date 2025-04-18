#!/bin/sh

#### daemon_sillytalk.sh #######

# Requirments:
#   Install the 'daemon' program to one of the paths specified in $PATH.
#   Run this script as superuser:
#
#   >  sudo daemon "$(pwd)/daemon_sillytalk.sh"

# Description:
#   Starts a shell detached from you user session, and talks a countdown
#   to the logfile.
#
#   Follow daemon program mechanics on syslog-outputs:
#
#   >  tail -f /var/log/syslog
#
#   As 'daemon' forks into background and detaches from current session ID,
#   its root-path becomes '/', aslong you did not specify the path to run in
#   by typing:
#
#   >  sudo daemon -l <yourpath> "$(pwd)/daemon_sillytalk.sh"
#
#   The logfile of this script will be created at <yourpath>.
#
#   You can check the program being alive by comparing its PID:
#
#   >  pidof sh
#   >  cat /var/run/daemonized_program.pid
#   >  ps -a


cnt=20
delay=4.0
logf=daemon_logfile.txt
msg=''

touch $logf

while [ $cnt -gt 0 ]; do
  msg="Sillytalk Daemon ($(date +%Y-%m-%d_%H:%M:%S)) $cnt"
  printf '%s\n' "$msg" >> $logf
  echo "$msg"

  cnt=$(( $cnt - 1 ))
  sleep $delay
done

echo "Sillytalk Daemon: End."
exit 0
