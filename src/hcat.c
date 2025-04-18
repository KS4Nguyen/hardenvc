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

#include <stdio.h>
#include <unistd.h>
#include "pty.h"


#define NAME_SIZE 80


const char *stdin_filename = "standard input";
FILE *target_file;

  
static void cleanup( void )
{
  if ( NULL != target_file )
    fclose( target_file );
}


static void usage( const char *program_name )
{
  printf( "Usage: %s [OPTIONS] [input files]\n", program_name );
  printf( " OPTIONS:\n" );
  printf( " -f <file> : File to write to.\n" );
  printf( " -h : Print this help.\n" );
  printf( " -i : Ignore EOF (terminate with CTRL+C).\n" );
  printf( " -A : Translate to a HEX represenation of input ASCII sequence.\n" );
  printf( " -H : Translate HEX to ASCII.\n" );
  printf( " -v : Show options when executed.\n" );
  printf( "\n" );
}


#ifdef LINUX
  #define OPTSTR "+f:hiAHv"
#else
  #define OPTSTR "f:hiAHv"
#endif

int main( int argc, char **argv )
{
  int ret = -1;             // return value
  int fdout;
  int verbose = 0;          // verbose mode
  int help = 0;             // print program help
  int ieof = 0;             // ignore EOF
  int a2h, h2a = 0;         // ASCII/HEX translation
  int i = 0;                // an index
  int exp = 0;              // explicit file mode
  int c;                    // option parser character
  const char *pname = argv[0];
  char *target = NULL;      // output file name
  char **pargs = NULL;      // additional arguments other than option args
  
  target_file = NULL;

  opterr = 0;               // from: unistd()
  while ( EOF != (c = getopt( argc, argv, OPTSTR)) )
  {
    switch( c ) {
      case 'v' : verbose = 1;                                   break;
      case 'f' : exp = 1;
                 target = optarg;                               break;
      case 'h' : help = 1;                                      break;
      case 'i' : ieof = 1;                                      break;
      case 'A' : a2h = 1;                                       break;
      case 'H' : h2a = 1;                                       break;
      case '?' : err_sys( "Unrecognized option: -%c", optopt ); break;
    }
  }

  // Stupid users...
  if ( argc <= (optind-1) )
    err_sys( "Usage: %s [-AHhiv -f <target file>] [infiles (stdin if none)]", argv[0] );

  if ( help == 1 ) {
    usage( pname );
    exit( 0 );
  }

  if ( 0 > atexit( cleanup ) )
    err_sys( "Cannot install the exit-handler for streams" );

  if ( SIG_ERR == signal_intr( SIGINT, sig_int ) )
    err_sys( "Failed to install signal handler for SIGINT" );

  // Write to file:
  if ( exp == 1 ) {
    if ( NULL == (target_file = fopen( (const char*)target, "w" )) )
      err_sys( "Cannot open %s", target );

    if ( STDOUT_FILENO != (fdout = fileno( target_file )) ) {
      if ( dup2( fdout, STDOUT_FILENO ) != STDOUT_FILENO )
        err_sys( "Stream duplication failure" );
    }
  } else {
    fdout = STDOUT_FILENO;
  }

  /*
  if ( fdout != STDOUT_FILENO ) {
    if ( dup2( fdout, STDOUT_FILENO ) != STDOUT_FILENO )
      err_sys( "Stream duplication failure on stdout" );
  }
  */

  if ( (argc - optind) < 1 ) {
    pargs = (char**)&stdin_filename;
  } else {
    pargs = &argv[optind];
  }

  // Show input/outputs:
  if ( verbose == 1 ) {
    fprintf( stderr, "Input files:" );
    for ( i=0; i<(argc-optind); i++ ) {
      fprintf( stderr, " %s", pargs[i] );
    }
    fprintf( stderr, "\nTarget file FD=%i: %s\n", fdout, target );
  }

  // Read from stdin if there's nothing else to do:
  if ( ! argv[0] )
    *--argv = (char*)"-";

  pty_buffers_atexit();

  do {
    ret = hcat( fdout, pargs, a2h, h2a, verbose );
  } while ( 1 == ieof );

  return ( ret );
}
// EOF
