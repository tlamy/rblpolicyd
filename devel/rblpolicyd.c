/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/), which allows combining different RBLs with weights.

   Copyright (C) 2004 Thomas Lamy

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  

*/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#define __MAIN__
#include "globals.h"

#include <stdio.h>
#include <sys/types.h>
#include <getopt.h>
#include <syslog.h>
#include "system.h"

#include "cfgfile.h"
#include "pidfile.h"

#define EXIT_FAILURE 1

char *xmalloc ();
char *xrealloc ();
char *xstrdup ();


static void usage (char *cfgfile, char *pidfile, int exitcode);

/* getopt_long return codes */
enum {DUMMY_CODE=129
};

/* Option flags and variables */


static struct option const long_options[] = {
  { "--verbose",	0, NULL, 'v'    },
  { "--debug",		0, NULL, 'd'    },
  { "--foreground",	0, NULL, 'f'    },
  { "--cfgfile",	1, NULL, 'c'    },
  { "--pidfile",	1, NULL, 'p'    },
  { "--max-children",	1, NULL, 'm'    },
  { "--help",		0, NULL, 'h'    },
  { "--version",	0, NULL, 'V'    },
  { NULL,		0, NULL,  0     }
};

int
main (int argc, char **argv)
{
  char *cfgfile = strdup(DEFAULT_CFGFILE);
  char *pidfile = strdup(DEFAULT_PIDFILE);
  cfgitem_t *cfg;
  int c, res;
  char *port = NULL;
  pid_t pid;

  verbose = 0;
  debug = 0;
  foreground = 0;
  terminate = 0;
  maxchilds = 10;
  progname = argv[0];

  openlog("rbl-policyd", LOG_PID, LOG_DAEMON);

  while (1) {
    int option_index = 0;
    c = getopt_long (argc, argv, "vdfc:p:m:hV", long_options, &option_index);
    if (c == -1)
      break;
    switch (c) {
      case 'v':
	verbose++;
	break;

      case 'd':
	debug++;
	break;

      case 'f':
	foreground++;
	break;

      case 'c':
	if (cfgfile) {
	  free(cfgfile);
	}
	cfgfile = strdup(optarg);
	break;

      case 'p':
	if (pidfile) {
	  free(pidfile);
	}
	pidfile = strdup(optarg);
	break;

      case 'm':
	maxchilds = atoi(optarg);
	break;

      case 'h':
	usage(cfgfile, pidfile, 0);
	break;  /* not reached */

      case 'V':
	printf ("rblpolicyd %s\n", VERSION);
	exit (0);
	break;

      default:
	fprintf(stderr, "%s: Unknown option '%c' passed\n\n", progname, c);
	usage(cfgfile, pidfile, EXIT_FAILURE);
	break;
    }
  }
  if (argc - optind != 1) {
    usage(cfgfile, pidfile, EXIT_FAILURE);
  }
  port = argv[optind];
  dbg("Checking for pidfile in '%s'\n", pidfile);
  if (pid_check(pidfile) != 0) {
    syslog(LOG_ERR, "Failed to write pid file %s", pidfile);
    free(cfgfile);
    free(pidfile);
    exit(EXIT_FAILURE);
  }
  dbg("Reading configuration from '%s'\n", cfgfile);
  if ((cfg = cfg_read(cfgfile)) == NULL) {
    free(cfgfile);
    free(pidfile);
    exit(EXIT_FAILURE);
  }

  if (!foreground && !debug) {
    pid = fork();
    if (pid > 0) {
      syslog(LOG_INFO, "Forked into background. Child pid is %d.\n", pid);
      exit(0);
    } else if (pid < 0) {
      syslog(LOG_NOTICE, "Failed to fork: %s\n", strerror(errno));
      fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
      exit(EXIT_FAILURE);
    }
    /* Only reached in background */
    (void) close(0);
    (void) close(1);
    (void) close(2);
    (void) open("/", O_RDONLY);
    (void) dup2(0,1);
    (void) dup2(0,2);
    setsid();
  }
  res = policyserver(cfg, port);
  syslog(LOG_INFO, "shutdown");
  return (res);
}

static void
usage (char *cfgfile, char *pidfile, int status)
{
  printf (_("%s - a policy daemon for Postfix which allows combining different RBLs with weights.\n"), progname);
  printf (_("Usage: %s [OPTION]... [port]\n"), progname);
  printf (_("\
Options:\n\
  -v, --verbose              verbose output\n\
  -d, --debug                show debug output\n\
  -f, --foreground           keep program in foreground\n\
  -c FILE, --cfgfile FILE    use config file FILE (current: %s)\n\
  -p FILE, --pidfile FILE    use file FILE to store pid (current: %s)\n\
  -h, --help                 display this help and exit\n\
  -V, --version              output version information and exit\n\
"), cfgfile, pidfile);
  exit (status);
}
