# hardenvc

## Summary
This package provides userspace programs to connect/reconnect to terminal devices and pseudoterminal devices for automated control of programs/hardware that expects user inputs during runtime to force a desired reaction or result.

In other words: Make non-automated entity-behaviour automatable.

The sourcecodes are written in C, portable to many UNIX-like OS's, using POSIX libraries and standard ISO-C. It also supports, g++ support (tested and compiled with g++ (Ubuntu 9.2.1-9ubuntu2) 9.2.1 and g++ (Raspbian 8.3.0-6+rpi1) 8.3.0 and other embedded OS's.

hcat:   An implementation of HEX-'cat' with optional HEX/ASCII translation and vice versa.

pty:    Starts a program and allocates its stdin and stdout to an pseudoterminal
        device with lowest free device number, e.g. /dev/pts/2. Also, it can start a
        driver program and connects its stdin/stdout to the PTS. This is usefull, when
        you need to run a program in background, but want to re-attach later. In some kind,
        it behaves like a terminal-multiplexer. Another application would be testing
        software that connects to a terminal device, but without actually having real
        hardware plugged.

tcat:   "Terminal connnect and translate". Connects to a TTY/PTS or whatever device, tests
        whether it provides terminal capabilities. With the -t option set, it translates
        all characters typed on the stdin to HEX values, also makes its raw data bytes
        visible in ASCI. This is usefull, if you have a terminal device connected that
        expects data in binary format, e.g. a microchip whoes registers are to be writen
        to or read during runtime. See program help for more information and capabilities
        on the TTY.

All programs use short-option switches. To print usage information and help, type:

  hcat -h
  pty -h 


## Installation
If you just want to get the hcat and pty program run:

  make

If you like to use the programs of this library and the testing stuff aswell, run:

  make all

Installation requires extended user-priviledges:

  sudo make install

Deinstallation:

  sudo make uninstall


## Documentation
Create it by yourself (Doxygen required) by running:

  doxygen

A 'make clean' will remove doxygen build artefacts too.


## Bugreporting/Suggestions
Mail at: sebastian.nguyen86@gmail.com


## Licensing
GPLv3. See LICENSE document.
