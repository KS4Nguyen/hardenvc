#!/bin/sh

#### pty_echo #######

# Requirments:
#   Install the 'pty' program or provide it's path to $PATH.
#   Also install 'echol'

# Description:
#   Creates a new pseudoterminal under /dev/pts/ and echos everyting (loopback).
#   Verbose mode is activated.
#   Abort with CTRL-C or SIGTERM.
#
#   echol : Loops echo until CTRL-C abort."
#           Argument passed on echol makes it prompt on every new line.

pty -v -c -e 'echol -s "New line instead whitespaces: "'

exit $?


