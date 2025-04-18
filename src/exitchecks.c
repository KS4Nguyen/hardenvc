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

#ifndef EXIT_SUCCESS
  #define EXIT_SUCCESS  0
#endif

#ifndef EXIT_FAILURE
  #define EXIT_FAILURE  1
#endif


int dorec;         // do-recurse (only affects exit-handler test)
int verbose;       // verbose mode
const char *pname; // program name


void exit_counter( void );


void usage( void );


#ifdef LINUX
  #define OPTSTR "+mse:rhv"
#else
  #define OPTSTR "mse:rhv"
#endif

int main( int argc, char *argv[] )
{
  int c;            // choser for options
  int test_errmsg, test_errsys, test_atexit, help = 0;
  long max_atexits = _SC_ATEXIT_MAX; // maximum number of exit-handlers
  long reg;         // sysconfig register

  pname = argv[0];

  opterr = 0;       // from: unistd()
  while ( EOF != (c = getopt( argc, argv, OPTSTR)) )
  {
    switch( c ) {
      case 'm' : test_errmsg = 1;   break;
      case 's' : test_errsys = 1;   break;
      case 'e' : test_atexit = 1;
                 max_atexits = (long)optarg;
                 if ( max_atexits < 0 ) {
                   fprintf( stderr, "Invalid amount of exit-handlers!\n" );
                   max_atexits = _SC_ATEXIT_MAX;
                 }  
                                    break;
      case 'r' : dorec = 1;         break;
      case 'h' : help = 1;          break;
      case 'v' : verbose = 1;       break;
      case '?' : fprintf( stderr, "Unrecognized option: -%c", optopt ); break;
    }
  }

  if ( 1 == help ) {
    usage();
    exit( EXIT_SUCCESS );
  }

  if ( 1 == test_atexit ) {
    if ( 1 == verbose )
      printf( "\nTEST atexit():\n\n" );

    reg = sysconf( max_atexits );
    if ( 1 == verbose )
      printf( "Sysconfig register: %ld\n", reg );
  
    if ( 0 != atexit( exit_counter ) ) {
      fprintf( stderr, "Failed installing the exit handler!\n" );
      exit( EXIT_FAILURE );
    }
  }

  if ( 1 == test_errmsg ) {
    if ( 1 == verbose )
      printf( "\nTEST err_msg():\n\n" );

    err_msg( argv[0], " SUCCESS" );
  }

  if ( 1 == test_errsys ) {
    if ( 1 == verbose )
      printf( "\nTEST err_sys():\n\n" );

    err_sys( argv[0], " SUCCESS" );
  }

  exit( EXIT_SUCCESS );
}


void exit_counter( void )
{
  static int cnt = 0;

  if ( 1 == verbose )
    printf( "Exit count: %d\n", ++cnt );

  if ( 1 == dorec )
    exit( cnt );
}


void usage( void )
{
  printf( "\nUsage: %s <OPTIONS [argument]>\n", pname );
  printf( "  OPTIONS\n" );
  printf( "   -m       : Test error messaging.\n" );
  printf( "   -s       : Test error messaging (with program termination).\n" );
  printf( "   -e <num> : Test atexit() handlers and set the limit to <num>.\n" );
  printf( "   -r       : Only valid with -e. Test the OS ability of\n" );
  printf( "              calls of exit-handlers. Linux does not do that.\n" );
  printf( "   -v       : Verbose mode.\n" );
  printf( "   -h       : Print this help.\n" );
  printf( "\n" );
}
