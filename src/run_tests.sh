#!/bin/sh
starttime="$(date)"
endtime=''


###################################################
## Test OS abilities: STDERR, atexit()-recursion ##
###################################################
test_exit()
{
	if [ -e ./bin/exitchecks ]; then
		./bin/exitchecks -v -m -s -e 5
	fi
}


###################################################
## pty-program in loopback-test (no driver) #######
###################################################
test_pty_nodriver()
{
	if [ -e ./bin/pty ]; then
	  #  /// \todo  Abort after certain time.
		./bin/pty -v -n -e ./bin/echol logfile.txt
	fi
}


###################################################
## pty-program in loopback-test (with driver) #####
###################################################
test_pty()
{
  #  /// \todo  Write test.
  echo ''
}


###################################################
## Test ttychat HEX-translation ###################
###################################################
test_pty()
{
	INPUT="test"
  EXPECTED="746573740a"

  if [ -e ./bin/pty ]; then
    #echo $INPUT | ./bin/ttychat -i -t $INPUT | grep $EXPECTED
    ./bin/tcat -i -t $INPUT
    if [ $? -eq 0 ]; then
      printf '\nTest ttychat: Success\n'
    else
      printf '\nTest ttychat: Failure\n'
    fi
  fi
}


printf '\nStarting unit tests and pty-testloop\n'
printf '\nStart time: %s\n' "$starttime"
printf '\nAbort with CTRL+C\n\n'

#test_exit
printf '\n'

#test_pty_nodriver
printf '\n'

test_pty
printf '\n'

endtime="$(date)"
printf '\nEnd time: %s\n\n' "$endtime"

exit 0
