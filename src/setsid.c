/*
 * setsid.c -- execute a command in a new session
 * Rick Sladkey <jrs@world.std.com>
 * In the public domain.
 *
 * 1999-02-22 Arkadiusz Miśkiewicz <misiek@pld.ORG.PL>
 * - added Native Language Support
 *
 * 2001-01-18 John Fremlin <vii@penguinpowered.com>
 * - fork in case we are process group leader
 *
 * 2008-08-20 Daniel Kahn Gillmor <dkg@fifthhorseman.net>
 * - if forked, wait on child process and emit its return code.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

/*
 * 2020-06-10 Sebastian Nguyen <sebastian.nguyen@asog-central.de>
 * - Using setsid.c in the 'hardenvc' package here, instead of including util-
 *   linux libraries, copy the few needed functions to here.
 * #include "c.h"
 * #include "nls.h"
 * #include "closestream.h"
 */

  #include <errno.h>

  #ifndef CLOSE_EXIT_CODE
    #define CLOSE_EXIT_CODE EXIT_FAILURE
  #endif

  static inline int
  close_stream(FILE * stream)
  {
  #ifdef HAVE___FPENDING
	  const int some_pending = (__fpending(stream) != 0);
  #endif
	  const int prev_fail = (ferror(stream) != 0);
	  const int fclose_fail = (fclose(stream) != 0);

	  if (prev_fail || (fclose_fail && (
  #ifdef HAVE___FPENDING
					    some_pending ||
  #endif
					    errno != EBADF))) {
		  if (!fclose_fail && !(errno == EPIPE))
			  errno = 0;
		  return EOF;
	  }
	  return 0;
  }

  /* Meant to be used atexit(close_stdout); */
  static inline void
  close_stdout(void)
  {
	  if (close_stream(stdout) != 0 && !(errno == EPIPE)) {
	  	if (errno)
	  		//warn(_("write error"));
	  		fprintf( stderr, "write error" );
	  	else
	  		//warnx(_("write error"));
	  		fprintf( stderr, "write error" );
	  	_exit(CLOSE_EXIT_CODE);
	  }

	  if (close_stream(stderr) != 0)
		  _exit(CLOSE_EXIT_CODE);
  }

  static inline void
  close_stdout_atexit(void)
  {
	  /*
	   * Note that close stdout at exit disables ASAN to report memory leaks
	   */
  #if !defined(__SANITIZE_ADDRESS__)
	  atexit(close_stdout);
  #endif
  }


static void usage(void)
{
	FILE *out = stdout;
	printf("setsid [options] <program> [arguments ...]\n");
	printf("Run a program in a new session.\n");
	printf("\nOptions:\n");
	fputs(" -c, --ctty     set the controlling terminal to the current one\n", out);
	fputs(" -f, --fork     always fork\n", out);
	fputs(" -w, --wait     wait program to exit, and use the same return\n", out);
  printf("\nModyfied program 'setsid.c' as part of 'coreutils-8.30' to\n");
  printf("compile without c.h, closestream.h and nls.h (utils-linux).\n");
  printf("Original source code (GPLv2): Rick Sladkey <jrs@world.std.com> and others.\n");
  printf("Modifications done by: Sebastian Nguyen <sebastian.nguyen@asog-central.de>\n");
	exit(EXIT_SUCCESS);
}

/*
static void __attribute__((__noreturn__)) usage(void)
{
	FILE *out = stdout;
	fputs(USAGE_HEADER, out);
	fprintf(out, _(
		" %s [options] <program> [arguments ...]\n"),
		program_invocation_short_name);

	fputs(USAGE_SEPARATOR, out);
	fputs(_("Run a program in a new session.\n"), out);

	fputs(USAGE_OPTIONS, out);
	fputs(_(" -c, --ctty     set the controlling terminal to the current one\n"), out);
	fputs(_(" -f, --fork     always fork\n"), out);
	fputs(_(" -w, --wait     wait program to exit, and use the same return\n"), out);

	printf(USAGE_HELP_OPTIONS(16));

	printf(USAGE_MAN_TAIL("setsid(1)"));
	exit(EXIT_SUCCESS);
}
*/

/// End of change


int main(int argc, char **argv)
{
	int ch, forcefork = 0;
	int ctty = 0;
	pid_t pid;
	int status = 0;

	static const struct option longopts[] = {
		{"ctty", no_argument, NULL, 'c'},
		{"fork", no_argument, NULL, 'f'},
		{"wait", no_argument, NULL, 'w'},
		//{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

/*
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
*/
	close_stdout_atexit();

	while ((ch = getopt_long(argc, argv, "+hcfw", longopts, NULL)) != -1)
		switch (ch) {
		case 'c':
			ctty=1;
			break;
		case 'f':
			forcefork = 1;
			break;
		case 'w':
			status = 1;
			break;

		case 'h':
			usage();
			exit(EXIT_SUCCESS);

		default:
			usage();
			exit(EXIT_FAILURE);
		}

	if (argc - optind < 1) {
		//warnx(_("no command specified"));
		//errtryhelp(EXIT_FAILURE);
		usage();
		exit(EXIT_FAILURE);
	}

	if (forcefork || getpgrp() == getpid()) {
		pid = fork();
		switch (pid) {
		case -1:
			fprintf( stderr, "fork() failure.\n" );
			exit(EXIT_FAILURE);
		case 0:
			/* child */
			break;
		default:
			/* parent */
			if (!status)
				return EXIT_SUCCESS;
			if (wait(&status) != pid) {
				fprintf( stderr, "wait() failure.\n");
				exit(EXIT_FAILURE);
		  }
			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			fprintf( stderr, "%i: Child %d did not exit normally.\n", status, pid );
			exit( status );
		}
	}
	if (setsid() < 0) {
		/* cannot happen */
		fprintf( stderr, "setsid() failed.\n");
		exit(EXIT_FAILURE);
  }

	if (ctty && ioctl(STDIN_FILENO, TIOCSCTTY, 1)) {
	  fprintf( stderr, "Failed to set the controlling terminal.\n");
		exit(EXIT_FAILURE);
  }
	execvp(argv[optind], argv + optind);
	//errexec(argv[optind]);
}
