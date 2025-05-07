/* vi: set sw=4 ts=4: */

/*
 * Copyright (C) 2020
 * Khoa Sebastian Nguyen
 * <sebastian.nguyen@asog-central.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "pty.h"
#include <signal.h>
#include <time.h>
#include "daemon.h"

#define BUFLEN              1024
#define DEFAULT_TIMEOUT     1000
#define TIMEOUT_GRANULARITY 40
#define POLLING_TIMEOUT     10

#define ms_sleep( x )       usleep( 1000 * x )

#define MAX_EXEC_LENGTH     128


int fdm;      // master side PTS
int fds;      // slave side PTS
int detached; // run as detached background process
struct termios orig_termios;
struct winsize orig_size;
struct winsize *size;
pid_t child_pids;
char *cmd;    // own commandline
char *prog;   // pexec() of program to start
char *prog_list;
char *driver; // pexec() of driver
char *driver_list;


/*!
 * \brief     Terminal help when option -h is passed.
 */
void usage( char *prog_name );


/*!
 * \brief     Exit-hander to restore user original terminal settings. And to
 *            terminate running child processes.
 * \note      By convention when using as a reference of atexit() argument, this
 *            signal handler takes no arguments and has no return-value.
 * \note      POSIX.1 allows the result of exit() as undefined, if called more
 *            than once. An exit-hander could call exit(), which would result
 *            in an infinite recursion. Linux does not allow that. To prevent
 *            problems, this exit-handler does not do that.
 */
static void cleanup( void )
{
  if ( child_pids > 0 ) {
    if ( 0 > kill( child_pids, SIGTERM ) ) // terminate child running execp()
      err_msg( "Failed sending SIGTERM to child processes" );
  }

  if ( 0 == detached )
    tty_reset( fdm, &orig_termios, size );

  free( prog );
  if ( NULL != prog_list )
    free( prog_list );
  if ( NULL != prog )
    free( driver );
  if ( NULL != driver_list )
    free( driver_list );
  if ( NULL != cmd )
    free( cmd );
}


/*!
 * \brief  Fork to child: It copies STDIN to PTS-master.
 *         Parent: It copies PTS-master to STDOUT.
 * \param  [IN]  fdm          Filedescriptor of the master.
 * \param  [IN]  ignore_eof   Ignore EOF character (inifinite run).
 */
void ptym_process_stdio( int pty_amaster, int ignore_eof );


char *int_onoff( int onoff )
{
  static char state_on[] = "on";
  static char state_off[] = "off";

  if ( 1 == onoff )
    return( &state_on[0] );

  return( &state_off[0] );  
}


/*!
 * \brief  Automate an interactive program from non-interactive source by
 *         connecting the programs STDIN and STDOUT to the source.
 * \param  [IN]  argc         Number of parameters passed.
 * \param  [IN]  **argv       pty-program options and arguments.
 * \return Returns 0 when finished, or error-code.
 * \option Default: User TTY (common case is the user starts 'pty' from a shell)
 * \image  html               pty_user.png
 * \option -c     : User and other processes connected to /dev/pts/X
 * \image  html               pty_ctrl.png
 * \option -d     : Driver program (automated I/O)
 * \image  html               pty_driver.png
 */
#ifdef LINUX
  #define OPTSTR "+bcd:ehinruv"
#else
  #define OPTSTR "bcd:ehinruv"
#endif
int main( int argc, char **argv )
{
  int    ignoreeof = 0;        // allow background process (detached STDIN)
  int    noecho = 0;           // 'echo' is for interactive endpoints only 
  int    help = 0;             // stupid users
  int    verbose = 0;
  int    nochr = 0;
  int    interactive = 1;      // window and line manipulation
  int    nocontrol = 1;        // do not allow control of the PTS
  int    rederr = 0;           // redirect driver stderr to PTS
  int    c;                    // option choser
  pid_t  pid;                  // parent/child process ID after fork()
  char   slave_name[PTS_NAME_LENGTH];

  detached = 0;
  size = NULL;
  cmd = NULL;
  prog_list = NULL;
  driver = NULL;
  driver_list = NULL;
  prog = (char*)malloc( MAX_EXEC_LENGTH );
  interactive = isatty( STDIN_FILENO );
  sigcaught = 0;
  child_pids = 0;

  opterr = 0;           // from: unistd()
  while ( EOF != (c = getopt( argc, argv, OPTSTR)) )
  {
    switch( c ) {
      case 'b' : detached = 1;      break;
      case 'c' : nocontrol = 0;     break;
      case 'd' : if ( NULL == (driver = (char*)malloc( MAX_EXEC_LENGTH )) )
                   err_sys( "Not enough space for argument parsing" );
                 driver_list = args_to_argl( driver, optarg,
                                             (size_t)MAX_EXEC_LENGTH );
                                    break;
      case 'e' : noecho = 1;        break;
      case 'h' : help = 1;          break;
      case 'i' : ignoreeof = 1;     break;
      case 'n' : interactive = 0;   break;
      case 'r' : rederr = 1;        break;
      case 'u' : nochr = 1;         break;
      case 'v' : verbose = 1;       break;
      case '?' : err_sys( "Unrecognized option: -%c", optopt ); break;
    }
  }

  if ( 1 == help ) {
    usage( argv[0] );
    exit( 0 );
  }

  if ( argc <= optind )
    err_sys( "Usage: %s [-bcehinruv -d \"driver [args]\"] \"<program> [args]\"",
             argv[0] );

  if ( (1 == verbose) && (1 == detached) )
    err_msg( "Option '-b' is not implemented yet. Sorry.\n" );

  /// \todo  Implement option-check for singleton and multi string program.
  prog_list = args_to_argl( prog, argv[optind], (size_t)MAX_EXEC_LENGTH );

  // Daemonize and reestablish STDIO before duplex:
  if ( 0 == detached ) {
    nochr = 0;
  } else {
    rederr = 0;
    cmd = (char*)malloc( 128 * sizeof( char ) );

    // Track full name of the execution for syslogging:
    if ( 0 == nochr )
      cmd = strrchr( argv[0], '/' );

    if ( NULL == cmd ) {
      cmd = argv[0];
    } else {
      c = 0;
      while ( c < argc ) {
        cmd++;
      }
    }

    if ( 1 == daemon_already_running( nochr ) ) {
      syslog( LOG_ERR, "Daemon already running" );
      exit( 1 );
    }

    daemon_daemonize( cmd, nochr, 1 );
  }

  tty_save( STDIN_FILENO, &orig_termios, &orig_size );
  size = &orig_size;

  // Create PTY-master/slave with appropriate terminal settings:
  if ( 1 == interactive )
    pid = pty_fork_init( &fdm, slave_name, sizeof( slave_name ), size,
                         nocontrol );
  else
    pid = pty_fork_init( &fdm, slave_name, sizeof( slave_name ),
                         NULL, nocontrol );

  // Forked from pty_fork_init():
  if ( 0 > pid ) {
    err_sys( "Failed to fork into master/slave-processes" );
    exit( 1 );
  }

  if ( 0 == pid ) {
    //////////////////////////////////
    // Inside the child process:    //
    //////////////////////////////////
    if ( 1 == noecho )
     tty_echo_disable( STDIN_FILENO );

    //////////////////////////////////
    // Replace child process image: //
    //////////////////////////////////
    if ( NULL == prog_list ) { // allow seperate arguments and string parsed
      if ( execvp( argv[optind], &argv[optind] ) )
        err_sys( "Execution error: %s", argv[optind] );
    } else {
      if ( execlp( prog, prog_list, (char*)NULL ) )
        err_sys( "Execution error: %s", prog );
    }
  }

  ////////////////////////////////
  // Inside the parent process: //
  ////////////////////////////////
  child_pids = pid;

  /*!
   * \note   Install the exit-hander to restore users terminal settings as soon
   *         as possible after forking into parent process ID.
   */

  if ( 0 != atexit( cleanup ) )
    err_sys( "Cannot install the exit-handler" );

  if ( SIG_ERR == signal_intr( SIGINT, sig_int ) )
    err_sys( "Failed to install signal handler for SIGINT" );

  // Tell what is going on:
  if ( (1 == verbose) && (0 == detached) ) {
    err_msg( "PTY-slave:        %s\n", slave_name );
    err_msg( "Interactive:      %s\n", int_onoff( interactive ) );
    err_msg( "Ignore EOF:       %s\n", int_onoff( ignoreeof ) );
    err_msg( "No TTY control:   %s\n", int_onoff( nocontrol ) );
    err_msg( "Program:          %s %s\n", prog, prog_list );
    err_msg( "Driver:           %s %s\n", driver, driver_list );
  }

  // In Stevens and Ragos book they say '1 == interactive':
  if ( (NULL != driver) || (0 == interactive) ) {
    if ( 0 > tty_raw_blocking( STDIN_FILENO, 0 ) ) // read 1 byte at a time
      err_sys( "Cannot set STDIN raw-mode" );
  }

  // Start driver program whose STDIO is full-duplex with PTY: 
  if ( NULL != driver )
    do_driver_argl( driver, driver_list, rederr );

  // Duplicate STDIN to PTY-master, and PTY-master to STDOUT:
  ptym_process_stdio( fdm, ignoreeof ); // original: loop()

  exit( 0 ); // should never reach here, due to ptym_process_stdio() is a loop.
}


void ptym_process_stdio( int pty_amaster, int ignore_eof )
{
  int   nread = 0;
  int   pac = 0; // parent abort condition (on read)
  pid_t child;
  char  buf[BUFLEN];

  //fflush( stdin );
  //fflush( stdout );

  if ( (child = fork()) < 0 ) {
    err_sys( "Failed forking into read/write loop" );
  } else if ( 0 == child ) {

    ///////////////////////////
    // Inside child process: //
    ///////////////////////////

    close( STDOUT_FILENO );

    while ( -1 < (nread = read( STDIN_FILENO, &buf, BUFLEN )) ) {
      if ( 0 == nread ) {
        if ( 0 == ignore_eof )
          break;
      } else {
        if ( 0 > (write( pty_amaster, &buf, nread )) ) {
          err_msg( "Failed writing to PTY-master FD=%i", pty_amaster );
          break;
        }
      }

      //ms_sleep( POLLING_TIMEOUT ); // reduce CPU load, better in VMIN/VTIME
    }

    /*!
     * \note    Normally we terminate after data transmission has finished (EOF
     *          detected).
     *          Also, a EOF on stdio is equivalent with a HUP-signal to a
     *          controlling terminal. Enjoying
     *          the communicative silence by ignoring any EOF won't stop us from
     *          doing nothing. Hence we somehow fell out of the while-ing, we
     *          better tell the more responsible parent process about our
     *          failure in doing nothing or something.
     */
    if ( 1 == ignore_eof ) {
      // Child notifies parent, because we should not be here, but no hurry...
      ms_sleep( 5 ); // ...maybe the parent has not finished registering the
      // SIGTERM at the kernel so slowly fast:
      kill( getppid(), SIGTERM );
      /*
       * Besides, a 'kill' from a child reaches all sisters and brothers of this
       * process group, if the parent has granted so.
       */
    }
    
    if ( 0 < nread )
      err_sys( "Read failure on STDIN" );

    exit( 0 ); // child cannot return
  }

  ////////////////////////////
  // Inside parent process: //
  ////////////////////////////

  /*!
   * \note:  Let PTY slave side open after forking, because driver-program is
   *         connected on parent-slave if option set:
   *         *  close( STDIN_FILENO );
   */

  if ( SIG_ERR == signal_intr( SIGTERM, sig_term ) )
    err_sys( "Failed to install signal handler for SIGTERM" );

  if ( ignore_eof > 0 )
    pac = -1; // read aslong more than 'pac' bytes received.
  else
    pac = 0; // marks EOF reached

  // Read/write till error, a valid EOF detected or signal interrupt:
  while ( pac < (nread = read( pty_amaster, &buf, BUFLEN )) ) {
    if ( nread > 0 ) {
      if ( write( STDOUT_FILENO, &buf, nread ) != nread  ) {
        err_msg( "Failed writing to STDOUT" );
        break;
      }
    }
    //ms_sleep( POLLING_TIMEOUT ); // reduce CPU load average
  }

  // Prevent child from growing-up as a orphan in a zombie nation:
  if ( 0 == sigcaught )
    kill( child, SIGTERM ); // child did not cause the interruption

  if ( 0 < nread )
    err_sys( "Read failure on PTY-master" );

  // Signal caught, error occured or EOF detected, parent returns to caller.
}


void usage( char *prog_name )
{
  printf( "Usage: %s [OPTIONS] \"<program> [ARGS]\"\n", prog_name );
  printf( "  Run a program connected to a PTY/PTS device (pseudo-terminal.)\n" ); 
  printf( "\n  OPTIONS:\n" );
  printf( "    -b        Run in background (detached from user session).\n" );
  printf( "    -c        Do not allow parent process control the terminal.\n" );
  printf( "    -d <drv>  Redirect programs stdin/stdout to driver program.\n" );
  printf( "    -r        Redirect driver stderr to terminal device.\n" );
  printf( "    -e        Disable echo on terminal output.\n" );
  printf( "    -i        Ignore EOF on read (Use: CTRL-C to stop).\n" );
  printf( "    -n        No interactive.\n" );
  printf( "    -v        Verbose mode. Print additional information on stderr.\n" );
  printf( "    -u        Unmount protected. Change to '/' root directory\n" );
  printf( "              (takes only effect when -b is set).\n" );
  printf( "    -h        Print this help.\n" );
  printf( "\n  ARGS:\n" );
  printf( "    Optional arguments for <program> and <drv>. Use quoted\n" );
  printf( "    strings to seperate these from %s arguments:\n", prog_name );
  printf( "    %s -e -d \"<drv> <args>\" -c \"<program> <args>\"\n",
          prog_name );
  printf( "\n  Notes:\n" );
  printf( "    <drv> and <program> name size is limitted to %d.\n",
          MAX_EXEC_LENGTH );
  printf( "    When running in background ('-b' option set) daemon PID is\n" );
  printf( "    stored in /var/run/%s, and '-r' option is ignored.\n",
          LOCKFILE );
  //printf( "    <args> is limitted to %d characters include whitespaces.\n",
  //        MAX_ARGS_LENGTH );
}
// EOF
