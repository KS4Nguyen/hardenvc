################################################################################
## \name        Makefile
## \author      ksnguyen
## \date        2020-05-30
## \description Makefile for the PTY package.
## \note        License information below and within this package LICENSE file
################################################################################

################################################################################
## Khoa Sebastian Nguyen
## <sebastian.nguyen@asog-central.de>
##
## This program is free software; you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
##
################################################################################

debug := on

CFLAGS += -Wall
ifeq ($(debug),on)
	CFLAGS += -D=DEBUG
	CFLAGS += -Werror -g
#	CFLAGS += -fmax-errors=6
else
	CFLAGS += -O0 -Wno-unused -Wnonnull
	CFLAGS += -fstack-protector #-Wstack-protector
	CFLAGS += -Wstringop-overflow #-Wstringop-truncation
endif

## compiler version
#GCC := gcc
GCC := g++

SRC := ./src
LIBS += -L./lib
IPATH := /usr/bin

PROGRAMS := tcat hcat echol attachtty


.PHONY: all clean $(PROGRAMS)

pty:
	@echo "Compiling: $@"
	$(GCC) $(CFLAGS) $(SRC)/pty.c $(SRC)/daemon.c $(SRC)/main.c -o ./bin/$@ -lpthread

$(PROGRAMS):
	@echo "Compiling: $@"
	$(GCC) $(CFLAGS) $(SRC)/pty.c $(SRC)/$@.c -o ./bin/$@

all: pty $(PROGRAMS) daemon capture exitchecks tools

daemon:
	@echo "Compiling: $@"
	$(GCC) $(CFLAGS) -D=DAEMON_HAVE_MAIN $(SRC)/pty.c $(SRC)/$@.c -o ./bin/$@ -lpthread

setsid:
	@echo "Compiling: $@"
	$(GCC) $(CFLAGS) $(SRC)/$@.c -o ./bin/$@


exitchecks:
	@echo "Compiling: $@"
	$(GCC) -Wall -O0 $(SRC)/pty.c $(SRC)/$@.c -o ./bin/$@

capture:
	@echo "Compiling: $@"
	gcc -Wall -O0 $(SRC)/$@.c -o ./bin/$@

tools: capture tcat hcat echol exitchecks setsid
	@echo "Generating test-files in ./bin"
	@sh -c 'if [ ! -e ./bin/ffifo ] ; then mkfifo ./bin/ffifo ; fi'
	@sh -c 'cp ./src/*.sh ./bin/ && chmod a+x ./bin/*.sh'

test: $(PROGRAMS)
	@sh -c ./bin/run_tests.sh

install: $(PROGRAMS) pty daemon
	@echo "Installing $(PROGRAMS) pty -> $(IPATH) ..."
	@cd ./bin && if [ -d $(IPATH) ] ; then install -C -v -t $(IPATH) $(PROGRAMS) pty daemon; fi

uninstall:
	@echo "Uninstalling $(PROGRAMS) pty"
	cd $(IPATH) && rm $(PROGRAMS) pty daemon

documenation:
	doxygen

clean:
	@sh -c 'touch ./doc/html/dummy && rm -rf ./doc/html/*'
	@sh -c 'touch ./bin/dummy && rm ./bin/*'
	@sh -c 'touch ./lib/dummy && rm ./lib/*'
	@sh -c 'touch ./dummy.log && rm *.log'

## EOF
