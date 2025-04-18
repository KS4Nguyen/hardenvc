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

/*!
 * \note          Echo-loop echoes stdin to stdout and logfile, if given by
 *                program argument. Does not exit on EOF, exit with CTRL+C.
 */

#include "pty.h"
#include <signal.h>
#include <time.h>


#define ms( x )             usleep( 1000 * x )
#define DL                  2
#define DEFAULT_BUFSIZE     2048


const char *pname;
char linefeed;     // user can change linefeed if desired
char *prompt;      // provide a prompt for caller
char *filename;    // logfile
FILE *file;


void usage( const char *prog_name );


/*!
 * \brief   Exit handler of atexit() call. Closes logfile and deallocates
 *          reserved buffer memory.
 */
static void cleanup( void )
{
  dbg_msg( "Cleanup handler called." );
  if ( NULL != file )
    fclose( file );

  free( prompt );  
}


#ifdef LINUX
  #define OPTSTR "+b:hf:l:sv"
#else
  #define OPTSTR "b:hf:l:sv"
#endif
int main( int argc, char *argv[] )
{
  int verbose = 0;           // additional runtime information
  int use_prompt = 0;        // prompt each new line with user defined pattern
  int help = 0;              // how to use this program
  int safely = 0;
  ssize_t nread, nwrite = 0; // character amounts
  size_t bufsize = DEFAULT_BUFSIZE;
  size_t prompt_len = 0;     // amount of prefixed characters
  int fds = -1;              // FD of logfile
  int c, i = 0;              // arguments parser
  char buf[bufsize+1];
  char *ccur = NULL;         // address of current new-line character in (hot)
  char *cold = NULL;         // position of old character

  linefeed = '\n';           // end of line character
  pname    = argv[0];
  prompt   = NULL;           // provide a prompt for caller
  filename = NULL;           // logfile
  opterr   = 0;
  buf[bufsize] = 0;          // guarding zero termination

  while ( EOF != (c = getopt( argc, argv, OPTSTR)) )
  {
    switch( c ) {
      case 'b' : bufsize = (size_t)atoi( optarg ); break;
      case 'h' : help = 1;                         break;
      case 'f' : filename = (char*)malloc( strlen( optarg)+1 );
                 filename = optarg;                break;
      case 's' : safely = 1;                       break;
      case 'l' : linefeed = *optarg;               break;
      case 'v' : verbose = 1;                      break;
      default  : err_sys( "Unrecognized option: -%c\n", optopt );
                 exit( EXIT_FAILURE );             break;
    }
  }

  if ( argc < optind ) {
    err_msg( "Usage: %s [-b <bs> -hv -f <file> -l <lf>] [prompt]",
             pname );
    exit( EXIT_FAILURE );
  }

  if ( 1 == help ) {
    usage( pname );
    exit( EXIT_SUCCESS );
  }

  // Parse multi string prompt:
  for ( i=0; argc>(optind+i); i++ ) {
    prompt_len += strlen( argv[optind+i] ) + 1;
    dbg_msg( "Determinated prompt length: %lu", prompt_len );
  }

  if ( 0 < prompt_len ) {
    use_prompt = 1;
    prompt = (char*)malloc( prompt_len+1 );
    stricpy( prompt, argv[optind], prompt_len, ' ' ); // stricpy() appends 0
  }

  // Open logfile:
  if ( NULL != filename ) {
    if ( NULL == (file = fopen( filename, "a" )) )
      err_sys( "Cannot open file: %s", filename );

    fds = fileno( file );
    if ( STDOUT_FILENO != dup2( fds, STDOUT_FILENO ) )
      err_sys( "Cannot duplicate stdout to %s", filename );
  } else {
    fds = STDOUT_FILENO;
  }

  if ( 0 > atexit( cleanup ) )
    err_sys( "Error: Cannot install exit handler" );

  if ( SIG_ERR == signal_intr( SIGINT, sig_int ) )
    err_sys( "Failed to install signal handler for SIGINT" );

  if ( 1 == verbose ) {
    fprintf( stderr, "Prompt:         %s\n",     prompt );
    fprintf( stderr, "File (FD=%i):    %s\n",    fds, filename );
    fprintf( stderr, "Linefeed (HEX): 0x%02X\n", linefeed );
    fprintf( stderr, "Buffer size:    %lu\n",    bufsize );
  }

  fflush( stdin );

  switch ( use_prompt ) {
    case 1:

      while ( -1 < (nread = read( STDIN_FILENO, &buf, bufsize )) ) {
        if ( 0 == nread )
          continue;

        //buf[nread] = 0; // limit strchr() to buf-address range 
        /*!
         * \note  strchr( const char *s, int c )
         *        "[...] return[s] a pointer to the matched character or NULL if
         *        the character is not found. The terminating null byte is 
         *        considered part of the string, so that if c is specified as
         *        '\0', these functions return a pointer to the terminator.
         */
        for ( cold=&buf[0]; nread>0; cold=ccur+1 ) {
          if ( NULL == (ccur = strchr( cold, linefeed )) ) {
            if ( 1 > (nwrite = 1 + (ssize_t)ccur - (ssize_t)cold) )
              break;
          } else {
            nwrite = nread;
          }

          dbg_msg( "Remaining %i to write (%i)\n  Start: %lu\n  End:   %lu", 
                   nread, nwrite, (size_t)cold, (size_t)ccur );

          write_or_warn( fds, prompt, prompt_len );
          write_or_warn( fds, cold, (size_t)nwrite );

          nread -= nwrite;
        }

        if ( 1 == safely )
          write_or_warn( fds, prompt, prompt_len );
      }

    default:
      for ( ; -1 < (nread = read( STDIN_FILENO, &buf, bufsize )); ms( DL ) ) {
        if ( 0 == nread )
          continue;

        write_or_warn( fds, &buf, (size_t)nread );
      }
  }

  if ( 0 > nread )
    err_sys( "Read failure." );

  exit( EXIT_SUCCESS );
}


void usage( const char *prog_name )
{
  printf( "Usage: %s [OPTIONS] [prompt]\n", prog_name );
  printf( "  OPTIONS:\n" );
  printf( "    -b <bs>   : Buffer size (default: %i bytes).\n",
          (int)( DEFAULT_BUFSIZE ) );
  printf( "    -f <file> : Also log each printed line to <file>.\n" );
  printf( "    -h        : Print this help.\n" );
  printf( "    -l <lf>   : Linefeed character (default: 0x%02X).\n", linefeed );
  printf( "    -s        : Safe-prompt the last line printed, before re-read\n" );
  printf( "    -v        : Tell what is done.\n" );
  printf( "  \'prompt\' is optional pattern, that prefixes every new line.\n" );
  puts( "" );
}
// EOF
