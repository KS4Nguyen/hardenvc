/* vi: set sw=4 ts=4: */
#ifndef _PTY_DAEMON_H
  #define _PTY_DAEMON_H

#include <syslog.h>

#ifndef _REENTRANT
  #define _REENTRANT
  #include <pthread.h> // compile with: -lpthread
#endif

/// \todo  Check if _XOPEN_SOURCE 500 is neccessary:
//#define _XOPEN_SOURCE 500
//#include <termios.h>

#define LOCKFILE  "daemonized_program.pid"

//#define DAEMON_HAVE_MAIN 


int is_daemonized( void );


/*!
 * \brief    Check LOCKFILE if daemon is already running. Useful for creating
 *           single-instance daemons.
 * \param    [IN]  no_root_change When set to 1, the LOCKFILE is expected to be
 *                              located in the working directory the program is
 *                              run. When set to 0, the LOCKFILE inside /var/run
 *                              is used.
 * \return   Returns 1 if another daemon is already running, otherwise 0.
 */
int daemon_already_running( int nochdir );


/*!
 * \brief    Make the calling process become a daemon process by fork() and
 *           detaching from calling terminal (run in the background as system
 *           daemon).
 * \note     daemon_daemonize() behaves similar to Linux daemon(), that is not
 *           part of POSIX.1. A similar function appears on the BSDs. The
 *           daemon() function first appeared in 4.4BSD. Hence the GNU C library
 *           implementation of daemon() was taken from  BSD, and does not employ
 *           the double-fork technique, daemon_daemonize() performs the double
 *           fork indeed.
 * /// \todo Actually the 'double-fork' is not implemented yet. Do it, when the
 *           other shit has beed fixed!
 * \param    [IN]  *cmd         The command-line of the caller.
 * \param    [IN]  nochdir      If nochdir is 0, change the current working
 *                              directory to the root directory ("/");
 *                              otherwise, the current working directory is left
 *                              unchanged.
 * \param    [IN]  noclose      If noclose is 0, redirect STDIO and STDERR
 *                              to /dev/null; otherwise, no changes are made to
 *                              these file descriptors.
 * \note     If the programs STDIO and STDERR should be redirected to a new
 *           terminal file, call one of these functions afterwards:
 *           *  daemon_attach_stdio()
 *           *  daemon_open_tty()
 * \return   On success, the new session ID of the calling process is returned.
 *           On error, (pid_t) -1 is returned, and errno is set to EPERM to
 *           indicate the error.
 * \note     Conforming to POSIX.1-2001, POSIX.1-2008, SVr4
 *
 *           Process group:
 *           *  A child created via fork() inherits its parent's session ID. The
 *              session ID is preserved across an execve().
 *           *  A process group leader is a process whose process group ID
 *              equals its PID. Disallowing a process group leader from calling
 *              setsid() prevents the possibility that a process group leader
 *              places itself in a new session while other processes in the
 *              process group remain in the original session; such a scenario
 *              would break the strict two-level hierarchy of sessions and
 *              process groups. In order to be sure that setsid() will succeed,
 *              we call fork() and have the parent exit(), while the child
 *              (which by definition cannot be a process group leader) calls
 *              setsid().
 */
pid_t daemon_daemonize( const char *cmd, int nochdir, int noclose );


/*!
 * \brief    Redirect STDIO/STDERR to existing TTY and save its line-discipline.
 * \param    [OUT] *tp          Fetch terminal capabilities. 
 * \param    [IN]  *tty         The TTY file in the filesystem to attach to.
 * \param    [IN]  steal_tty    1: Try to steal control over the TTY from its
 *                                 original controlling process.
 *                              0: Do not try to get control of the terminal.
 * \warning  In case 'steal_tty' is set, other processes of that process group
 *           will receive SIGHUP and might terminate. Only  works with super-
 *           user rights.
 * \return   On success, the FD associated with open *tty is returned.
 *           On error, -1 is returned and errno is set accordingly.
 * \note     Conforming to POSIX.1-2001, POSIX.1-2008, SVr4
 *
 *           Closing the old TTY-file:
 *           *  If a session has a controlling terminal, and the CLOCAL flag for
 *              that terminal is not set, and a terminal hangup occurs, then the
 *              session leader is sent a SIGHUP signal.
 *           *  If a process that is a session leader terminates, then a SIGHUP
 *              signal is sent to each process in the foreground process group
 *              of the controlling terminal.
 */
int daemon_attach_tty( struct termios *tp, char *tty, int steal_tty,
                       int verbose );


/// \todo  Implement by encapsulating sa-initialization and thread installation for SIGHUP-handler.
void dameon_redirect_stderr_to_syslog( void );


/*
 * Some inspiration of systemd-netlogd:
 *    int getenv_for_pid( pid_t pid, const char *field, char **_value );
 *    bool is_main_thread( void );
 */

#endif // _PTY_DAEMON_H
// EOF
