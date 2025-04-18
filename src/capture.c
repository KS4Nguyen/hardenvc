/* This tool sends a command string over a serial line and captures the output
 * of the target device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>

#define DEBUG 0

#if DEBUG
#define MAX_RESP_DELAY	15
#else
#define MAX_RESP_DELAY	1
#endif

#define BUFLEN		512
#define MAX_CONFIG_LEN	1024
#define CONFIG_FILE	"/.captureconfig"
#define CC_TAG		"cc="
#define CHAR_DELAY_TAG	"delay="
#define TIMEOUT_TAG	"show_timeout="
#define TIME_USED_TAG	"show_timeout_usage="
#define TIMEOUT_GRANULARITY 100
#define MIN_MARGIN	30
#define MARGIN_WARN	"TIMEOUT CRITICAL"

#define ARG_TIMEOUT	"-t"
#define ARG_DEVICE	"-d"
#define ARG_EXITTEXT	"-e"
#define ARG_VERBOSE	"-v"
#define ARG_USAGE1	"-h"
#define ARG_USAGE2	"--help"
#define TIMEOUT_DEFAULT	1000
#define DEVICE_DEFAULT	"/dev/ttyS0"

#define RETURN_MATCH	0
#define RETURN_TIMEOUT	1
#define RETURN_ERROR	2

struct termios orig_termios;
int dut_con;
int ccfile;
int cc = 0;

FILE* fconfig;

void die(const char *s) {
	printf("ERROR: %s\n", s);
	exit(RETURN_ERROR);
}


void usage(char* progname) {
	printf("%s sends strings to serial port and captures response:\n", progname);
	printf("Usage: %s [-h] [-v] [-t timeout] [-d device] [-e exit_text] \"command line\"\n", progname);
	printf("Send \"command line\" to \"device\" and wait for \"exit_text\" as reponse, but not longer than \"timeout\" ms.\n");
	printf("Default for \"device\" is /dev/ttyS0\n");
	printf("Default for \"timeout\" is 1000(ms)\n");
	printf("Timeout resolution is limited to chunks of 100ms\n");
	printf("Be sure to enclose \"command line\" in double quotes if it contains spaces.\n");
	printf("If you don't want to send anything, use \"\" as \"command line\"\n");
	exit(RETURN_ERROR);
}


/* put terminal in raw mode - see termio(7I) for modes */
void tty_raw(int fd, unsigned int timeout)
{
	struct termios raw;

	raw = orig_termios;  /* copy original and then modify below */

	/* input modes - clear indicated ones giving: no break, no CR to NL,
	no parity check, no strip char, no start/stop output (sic) control */
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

	/* output modes - clear giving: no post processing such as NL to CR+NL */
	raw.c_oflag &= ~(OPOST);

	/* control modes - set 8 bit chars */
	raw.c_cflag |= (CS8);

	/* local modes - clear giving: echoing off, canonical off (no erase with
	backspace, ^U,...),  no extended functions, no signal chars (^Z,^C) */
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

	/* control chars - set return condition: min number of bytes and timer */
	raw.c_cc[VMIN] = 5; raw.c_cc[VTIME] = 8; /* after 5 bytes or .8 seconds
						after first byte seen      */
	raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 0; /* immediate - anything       */
	raw.c_cc[VMIN] = 2; raw.c_cc[VTIME] = 0; /* after two bytes, no timer  */
	raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = timeout/TIMEOUT_GRANULARITY; /* after a byte or 1 second  */

	/* put terminal in raw mode after flushing */
	if (tcsetattr(fd, TCSAFLUSH,&raw) < 0)
		die("can't set raw mode");
}


/* exit handler for tty reset */
void tty_atexit(void) {

	//printf("Restored previous tty settings...\n");
	tcsetattr(dut_con, TCSAFLUSH, &orig_termios);
}


unsigned int rematch(char *pattern, char *buf, unsigned int current) {
	unsigned int n;
	unsigned int m;

	for (n=1; n<=current; n++) {
		//write(STDOUT_FILENO, buf+n-1, 1);
		if (cc) write(ccfile, buf+n-1, 1);
		if (strncmp(buf+n, pattern, current-n) == 0) {
			for (m=0; m<(current-n); m++) {
				buf[m] = buf[n+m];
			}
			buf[m]=0;
			return (current-n);
		}
	}

	return n;
}


int main(int argc, char* argv[]) {
	unsigned int arglen;
	unsigned int timeout_arg = 1000;
	unsigned int timeout_val = 0;
	unsigned int timeout_loop = 1;
	unsigned int verbose = 0;
	char *exit_text = NULL;
	char *cmdline = NULL;
	char *device = DEVICE_DEFAULT;
	char *hide_text = NULL;
	unsigned int n;
	unsigned int hide_idx = 0;
	int ret = RETURN_ERROR;
	char c;
	char line[MAX_CONFIG_LEN];
	char *config_name;
	char *match;
	char *s;
	const char *homedir;
	char ccfilename[MAX_CONFIG_LEN];
	unsigned int chardelay = 0;
	unsigned int show_timeout = 0;
	unsigned int show_timeout_usage = 0;
	char msg[MAX_CONFIG_LEN];
	int margin;
	ssize_t readlen;
#if DEBUG
	short unsigned int port;
#endif


	if (argc == 1) {
		usage(argv[0]);
	}

	for (n=1; n<argc; n++) {
		// check for empty command first, because strncmp fails @ len=0
		if (strlen(argv[n]) == 0) {
			cmdline = argv[n];
		} else
		if (strncmp(argv[n], ARG_TIMEOUT, strlen(argv[n])) == 0) {
			if (argc <= (n+1)) {
				printf("missing timeout value");
				usage(argv[0]);
			}
			sscanf(argv[n+1], "%i%n", &timeout_arg, &arglen);
			if (arglen != strlen(argv[n+1])) {
				printf("invalid timeout value");
				usage(argv[0]);
			}
			n++;
		} else
		if (strncmp(argv[n], ARG_DEVICE, strlen(argv[n])) == 0) {
			if (argc <= (n+1)) {
				printf("missing device name");
				usage(argv[0]);
			}
			n++;
			device = argv[n];
		} else
		if (strncmp(argv[n], ARG_EXITTEXT, strlen(argv[n])) == 0) {
			if (argc <= (n+1)) {
				printf("missing exit text");
				usage(argv[0]);
			}
			n++;
			exit_text = argv[n];
		} else
		if (strncmp(argv[n], ARG_VERBOSE, strlen(argv[n])) == 0) {
			verbose = 1;
		} else
		if (strncmp(argv[n], ARG_USAGE1, strlen(argv[n])) == 0) {
			usage(argv[0]);
		} else
		if (strncmp(argv[n], ARG_USAGE2, strlen(argv[n])) == 0) {
			usage(argv[0]);
		} else
			cmdline = argv[n];
	}

	if (!cmdline)
		die("no command line given");

	if (verbose) {
		printf("command: %s\n", cmdline);
		printf("device: %s\n", device);
		printf("exit text: %s\n", exit_text?exit_text:"None");
		printf("timeout: %d ms\n", timeout_arg);
	}

	/*
	 * read configuration file, if it exists
	 */
	if ((homedir = getenv("HOME")) == NULL) {
		homedir = getpwuid(getuid())->pw_dir;
	}
	//printf("env: ~: %s\n", homedir);

	config_name = (char *)malloc(strlen(homedir) + strlen(CONFIG_FILE) + 2);
	strcpy(config_name, homedir);
	strcat(config_name, CONFIG_FILE);

	fconfig = fopen(config_name, "r");
	if (fconfig > 0) {
		while (fgets(line, MAX_CONFIG_LEN, fconfig) != NULL) {
			// check for comment line
			if (line[0] == '#')
				continue;

			// check for "enable logging" entry
			if ((match = strstr(&line[0], CC_TAG)) != 0) {
				for (s= &line[0] + strlen(line)-1; s > &line[0]; s--) {
					if ((*s == '\n')||(*s == '\r')) {
						*s = 0;
						continue;
					} else
						break;
				}
				strcpy(ccfilename, match + strlen(CC_TAG));
				ccfile = open(ccfilename, O_RDWR | O_APPEND | O_CREAT);
				if (ccfile > 0) {
					// enable carbon copy (aka logging)
					if (verbose)
						printf("logging all traffic to %s\n", ccfilename);
					cc = 1;
				}
				continue;
			}

			// check for delay to apply when sending characters
			if ((match = strstr(&line[0], CHAR_DELAY_TAG)) != 0) {
				//printf("line: %s\n", match);
				if (sscanf(match + strlen(CHAR_DELAY_TAG), "%d", &chardelay) == 1) {
					if (verbose)
						printf("setting delay to %d milliseconds\n", chardelay);
				} else
					if (verbose)
						printf("Invalid delay setting\n");
				continue;
			}

			// check for showing timeouts if logging is enabled
			if ( cc && ((match = strstr(&line[0], TIMEOUT_TAG)) != 0)) {
				//printf("line: %s\n", match);
				if (sscanf(match + strlen(TIMEOUT_TAG), "%d", &show_timeout) == 1) {
					if (verbose && show_timeout)
						printf("Print timeout events to logfile\n");
				} else
					if (verbose && show_timeout)
						printf("Invalid setting for showing timeout events\n");
				continue;
			}

			// check for showing timeouts usge info if logging is enabled
			if ( cc && ((match = strstr(&line[0], TIME_USED_TAG)) != 0)) {
				//printf("line: %s\n", match);
				if (sscanf(match + strlen(TIME_USED_TAG), "%d", &show_timeout_usage) == 1) {
					if (verbose && show_timeout_usage)
						printf("Print timeout usage to logfile\n");
				} else
					if (verbose && show_timeout_usage)
						printf("Invalid setting for showing timeout usage\n");
				continue;
			}
		}
	} else {
		//printf("no config file found\n");
	}

	dut_con = open(device, O_RDWR);

	if (dut_con == -1)
		die("could not open device");

	if (! isatty(dut_con))
		die("not on a tty");

	if (exit_text) {
		hide_text = malloc(strlen(exit_text)+1);
		if (!hide_text)
			die("Out of memory!");
		hide_text[strlen(exit_text)] = 0;
	}

	/* check timeout granularity; must be n*TIMEOUT_GRANULARITY */
	if (timeout_arg % TIMEOUT_GRANULARITY) {
		sprintf(msg, "Invalid timeout granularity. Must be a multiple of %dms\n", TIMEOUT_GRANULARITY);
		die(msg);
	}

	/* calculate counter for timeout loop */
	if (timeout_arg >= TIMEOUT_GRANULARITY) {
		timeout_loop = timeout_arg/TIMEOUT_GRANULARITY;
		timeout_val = TIMEOUT_GRANULARITY;
	}

	/* store current tty settings in orig_termios */
	if (tcgetattr(dut_con, &orig_termios) < 0)
		die("can't get tty settings");

	/* register the tty reset with the exit handler */
	if (atexit(tty_atexit) != 0)
		die("atexit: can't register tty reset");


	tty_raw(dut_con, timeout_val);

	/*
	 * send 'cmdline' only if it is not empty.
	 * This allows for just waiting for a string without sending anything.
	 */
	if (strlen(cmdline) != 0) {
		if (chardelay) {
			int i;
			for (i=0; i<strlen(cmdline); i++) {
				write(dut_con, &cmdline[i], 1);
				usleep(chardelay*1000);
			}
			usleep(chardelay*1000);
			write(dut_con, "\n", strlen("\n"));
		} else {
			write(dut_con, cmdline, strlen(cmdline));
			write(dut_con, "\n", strlen("\n"));
		}
		if (cc) {
			write(ccfile, cmdline, strlen(cmdline));
			write(ccfile, "\n", strlen("\n"));
		}
	}

	/*
	 * Scan input for exit sequence until match or timeout.
	 * Every character which is not part of the exit sequence shall be
	 * printed on stdout.
	 * If one or more characters match the exit sequence, they will be
	 * copied to a separate buffer (hide_text) because the exit sequence
	 * shall not be printed. If there is finally a mismatch, the buffer is
	 * scanned bytewise if there could be another match.
	 *
	 * Example: exit sequence is "abac" and input is "ababac"
	 * Receiving the first character 'a' is a match and stored in hide_text.
	 * The same is true for 'b' and 'a'. The next input character 'b'
	 * invalidates hide_text. But hide_text (which is "abab" now) must not
	 * be thrown away (i.e. printed) completely, but only the first two
	 * chars because the third and fourth character again match the exit
	 * sequence. This rematching of the hide_text buffer is done in
	 * rematch() (surprise!)
	 * It returns the number of characters in hide_text which still match
	 * the exit sequence. In the example above, rematch returns 2 and
	 * hide_text contains "ab" then.
	 */
	for (;;) {
		readlen = read(dut_con, &c, 1);
		if (readlen < 0) {
			printf("ERROR reading from %s\n", device);
			ret = RETURN_ERROR;
			break;
		} else if (readlen == 0) {
			if (--timeout_loop)
				continue;
			//write(STDOUT_FILENO, hide_text, hide_idx);
			if (cc) write(ccfile, hide_text, hide_idx);
			if (cc && show_timeout && exit_text) {
				sprintf(msg, "\n======== TIMEOUT! (%dms) ========\n", timeout_arg);
				write(ccfile, msg, strlen(msg));
			}
			ret = RETURN_TIMEOUT;
			break;
		} else {
			if (exit_text) {
				if (c == exit_text[hide_idx]) {
					hide_text[hide_idx++] = c;
					if (hide_idx == strlen(exit_text)) {
						//c='\n';
						//write(STDOUT_FILENO, &c, 1);
						if (cc) write(ccfile, hide_text, strlen(exit_text));
						if (cc && show_timeout_usage) {
							margin = timeout_loop*TIMEOUT_GRANULARITY*100 / timeout_arg;
							sprintf(msg, "\n-------- Timeout info: %dms left (of %dms) -------- %s\n", timeout_loop*TIMEOUT_GRANULARITY, timeout_arg, margin<MIN_MARGIN?MARGIN_WARN:"");
							write(ccfile, msg, strlen(msg));
						}
						ret = RETURN_MATCH;
						break;
					}
				}
				else {
					if (hide_idx) {
						hide_text[hide_idx++] = c;
						hide_idx = rematch(exit_text, hide_text, hide_idx);
					} else {
						//write(STDOUT_FILENO, &c, 1);
						if (cc) write(ccfile, &c, 1);
					}
				}
			} else {
				//write(STDOUT_FILENO, &c, 1);
				if (cc) write(ccfile, &c, 1);
			}
		}
	}

	return ret;
}
