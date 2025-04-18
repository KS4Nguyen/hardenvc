#!/bin/sh

#### pty_script.sh #######

# Requirments:
#   Install the 'pty' program or provide it's path to $PATH.

# Description:
#   The 'script' program. Run in a new shell and copy everything what appears on
#   console screen to file named 'typescript' or to filename.
#   Verbose mode is activated.
#   Abort with CTRL-C or SIGTERM.

pty "${SHELL:-/bin/sh}" | tee typescript

exit $?


