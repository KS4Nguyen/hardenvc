/* vi: set sw=4 ts=4: */

/*!
 * \version  1.0.5
 * \author   ksnguyen
 * \date     2020-02-15   Library created.
 * \date     2020-03-22   Fully tested for non _REENTRANT functions.
 * \date     2020-03-23   Implemented POSIX compliant rentrant versions for
 *                        multithreaded programs.
 * \date     2020-04-11   Adjusted functions from coreutils to read from stdin
 *                        and write to stdout.
 * \date     2020-04-15   Added struct winsize for portability in case the OS
 *                        does not provide TIOCGWINSZ.
 * \date     2020-04-28   Optimized tty_<functions> and wrote some Doxygen
 *                        comments for them.
 * \data     2020-04-29   After still having trouble with the err_sys() and
 *                        err_msg() function to format datatypes other than
 *                        char*, I just kicked out these implementations and
 *                        used the ones provided by Stevens book.
 *                        Introduced TTY linediscipline check for RAW mode and
 *                        diverted the tty_raw() capabilities to:
 *                        1)  tty_raw_timeout() for polling and ensure reliable
 *                            communication with tardy serial devices.
 *                        2)  tty_raw_blocking() whose purpose can be expecting
 *                            a minimum number of bytes to receive from the TTY,
 *                            e.g. you read want to read out a register set from
 *                            a microcontrol unit and you know how many bytes
 *                            will arrive.
 *                        Carefully checked and applied the correct use of the
 *                        TCSANOW/FLUSH/DRAIN effects.
 * \date     2020-05-09   String with program arguments to pointer of argv[].
 *                        Fixed do_driver() with do_driver_argl() and
 *                        do_driver_argv().
 * \date     2020-05-25   Debug message added; Added stricpy() from echol.c.
 *                        Implemtented automatic address-freeing mechanism to
 *                        functions using dynamic memory allocations (using
 *                        atexit()).
 * \date     2020-05-30   Fixed loop_duplex_stdio() linefeed settings for option
 *                        'nolf' ignoring CR/NL on STDIN.
 *                        Added window-size settings and restore.                 
 *
 * \note
 *           The source code of this library is intended to for implementations
 *           of multi-threaded or multi-processed programs that create and
 *           control pseudoterminal devices. In addition this library is
 *           intended to be compatible to at least these UNIX like platforms:
 *
 *             UNIX
 *             GNU/Linux
 *             Solaris
 *             System V
 *
 *           These functions are intended to create pseudo terminal interfaces
 *           within a UNIX environment. This is usefull for testing terminal
 *           programs driving hardware terminal devices.
 *
 * \note     The author of this library has worked through chapter 19 of W.
 *           Richard Stevens, Steven A. Rago "Advanced Programming in the UNIX
 *           Environment" (3rd edition), describing different architectural
 *           concepts for implementing user-space programs that create one or
 *           more PTS devices at runtime. The functions of this library are
 *           original to book ones. Additional functions where added for
 *           working with POSIX thread library.
 *
 *           The functions of the producer/consumer model to replace the loop()
 *           (pty.c example on page 732) are implemented to be POSIX thread-
 *           ready:
 *
 *             pty_process_stdio() [forking functions]
 *             ptym_to_stdout()    [reentrant]
 *             ptym_from_stdin()   [reentrant]
 *
 *           In case of implementing a multiprocess program, the initialization
 *           steps within pty_fork_init() are:
 *
 *             ptym_open()
 *             ptys_open()
 *
 *           For multithreaded programs the initialization steps are aggregated
 *           to:
 *
 *             pty_pair_init()
 */

#ifndef _PTY_H
  #define _PTY_H

#ifndef SOLARIS
  #ifndef _GNU_SOURCE
    #define _GNU_SOURCE       // this shit took me a whole day to understand
  #endif
#endif

#include <stdio.h>
#include <stdlib.h>           // grantpt(), unockpt(), posix_openpt(), ptsname()
                              // exit(), [atexit() >> main.c]
#include <string.h>           // strncpy(), malloc()
#include <unistd.h>           // read(), write(), opterr

#if defined( SOLARIS )
  #include <stropts.h>
#endif

#include <fcntl.h>            // posix_openpt(), open() O_FLAGS
#include <termios.h>          // termios, tcgetattr(), tcsetattr(), ttyname()

#ifndef TIOCGWINSZ
  #include <sys/ioctl.h>
#else
  #ifndef winsize
  typedef struct {
    unsigned short ws_row;    // rows, in characters
    unsigned short ws_col;    // columns, in characters
    unsigned short ws_xpixel; // horizontal size, pixels (unused)
    unsigned short ws_ypixel; // vertical size, pixels (unused)
  } winsize;
  #endif
#endif

#define PTS_NAME_LENGTH 20    // such as /dev/pts/XY

#include <signal.h>


/*!
 * \param  [IN]    *msg  Error message.
 * \brief  Print and error message, but let program be alive. Should do
 *         further error-handling.
 */
void err_msg( const char *msg, ... );


/*!
 * \brief  Terminate the program with error-message.
 * \param  [IN]    *msg  Error message.
 * \note   Fatal error related to a system call.
 */
void err_sys( const char *msg, ... );


/*!
 * \brief  Terminate the program with error-message.
 * \param  [IN]    *msg  Error message.
 * \note   Fatal error unrelated to a system call.
 */
void err_quit( const char *msg, ... );


void err_exit( int error, const char *msg, ... );


/*!
 * \brief  Debugging message print with N arguments passed.
 * \param  [IN]    *msg  Debug message with format arguments.
 */
void dbg_msg( const char *msg, ... );


typedef void Sigfunc( int );

#ifndef uint8_t
  typedef unsigned char   uint8_t;
#endif


static volatile sig_atomic_t sigcaught;


/*!
 * \brief   Rise the volatile accessible signal flag 'sigcaught'.
 * \param   [IN]    noarg      Unused argument, just to ensure compatibility to
 *                             function type 'Sigfunc'.
 */
void sig_term( int noarg );


/*!
 * \brief  Just exit(0). This is useful for programs which have a atexit()
 *         installed. A ^C on console keyboard does not terminate in a regular,
 *         and none of the installed atexit() functions would be called.
 */
void sig_int( int noarg );


/*!
 * \brief   Notify window size changed.
 * \param   [IN]    signo      Unused argument.
 */
void sig_winch( int norarg );


/*!
 * \brief    Interrupt handler to execute when 'signo' is raised.
 * \param    [IN]    *func     Pointer to the interrupt routine.
 */
Sigfunc *signal( int signo, Sigfunc *func );


/*!
 * \brief    Interrupt handler to execute when 'signo' is raised. Program
 *           termination with exit() called.
 * \param    [IN]    *func     Pointer to the interrupt routine.
 */
Sigfunc *signal_intr( int signo, Sigfunc *func );


/*!
 * \brief   This function should be called when using one of the folling
 *          functions that reserve dynamic allocated space:
 *          *      hcat()
 *          *      loop_duplex_stdio()
 *          This is to prevent memory leaks by installing an exit-handler, that
 *          releases the reserved address areas on program termination. You also
 *          should use it in conjunction with:
 *          *      signal_intr()
 */
void pty_buffers_atexit( void );


/*!
 * \brief   Open a file. In case of STDIN, warn the user. This function
 *          originates from 'coreutils's 'cat'-program:
 *
 *             <program> | cat > <otherfile>
 *
 * \note    When reading from stdin in nonblocking mode, error EAGAIN will rise.
 *          due to UNIX API specification, The file descriptor fd refers to a
 *          file other than a socket and has been marked nonblocking
 *          (O_NONBLOCK), and the read would block. See Linux manpage open(2)
 *          for further details on the O_NONBLOCK flag.
 *          However this functions purpose is to warn in such cases.
 * \param   [IN]    *filename  Filename of the input file or device or stream or
 *                             whatever.
 * \return  The filedescriptor of file or stdin. -1 on error.
 */  
int open_for_read_or_warn_stdin( const char *filename, int verbose );


/*!
 * \brief   Just the same as open_for_read_or_warn() just the other way round.
 */
int open_for_write_or_warn_stdout( const char *filename, int verbose );


/*!
 * \brief    Write to designated file descriptor. On write failure print FD and
 *           call exit().
 * \param    [IN]  fd          Filedescriptor to target file.
 * \param    [IN]  *buf        Source address of data.
 * \param    [IN]  len         Number of bytes to transfer.
 * \return   Number of bytes written.
 */
ssize_t write_or_warn( int fd, const void *buf, size_t len );


/*!
 * \brief    Writes to file associated to filedescriptor fd. In case of using
 *           larget file to write to, this function use subsequent write-cycles
 *           till all data is written.
 * \param    [IN]  fd          Filedescriptor to target file.
 * \param    [IN]  *buf        Source address of data.
 * \param    [IN]  len         Number of bytes to transfer.
 * \return   On error, returns -1, number of bytes actually written otherwise.
 */
ssize_t full_write( int fd, const void *buf, size_t len );


/*!
 * \brief  Erik Andersen says for the busybox nonblock_immune_read():
 *   "Suppose that you are a shell. You start child processes. They work and
 *   eventually exit. You want to get user input. You read stdin. But what
 *   happens if last child switched its stdin into O_NONBLOCK mode?
 *   ***SURPRISE! It will affect the parent too! ***
 *   *** BIG SURPRISE! It stays even after child exits! ***
 *
 *   This is a design bug in UNIX API.
 *
 *       fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
 *
 *   will set nonblocking mode not only on _your_ stdin, but also on stdin of
 *   your parent, etc.
 *
 *   In general,
 *
 *       fd2 = dup(fd1);
 *       fcntl(fd2, F_SETFL, fcntl(fd2, F_GETFL) | O_NONBLOCK);
 *
 *   sets both fd1 and fd2 to O_NONBLOCK. This includes cases where duping is
 *   done implicitly by fork() etc.
 *
 *   We need
 *
 *       fcntl(fd2, F_SETFD, fcntl(fd2, F_GETFD) | O_NONBLOCK);
 *
 *   (note SETFD, not SETFL!) but such thing doesn't exist.
 *
 *   Alternatively, we need nonblocking_read(fd, ...) which doesn't require
 *   O_NONBLOCK dance at all. Actually, it exists:
 *
 *       n = recv(fd, buf, len, MSG_DONTWAIT);
 *
 *   "MSG_DONTWAIT: Enables non-blocking operation; if the operation would
 *   block, EAGAIN is returned." - but recv() works only for sockets!
 * " [...]
 *
 * Erik does use a polling-structure that doesn't care about O_NONBLOCK flag,
 * so we do, just without the rest of the libbb.h.
 *
 * \param    [IN]  fd            The filedescriptor to read from.
 * \param    [IN]  *buf          Address of buffer to read to.
 * \param    [IN]  count         Number of bytes to read.
 * \return   The actual number of bytes read, on error: -1
 */ 
ssize_t nonblock_immune_read( int fd, void *buf, size_t count );


/*!
 * \brief    Concatenate several files to one resulting file.
 * \param    [OUT] fd_concat     The filedescriptor referring to target file.
 * \param    [IN]  **argv        The list of filenames.
 * \param    [IN]  a2h           Treat input data as ASCII sequence and
 *                               translate to HEX byte representation on the
 *                               output data [0-9,A-F]. All other input data not
 *                               representing on of these HEX numbers are
 *                               translated to 0x00.
 * \param    [IN]  h2a           Translate input bytestream to ASCII on output.
 * \param    [IN]  verbose       Warn about opening a file that is not stdin.
 * \note     You can only translate from HEX to ASCII or vice versa. Anyhow, if
 *           a2h is set to 1, h2a is ignored.
 * \return   On success returns 0, otherwise 1.
 */
int hcat( int fd_concat, char **argv, int a2h, int h2a, int verbose );


/*!
 * \brief    ASCII to HEX translation: Characters representing hexadecimal
 *           numbers are translated to according unsigned numbers.
 * \note     Two single chars are translated into one byte aka unsigned HEX. The
 *
 *                            Keyboard-in:      Byte order:       Decimal:
 *           Byte-position:   01--23--45--67    MSB--------LSB    -3---2---1---0
 *           Example:         3C  02  A5  01    3C  02  A5  01    60   2 165   1
 *           Example:         C0  2A  50  1*    0C  02  A5  01    12   2 165   1
 *
 * \param    [OUT] *out          Pointer to buffer to store translated bytes.
 * \param    [IN]  out_size      Size of result buffer.
 * \param    [IN]  *in           Input string.
 * \param    [IN]  in_size       Number of characters to translate.
 * \return   Size of translation result (number of bytes written to *out) 
 */  
size_t snprintu8( uint8_t *out, size_t out_size, char *in, size_t in_size );


/*!
 * \brief    Translates a buffer of uint8_t numbers to a string, that may
 *           contain characters of [0..9, a..f)
 * \param    [out] *out          The buffer to put the translated string into.
 * \param    [in]  out_size      The size of the out-buffer, that should even
 *                               last at least 2 times as big as the in-buffer
 *                               of uint8_t numbers. If smaller translation is
 *                               truncated to out_size.
 * \param    [in]     *in        Input-buffer of uint8_t numbers.
 * \param    [in]  in_num        Amount of numbers that should be converted.
 *                               There is no error-handling implementation for
 *                               the case, that in_num is higher than the actual
 *                               size of the in-buffer!
 * \return   Size of translation result (number of characters written to *out) 
 */
size_t u8nprints( char *out, size_t out_size, uint8_t *in, size_t in_num );


/*!
 * \brief    Interleaved string copy with marking character.
 * \param    [OUT] *dest         Destination string address.
 * \param    [IN]  *src          Source address of input string to parse.
 * \param    [IN]  n             Number of bytes to copy. The n-th byte will be
 *                               '\0' terminated.
 * \param    [IN]  div           Dividing character between elements to replace
 *                               with '\0' string terminators of source.
 * \warning  Hence there is no buffer overflow protection, this function is
 *           highly unsafe. Ensure 'dest' is big enough to hold all elements.
 */
char *stricpy( char *dest, const char *src, size_t n, const char div );


/*!
 * \brief    Read from a file (fd_read) and write to a file (fd_write). When
 *           using a PTS, TTY or FIFO file you must set fd_read == fd_write. If
 *           you want just echo the STDIN to STDOUT, set fd_read to -1.
 * \param    [IN]  fd_read       Filedescriptor to read from. If '-1' STDIN will
 *                               just echo back to STDOUT.
 * \param    [IN]  fd_write      Filedescriptor to write the STDIN to.
 * \param    [IN]  ieof          Ignore EOF on fd_read stream.
 * \param    [IN]  translate     Handle all data from fd_read as HEX values and
 *                               translate them to according ASCII sequences.
 *                               Also translate all character from STDIN to
 *                               according HEX sequences.
 * \param    [IN]  andlf         Append LF when translating. Some applications
 *                               need a line termination to process data.
 * \param    [IN]  bufsize       Size of read/write buffers for data transfer.
 * \param    [IN]  nolf          Do not translate linefeed from read
 * \param    [IN]  *linefieed    Append linefeed at end of line.
 */ 
void loop_duplex_stdio( int fd_read, int fd_write, int ieof, int translate, 
                        size_t bufsite, int nolf, char *linefeed );

                        
/*!
 * \brief    The posix_openpt() is used as a portable way to open an anavailable
 *           PTY master device. The single UNIX specification includes several
 *           functions as part of the XSI option in an attempt to unify the
 *           methods. These extensions are based on the functions originally
 *           provided to manage STREAMS-based PTYs in System V Release 4. The
 *           posix_openpt() returns the file descriptor of next available PTY
 *           master if o.k, -1 on error.
 * \param    [OUT] *pts_name      Return the device name e.g. /dev/pts/2.
 * \param    [IN]  pts_namesz     PTS maximum name-size allowed.
 * \param    [IN]  no_ctty        If set to 'true', prevent the master device
 *                                from becomming a controlling terminal of the
 *                                caller (using O_NOCTTY to configure termios).
 * \return   The file descriptor of the master.
 */
int ptym_open( char *pts_name, int pts_namesz, int no_ctty );


/*!
 * \brief    Open slave PTY. The slave is intended to become the STDIN and
 *           STDOUT of the called program.
 * \param    [IN]  *pts_name      Give the slave-device name that is to be
 *                                opened. The PTS-name originates from the
 *                                master.
 * \deprecated
 * \param    [IN]  no_ctty        If set to 'true', prevent the slave side of
 *                                /dev/pts/X from becomming a controlling
 *                                terminal.
 * \return   The file descriptor of the slave.
 */
int ptys_open( char *pts_name, int no_ctty );


/*!
 * \note     For multithreaded programs using _REENTRANT.
 * \brief    In a single process (but maybe multithreaded) program this function
 *           initializes both, the master side and the slave side of the PTS-
 *           device reporing error messages on opening failure.
 *           On System-V derived systems, the file returned by the ptsname and
 *           ptsname_r functions may be STREAMS-based, and therefore require
 *           additional processing after opening before it actually behaves as a
 *           pseudo terminal.
 * \param    [OUT] *fdm_ptr       Address of the master's filedescriptor.
 * \param    [OUT] *fds_ptr       Address of the slave's filedescriptor.
 * \param    [OUT] *slave_name    Destination to store the PTS name to.
 * \param    [IN]  *slave_namesz  PTS-name size (cannot evaluated with strlen()
 *                                because slave_name is PTS_NAME_LENGTH chars
 *                                predefined.
 * \param    [OUT] *slave_termios Address of the terminal settings.
 * \param    [OUT] *slave_winsize Structure that holds the window-size.
 * \param    [IN]  no_ctty        If set to 'true', process will own control
 *                                over the terminal device.
 * \return   0 on success, -1 on failure.
 */
int pty_pair_init( int *fdm_ptr, int *fds_ptr, char *slave_name,
                   int slave_namesz, struct winsize *slave_winsize,
                   int no_ctty );


/*!
 * \note     For multiprocessed programs not using POSIX threads.
 * \brief    Initialize the terminal discipline, and fork the program in two
 *           processes:
 *           --> PTS-master (parent process)
 *           --> PTS-slave (child process)
 *           In addition, if running an interactive program, the terminal
 *           window-size is adjusted.
 * \param    [OUT] *fdm_ptr       Pointer to PTS-master file descriptor.
 * \param    [IN]  *slave_name    PTS-name.
 * \param    [IN]  *slave_namesz  PTS-name size (cannot evaluated with strlen()
 *                                because slave_name is PTS_NAME_LENGTH chars
 *                                predefined.
 * \param    [OUT] *slave_winsize Structure that holds the window-size.
 * \param    [IN]  no_ctty        If set to 'true', process will own control
 *                                over the terminal device.
 * \return   -1: Error, 0: Child PID, other: Parent PID.
 */ 
pid_t pty_fork_init( int *fdm_ptr, char *slave_name,
                     int slave_namesz, struct winsize *slave_winsize,
                     int no_ctty );


/*!
 * \brief    Save terminal settings and window settings. Intended to use with
 *           tty_reset() to restore original terminal settings before use.
 * \param    [IN]  fd             Filedescriptor associated with the terminal.
 * \param    [OUT] *save_termios  Address to save the terminal line-discipline.
 * \param    [OUT] *save_size     Address of window-structure that will remember
 *                                the current terminal window configuration.
 */
void tty_save( int fd, struct termios *save_termios,
               struct winsize *save_size );

               
/*!
 * \brief    Restore original terminal settings.
 * \param    [IN]  fd             Filedescriptor associated with the terminal.
 * \param    [IN]  *load_termios  Address to terminal configuration, that must
 *                                be backupped before.
 * \param    [IN]  *load_winsz    Address to terminal window configuration, that
 *                                must be backupped before.
 * \return   Returns 0 on success, -1 on error.
 */
int tty_reset( int fd, struct termios *load_termios,
               struct winsize *load_winsz );


/*!
 * \brief    Disables the echo-function for the terminal associated with fd.
 *           This immediately takes effect regardless whether data transmission
 *           is in progress.
 * \param    [IN]  fd             Terminals' file descriptor.
 */
void tty_echo_disable( int fd );


/*!
 * \brief    Enables the echo-function for the terminal associated with fd.
 *           This immediately takes effect regardless whether data transmission
 *           is in progress.
 * \param    [IN]  fd             Terminals' file descriptor.
 */
void tty_echo_enable( int fd );


/*!
 * \return   Returns 0 on success, -1 on error.
 */
//int tty_cbreak( int fd );


/*!
 * \brief    Set the software flow control XON/XOFF for TTY devices.
 *           The change take effect after all output has been transmmitted.
 *           Furthermore, when the change takes place, all input data that has
 *           not been read is discarded (flushed).
 */
int tty_xonoff( int fd );


/*!
 * \brief    Disable canonical mode and block until a specified amount of data
 *           is received. This is usefull to reduce system load, for example
 *           when executing in a parallel thread.
 * \param    [IN]  fd             Terminals' file descriptor.
 * \param    [IN]  exp_min_amout  Wait until the expected minimum amount of data
 *                                has captured. IF 0 wait for any number of
 *                                bytes are pending.
 * \return   Returns 0 on success, -1 on error.
 */
int tty_raw_blocking( int fd, size_t exp_min_amount );


/*!
 * \brief    Disable canonical mode and wait at most timeout has expired. When
 *           receiving subsequent data the timeout-counter will be reset.
 * \param    [IN]  fd             Terminals' file descriptor.
 * \param    [IN]  timeout        Timeout value [ms], that must be a multiple of
 *                                100. If not met this timeout-granularity, this
 *                                function is going to warn on STDERR and sets
 *                                the next valid value.
 * \return   Returns 0 on success, -1 on error. An invalid timeout-setting does
 *           not cause this function to return -1.
 */
int tty_raw_timeout( int fd,  unsigned int timeout );


void tty_interactive( int fd, struct winsize *set_size );


/*!
 * \brief    Changes the window size associated with the pseudo terminal
 *           referred to by masterfd.
 * \param    [IN]  row, col, xpixel, ypixel specify the new window size.
 * \return   On success, returns 0. On error, returns -1 and warns on STDERR.
 * \author   Author: Tatu Ylonen <ylo@cs.hut.fi>
 *           Copyright (C) 1999-2010 raf <raf@raf.org>
 *           Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *             All rights reserved
 *
 *           libslack - http://libslack.org/
 *           pseudo.h/c
 */
int tty_change_window_size( int masterfd, int row, int col,
                            int xpixel, int ypixel );


/*!
 * \brief    Seperate a string into basename and appending argument list.
 * \param    [IN]  *strlist        Input string (must be '\0' terminated).
 * \param    [OUT] *basename       Resulting string. All whitespaces will be
 *                                 replaced with (char*)0 terminators.
 * \param    [IN]  basename_max_size Size of the character-array. Prevents
 *                                 overflow.
 * \return   String containing arguments without basename.
 */
char *args_to_argl( char *basename, const char *strlist,
                    size_t basename_max_size );


/*!
 * \brief    To start a program or drver-program with arguments passed, it have
 *           to be ellipsed by quotation marks or single-ticks. Converts the
 *           whitespace seperated arguments to an argv[].
 * \param    [IN]  *strlist        Input string (must be '\0' terminated).
 * \param    [OUT] *strv[]         Resulting vector of strings.
 * \return   Number of arguments passed.
 */
char **args_to_argv( const char *strlist );


int do_driver_argl( char *driver, char *driver_list, int redirect_err );


/*!
 * \brief    With an interactive program, one line of input may generate many
 *           lines of output, and normally decides further actions depending on
 *           whom the output was.
 *
 *           To drive an interactive program from script, the program "expect"
 *           does that with an additional command language. To take a more
 *           simple path to implementation here, one can connect to a driver
 *           process for its input and output:
 *           * STDOUT of driver is PTY's standard input
 *           * STDIN  of driver is PTY's standard output
 *
 *           The co-process is connected with one single full-duplex
 *           bidirectional pipe instead of using two half-duplex pipes. Even
 *           though it has its STDIN and STDOUT connected to the driving process
 *           the user can still communicate with the driver process over
 *           the /dev/tty.
 *
 * \param    [IN]    *driver       Commandline of driver to start.
 * \param    [IN]    redirect_err  Also redirect the STDERR to the PTY device.
 * \return   Error code of driver.
 */
int do_driver_argv( char *driver[], int redirect_err );


/*!
 * \brief    Cread full-duplex pipe for interprocess commmunication.
 * \param    Master/slave descriptor array of the pipe.
 * \return   The pipe created.
 */
int fd_pipe( int fd[ 2 ] );

#endif // _PTY_H
// EOF
