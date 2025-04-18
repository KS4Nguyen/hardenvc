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

#define BUFLEN   ( 128 )

#define P_IN     1                      // pipe in-port (write) 
#define P_OUT    0                      // pipe out-port (read)

#ifdef LINUX
  #define OPTSTR "+acd:ehiIL:nrt:vx"
#else
  #define OPTSTR "acd:ehiIL:nrt:vx"
#endif

#define MAX_EXEC_LENGTH ( (size_t)128 )

const char *stdin_filename = "standard input";
const char *pname;            // ourselves name
char *driver;                 // driver program
char *driver_args;            // driver program arguments
int fdout;                    // file descriptor to write to
int fdin;                     // file descriptor to read from
struct winsize *stdin_size;
struct termios stdin_termios; // saved original STDIN settings
struct termios orig_termios;  // saved original device settings
int interactive;              // terminal capabilities, atexit(void)


void usage( const char *program_name );


/*!
 * \brief  Userfriendly translation for ON/OFF string representation of int.
 * \param  [IN]     onoff      1 :ON; 0 :OFF
 * \note   Pointer to (static) strings "ON" or "OFF". 
 */
static char *int_onoff( int onoff )
{
  static char state_on[] = "on";
  static char state_off[] = "off";

  if ( 1 == onoff )
    return( &state_on[0] );

  return( &state_off[0] );  
}


static void restore_stdin( void )
{
  tty_reset( STDIN_FILENO, &stdin_termios, stdin_size );
}


static void restore_device( void )
{
  tty_reset( fdin, &orig_termios, NULL );
}


static void cleanup( void )
{
  free( driver );
  free( driver_args );
}


#if defined( SOLARIS )
static void solaris_ldterm( int fd )
{
  int setup = 0;

  // "Check if stream is already setup by autopush facility":    
  if ( 0 > (setup = ioctl( fd, I_FIND, "ldterm" )) ) {
    err_sys( "Device autopush failure" );

    if ( 0 == setup ) { // pckt and ptem are for pseudoterminals
      if ( 0 > (ioctl( fd, I_PUSH, "ldterm" )) )
        err_sys( "Device line discipline settings failure (ldterm)" );
      if ( 0 > (ioctl( fd, I_PUSH, "ttcompat" )) )
        err_sys( "Device line discipline settings failure (ttcompat)" );
    }
  }
}
#endif


int main( int argc, char *argv[] )
{
  int c;                     // chooser for options
  int ignoreeof = 0;         // ignores EOF
  int xon = 0;               // soft flow-control, if terminal device
  int noctl = 1;             // calling process cannot control terminal
  int translate = 0;         // translate ASCII characters to HEX values
  int noecho = 0;            // disable echo to STDOUT
  int ignorelf = 0;          // do not translate linefeed
  int rederr = 0;
  char *newnl = NULL;        // replacing LF after translation HEX <-> ASCII
  int usedriver = 0;         // enabled driver run
  int inpipe = 0;            // assume piped input if STDIN is no TTY
  int help = 0;              // print program help
  int verbose = 0;           // give some additional information
  unsigned int timeout = 0;  // some devices do not process fluently
  const char *target;        // device to open
  struct winsize winsz_user; // our terminal window

  // Initialize program-wide variables:
  interactive = 1;
  sigcaught = 0;
  pname = argv[0];
  opterr = 0;           // from: unistd()

  stdin_size = NULL;
  driver = (char*)malloc( MAX_EXEC_LENGTH );
  driver_args = NULL;  // driver program arguments

  if ( 0 != atexit( cleanup ) )
    err_sys( "Cannot install the exit-handler" );

  while ( EOF != (c = getopt( argc, argv, OPTSTR)) )
  {
    switch( c ) {
      case 'a' : translate = 1;     break;
      case 'c' : noctl = 0;         break;
      case 'd' : driver_args = args_to_argl( driver, optarg, MAX_EXEC_LENGTH );
                 usedriver = 1;     break;
      case 'e' : noecho = 1;        break;
      case 'h' : help = 1;          break;
      case 'i' : ignoreeof = 1;     break;
      case 'I' : ignorelf = 1;      break;
      case 'L' : newnl = optarg;    break;
      case 'n' : interactive = 0;   break;
      case 'r' : rederr = 1;        break;
      case 'v' : verbose = 1;       break;
      case 't' : sscanf( optarg, "%u", &timeout ); break;
      case 'x' : xon = 1;           break;
      case '?' : err_sys( "Unrecognized option: -%c", optopt ); break;
    }
  }

  // Stupid users...
  if ( 1 == help ) {
    usage( pname );
    exit( 0 );
  }

  if ( argc <= optind-1 )
    err_sys( "Usage: %s [ -aehiInrvx -d <DRV> -t <TO> -L <LF> ] <device>", argv[0] );

  if ( 0 == usedriver )
    driver = NULL;
 
  // Allow STDIN connected to other processes STDOUT (piped-mode):.
  if ( 1 == isatty( STDIN_FILENO ) ) {
    inpipe = 0;
    stdin_size = &winsz_user;
    tty_save( STDIN_FILENO, &stdin_termios, stdin_size );

    if ( 0 > atexit( restore_stdin ) )
      err_sys( "atexit() failure for STDIN" );  
  } else {
    inpipe = 1;
    interactive = 0;
  }

  // Identify target device and set terminal modes, if any:
  if ( (argc - optind) < 1 ) {
    // Attach to terminal or loop to STDOUT:
    target = stdin_filename;
    fdin = -1;

    if ( fdout != STDOUT_FILENO ) {
      if ( dup2( fdout, STDOUT_FILENO ) != STDOUT_FILENO )
        err_sys( "Cannot establish echo-loop" );
    }
  } else {
    target = argv[optind];

    ///////////////////////////////
    // Open unknown file type /////
    ///////////////////////////////

    fdin  = open_for_read_or_warn_stdin( target, verbose );
    fdout = open_for_write_or_warn_stdout( target, verbose );

    if ( 1 == isatty( fdin ) ) {
      /////////////////////////////
      // Handle file as terminal //
      /////////////////////////////

      tty_save( fdin, &orig_termios, NULL );
      if ( 0 > atexit( restore_device ) )
        err_sys( "atexit() failure for device" );

    #if defined( SOLARIS )
      solaris_ldterm( fdin );
      solaris_ldterm( fdout );
    #endif

      if ( 1 == interactive )
        tty_interactive( fdin, NULL );
      else
        tty_raw_timeout( fdin, timeout );

      if ( 1 == xon )
        tty_xonoff( fdout );      
    }
  }

  // Inform what is going on:
  if ( 1 == verbose ) {
    fprintf( stderr, "\nDevice or file:  %s\n", target );
    fprintf( stderr, "Interactive:     %s\n", int_onoff( interactive ) );
    fprintf( stderr, "Hex-translation: %s\n", int_onoff( translate ) );
    fprintf( stderr, "Disable echo:    %s\n", int_onoff( noecho ) );
    fprintf( stderr, "Disable control: %s\n", int_onoff( noctl ) );
    fprintf( stderr, "Linefeed:        %s\n", newnl );
  }


  ////////////////////////////////
  // Adjust STDIN to file type ///
  ////////////////////////////////

  if ( 0 == inpipe ) {
    if ( 1 == interactive )
      tty_interactive( STDIN_FILENO, stdin_size );

    if ( 1 == noecho )
      tty_echo_disable( STDIN_FILENO );
  }

  // Start the driver program by attaching STDIN/STDOUT to full-duplex pipe:
  if ( 1 == usedriver )
    do_driver_argl( driver, driver_args, rederr );

  // Install buffer-free-handler for loop_duplex_stdio():
  pty_buffers_atexit();

  if ( SIG_ERR == signal_intr( SIGINT, sig_int ) )
    err_sys( "Failed to install signal handler for SIGINT" );

  // Fork into reader-/writer-process:
  loop_duplex_stdio( fdin, fdout, ignoreeof, translate, BUFLEN, ignorelf, newnl );

  exit( 0 );
}


void usage( const char *program_name )
{
  printf( "Usage: %s [OPTIONS] <device>\n", program_name );
  printf( "  OPTIONS:\n" );
  printf( "    -a       : Translate ASCII to HEX on stdin/stdout and vice versa.\n" );
  printf( "    -c       : Permit control of device terminal.\n" );
  printf( "    -d <DRV> : Driver program to attach to device.\n" );
  printf( "    -e       : Disable echo.\n" );
  printf( "    -h       : Print this help.\n" );
  printf( "    -i       : Ignore EOF on terminal. Do not stop.\n" );
  printf( "    -I       : Do not append CR/LF on write.\n" );
  printf( "    -L <LF>  : Append additional LF on output. (default: none)\n" );
  printf( "               LF can be more than 1 byte long.\n" );
  printf( "    -n       : No-interactive. Do not use terminal modes.\n" );
  printf( "    -t <TO>  : Maximum time [ms] etween subsequent characters.\n" );
  printf( "    -r       : Redirect stderr from driver to device.\n" );
  printf( "    -v       : Show options when executed.\n" );
  printf( "    -x       : Activate device XON/OFF software flow control.\n" );
  printf( "\n  DRV:\n" );
  printf( "    The driver programs stdin/stdout will be connected the terminal.\n" );
  printf( "    This can be usefull, when you want to automate an interactive\n" );
  printf( "    program, or even to attach a new shell to the PTS.\n" );
  printf( "\n  TO:\n" );
  printf( "    After timeout on terminal read, print all data of bufferd line.\n" );
  printf( "    This is usefull, when handling long cables or acting within\n" );
  printf( "    electromagnetic disturbed envirnments, or just in case the\n" );
  printf( "    communication endpoint is a bit slow in processing.\n" );
}


/*
// Some mindless stuff, I still want to try...
     if ( fdin != STDIN_FILENO ) {
       if ( (dup2( fdin, STDIN_FILENO )) != STDIN_FILENO )
         err_sys( "Failed duplicating STDIN to device FD=", fdin );
     }

     if ( fdout != STDOUT_FILENO ) {
       if ( (dup2( fdout, STDOUT_FILENO )) != STDOUT_FILENO )
         err_sys( "Failed duplicating STDOUT to device FD=", fdout );
     }

void *somepipestuff( void *pipefd_rdwr )
{
  int *fd_read = NULL;
  int *fd_write = NULL; 
  int pipe_failure = 0;
  int pipefd[2];

  fd_read  = (int*)&pipefd_rdwr[0];
  fd_write = (int*)&pipefd_rdwr[1];

  // Allow STDIN/STDOUT to be redirected from/to fd_read/fd_write:
  //if ( fd_pipe( pipefd ) < 0 ) // UNIX socket pair
  if ( pipe( pipefd ) < 0 )
    err_sys( "Could not create duplex pipe" );

  DUP2( fd_read, pipefd[P_IN] );
  DUP2( fd_write, pipefd[P_OUT] );
  fprintf( stderr, "Debug: Device piped\n" );

  while ( -1 < nread ) {
    nread = read( pipefd[P_OUT], &buffer, BUFLEN );
    close( pipefd[P_OUT] );
    close( pipefd[P_IN] );
}
*/

// EOF
