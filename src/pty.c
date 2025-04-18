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
#include <assert.h>


/*!
 * \note          ISO C89/C99 define stdin as a macro representing the
 *                filepointer FILE *stdin referring to the file "standard input"
 *                which is a input stream.
 *                So we can check if a FILE* is actually referring to it.
 */
const char *standard_input  = "standard input";
const char *standard_output = "standard output";
const char ascii_lf         = '\n';
const char ascii_null       = 0;    // (const char)NULL
const char ascii_space      = 32;
const char ascii_stick      = 39;   // single tick
const char ascii_dtick      = 34;   // quotation mark

/*!
 * \brief         Print our own error-description and append the error type
 *                provided by <errno.h>.
 * \param         [IN]  err_flag    Error flag to use.
 * \param         [IN]  err         The error-number, usually errno.
 * \param         [IN]  *fmt        Formatted error-message.
 * \param         [IN]  ap          Argument pointer to message parts.
 */
#define MAX_ERR_MSG_SIZE 256
static void _err_printva( int err_flag, int err, const char *fmt, va_list ap ) {
  static char buf[ MAX_ERR_MSG_SIZE ]; // retain space to allow many, many errors

  vsnprintf( buf, MAX_ERR_MSG_SIZE-1, fmt, ap );

  if ( err_flag )
    // First user error description then error type:
    snprintf( buf+strlen( fmt ), MAX_ERR_MSG_SIZE-strlen( buf )-1, ": %s",
              strerror( err ) );

  strcat( buf, "\n" );  // append linefeed
  fflush( stdout );     // in case STDOUT and STDERR were combined
  fputs( buf, stderr ); // finally inform on STDERR

  memset( &buf, ascii_null, MAX_ERR_MSG_SIZE );
  fflush( NULL );       // flush all stdio output streams
}


void err_msg( const char *msg, ... )
{
  va_list ap;

  va_start( ap, msg );
  _err_printva( 0, 0, msg, ap );
  va_end( ap );
}



void err_sys( const char *msg, ... )
{
  va_list ap;

  va_start( ap, msg );
  // Print concatenated error message and show error type );
  _err_printva( 1, errno, msg, ap );
  va_end( ap );

  exit( EXIT_FAILURE );
}


void err_quit( const char *msg, ... )
{
  va_list ap;

  va_start( ap, msg );
  // Print concatenated error message and show error type );
  _err_printva( 0, 0, msg, ap );
  va_end( ap );

  exit( 1 );
}


void err_exit( int error, const char *msg, ... )
{
  va_list ap;

  va_start( ap, msg );
  // Print concatenated error message and show error type );
  _err_printva( 1, error, msg, ap );
  va_end( ap );

  exit( 1 );
}


void dbg_msg( const char *msg, ... )
{
#ifdef DEBUG
  va_list ap;

  va_start( ap, msg );
  fprintf( stderr, "DEBUG [%i]: ", errno);
  _err_printva( 0, 0, msg, ap );
  va_end( ap );
#else
  ;
#endif
}


static void _printwz( int fd )
{
  struct winsize size;

  if ( ioctl( fd, TIOCGWINSZ, (char*)&size ) < 0 ) 
    err_sys( "TIOCGWINSZ error" );

  // Notify the window-changes:
  fprintf( stderr, "%d rows, %d columns\n", size.ws_row, size.ws_col );
}


void sig_term( int noarg )
{
  sigcaught = 1; // set flag atomically
}


void sig_winch( int noarg )
{
  fprintf( stderr, "Changed window size: " );
  _printwz( STDIN_FILENO );
}


void sig_int( int noarg )
{
  exit( EXIT_SUCCESS );
}


Sigfunc *signal( int signo, Sigfunc *func )
{
  struct sigaction act, oact;

  act.sa_handler = func;

  sigemptyset( &act.sa_mask );

  act.sa_flags = 0;

  if ( signo == SIGALRM ) {
    #ifdef SA_INTERRUPT
      act.sa_flags |= SA_INTERRUPT;
    #endif
  } else {
    act.sa_flags |= SA_RESTART;
  }

  if ( 0 > sigaction( signo, &act, &oact ) )
    return( SIG_ERR );

  return( oact.sa_handler );
}


Sigfunc *signal_intr( int signo, Sigfunc *func )
{
  struct sigaction act, oact;

  act.sa_handler = func;

  sigemptyset( &act.sa_mask );

  act.sa_flags = 0;

  if ( signo == SIGALRM ) {
    #ifdef SA_INTERRUPT
      act.sa_flags |= SA_INTERRUPT;
    #endif
  }

  if ( 0 > sigaction( signo, &act, &oact ) )
    return( SIG_ERR );

  return( oact.sa_handler );
}


typedef struct {
  uint8_t num;
  char rep;
} tLut_u8_c;


// uint8_t nibble-char table (look-up table or dictionary, or whatever to name)
static const tLut_u8_c u8nct[] = {
  { 0x00, '0' }, // for HEX to char conversion, valid values start here...
  { 0x01, '1' },
  { 0x02, '2' },
  { 0x03, '3' },
  { 0x04, '4' },
  { 0x05, '5' },
  { 0x06, '6' },
  { 0x07, '7' },
  { 0x08, '8' },
  { 0x09, '9' },
  { 0x0A, 'a' },
  { 0x0B, 'b' },
  { 0x0C, 'c' },
  { 0x0D, 'd' },
  { 0x0E, 'e' },
  { 0x0F, 'f' }, // ...and end here.
  { 0x0A, 'A' }, // regard for upper- and lower-cases character translation
  { 0x0B, 'B' },
  { 0x0C, 'C' },
  { 0x0D, 'D' },
  { 0x0E, 'E' },
  { 0x0F, 'F' }
};


static void _u8toc( char *high_nibble_conv, char *low_nibble_conv, uint8_t in )
{
  int i;

  for ( i=0; i<16; i++ ) {
    if ( (0x0F & (in >> 4)) == u8nct[i].num ) {
      *low_nibble_conv = u8nct[i].rep;
      break;
    }
  }

  for ( i=0; i<16; i++ ) {
    if ( (0x0F & in) == u8nct[i].num ) {
      *high_nibble_conv = u8nct[i].rep;
      break;
    }
  }
}


static uint8_t _ctou8( char in_char )
{
  int i;
  uint8_t out;

  /*!
   * \note   Using conversion LUT to represent all ASCII symbols not in range
   *         ['0'..'9', 'a'..'f', 'A'..'F'] as 0x00 to close the memory
   *         segmentation error risk.
   */ 
  for ( i=0; i<21 ; i++ ) {
    if ( in_char == u8nct[i].rep ) {
      out = u8nct[i].num;
      break;
    }

    out = 0x00;
  }

  return ( out );
}


size_t snprintu8( uint8_t *out, size_t out_size, char *in, size_t in_size )
{
  size_t os = 0;    // out-size
  size_t i = 0;     // out-index of concatenated byte
  size_t index = 0; // in-index
  const size_t shift = (in_size % 2); // decide if leading 0 is to append
  uint8_t h, l;     // high and low nibble

  // Prevent memory excess violation
  if ( out_size < (in_size/2 + shift) ) {
    if ( 0 == (os = out_size) )
      return ( 0 ); // catch zero-output
  } else {
    os = in_size/2 + shift; // guard input-buffer access
  }

  // Input LSB first:
  /*
  for ( i=0; i<os; i++ ) {
    index = in_size - 1 - (i*2); 
    l = _ctou8( in[index]   );
    h = _ctou8( in[index-1] ); // input MSNibble first
    out[i] = (0xF0 & (h<<4)) | (0x0F & l);
  }
  */

  // input MSB first:
  if ( 1 == shift )
    out[0] = (0x0F & _ctou8( in[0] ));

  for ( i=shift; i<os; i++ ) {
    index = (i*2) + 1 - shift;
    l = _ctou8( in[index]   );
    h = _ctou8( in[index-1] ); // input MSNibble first
    out[i] = (0xF0 & (h<<4)) | (0x0F & l);
  }

  return ( i ); 
}


size_t u8nprints( char *out, size_t out_size, uint8_t *in, size_t in_num )
{
  size_t i = 0;
  size_t brs = in_num; // bytes reserved for storing translated characters

  // Truncate, if too big for result buffer:
  if ( out_size < (in_num*2) )
    brs = out_size/2;

  for( i=0; i<brs; i++ ) {
    /*!
     * \note    Order like this:
     *          i=0: 0x74    out[0]:  0x04
     *                       out[1]:  0x07
     *          i=1: 0x5A    out[2]:  0x0A
     *                       out[3]:  0x05
     */
    _u8toc( &out[2*i+1], &out[2*i], in[i] );
  }

  return( i*2 ); 
}


char *stricpy( char *dest, const char *src, size_t n, const char div )
{
  size_t i;

  for ( i=0; i<n-1; i++ )
    if ( src[i] == '\0' )
      dest[i] = div;
    else
      dest[i] = src[i];

  dest[n] = '\0';

  return dest;
}

  
int open_for_read_or_warn_stdin( const char *filename, int verbose )
{
  int fd = STDIN_FILENO;

  if ( *filename != *standard_input ) {
    if ( 0 > (fd = open( filename, O_RDONLY )) ) // (O_RDONLY | O_NONBLOCK);
      err_msg( "Cannot open %s for read", (char*)filename );

    if ( 1 == verbose )
      fprintf( stderr, "Warning: %s FD=%i is not stdin\n", filename, fd );
  }

  return fd;
}


int open_for_write_or_warn_stdout( const char *filename, int verbose )
{
  int fd = STDOUT_FILENO;

  if ( *filename != *standard_output ) {
    if ( 0 > (fd = open( filename, O_WRONLY )) )
      err_msg( "Cannot open %s for write", (char*)filename );

    if ( 1 == verbose )
      fprintf( stderr, "Warning: %s FD=%i is not stdout\n", filename, fd );
  }

  return fd;
}


ssize_t write_or_warn( int fd, const void *buf, size_t len )
{
  ssize_t ret;

  if ( 0 > (ret = write( fd, buf, len )) )
    err_sys( "Write failure (FD=%i) ", fd );

  return ( ret );
}


ssize_t full_write( int fd, const void *buf, size_t len )
{
  ssize_t cc = 0;
  ssize_t total = 0;
  size_t remaining = len;

  while ( (remaining > 0) ) { //|| (errno != EINTR) ) {
    cc = write( fd, buf, remaining );

    if ( cc < 0 ) {
      if ( total > 0 ) { // EOF detected
        return total;
      }

      return cc; // write() returns -1 on failure.
    }

    total += cc;
    buf = ((const char *)buf) + cc;
    remaining -= cc;
  }

  return total;
}


#define NO_TIMEH_TIMEOUT_LIMIT   ( 2 )
ssize_t nonblock_immune_read( int fd, void *buf, size_t count )
{
  unsigned long nt_timeout = (NO_TIMEH_TIMEOUT_LIMIT * 1000000); 
  ssize_t n = -1;

  do {
    n = read( fd, buf, count); // ignore read
    if ( (n >= 0) || (EAGAIN != errno) )
      return n;

    // If fd is in O_NONBLOCK mode. Wait and repeat:
    nt_timeout--;

  } while ( (n < 0) && (errno == EINTR) && (nt_timeout > 0) );

  return n;
}


/*!
 * \note   To prevent memory leaks, the dynamic allocated buffers of theese
 *         functions are initialized in this section:
 *         *   hcat()
 *         *   loop_driver_stdio()
 *         *   args_to_argv()
 *         The calling program should install the automatic memory freeing
 *         function pty_buffers_atexit() before usage.
 */
void *hcat_tbuf   = NULL; // hcat()              pointer to translation buffer
void *hcat_bigbuf = NULL; // hcat()              pointer to concatenation buffer
void *lds_tbuf    = NULL; // loop_duplex_stdio() pointer to translation buffer
void *lds_buffer  = NULL; // loop_duplex_stdio() pointer to read/write buffer
char **a2av_strv  = NULL; // args_to_argv()      pointer to result string-array


static void _bfrees( void ) {
  free( hcat_tbuf );
  free( hcat_bigbuf );
  free( lds_tbuf );
  free( lds_buffer );
  free( a2av_strv );

  /*!
   * \note  free() does nothing, if address is NULL.
   */

  // Fuse further usage:
  //hcat_tbuf = NULL;
  // [...] = NULL;
}


static void _bflush( void *addr, size_t flushsize, const char *initval )
{
  int c = (int)*initval;

  // Using <string.h>:
  if ( NULL == memset( addr, c, flushsize ) )
    err_sys( "Buffer flush failure" );
}


void pty_buffers_atexit( void )
{
  if ( 0 != atexit( _bfrees ) )
    err_sys( "Cannot install the exit-handler" );
}


#define BIG_BUFFER_SIZE ( 1024*sizeof( char ) )
int hcat( int fd_concat, char **argv, int a2h, int h2a, int verbose )
{
  int fd = STDIN_FILENO;
  int retval = EXIT_SUCCESS;
  size_t nwrite = 0;
  ssize_t nread = 0;
  ssize_t nwritten = 0;

  hcat_tbuf = NULL;
  hcat_bigbuf = malloc( BIG_BUFFER_SIZE );

  if ( 1 == a2h ) {
    if ( NULL == (hcat_tbuf = malloc( BIG_BUFFER_SIZE/2 + 1 )) )
      err_sys( "Not enough space for translation buffer" );
  } else if ( 1 == h2a ) { // indeed tbuf is double bigbuf
    if ( NULL == (hcat_tbuf = malloc( BIG_BUFFER_SIZE*2 )) )
      err_sys( "Not enough space for translation buffer" );
  }

  /*!
   * \brief   For each filename in **argv, append the file to resulting file. We
   *          continue concatenating or translating from ASCII to HEX (or vice
   *          versa) aslong no errors occur during file-processing.
   */
  do {
    fd = open_for_read_or_warn_stdin( *argv, verbose );

    if ( fd < 0 ) {
      goto HCAT_ERROR_OUT;
    } else {
      /*!
       * \brief   Due to limitted buffer size (*bigbuf), we continue buffered
       *          read/write until reading from input file has reached EOF.
       */
      do {
        nread = nonblock_immune_read( fd, hcat_bigbuf, BIG_BUFFER_SIZE );

        if ( nread > 0 ) {
          if ( 1 == a2h ) {
            _bflush( hcat_tbuf, nread/2 + 1, &ascii_null );
            //_bflush( tbuf, BIG_BUFFER_SIZE/2 + 1, &ascii_null );

            // ASCII to HEX:
            nwrite = snprintu8( (uint8_t*)hcat_tbuf, ((size_t)nread)/2 + \
                                ((size_t)nread)%2, (char*)hcat_bigbuf, \
                                (size_t)nread );

            nwritten = full_write( fd_concat, hcat_tbuf, nwrite );
          } else if ( 1 == h2a ) {
            _bflush( hcat_tbuf, nread*2, &ascii_null );
            //_bflush( tbuf, BIG_BUFFER_SIZE*2, &ascii_null );

            // HEX to ASCII:
            nwrite = u8nprints( (char*)hcat_tbuf, ((size_t)nread)*2, \
                                (uint8_t*)hcat_bigbuf, (size_t)nread );

            nwritten = full_write( fd_concat, hcat_tbuf, nwrite );
          } else {
            nwritten = full_write( fd_concat, hcat_bigbuf, nread );
          }

          if ( verbose == 1 ) {
            fprintf( stderr, "\n%i bytes read from %s\n", (int)nread, *argv );
            fprintf( stderr, "%i bytes transferred\n", (int)nwritten );
          }
        }
      } while ( (nread > 0) && (nread < (ssize_t)BIG_BUFFER_SIZE) );

      if ( fd != STDIN_FILENO )
        close( fd );

      if ( nread < 0 )
        goto HCAT_ERROR_OUT;

      fflush( stdout );
      continue;
    }

    HCAT_ERROR_OUT:
      retval = EXIT_FAILURE;
      break;
  } while ( *++argv ); // continue processing next file, if any

  free( hcat_tbuf );
  free( hcat_bigbuf );

  // Make it bullet-proof:
  hcat_tbuf = NULL;
  hcat_bigbuf = NULL;

  return retval;
}


// Take care of the linefeed and null-termination of a string:
void loop_duplex_stdio( int fd_read, int fd_write, int ieof, int translate,
                        size_t bufsize, int nolf, char *linefeed )
{
  pid_t pid = -1;    // distinguish parrent/child process
  int nread, nwrite = 0;
  size_t lfsize = 0;

  if ( NULL != linefeed )
    lfsize = strlen( linefeed );

  lds_tbuf = NULL;   // pointer to read/write buffer
  lds_buffer = NULL; // pointer to translation buffer

  // Reserve space for read/write buffer:
  if ( NULL == (lds_buffer = malloc( bufsize )) )
    err_sys( "Not enough space for read/write buffers" );

  fflush( stdin );
  fflush( stdout );

  // fd_read == -1 demands just echoing back STDIN to STDOUT:
  if ( -1 != fd_read ) {
    if ( (pid = fork()) < 0 )
      err_sys( "Failed forking into read/write loop" );
  }

  if ( 0 == pid ) {
    /////////////////////////////
    // Inside child or no-fork //
    /////////////////////////////

    // Close master channel:
    close( fd_write );
    close( STDIN_FILENO );

    if ( 1 == translate ) {
      // Reserve space for translation buffer (is twice as big a buf size):
      if ( NULL == (lds_tbuf = malloc( bufsize*2 )) )
        err_sys( "Not enough space for translation buffer" ); 
    }

    while ( (-1 < nwrite) && (-1 < nread) ) {
      // Read from device and write to STDOUT:
      if ( 0 < (nread = read( fd_read, lds_buffer, bufsize )) ) {
        //if (1 == nolf)
        //  nread -= 1;

        if ( 1 == translate ) {
          _bflush( lds_tbuf, nread*2, &ascii_null );

          // HEX to ASCII:
          nwrite = u8nprints( (char*)lds_tbuf, ((size_t)nread)*2, \
                              (uint8_t*)lds_buffer, (size_t)nread );

          nwrite = write_or_warn( STDOUT_FILENO, lds_tbuf, nwrite );
        } else {
          nwrite = write_or_warn( STDOUT_FILENO, lds_buffer, nread );
        }

        if ( NULL != linefeed )
          write_or_warn( STDOUT_FILENO, &linefeed, lfsize );
      } else if ( 0 == ieof ) {
        break;
      }
    }

    /*!
     * \note    We always terminate, when we encounter an EOF on stdio (hang-
     *          up sequence), but we notify the parent only when ignoreeof was
     *          set.
     */
    if ( 1 == ieof )
      // Child notifies parent:
      kill( getppid(), SIGTERM );

    if ( 0 < nread )
      err_sys( "Read failure on device FD=%i", fd_read );

    exit( 0 ); // child cannot return, but we do, if not forked
  }

  ////////////////////////////
  // Inside parent process: //
  ////////////////////////////

  // Close slave channel:
  if ( -1 < fd_read ) {
    close( fd_read ); // close only if given (not in echo-mode)
    close( STDOUT_FILENO );
  }

  if ( 1 == translate ) {
    // HEX buffer is half the size of ASCII buffer:
    if ( NULL == (lds_tbuf = malloc( bufsize/2 + 1 )) )
      err_sys( "Not enough space for translation buffer" ); 
  }

  if ( SIG_ERR == signal_intr( SIGTERM, sig_term ) )
    err_sys( "Cannot install signal handler for SIGTERM" );

  while ( (-1 < nwrite) && (-1 < nread) ) {
    // Read from STDIN and write to device:
    if ( 0 < (nread = read( STDIN_FILENO, lds_buffer, bufsize )) ) {
      if (1 == nolf)
        nread -= 1;

      if ( 1 == translate ) {
        _bflush( lds_tbuf, nread/2 + 1, &ascii_null );

        // ASCII to HEX:
        nwrite = snprintu8( (uint8_t*)lds_tbuf, ((size_t)nread)/2 + \
                            ((size_t)nread)%2,
                            (char*)lds_buffer, (size_t)nread );

        nwrite = write_or_warn( fd_write, lds_tbuf, nwrite );

      } else {
        nwrite = write_or_warn( fd_write, lds_buffer, nread );
      }
      if ( NULL != linefeed ) // guarded non-exclusively before
        //write_or_warn( STDOUT_FILENO, &linefeed, 1 );
        write_or_warn( fd_write, &linefeed, lfsize );
    }
  }

  if ( 0 < nread )
    err_msg( "Failed reading from stdin" );

  // Make it bullet-proof:
  if ( NULL != lds_tbuf )
    free( lds_tbuf );
  lds_tbuf = NULL;

  if ( NULL != lds_buffer )
    free( lds_buffer );
  lds_tbuf = NULL;
 
  // Signal caught, error occured or EOF detected, parent returns to caller.
}


int ptym_open( char *pts_name, int pts_namesz, int no_ctty )
{
  int err = 0;
  int fdm = -1;
  char *ptr = NULL;

  if ( 1 == no_ctty ) {
    if ( 0 > (fdm = posix_openpt( O_RDWR | O_NOCTTY )) ) {
      err_msg( "POSIX pseudo-terminal open failed." );
      return( -1 );
    }
  } else {
    if ( 0 > (fdm = posix_openpt( O_RDWR )) ) {
      err_msg( "POSIX pseudo-terminal open failed." );
      return( -1 );
    }
  }

  /*!
   * \note  Some older UNIX implementations that support System V (aka UNIX 98)
   *        PTSs do not have this function, but it is easy to implement:
   *        *  int posix_openpt( int flags )
   *           {
   *             return( open( "/dev/ptmx", flags ) );
   *           }
   *        *  Also file permissions and ownership would have to be set.
   */

  // Change permissions of slave PTY device:
  if ( 0 > grantpt( fdm ) ) { // grant access to slave
    assert_perror( errno );
    goto PTYM_ERROUT;
  }

  // Allow the slave PTY to be opened:
  if ( 0 > unlockpt( fdm ) ) { // clear slaves lock flag
    assert_perror( errno );
    goto PTYM_ERROUT;
  }

  if ( (ptr = ptsname( fdm )) == NULL ) { // grab the slaves name
    assert_perror( errno );
    goto PTYM_ERROUT;
  }

  // Return slaves name (stringyfied):
  strncpy( pts_name, ptr, pts_namesz );
  pts_name[pts_namesz-1] = '\0';

  return( fdm );

  PTYM_ERROUT:

  err = errno;
  close( fdm );
  errno = err;

  return( -1 );
}


int ptys_open( char *pts_name, int no_ctty )
{
  int fds = -1; // slave side of the terminal
  /*
  #ifdef TIOCNOTTY
    int fdp;    // disconnect from parent control
  #endif
  */
  #if defined( SOLARIS )
    int err, setup = 0;
  #endif

  fds = open( pts_name, (O_RDWR) );
  /*
  if ( 1 == no_ctty ) {
    fds = open( pts_name, (O_RDWR) );
  } else {
    #ifdef TIOCNOTTY
      if ( 0 < (fdp = open( PATH_TTY, O_RDWR | O_NOCTTY )) ) {
        ioctl( fdp, TIOCNOTTY, NULL );
        close( fdp );
	  }
    #endif
    fds = open( pts_name, (O_RDWR | O_NOCTTY)); 
  }
  */

  if ( 0 > fds ) {
    assert_perror( errno );
    return( -1 );
  }

  #if defined( SOLARIS )
    // "Check if stream is already setup by autopush facility":    
    if ( 0 > (setup = ioctl( fds, I_FIND, "ldterm")) ) {
      goto PTYS_ERROUT;

    if ( 0 == setup ) {
      if ( 0 > (ioctl( fds, I_PUSH, "ptem" )) )
        goto PTYS_ERROUT;

      if ( 0 > (ioctl( fds, I_PUSH, "ldterm" )) )
        goto PTYS_ERROUT;
 
      if ( 0 > (ioctl( fds, I_PUSH, "ttcompat" )) )
        goto PTYS_ERROUT;

    PTYS_ERROUT:
      err = errno;
      close( fds );
      errno = err;

      return( -1 );
    }
  #endif

  return( fds );
}


int pty_pair_init( int *fdm_ptr, int *fds_ptr, char *slave_name,
                   int slave_namesz, struct winsize *slave_winsize, int no_ctty )
{
  int fdm, fds;
  char pts_name[PTS_NAME_LENGTH];

  if ( (fdm = ptym_open( pts_name, sizeof( pts_name ), no_ctty )) < 0 )
    err_sys( "Cannot open PTY-master FD=%i", fdm );
  
  if ( NULL != slave_name ) {
    strncpy( slave_name, pts_name, slave_namesz );
    slave_name[slave_namesz-1] = '\0';
  }

  //if ( 0 > setsid() )
  //  err_sys( "Cannot set new session ID" );

  // System V acquires controlling terminal on open():
  if ( 0 > (fds = ptys_open( pts_name, no_ctty )) )
    err_sys( "Cannot open PTY-slave FD=%i", fds );

  //close( fdm );

  tty_interactive( fds, slave_winsize );

  // Attach STDIN/STDOUT to the PTS-slave:
  if ( dup2( fds, STDIN_FILENO ) != STDIN_FILENO )
    err_sys( "Cannot duplicate STDIN to PTS-slave" );

  if ( dup2( fds, STDOUT_FILENO ) != STDOUT_FILENO )
    err_sys( "Cannot duplicate STDOUT to PTS-slave" );

  if ( dup2( fds, STDERR_FILENO ) != STDERR_FILENO )
    err_sys( "Cannot duplicate STDERR to PTS-slave" );

  if ( fds != STDIN_FILENO && fds != STDOUT_FILENO && fds != STDERR_FILENO ) {
    close( fds );
    return( -1 );
  }

  *fds_ptr = fds; // return the slave filedescriptor
  *fdm_ptr = fdm; // return1 the master filedescriptor

  return( 0 );
}


pid_t pty_fork_init( int *fdm_ptr, char *slave_name, int slave_namesz,
                     struct winsize *slave_winsize, int no_ctty )
{
  int fdm, fds;
  pid_t pid;
  char pts_name[PTS_NAME_LENGTH];

  if ( (fdm = ptym_open( pts_name, sizeof( pts_name ), no_ctty)) < 0 )
    err_sys( "Cannot open PTY-master FD=%i", fdm);

  if ( NULL != slave_name ) {
    strncpy( slave_name, pts_name, slave_namesz );
    slave_name[slave_namesz-1] = '\0';
  }

  switch ( pid = fork() ) {
  case -1:
    close( fdm );
    err_msg( "Cannot fork() for pseudo terminal generation" );
    pid = 0; // does never reach here

  case 0:
    ///////////////////////////////
    // Inside the child process: //
    ///////////////////////////////
    if ( 0 > setsid() )
      err_sys( "Cannot set new session ID" );

    // System V acquires controlling terminal on open():
    if ( 0 > (fds = ptys_open( pts_name, no_ctty )) )
      err_sys( "Cannot open PTY-slave FD=%i", fds );

    tty_interactive( fds, slave_winsize );

    // Attach STDIN/STDOUT to the PTS-slave:
    if ( dup2( fds, STDIN_FILENO ) != STDIN_FILENO )
      err_sys( "Cannot duplicate STDIN to PTS-slave" );

    if ( dup2( fds, STDOUT_FILENO ) != STDOUT_FILENO )
      err_sys( "Cannot duplicate STDOUT to PTS-slave" );

    if ( dup2( fds, STDERR_FILENO ) != STDERR_FILENO )
      err_sys( "Cannot duplicate STDERR to PTS-slave" );

    if ( fds != STDIN_FILENO && fds != STDOUT_FILENO && fds != STDERR_FILENO ) {
      close( fds );
      dbg_msg( "PTY-slave FD=%i not STDIO/STDERR", fds );
    }

    close( fdm );

  default:
    ////////////////////////////////
    // Inside the parent process: //
    ////////////////////////////////
    *fdm_ptr = fdm; // return the master filedescriptor

    /*!
     * \note  Not closing the slave FD is not an error here, because we open
     *        open the slave side of the PTY after fork().
     */
  }

  return( pid );
}


void tty_echo_disable( int fd )
{
  struct termios fetched;
  if ( 0 > tcgetattr( fd, &fetched ) )
    err_sys( "Failed to access terminal settings" );

  fetched.c_lflag &= ~( ECHO | ECHOE | ECHOK | ECHONL );
  // Disable CR/NL:
  fetched.c_oflag &= ~( ONLCR ); 

  // Immediately change the echo-behaviour:
  if ( 0 > tcsetattr( fd, TCSANOW, &fetched ) )
    err_sys( "Failed to write terminal settings" );
}


void tty_echo_enable( int fd )
{
  struct termios fetched;
  if ( 0 > tcgetattr( fd, &fetched ) )
    err_sys( "Failed to access terminal settings" );

  fetched.c_lflag |= ( ECHO | ECHOE | ECHOK | ECHONL );
  // Disable CR/NL:
  fetched.c_oflag |= ( ONLCR ); 

  // Immediately change the echo-behaviour:
  if ( 0 > tcsetattr( fd, TCSANOW, &fetched ) )
    err_sys( "Failed to write terminal settings" );
}


int tty_cbreak( int fd )
{
  int err = 0;
  struct termios tflags;
  struct termios tbackup;

  if ( 0 > tcgetattr( fd, &tflags ) ) {
    err_msg( "Failed to access terminal settings" );
    return ( -1 );
  }

  tbackup = tflags;
  
  // Turn echo off, cannonical-mode off:
  tflags.c_lflag &= ~( ECHO | ICANON );

  if ( 0 > (err = tcsetattr( fd, TCSANOW, &tflags )) ) {
    err_msg( "Failed configuring terminal cbreak" );
    goto CBREAK_ERROUT;
  }

  // Verify that the changes stuck:
  if ( 0 > (err = tcgetattr( fd, &tflags )) ) {
    goto CBREAK_ERROUT;
  }

  // Restore original settings, if only some of these changes took effect:
  if ( (tflags.c_cflag & (ECHO | ICANON)) ) {
    err_msg( "Not all settings took effect for cbreak" );
    goto CBREAK_ERROUT;
  }

  return ( 0 );

CBREAK_ERROUT:
  tcsetattr( fd, TCSAFLUSH, &tbackup );
  err_msg( "Tried to restore terminal settings" );
  return ( err );  
}


int tty_xonoff( int fd )
{
  struct termios fetched;

  if ( 0 > tcgetattr( fd, &fetched ) )
    err_sys( "Failed to access terminal settings" );

  fetched.c_iflag |= (IXON | IXOFF);

  /*!
   * \note   To disable flow control use:
   *         fetched.c_iflag &= ~(IXON | IXOFF | IXANY);
   */

  if ( 0 > tcsetattr( fd, TCSAFLUSH, &fetched ) )
    err_sys( "Failed to write terminal settings" );

  return ( 0 );
}


/*!
 * \brief  Make preparations to set the terminal in RAW mode. This is usefull
 *         when connecting to real hardware that cannot cope with special
 *         characters. Some electronic devices expect bare hexadecimal patterns
 *         to configure and control internal states. The most stupid of them
 *         even get problems with CR/LF.
 * \param  [OUT] *tt               The terminal structure, that is to hold the
 *                                 line discipline settings.
 */
static void _tty_raw( struct termios *tt )
{
  // Disable signal-interruption and break, no CR/NL, use dump settings:
  tt->c_iflag &= ~(ICRNL | INPCK | ISTRIP | BRKINT);

  // Canonical mode off, extended input off, signal chars off:
  tt->c_lflag &= ~(ICANON | ISIG | IEXTEN);
  
  // Clear size bits and turn off parity checking for 8bit mode:
  tt->c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
  tt->c_cflag |= (CS8);

  // No output processing:
  tt->c_oflag &= ~(OPOST);
}


/*!
 * \brief  Consistency check of terminal line discipline for RAW mode. It is
 *         to check if changes applied on the TTY-device actually have taken
 *         effect.
 * \üaram  [IN]  *tt               Adress of the termios structure whose RAW
 *                                 configuration is to check.
 * \return On success it returns 0, -1 otherwise.
 */
static int _tty_raw_check( struct termios *tt )
{
  if ( 0 != ( (tt->c_iflag & (ICRNL | INPCK | ISTRIP || BRKINT)) ||
              (tt->c_lflag & (ICANON | ISIG | IEXTEN)) ||
              (tt->c_cflag & (CSIZE | PARENB | CRTSCTS)) != (CS8) ||
              (tt->c_oflag & (OPOST)) ) )
    return ( -1 );
  else
    return ( 0 );
}

 
#define TTY_DEFAULT_BAUDRATE    9600 // typical baudrate [symbols per seconds]
int tty_raw_blocking( int fd, size_t exp_min_amount )
{
  struct termios tflags;
  struct termios tbackup;

  if ( 0 > tcgetattr( fd, &tflags ) ) {
    err_msg( "Failed to read terminal settings" );
    return ( -1 );
  }

  tbackup = tflags;
  _tty_raw( &tflags ); // set typical non-canonical stuff

  //////////////////////////////////////////////////
  // Block until minimum amount of bytes received //
  //////////////////////////////////////////////////

  tflags.c_cc[VMIN] = exp_min_amount;
  tflags.c_cc[VTIME] = 0;

  // Update the terminal line discipline:
  if ( 0 > tcsetattr( fd, TCSANOW, &tflags ) ) {
    err_msg( "Could not set terminal to raw-mode" );
    return ( -1 );
  }

  // Verify that the changes stuck, tcsetattr() returns with 0 when partly ok:
  if ( 0 > tcgetattr( fd, &tflags ) )
    goto TTY_RAWB_ERROUT;

  // Restore original settings, if only some of these changes took effect:
  if ( (_tty_raw_check( &tflags ) != 0) ||
       (tflags.c_cc[VMIN] != exp_min_amount) ||
       (tflags.c_cc[VTIME] != 0) ) {
    err_msg( "Not all settings took effect for raw-mode" );
    goto TTY_RAWB_ERROUT;
  }

  return ( 0 );

TTY_RAWB_ERROUT:
  tcsetattr( fd, TCSAFLUSH, &tbackup );
  errno = EINVAL;
  err_msg( "Tried to restore terminal settings" );

  return ( -1 );
}


#define TIMEOUT_GRANULARITY     ( (unsigned int) 100 ) // VTIME has 100ms steps 
int tty_raw_timeout( int fd,  unsigned int timeout )
{
  struct termios tflags;
  struct termios tbackup;
  unsigned int tr = timeout%TIMEOUT_GRANULARITY; // check valid input
  unsigned int to = (timeout - tr); // clean division
  
  // Scale to VTIME resolution:
  if ( tr > 0 ) {
    to += TIMEOUT_GRANULARITY;
    err_msg( "Warning: Invalid timeout. Adjusted to %lu [ms]", to );
  }
  to /= TIMEOUT_GRANULARITY;

  if ( 0 > tcgetattr( fd, &tflags ) ) {
    err_msg( "Failed to read terminal settings FD=%i", fd );
    return ( -1 );
  }

  _tty_raw( &tflags ); // set typical non-canonical stuff

  ///////////////////////////////
  // Wait till timeout expires //
  ///////////////////////////////

  tflags.c_cc[VMIN] = 0; tflags.c_cc[VTIME] = to; // capture till timeout
  /*
  tflags.c_cc[VMIN] = 0; tflags.c_cc[VTIME] = 0;  // immediate - anything
  tflags.c_cc[VMIN] = 2; tflags.c_cc[VTIME] = 0;  // blocking until VMIN bytes
  */

  // Update the terminal line discipline:
  if ( 0 > tcsetattr( fd, TCSAFLUSH, &tflags ) ) {
    err_msg( "Could not set terminal to raw-mode" );
    return ( -1 );
  }

  // Verify that the changes stuck, tcsetattr() returns with 0 when partly ok:
  if ( 0 > tcgetattr( fd, &tflags ) )
    goto TTY_RAWT_ERROUT;

  // Restore original settings, if only some of these changes took effect:
  if ( (_tty_raw_check( &tflags ) != 0) ||
       (tflags.c_cc[VMIN] != 0) ||
       (tflags.c_cc[VTIME] != to) ) {
    err_msg( "Not all settings took effect for raw-mode" );
    goto TTY_RAWT_ERROUT;
  }

  return ( 0 );

TTY_RAWT_ERROUT:
  tcsetattr( fd, TCSAFLUSH, &tbackup );
  errno = EINVAL;
  err_msg( "Tried to restore terminal settings" );

  return ( -1 );
}


void tty_interactive( int fd, struct winsize *set_size )
{
  struct termios set_termios;

  if ( 0 > tcgetattr( fd, &set_termios ) )
    err_sys( "Failed to read terminal settings" );

  #if defined( BSD )
    // TIOCSCTTY is the BSD way to acquire a controlling terminal:
    if ( ioctl( fd, TIOCSCTTY, (char *)0 ) < 0 )
      err_sys( "Cannot access set TIOCSCTTY for FD=%i", fd ); 
  #endif

  if ( 0 > tcsetattr( fd, TCSANOW, &set_termios ) )
    err_sys( "Failed changing terminal discipline for FD=%i", fd );

  if ( NULL != set_size ) {
    if ( 0 > ioctl( fd, TIOCSWINSZ, set_size ) ) // 'S'et
      err_sys( "Failed to update window size for FD=%i", fd );
  }
}


int tty_change_window_size( int masterfd, int row, int col,
                            int xpixel, int ypixel )
{
  struct winsize win;

  if ( masterfd < 0 || row < 0 || col < 0 || xpixel < 0 || ypixel < 0 ) {
    err_msg( "Window size configuration failure for FD=%i (illegal value)",
             masterfd );
    return ( -1 );
  }

  win.ws_row = row;
  win.ws_col = col;
  win.ws_xpixel = xpixel;
  win.ws_ypixel = ypixel;

  if ( 0 != ioctl( masterfd, TIOCSWINSZ, &win ) ) {
    err_msg( "Cannot set window size for FD=%i", masterfd ); 
    return ( -1 );
  }

  return ( 0 );
}


void tty_save( int fd, struct termios *save_termios, struct winsize *save_size )
{ 
  // Fetch current terminal attributes:
  if ( 0 > (tcgetattr( fd, save_termios )) )
    err_sys( "Cannot fetch terminal attributes for FD=%i", fd );

  if ( NULL != save_size ) {
    if ( 0 > (ioctl( fd, TIOCGWINSZ, (char*) save_size )) ) // 'G'et
      err_sys( "Cannot get window settings for FD=%i", fd );
  }
}
  

int tty_reset( int fd, struct termios *load_termios,
               struct winsize *load_winsz )
{
  if ( 0 > tcsetattr( fd, TCSAFLUSH, load_termios ) ) {
    if ( 0 > tcsetattr( fd, TCSAFLUSH, load_termios ) )
      goto TTY_RESET_ERROUT;
  }

  if ( NULL != load_winsz ) {
    if ( 0 > ioctl( fd, TIOCSWINSZ, load_winsz ) )
      goto TTY_RESET_ERROUT;
  }

  return ( 0 );

  TTY_RESET_ERROUT:
    err_msg( "Failed reset terminal FD=%i", fd );

    return ( -1 );
}


/*!
 * \note   Escape characters used according to ASCII table:
 *         DEZ HEX value defines
 *         0   00  \0    ascii_null
 *         32  20  space ascii_space
 *         34  22  "     ascii_dtick
 *         39  27  '     ascii_stick
 */
char *args_to_argl( char *basename, const char *strlist, size_t basename_max_size )
{
  size_t reserve = 0;          // calculated memory required
  int pos        = 0;          // current character position (input)
  int os         = 0;        // offset start of arguments listed
  char ellipse   = '\0';       // determine double/single ticks enclosed string
  char c         = strlist[0]; // current character to parse
  char *strl     = NULL;

  // Warn no-input:
  if ( NULL == strlist ) {
    err_msg( "Argument parser received empty string" );
    return ( strl );
  }

  // Take regard of single ticks and quotation marks enclosuring:
  if ( ( ascii_dtick == c) || (ascii_stick == c) ) { /// \todo  Other whitespace-likes.
    ellipse = c;
    os = 1;
  }

  // Extract basename:
  dbg_msg( "Parsing [%s], ellipse: %i, offset: %i", strlist, (int)ellipse, os );
  while ( (pos+os < (int)strlen( strlist )+1) &&
          (pos < (int)basename_max_size) ) {
    c = strlist[pos+os];

    if ( (ascii_space == c) || (ascii_null == c) ) {
      basename[pos++] = '\0';
      dbg_msg( "Delimitter (%i): %i replaced", pos-1, (int)c );
      break;
    }

    basename[pos++] = c;
    dbg_msg( "Char (%i)      : %c", pos-1, basename[pos-1] );
  }

  // Lease unused space:
  reserve = (size_t)(pos - os);
  dbg_msg( "Basename      : %s [%lu]", basename, reserve );

  //if ( NULL == (basename = (char*)realloc( base, sizeof( char )*reserve )) )
  //  err_sys( "Argument parser failure" );

  // Parse to ellipsing terminator:
  if ( pos+(2*os) < (int)strlen( strlist )+1 ) { // 2x enclosured character
    if ( NULL == (strl = (char*)malloc( sizeof( char ) * \
                                        (strlen( strlist )+1-reserve) )) )
      err_sys( "Argument parser failure" );

    // Reinit parser:
    pos = 0;
    os += (int)reserve;

    dbg_msg( "offset: %i, pos: %i, reserve: %lu", os, pos, reserve );

    // Capture remaining arguments:
    while ( pos+os < (int)strlen( strlist ) ) {
      c = strlist[pos+os];

      if ( (ascii_space == c) || (ascii_null == c) ) {
        strl[pos] = ascii_space;
        dbg_msg( "Delimitter (%i): %i whitespaced", pos, (int)c );
        pos++;
        continue;
      }

      strl[pos] = c;
      dbg_msg( "Char (%i)      : %c", pos, strl[pos] );
      pos++;
    }

    // In case of enclosuring, replace ellipse with null-termination:
    if ( (c = strlist[pos+os]) != ellipse ) {
      strl[pos] = c;
      dbg_msg( "Last char (%i) : %c", pos, strl[pos] );
      pos++;
    }
     
    strl[pos] = '\0'; // replace enclosure character or usual termination

    // Lease unused space:
    reserve = (size_t)pos;
    //strlist = (char*)realloc( strlist, sizeof( char ) * reserve );
    dbg_msg( "Arg. list (%i) : %s", reserve, strl );
  }

  return ( strl );
}


char **args_to_argv( const char *strlist )
{
  size_t amax = strlen( strlist )+1; // longest argmuent in list
  size_t acount = 0;                 // counter of arguments parsed
  size_t i, pos = 0;                      // character index
  char c = strlist[0];
  char cbuf[amax];

  #define EMPTY_CBUF( X ) { for( i=0; i<amax; i++ ) X[i] = '\0'; }
  a2av_strv = NULL;

  // Guard empty string memory violation:
  if ( NULL == strlist ) {
    err_msg( "Argument parsed empty string" );
    return ( a2av_strv );
  }

  EMPTY_CBUF( cbuf );
  a2av_strv = (char**)calloc( 1, sizeof( char )*amax );
  i = 0;

  // Copy and dynamically resize string array, '\0' terminated each:
  while ( pos <= strlen( strlist )+1 ) {
    c = strlist[pos++];

    /// \todo  Check subsequent whitespaces.
    if ( (ascii_space != c) && (ascii_null != c) ) {
      cbuf[i++] = c;

      continue;
    }

    cbuf[i] = '\0';
    if ( amax < i )
      amax = i;

    a2av_strv[acount] = cbuf;
    dbg_msg( "Argument %lu: %s", acount, a2av_strv[acount] );

    if ( NULL == (a2av_strv = (char**)reallocarray( a2av_strv, ++acount+1,
                                               sizeof( char )*amax )) )
      err_sys( "Argument parser out of memory" );

    //EMPTY_CBUF( cbuf );
    i = 0;
  }

  a2av_strv[acount] = (char*)NULL;

  return ( a2av_strv );
}


int do_driver_argl( char *driver, char *driver_list, int redirect_err )
{
  pid_t child;
  int pipefd[2];
  int ret = 0;

  // Create a full-duplex pipe to communicate with driver (UNIX domain socket):
  if ( fd_pipe( pipefd ) < 0 )
    err_sys( "Stream pipe: Could not be created" );

  if ( 0 > (child = fork()) ) {
    err_sys( "Stream pipe: Failed to fork" );
  } else if ( 0 == child ) { // (?) isn't pid_t 0 the parent?
    // Inside child process
    close( pipefd[1] );

    if ( 1 == redirect_err ) {
      if ( dup2( pipefd[0], STDERR_FILENO ) != STDERR_FILENO ) {
        err_sys( "Stream pipe (child): Cannot duplicate STDERR" );
      }
    }

    if ( dup2( pipefd[0], STDIN_FILENO ) != STDIN_FILENO ) {
      err_sys( "Stream pipe (child): Cannot duplicate STDIN" );
    }

    if ( dup2( pipefd[0], STDOUT_FILENO ) != STDOUT_FILENO ) {
      err_sys( "Stream pipe (child): Cannot duplicate STDOUT" );
    }

    if ( (pipefd[0] != STDIN_FILENO) && (pipefd[0] != STDOUT_FILENO) )
      close( pipefd[0] );

    // Leave the STDERR for driver alone:
    ret = execlp( driver, driver_list, (char*)NULL );
    err_sys( "Stream pipe (child): Execution error for %s", driver );
  }

  // Parent process
  close( pipefd[0] );

  if ( 1 == redirect_err ) {
    if ( dup2( pipefd[1], STDERR_FILENO ) != STDERR_FILENO ) {
      err_sys( "Stream pipe (parent): Cannot duplicate STDERR" );
    }
  }

  if ( dup2( pipefd[1], STDIN_FILENO ) != STDIN_FILENO ) {
    err_sys( "Stream pipe (parent): Cannot duplicate STDIN" );
  }

  if ( dup2( pipefd[1], STDOUT_FILENO ) != STDOUT_FILENO ) {
    err_sys( "Stream pipe (parent): Cannot duplicate STDOUT" );
  }

  if ( (pipefd[1] != STDIN_FILENO) && (pipefd[1] != STDOUT_FILENO) )
    close( pipefd[1] );

  /*!
   * \note     The parent returns, but with STDIN and STDOUT connected to the
   *           driver.
   */
  return ( ret );
}


int do_driver_argv( char *driver[], int redirect_err )
{
  pid_t child;
  int pipefd[2];
  int ret = 0;

  // Create a full-duplex pipe to communicate with driver (UNIX domain socket):
  if ( fd_pipe( pipefd ) < 0 )
    err_sys( "Stream pipe: Could not be created" );

  if ( 0 > (child = fork()) ) {
    err_sys( "Stream pipe: Failed to fork" );
  } else if ( 0 == child ) { // (?) isn't pid_t 0 the parent?
    // Inside child process
    close( pipefd[1] );

    if ( 1 == redirect_err ) {
      if ( dup2( pipefd[0], STDERR_FILENO ) != STDERR_FILENO ) {
        err_sys( "Stream pipe (child): Cannot duplicate STDERR" );
      }
    }

    if ( dup2( pipefd[0], STDIN_FILENO ) != STDIN_FILENO ) {
      err_sys( "Stream pipe (child): Cannot duplicate STDIN" );
    }

    if ( dup2( pipefd[0], STDOUT_FILENO ) != STDOUT_FILENO ) {
      err_sys( "Stream pipe (child): Cannot duplicate STDOUT" );
    }

    if ( (pipefd[0] != STDIN_FILENO) && (pipefd[0] != STDOUT_FILENO) )
      close( pipefd[0] );

    // Leave the STDERR for driver alone:
    ret = execvp( driver[0], driver );
    err_sys( "Stream pipe (child): Execution error for %s", driver );
  }

  // Parent process
  close( pipefd[0] );

  if ( 1 == redirect_err ) {
    if ( dup2( pipefd[1], STDERR_FILENO ) != STDERR_FILENO ) {
      err_sys( "Stream pipe (parent): Cannot duplicate STDERR" );
    }
  }

  if ( dup2( pipefd[1], STDIN_FILENO ) != STDIN_FILENO ) {
    err_sys( "Stream pipe (parent): Cannot duplicate STDIN" );
  }

  if ( dup2( pipefd[1], STDOUT_FILENO ) != STDOUT_FILENO ) {
    err_sys( "Stream pipe (parent): Cannot duplicate STDOUT" );
  }

  if ( (pipefd[1] != STDIN_FILENO) && (pipefd[1] != STDOUT_FILENO) )
    close( pipefd[1] );

  /*!
   * \note     The parent returns, but with STDIN and STDOUT connected to the
   *           driver.
   */
  return ( ret );
}


#include <sys/socket.h>
int fd_pipe( int fd[ 2 ] )
{
  return ( socketpair( AF_UNIX, SOCK_STREAM, 0, fd ) );
}

// EOF
