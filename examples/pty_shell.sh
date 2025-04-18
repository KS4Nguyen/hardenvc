#!/bin/sh

#### pty_shell.sh #######

# Requirments:
#   Install the 'pty' program or provide it's path to $PATH.
#   Have 'echol' compiled in installed aswell.

# Description:
#   Starts a user shell that can be attached at /dev/pts/YX.
#   You can use the 'tcat' program or 'screen' if you have installed it.
#   Abort with CTRL-C or SIGTERM.
#   You also can start 'pty' to background to make it attachable for later.

newshell="${SHELL:-/bin/sh}"

pty -e -r -d $newshell -v 'echol -b 65536 -v -s # '

# For savety, secure the driving shell stdout with '#' ignore line character.
# (modifyed environment).
# The echol loops the shell stdout to the PTS-device with line-buffer size
# 65536. Hence the executed program (here echol) is driven by driver its
# stdout is also connected to the drivers stdin. The echo should never be
# evaluated as command to be executed by the shell. Thus we guard this my
# prompting each echoed with additional '#' character and left align each
# line.

exit 0


