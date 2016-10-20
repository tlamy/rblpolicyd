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

#include <stdio.h>
#include <sys/types.h>
#include <getopt.h>
#include <syslog.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_CTYPE_H
#include <ctype.h>
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <pthread.h>

#include "system.h"

#include "server.h"
#include "cfgfile.h"
#include "pidfile.h"
#include "globals.h"

#define EXIT_FAILURE 1



static void usage (char *pidfile, int exitcode);

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
  char *pidfile = strdup(DEFAULT_PIDFILE);
  int c, res;
  char *port = NULL;
  pid_t pid;
  int sock;
  struct sockaddr *sa = NULL;
  struct sockaddr_in  saip;
  struct sockaddr_un  saun;
  socklen_t salen;
  char *ch;
  int on = 1;

  pthread_mutex_init(&rblist_mutex, NULL);

  cfgpath = strdup(DEFAULT_CFGFILE);
  verbose = 0;
  debug = 0;
  foreground = 0;
  appstate = APP_RUN;
  maxthreads = 10;
  progname = argv[0];

  openlog("rbl-policyd", LOG_PID, LOG_MAIL);

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
	if (cfgpath) {
	  free(cfgpath);
	}
	cfgpath = strdup(optarg);
	break;

      case 'p':
	if (pidfile) {
	  free(pidfile);
	}
	pidfile = strdup(optarg);
	break;

      case 'm':
	maxthreads = atoi(optarg);
	break;

      case 'h':
	usage(pidfile, 0);
	break;  /* not reached */

      case 'V':
	printf ("rblpolicyd %s\n", VERSION);
	exit (0);
	break;

      default:
	fprintf(stderr, "%s: Unknown option '%c' passed\n", progname, c);
	usage(pidfile, EXIT_FAILURE);
	break;
    }
  }
  if (argc - optind != 1) {
    usage(pidfile, EXIT_FAILURE);
  }
  port = argv[optind];
  dbg("Checking for pidfile in '%s'\n", pidfile);
  if (pid_check(pidfile) != 0) {
    syslog(LOG_ERR, "Failed to write pid file %s", pidfile);
    free(cfgpath);
    free(pidfile);
    cfg_free(&rblist);
    closelog();
    exit(EXIT_FAILURE);
  }
  dbg("Reading configuration from '%s'\n", cfgpath);
  if ((rblist = cfg_read(cfgpath)) == NULL) {
    free(cfgpath);
    free(pidfile);
    cfg_free(&rblist);
    closelog();
    exit(EXIT_FAILURE);
  }


  if ((res = res_init()) != 0) {
    syslog(LOG_ERR, "Fatal: Cannot initialize resolver library");
    cfg_free(&rblist);
    free(pidfile);
    free(cfgpath);
    closelog();
    exit(EXIT_FAILURE);
    return -1;
  }

  if (port[0] == '/') {
    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (!sock) {
      fprintf(stderr, "Could not create UNIX socket %s: %s\n", port, strerror(errno));
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
    memset(&saun, 0, sizeof(struct sockaddr_un));
    saun.sun_family = AF_UNIX;
    strcpy(saun.sun_path, port);
    sa = (struct sockaddr *) &saun;
    salen = sizeof(struct sockaddr_un);
    if (bind(sock, (struct sockaddr *) &saun, salen) != 0) {
      fprintf(stderr, "Could not bind to UNIX socket %s: %s\n", port, strerror(errno));
      close(sock);
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
  } else if ((ch = strchr(port, '/')) != NULL) {
    /* host/port pair */
    char *hostname = NULL;
    char *portname = NULL;
    struct addrinfo hint;
    struct addrinfo *addr;
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (!sock) {
      fprintf(stderr, "Could not create INET socket %s: %s\n", port, strerror(errno));
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
    int opt=1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

    hostname = malloc(ch-port+1);
    memcpy(hostname, port, ch-port);
    hostname[ch-port] = '\0';
    portname = strdup(ch+1);
    
    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    if ((res = getaddrinfo(hostname, portname, &hint, &addr)) != 0) {
      fprintf(stderr, "Can not resolve %s/%s: %s\n", hostname, portname, gai_strerror(res));
      free(hostname);
      free(portname);
      close(sock);
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
    memcpy(&saip, addr->ai_addr, addr->ai_addrlen);
    sa = (struct sockaddr *) &saip;
    salen = sizeof(saip);

    freeaddrinfo(addr);
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
      perror("setsockopt(SO_REUSEADDR) failed");
    }
    dbg("Binding INET socket...(sa=0x%08x salen=%d)\n", &saip, salen);
    if (bind(sock, (struct sockaddr *) &saip, salen) != 0) {
      fprintf(stderr, "Could not bind to INET socket %s: %s\n", port, strerror(errno));
      close(sock);
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
  } else {
    struct addrinfo hint;
    struct addrinfo *addr;

    dbg("'%s' looks like INET service...\n", port);
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (!sock) {
      fprintf(stderr, "Could not create INET socket %s: %s\n", port, strerror(errno));
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
    int opt=1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));
    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    if ((res = getaddrinfo(NULL, port,  &hint, &addr)) != 0) {
      fprintf(stderr, "Can not resolve service %s: %s\n", port, gai_strerror(res));
      close(sock);
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
      return -1;
    }
    memcpy(&saip, addr->ai_addr, addr->ai_addrlen);
    freeaddrinfo(addr);
    sa = (struct sockaddr *) &saip;
    salen = sizeof(struct sockaddr_in);
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
      perror("setsockopt(SO_REUSEADDR) failed");
    }
    dbg("Binding INET socket...(sa=0x%08x salen=%d)\n", &saip, salen);
    if (bind(sock, (struct sockaddr *) &saip, salen) != 0) {
      fprintf(stderr, "Could not bind to INET socket %s: %s\n", port, strerror(errno));
      close(sock);
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(EXIT_FAILURE);
    }
  }
  if (listen(sock, 255) != 0) {
    fprintf(stderr, "Could not listen on UNIX socket %s: %s\n", port, strerror(errno));
    close(sock);
    cfg_free(&rblist);
    free(pidfile);
    free(cfgpath);
    closelog();
    exit(EXIT_FAILURE);
  }

  if (!foreground && !debug) {
    pid = fork();
    if (pid > 0) {
      syslog(LOG_INFO, "Forked into background. Child pid is %d.\n", pid);
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
      exit(0);
    } else if (pid < 0) {
      syslog(LOG_NOTICE, "Failed to fork: %s\n", strerror(errno));
      fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
      cfg_free(&rblist);
      free(pidfile);
      free(cfgpath);
      closelog();
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
  server(sock, sa, salen);
  syslog(LOG_INFO, "Shutting down listening socket");
  shutdown(sock, SHUT_RDWR);
  close(sock);
  free(cfgpath);
  free(pidfile);
  closelog();
  cfg_free(&rblist);
  return 0;
}


static void
usage (char *pidfile, int status)
{
  printf (_("%s - a policy daemon for Postfix which allows combining different RBLs with weights.\n"), progname);
  printf (_("Usage: %s [OPTION]... <port>\n"), progname);
  printf (_("\
<port> may be either a UNIX socket path, a host:port pair, or a plain\nport name oder -number.\n"));
  printf (_("\
Options:\n\
  -v, --verbose              verbose output\n\
  -d, --debug                show debug output\n\
  -f, --foreground           keep program in foreground\n\
  -m, --maxthreads n         create up to N worker threads (0=disable threads)\n\
  -c FILE, --cfgfile FILE    use config file FILE (current: %s)\n\
  -p FILE, --pidfile FILE    use file FILE to store pid (current: %s)\n\
  -h, --help                 display this help and exit\n\
  -V, --version              output version information and exit\n\
"), cfgpath, pidfile);
  exit (status);
}
