#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#define _GNU_SOURCE
#include <getopt.h>

#define __MAIN__
#include "globals.h"

#include "cfgfile.h"


struct option longopts[] = {
  { "--verbose",	0, NULL, 'v'	},
  { "--debug",		0, NULL, 'd'	},
  { "--foreground",	0, NULL, 'f'	},
  { "--cfgfile",	1, NULL, 'c'	},
  { "--pidfile",	1, NULL, 'p'	},
  { "--help",		0, NULL, 'h'	},
  { NULL,		0, NULL,  0	}
};

static int pid_get(char *pidfile) {
  FILE *fp;
  int c;
  int pid = 0;

  if ((fp = fopen(pidfile, "r")) == NULL) {
    return -1;
  }
  while ((c = fgetc(fp)) != EOF) {
    if (c >= '0' && c <= '9') {
      pid = 10*pid + (c-'0');
    } else {
      break;
    }
  }
  fclose(fp);
  return pid;
}

static int pid_check(char *pidfile) {
  struct stat st;
  int pid;
  if (stat(pidfile, &st) == 0) {
    pid = pid_get(pidfile);
    if (pid <= 0) {
      return 0;
    }
    if (kill(pid, 0) == 0) {
      fprintf(stderr, "Process %d is still running\n", pid);
      return -1;
    }
    if(S_ISREG(st.st_mode)) {
      verb("Removing stale pidfile %s\n", pidfile);
      unlink(pidfile);
    } else {
      fprintf(stderr, "Stale pidfile %s not removed: Not a regular file\n", pidfile);
      return -1;
    }
  }
  /* no pidfile */
  return 0;
}
  

int main (int argc, char ** argv) {
  int c;
  char *cfgfile = strdup(DEFAULT_CFGFILE);
  char *pidfile = strdup(DEFAULT_PIDFILE);
  cfgitem_t *cfg;
  int  port = 0;

  verbose = 0;
  debug = 0;
  foreground = 0;
  PROGNAME = argv[0];
  
  openlog("rbl-policyd", LOG_PID, LOG_DAEMON);

  while (1) {
    int option_index = 0;
    c = getopt_long (argc, argv, "vdfc:p:h", longopts, &option_index);
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
	
      case 'h':
	usage(cfgfile, pidfile);
	break;	/* not reached */

      default:
	fprintf(stderr, "%s: Unknown option '%c' passed\n\n", PROGNAME, c);
	usage(cfgfile, pidfile);
	break;
    }
  }
  if (argc - optind != 1) {
    usage(cfgfile, pidfile);
  }
  port = atoi(argv[optind]);
  if (port <= 0 || port >= 65535) {
    fprintf(stderr, "%s: bad port '%s' to listen on\n", PROGNAME, argv[optind]);
    exit(255);
  }
  dbg("Checking for pidfile in '%s'\n", pidfile);
  if (pid_check(pidfile) != 0) {
    syslog(LOG_ERR, "Failed to write pid file %s", pidfile);
    free(cfgfile);
    free(pidfile);
    exit(1);
  }
  dbg("Reading configuration from '%s'\n", cfgfile);
  if ((cfg = cfg_read(cfgfile)) == NULL) {
    free(cfgfile);
    free(pidfile);
    exit(1);
  }

  if (!foreground) {
    pid = fork();
    if (pid > 0) {
      syslog(LOG_INFO, "Forked into background. Child pid is %d.\n", pid);
      exit(0);
    } else if (pid < 0) {
      syslog(LOG_NOTICE, "Failed to fork: %s\n", strerror(errno));
      fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
      exit(255);
    }
    /* Only reached in background */
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
  }
  res = policyserver(cfg, port);
  syslog(LOG_INFO, "shutdown");
  return res;
}



