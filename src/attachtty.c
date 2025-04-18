#ifndef _XOPEN_SOURCE
 #define _XOPEN_SOURCE 500
#endif

#include "pty.h"
#include <sys/types.h>

#include <errno.h>
#include <syslog.h>
#include <stdarg.h>


/*!
 * \brief    From the Linux manpage:
 *           *  The function tcgetsid() returns the session ID of the current
 *              session that has the terminal associated to fd as controlling 
 *              terminal.
 *           *  This terminal must be the controlling terminal of the calling
 *              process.
 * \param    [IN]  fd         Filedescriptor to the SID.
 * \return   When fd refers to the controlling terminal of our session, the
 *           function tcgetsid() will return the session ID of this session.
 *           Otherwise, -1 is returned, and errno is set appropriately.
 * \note     Errors:
 *           *  EBADF         fd is not a valid file descriptor.
 *           *  ENOTTY        The calling process does not have a controlling
 *                            terminal, or it has one but it is not described by
 *                            fd.
 */
extern pid_t tcgetsid( int fd );


/*!
 * \note  From Linx manpage:
 *        *  Makes the process group with process group ID pgrp the foreground
 *           process group on the terminal associated to fd, which must be the
 *           controlling terminal of the calling process, and still be
 *           associated with its session. Moreover, pgrp must be a (nonempty)
 *           process group belonging to the same session as the calling process.
 *        *  If tcsetpgrp() is called by a member of a background process 
 *           group in its session, and the calling process is not blocking or
 *           ignoring SIGTTOU, a SIGTTOU signal is sent to all members of this 
 *           background process group.
 *
 *        Returns:
 *        *  When fd refers to the controlling terminal of the calling process,
 *           the function tcgetpgrp() will return the foreground  process 
 *           group ID of that terminal if there is one, and some value larger 
 *           than 1 that is not presently a process group ID otherwise.
 *        *  When fd does not refer to  the controlling  terminal of the calling 
 *           process, -1 is returned, and errno is set appropriately.
 *        *  When successful, tcsetpgrp() returns 0. Otherwise, it returns -1,  
 *           and errno is set appropriately.
 *
 *        Errors:
 *        *  EBADF            fd is not a valid file descriptor.
 *        *  EINVAL           pgrp has an unsupported value.
 *        *  ENOTTY           The calling process does not have a controlling
 *                            terminal, or it has one but it is not described by
 *                            fd, or, for  tcsetpgrp(), this controlling
 *                            terminal is no longer associated with the session
 *                            of the calling process.
 *        *  EPERM            pgrp has a supported value, but is not the process
 *                            group ID of a process in the same session as the
 *                            calling process.
 */
extern int tcsetpgrp( int fd, pid_t pgrp );


/*!
 * \name  Controlling Terminal: iocntl(), iocntl_tty()
 * \note  From Linux manpage:
 *        *  TIOCSCTTY int arg
 *               Make the given terminal the controlling terminal of the calling
 *               process. The calling process must be a session leader and not
 *               have a controlling terminal already. For this case, arg should
 *               be specified as zero.
 *               >> fork(), setsid(), close open filedescriptors first.
 *
 *               If this terminal is already the controlling terminal of a
 *               different session group, then the ioctl fails with EPERM,
 *               unless the caller has the CAP_SYS_ADMIN capability and arg
 *               equals 1, in which case the terminal is stolen, and all
 *               processes that had it as controlling terminal lose it.
 *               >> Interesting feature...
 *               >> Maybe SIGHUP will be sent to all childs of the bestolen
 *                  processes-group-leader?
 *
 *        *  TIOCNOTTY void
 *               If the given terminal was the controlling terminal of the
 *               calling process, give up this controlling terminal. If the
 *               process  was session leader, then send SIGHUP and SIGCONT to
 *               the foreground process group and all processes in the current
 *               session lose their controlling terminal.
 *
 *        Process group and session ID:
 *        *  TIOCGPGRP pid_t *argp
 *               When successful, equivalent to *argp = tcgetpgrp(fd). Get the
 *               process group ID of the foreground process group on this
 *               terminal.
 *
 *        *  TIOCSPGRP const pid_t *argp
 *               Equivalent to tcsetpgrp(fd, *argp). Set the foreground process
 *               group ID of this terminal.
 *
 *        *  TIOCGSID  pid_t *argp
 *               Get the session ID of the given terminal. This fails with the 
 *               error ENOTTY if the terminal is not a master pseudoterminal and
 *               not our controlling terminal. Strange.
 */


struct termios new_termios;


/*!
 * \brief  Set-up new TTY with STDIO and STDERR.
 * \param  [OUT] *tp            Line discipline content structure.
 * \param  [IN]  *tty           Device name. If passed "-", current STDIO is
 *                              used.
 * \param  [IN]  verbose        Print what is actually going on.
 * \return Returns the filedescriptor associated with the terminal device. 
 */
int daemon_open_tty( struct termios *tp, char *tty, int verbose );


static void _syslog_exit( const char *msg, ... )
{
  int errout = errno;
  va_list ap;

  va_start( ap, msg );
  syslog( LOG_ERR, msg, ap );
  va_end( ap );

  exit( errout );
}


int main( int argc, char **argv )
{
  int fd = STDIN_FILENO;
  char pname[80];
  char device[80];
  pid_t ctty_sid = -1;

  pname[80] = *argv[0];

  if ( 0 > (snprintf( device, 80, "%s", (char*)argv[1] )) ) {
    fprintf( stderr, "Usage: %s <filename>\n", &pname[0] );
    fprintf( stderr, "This program transmitts STDERR to syslog entries.\n" );
  }

  ctty_sid = tcgetsid( STDIN_FILENO );

  syslog( LOG_INFO, "Current terminal session: %i (FD=%i)", fd, ctty_sid );

	/*!
	 * \note  Open will fail on slip lines or exclusive-use lines if not running
	 *        as root; not an error.
	 */

  fd = daemon_open_tty( &new_termios, &device[0], 1 );

  //ttyname_r( fd, device, strlen( device ) );
  switch ( errno ) {
    case EBADF:
       err_quit( "TTY-name: Bad file descriptor" );
       break;
    case ENODEV:
       err_quit( "TTY-name: File descriptor refers to a slave pseudoterminal \
device but the corresponding pathname could not be found" );
       break;
    case ENOTTY:
       err_quit( "TTY-name: File descriptor does not refer to a terminal device" );
       break;
    default:
       break;
  }

  // Check if daemon_open_tty() did it right:
  ctty_sid = tcgetsid( STDIN_FILENO );
  fprintf( stderr, "Attached STDIN (FD=%i) to terminal session %i.\n",
          STDIN_FILENO, ctty_sid );
  syslog( LOG_INFO, "Attached STDIN (FD=%i) to terminal session %i.",
          STDIN_FILENO, ctty_sid );

  char const *new_tty = ttyname( STDIN_FILENO );
  if ( NULL == new_tty ) {
    fprintf( stderr, "Failed to get TTY name for STDIN (FD=%i).\n",
            STDIN_FILENO );
    syslog( LOG_ERR, "Failed to get TTY name for STDIN (FD=%i).",
            STDIN_FILENO );
    exit( EXIT_FAILURE );
  } else {
    fprintf( stderr, "%s: Process group ID: %i\n", new_tty,
            tcgetpgrp( STDIN_FILENO ) );
    syslog( LOG_INFO, "%s: Process group ID: %i", new_tty,
            tcgetpgrp( STDIN_FILENO ) );
  }

  exit( EXIT_SUCCESS );
}


int daemon_open_tty( struct termios *tp, char *tty, int verbose )
{
  #define PATH_MAX  63 // assuming pathname to TTY is somewhere in /dev/ path

  int fd = -1;
  int len = 0;
  int err = 0;
  int closed = 0;
  char buf[PATH_MAX+1];
  pid_t tid, sid;
  const pid_t pid = getpid();

  // Set up new standard input, unless we are given an already opened port:
  if ( strcmp( tty, "-" ) != 0 ) {
    len = snprintf( buf, sizeof( buf ), "%s", tty );
    if ( len < 0 || (size_t)len >= sizeof( buf ) )
		  _syslog_exit( "Path name too long: %s", tty );

    // Open existing file or create, if it does not exist:
    fd = open( buf, (O_RDWR | O_NONBLOCK | O_NOCTTY | O_CREAT), 0 );
    /// \todo  Check if '| O_CREAT' is good for creating a new terminal device.
    if ( 0 > fd ) {
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

    tid = tcgetsid( fd ); // capture terminal ID of device
    if ( 1 == verbose )
      syslog( LOG_INFO, "Opened %s (FD=%i)", tty, fd );

    if ( (tid < 0) || (pid != tid) ) {
      if (ioctl( fd, TIOCSCTTY, 1 ) == -1 ) {
        err = errno;
        errno = err;
        syslog( LOG_WARNING, "PID=%i failed to get control of %s (SID=%i)", pid,
                tty, tid );
        return ( err );
      }
    }

    close( STDIN_FILENO );
		errno = 0;

    /*!
     * \note  ioctl() with TIOCNOTTY
     *        *  Detach the calling process from its controlling terminal. If
     *           the process is the session leader, then SIGHUP and SIGCONT
     *           signals are sent to the foreground process group and all
     *           processes in the current session lose their controlling TTY.
     *        *  This ioctl() call works only on file descriptors connected to
     *           /dev/tty. It is used by daemon processes when they are invoked
     *           by a user at a terminal. The process attempts to open /dev/tty.
     *           If the open succeeds, it detaches itself from the terminal by
     *           using TIOCNOTTY, while - if the open fails - it is obviously
     *           not attached to a terminal and does not need to detach itself.
     */

		if ( ioctl( fd, TIOCNOTTY, 0 ) )
      syslog( LOG_INFO, "TIOCNOTTY ioctl failed" );

    close( fd );
    close( STDOUT_FILENO );
    close( STDERR_FILENO );
    errno = 0;
    closed = 1;

    if ( 0 != (fd = open( buf, (O_RDWR | O_NOCTTY | O_NONBLOCK), 0 )) )
      _syslog_exit( "STDIO redirection failure: Cannot reopen %s", tty );

    if ( ((tid = tcgetsid( fd )) < 0) || (pid != tid) ) {
      if (ioctl( fd, TIOCSCTTY, 1 ) == -1 )
        syslog( LOG_INFO, "Cannot acquire controlling TTY: %s", tty );
    }
  } else {
    // Ensure STDIO is ready for read/write:
    if ( (fcntl( STDIN_FILENO, F_GETFL, 0 ) & O_RDWR ) != O_RDWR )
      _syslog_exit( "%s: STDIN/STDOUT are not ready for read/write", tty );
    fd = STDIN_FILENO;
  }
  
  if ( 0 > (sid = tcsetpgrp( STDIN_FILENO, pid )) )
    _syslog_exit( "%s: Cannot reset to new process-group", tty );

 // Set up new STDOUT and STDERR file descriptors:
  if ( 1 == verbose ) {
    syslog( LOG_INFO, "%s: New process-group ID is %i (PID=%i)", tty, sid,
            pid );  
    syslog( LOG_INFO, "Redirecting STDIO and STDERR" );
  }

  // Get rid of the present outputs:
  if ( 0 == closed ) {
    close( STDOUT_FILENO );
    close( STDERR_FILENO );
    errno = 0;
  }

  // Setup new STDIO:
  if ( dup( STDIN_FILENO ) != 1 || dup( STDIN_FILENO ) != 2 )
    _syslog_exit( "Failed to attach stdin/stdout to %s", tty );

  /*
  if ( setenv( "TERM" , op->term, 1 ) != 0 )
    err_msg(_"Failed to set the %s environment variable", "TERM" );
  */

  return ( fd );
}
// EOF
