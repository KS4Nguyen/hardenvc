/* vi: set sw=4 ts=4: */

/*
 * Copyright (C) 2020
 * Khoa Sebastian Nguyen
 * <sebastian.nguyen@asog-central.de>
 *
 * The hcat() function is based on busybox-1.30.1 source code of bb_cat.c by:
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 * Licensed under GPLv2.
 *
 * full_write(), nonblock_immune_read() and open_or_warn_stdin() are also based
 * on busybox-1.30.1 functions by:
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Licensed under GPLv2 or later.
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

#include <errno.h>
#include <stdarg.h>

#include <signal.h>

/*!
 * \note  Loglevels:
	LOG_EMERG      system is unusable
	LOG_ALERT      action must be taken immediately
	LOG_CRIT       critical conditions
	LOG_ERR        error conditions
	LOG_WARNING    warning conditions
	LOG_NOTICE     normal, but significant, condition
	LOG_INFO       informational message
	LOG_DEBUG      debug-level message
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>     // syslogging

#include "daemon.h"

#define MAX_EXEC_LENGTH       128
#define LOCKMODE              (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)


sigset_t mask;                // guarded by pthread_sigmask()
struct sigaction sa;          // reset SIGHUP

static int daemonized;
static pthread_mutex_t mutex_daemonized = PTHREAD_MUTEX_INITIALIZER;

#if defined( DAEMON_HAVE_MAIN )
  char prog[MAX_EXEC_LENGTH]; // program to run in background
  char *prog_list;            // arguments
#endif


static void _syslog_exit( const char *msg, ... )
{
  int errout = errno;
  va_list ap;

  va_start( ap, msg );
  syslog( LOG_ERR, msg, ap );
  va_end( ap );

  exit( errout );
}


/*!
 * \brief  Make syslog-entries for SIGHUP and SIGBLOCK.
 * \note   This function is to be seen as 'static', and is run by
 *         daemon_redirect_stderr_to_syslog() as a thread.
 */
void *thr_sighandler_syslog_sighup( void *arg );


static void _reread( void )
{
  syslog( LOG_INFO, "Received SIGTERM. Exiting" );
  exit( 0 );
}

/*
static void _sigterm_syslog( int signo )
{
  syslog( LOG_INFO, "Re-reading configuration file" );
  _reread();
}
*/

/*!
 * \brief  Set the close-on-exit flag.
 * \param  fd    Filedescriptor whose settings are to reset.
 *
static int _set_cloexec( int fd )
{
  int val;

  if ( 0 < (val = fcntl( fd, F_GETFD, 0 )) )
    return ( -1 );

  val |= FD_CLOEXEC;
  return ( fcntl( fd, F_SETFD, val ) );
}
 */

static int _lockfile( int fd )
{
  struct flock fl;

  fl.l_type = F_WRLCK;
  fl.l_start = 0;
  fl.l_whence = SEEK_SET;
  fl.l_len = 0;

  return ( fcntl( fd, F_SETLK, &fl ) );
}


#if defined( DAEMON_HAVE_MAIN )
static void usage( const char *program_name )
{
  printf( "Usage: %s [OPTIONS] <program>\n", program_name );
  printf( " OPTIONS:\n" );
  printf( " -h  Print this help.\n" );
  printf( " -u  Unmount-safe. Change working directory to root-directory.\n" );
  printf( "     %s does not prevent other programs and processes from\n",
          program_name );
  printf( "     unmounting directories in the file system.\n" );
  printf( " -v  Verbose mode.\n" );
}


#ifdef LINUX
  #define OPTSTR "+h:uv"
#else
  #define OPTSTR "h:uv"
#endif
int main( int argc, char *argv[] )
{
  int err = -1;     // store errno
  int help = 0;
  int nochr = 1;    // do not change to '/'
  char c;
  int pcnt = 0;
  int verbose = 0;
  int our_pid = getpid();
  pthread_t tid;    // signal-handler thread ID
  pthread_mutex_t mutex_tid = PTHREAD_MUTEX_INITIALIZER;
  char *cmd = NULL; // program full-name with arguments to daemonize

  opterr = 0;               // from: unistd()
  while ( EOF != (c = getopt( argc, argv, OPTSTR)) ) {
    switch( c ) {
      case 'h' : help = 1; break;
      case 'u' : nochr = 0; break;
      case 'v' : verbose = 1; break;
      case '?' : err_sys( "Unrecognized option: -%c", optopt ); break;
    }
  }

  if ( 1 == help ) {
    usage( argv[0] );
    exit( 0 );
  }

  if ( argc <= optind )
    err_sys( "Usage: %s [-huv] \"<program> [args]\"", argv[0] );

  // Parse name of the execution:
  if ( 0 == nochr )
    cmd = strrchr( argv[0], '/' );

  if ( NULL == cmd ) {
    cmd = argv[0];
  } else {
    while ( pcnt < argc ) {
      cmd++;
    }
  }

  prog_list = args_to_argl( &prog[0], argv[optind], (size_t)MAX_EXEC_LENGTH-1 );

  dbg_msg( "Start daemonizing (PID=%i)", our_pid );

  // Become a system daemon:
  daemon_daemonize( cmd, nochr, 1 );

  //////////////////////////////////
  // Inside the child process:    //
  //////////////////////////////////

  // Uniquify to single-instance:
  if ( daemon_already_running( nochr ) ) // unequal to the identity
    _syslog_exit( "Daemon already running" );

  // Restauration of SIGHUP default and block all signals:
  sa.sa_handler = SIG_DFL;
  sigemptyset( &sa.sa_mask );
  sa.sa_flags = 0;

  if ( 0 > sigaction( SIGHUP, &sa, NULL ) )
    err_quit( "%s: Failed to disable SIGHUP", cmd );

  sigfillset( &mask );

  if ( (err = pthread_sigmask( SIG_BLOCK, &mask, NULL )) != 0 )
    err_exit( err, "SIG_BLOCK failure" );

  // Create a thread to handle SIGHUP and SIGTERM:
  err = pthread_create( &tid, NULL, thr_sighandler_syslog_sighup, NULL );
  if ( 0 != err )
    err_exit( err, "Cannot create thread for signal-handler" );

  // Tell what is going on:
  if ( 1 == verbose ) {
    our_pid = getpid();
    fprintf( stderr, "Daemon session ID:        %i\n", getsid( our_pid ) );
    pthread_mutex_lock( &mutex_tid );
    fprintf( stderr, "Signal handler thread ID: %lu\n", tid );
    pthread_mutex_unlock( &mutex_tid );
    fprintf( stderr, "Program to execvp():      %s %s\n", prog, prog_list );
    fprintf( stderr, "SIGHUP disabled.\n" );
  }

  //////////////////////////////////
  // Replace child process image: //
  //////////////////////////////////

  if ( NULL == prog_list ) { // allow seperate arguments and string parsed
    if ( 0 != (err = execvp( argv[optind], &argv[optind] )) )
      err_quit( "Execution error: %s", argv[optind] );
  } else {
    if ( 0 != (err = execlp( prog, prog_list, (char*)NULL )) )
      err_quit( "Execution error: %s", prog );
  }

  exit( 0 );
}
#endif


pid_t daemon_daemonize( const char *cmd, int nochdir, int noclose )
{
  int fd0, fd1, fd2;    // redirected STDIO/STDERR filedescriptors to /dev/null
  int i;                // filedescriptor index
  struct rlimit rl;     // close all open filedescriptors, if noclose is zero
  struct sigaction sa;
  pid_t pid = getpid();

  // Check if already running in background:
  pthread_mutex_lock( &mutex_daemonized );
  if ( 1 == daemonized ) {
    pthread_mutex_unlock( &mutex_daemonized );
    dbg_msg( "daemon_daemonize() called twice: Is already daemonized" );
    return ( pid );
  }

  // Clear file creation mask:
  umask( 0 );

  // Get maximum number of file descriptors:
  if ( 0 > getrlimit( RLIMIT_NOFILE, &rl ) )
    err_quit( "%s: Cannot get file limit", cmd );

  daemonized = 0;
  pthread_mutex_unlock( &mutex_daemonized );

  // Become the session leader to lose controlling TTY:
  if ( 0 > (pid = fork()) )
    err_quit( "%s: Fork failure", cmd );

  /// \todo  Perform the double-fork!
  else if ( 0 != pid )
    ////////////////////////////
    // Inside parent process: //
    ////////////////////////////
    exit( 0 );

  /////////////////////////////
  // Inside child:           //
  /////////////////////////////
  pthread_mutex_lock( &mutex_daemonized );
  pid = setsid();

  // Ensure future opens will not allocate controlling TTYs:
  sa.sa_handler = SIG_IGN;
  sigemptyset( &sa.sa_mask );

  sa.sa_flags = 0;
  if ( 0 > sigaction( SIGHUP, &sa, NULL ) )
    err_quit( "%s: Failed disabling signal SIGHUP", cmd );

  dbg_msg( "SIGHUP disabled" );

  /*!
   * \note  Change the current working directory to root, so we will not
   *        prevent file systems from being unmounted.
   */
  if ( 0 == nochdir ) {
    if ( 0 > chdir( "/" ) )
      err_quit( "%s: Cannot change to root directors '/'", cmd );
  }

  if ( 0 == noclose ) {
    dbg_msg( "Closing open filedescriptors. Redirecting STDIO to /dev/null" );

    // Close all open file descriptors:
    if ( rl.rlim_max == RLIM_INFINITY )
      rl.rlim_max = 1024;

    for ( i=0; i<(int)(rl.rlim_max); i++ )
      close( i );

    /*!
     * \note  After closing all open filedescriptors, error-messaging only can
     *        be done by syslog().
     */

    // Attach STDIO and STDERR to /dev/null:
    fd0 = open( "/dev/null", O_RDWR ); /// \todo Allow redirecting STDIO.
    fd1 = dup( 0 ); // fd1 = dup( fd0 );
    fd2 = dup( 0 ); // fd2 = dup( fd0 );

    // Initialize the log-file:
    if ( fd0 != 0 || fd1 != 1 || fd2 != 2 )
      _syslog_exit( "Unexpected FDs (%d, %d, %d) for replacing STDIO streams",
                    fd0, fd1, fd2 );
  }

  if ( 0 > pid )
    errno = EPERM;

  daemonized = 1;
  pthread_mutex_unlock( &mutex_daemonized );

  dbg_msg( "Daemonized to new session (PID=%i)", pid );
  return ( pid );
}


int daemon_attach_tty( struct termios *tp, char *tty, int steal_tty,
                       int verbose )
{
  #define PATH_MAX  63 // assuming pathname to TTY is somewhere in /dev/ path

  int err = 0;
  int fd;
  char buf[PATH_MAX+1];
  pid_t tid;            // target terminal session ID *tty belongs to
  pid_t pid = getpid();
 

  pthread_mutex_lock( &mutex_daemonized );
  if ( 0 == daemonized ) {
    pthread_mutex_unlock( &mutex_daemonized );
    syslog( LOG_WARNING, "daemon_attach_tty() PID=%i is not daemonized", pid );
    return ( -1 );
  }
  pthread_mutex_unlock( &mutex_daemonized );

  // Open existing file:
  if ( 0 > (fd = open( buf, (O_RDWR | O_NONBLOCK | O_NOCTTY), 0 )) ) {
  /// \todo  Check if '| O_CREAT' is good for creating a new terminal device.
    err = errno;
    if ( err == EBUSY || err == EACCES ) {
      errno = err;
      _syslog_exit( "Open failure: No access to %s", tty );
    }

    if ( err == EPERM ) {
      errno = err;
      _syslog_exit( "No permission to open %s", tty );
    }

    _syslog_exit( "PID=%i failed opening %s (unknown reason)", tty );
  }

  if ( 1 != isatty( fd ) )
    _syslog_exit( "%s is not a TTY. Abort.", tty );

  tid = tcgetsid( fd );

  // Session ID must belong to caller (normally daemonized does ensure this):
  if ( (tid < 0) || (pid != tid) ) {
    if ( 1 == steal_tty ) {
      if ( ioctl( fd, TIOCSCTTY, 1 ) < 0 ) {
        syslog( LOG_WARNING, "PID=%i failed to get control of %s (SID=%i)", pid,
                tty, tid );
        return ( -1 );
      }
    }
  }

  // Open a new file:
  if ( (fcntl( STDIN_FILENO, F_GETFL, 0 ) & O_RDWR) != O_RDWR ) {
    syslog( LOG_INFO, "%s is not open for read/write", tty );
    return ( -1 );
  }

  /// \todo THIS IS KEY:
  if ( tcsetpgrp( STDIN_FILENO, pid ) )
    _syslog_exit( "%s: Failed to reset process group", tty );

  // Set up new STDOUT/STDIN:
  if ( dup( STDIN_FILENO ) != 1 || dup( STDIN_FILENO ) != 2 )
    _syslog_exit( "%s: dup() failure. Cannnot recreate stdin/stdout", tty );

  // Unbreak loose FD for uncontrolled STDIO:

  // Attach to controller SID:

  return ( 1 );
}


int daemon_already_running( int nochdir )
{
  int fd;
  char buf[16];
  char lockf_concat[strlen( LOCKFILE ) + strlen( "/var/run/" ) + 1];

  if ( 1 == nochdir )
    sprintf( lockf_concat, "%s", LOCKFILE );
  else
    sprintf( lockf_concat, "/var/run/%s", LOCKFILE );

  pthread_mutex_lock( &mutex_daemonized );
  if ( 0 == daemonized ) {
    pthread_mutex_unlock( &mutex_daemonized );
    return( 1 );
  }
  pthread_mutex_unlock( &mutex_daemonized );

  fd = open( lockf_concat, (O_RDWR | O_CREAT), LOCKMODE );
  if ( 0 > fd ) {
    syslog( LOG_ERR, "Cannot open %s: %s", lockf_concat, strerror( errno ));
    exit( 1 );
  }

  if ( 0 > _lockfile( fd ) ) {
    if ( (errno == EACCES) || (errno == EAGAIN) ) {
      close( fd );
      return( 1 );
    }

    syslog( LOG_ERR, "Cannot lock %s: %s", lockf_concat, strerror( errno ));
    exit( 1 );
  }

  ftruncate( fd, 0 ); // flush PID information of previous running daemon
  sprintf( buf, "%ld", (long)getpid() );
  write( fd, buf, strlen( buf )+1 );

  return( 0 );
}


int is_daemonized( void )
{
  int tmp = 0;
  pthread_mutex_lock( &mutex_daemonized );
  tmp = daemonized;
  pthread_mutex_unlock( &mutex_daemonized );

  return ( tmp );
}


void *thr_sighandler_syslog_sighup( void *arg )
{
  int err, signo;

  for ( ;; ) {
    err = sigwait( &mask, &signo );
    if ( err != 0 )
      _syslog_exit( "sigwait() failure" );
  }

  // Inform user about daemonized process status:
  switch ( signo ) {
    case SIGHUP:
      syslog( LOG_INFO, "Re-reading configuration file" );
      _reread();
      break;

    case SIGTERM:
      syslog( LOG_INFO, "SIGTERM catched. Exit." );
      exit( 0 );
      break;

    default:
      syslog( LOG_ERR, "Unexpected signal: %d\n", signo );
  }
}

// EOF
