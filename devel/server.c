/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/),
   which allows combining different RBLs with weights.

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


#include <stdio.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <signal.h>
#include <sys/wait.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <pthread.h>

#include "system.h"

#include "cfgfile.h"
#include "thrmgr.h"
#include "worker.h"
#include "stats.h"
#include "globals.h"


static void sigterm(int signo) {
  syslog(LOG_INFO, "Received signal %d - shutting down", signo);
  appstate = APP_EXIT;
}


#define CHUNK	1024

static char * read_request(int sock) {
  char *buf = NULL;
  fd_set fds;
  int bufsize = 0;
  int bread = 0;
  struct timeval tv;
  int ret;
  
  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  
  tv.tv_sec = 10;
  tv.tv_usec = 0;

  while(1) {
    ret = select(sock+1, &fds, NULL, NULL, &tv);
    if(ret < 0) {
      syslog(LOG_NOTICE, "select(): %s", strerror(errno));
      if (buf) {
	free (buf);
      }
      return NULL;
    } else if (ret == 0) {
      // syslog(LOG_NOTICE, "Timeout reading request (%d bytes read so far)", bread);
      if (buf && bufsize > bread) {
  	buf[bread] = '\0';
      }
      return buf;
    } else {
      syslog(LOG_INFO, "select() returned %d", ret);
      if(bufsize - bread < CHUNK) {
	bufsize += CHUNK;
	buf = realloc(buf, bufsize);
	if (buf == NULL) {
	  syslog(LOG_ERR,"Could not realloc receive buffer to %d bytes: %s", bufsize, strerror(errno));
	  return NULL;
	}
      }
      ret = read(sock, buf+bread, bufsize-bread-1);
      if (ret <= 0) {
	syslog(LOG_NOTICE, "Could not read from socket: %s", strerror(errno));
	if (buf && bufsize > bread) {
	  buf[bread] = '\0';
	}
	return buf;
      }
      bread += ret;
      buf[bread] = '\0';
      if (strcmp(buf+bread-4, "\r\n\r\n") == 0 || strcmp(buf+bread-2, "\n\n") == 0) {
	/* two line terminators -> end of request */
	break;
      }
    }
  }
  return buf;
}

static int num_requests = 0;
static time_t start = 0;

static void sigstatus(int sig) {
  stats_log();
}

int server(int sock, struct sockaddr *sa, socklen_t salen) {
  int ret;
  int conn;
  cfgitem_t *newlist;
  struct sigaction sa_term, sa_usr1;
  sigset_t sigset;

  /* Setup signal handling */
  memset(&sa_term, 0, sizeof(struct sigaction));
  memset(&sa_usr1, 0, sizeof(struct sigaction));
  sigfillset(&sigset);

  sa_term.sa_handler = sigterm;
  sigemptyset(&sa_term.sa_mask);
  sigaddset(&sa_term.sa_mask, SIGTERM);
  sigaddset(&sa_term.sa_mask, SIGINT);
  sigaction(SIGTERM, &sa_term, NULL);
  sigaction(SIGINT, &sa_term, NULL);
  sigdelset(&sigset, SIGTERM);
  sigdelset(&sigset, SIGINT);

  sa_usr1.sa_handler = sigstatus;
  sigemptyset(&sa_usr1.sa_mask);
  sigaddset(&sa_usr1.sa_mask, SIGUSR1);
  sigaction(SIGUSR1, &sa_usr1, NULL);
  sigdelset(&sigset, SIGUSR1);

  sigprocmask(SIG_SETMASK, &sigset, NULL);

  /* Initialize statistics module */
  stats_start();
  
  /* Accept new network connections */
  start = time(NULL);
  while (appstate != APP_EXIT && appstate != APP_ERROR) {
    socklen_t salen = sizeof(*sa);
    dbg("Waiting for connection");
    ret = accept(sock, sa, &salen);
    if (ret < 0) {
      if (errno != EINTR) {
	syslog(LOG_ERR, "accept() failure: %s", strerror(errno));
      } else {
	dbg("accept(): interrupted; appstate=%d", (int) appstate);
	if (appstate == APP_RUN) {
	  continue;
	}
	break;
      }
      close(sock);
      if (appstate == APP_RUN) {
	appstate=APP_ERROR;
      }
      break;
    }
    switch (appstate) {
    case APP_RUN:
      conn = ret;
      num_requests++;
      dbg("Accepted connection; sock=%d conn=%d", sock, conn);
      if (maxthreads > 0) {
	if ((ret = wthread_create(conn)) != 0) {
	  syslog(LOG_NOTICE, "Could not create new worker thread; closing request: %s", thr_error(ret));
	  shutdown(conn, SHUT_RDWR);
	  close(conn);
	}
      } else {
	worker_th((void *) conn);
      }
      break;

    case APP_RELOAD:
      /* TODO: Testing */
      if((ret = thr_waitcomplete()) != 0) {
	syslog(LOG_NOTICE, "Could not reload configuration; %d workers still alive", ret);
	appstate = APP_RUN;	/* perhaps APP_EXIT? */
	break;
      }
      syslog(LOG_INFO, "Reloading configuration from '%s'", cfgpath);
      newlist = cfg_read(cfgpath);
      if (!newlist) {
	syslog(LOG_INFO, "Error loading configuration from '%s', keeping old config", cfgpath);
      } else {
	cfg_free(&rblist);
	rblist = newlist;
	syslog(LOG_INFO, "Reload ok.");
      }
      appstate = APP_RUN;
      break;

    case APP_EXIT:
    case APP_ERROR:
      break;
    }
  }
  return 0;
}

char * parse_request(char * const req) {
  char *c, *p;
  char *result;
  if ((c = strstr(req, "client_address=")) == NULL) {
    return NULL;
  }
  c += 15;	/* skip to first value byte */
  if((p = strchr(c, '\r')) == NULL) {
    p = strchr(c, '\n');
  }
  if (!p) {
    syslog(LOG_NOTICE, "EOL terminator not found in '%s'", c-15);
    return NULL;
  }
  result = malloc(p-c+1);
  if (!result) {
    syslog(LOG_NOTICE, "Could not allocate %d bytes for client address", p-c+1);
    return NULL;
  }
  memcpy(result, c, p-c);
  result[p-c] = '\0';
  return result;
}

  
 
void process_request(int conn) {
  char *request = NULL;
  char *client = NULL;
  char rqname[1024];
  int score = 0;
  int err = 0;
  char *rdn = NULL;
  cfgitem_t *rbl;
  struct hostent *h;
  char *reply = NULL;
  int replen = 0;
  char result[128];
  int o1=-1,o2=-1,o3=-1,o4=-1;
  
  request = read_request(conn);
  if (!request) {
    syslog(LOG_NOTICE, "Error reading request");
    err++;
  }
  if (!err) {
    client = parse_request(request);
    if (!client) {
      syslog(LOG_NOTICE, "Could not parse request '%s'", request);
      err++;
    }
  }
  if (!err) {
    dbg("Client address: '%s'", client);
    rdn = malloc(strlen(client)+1);
    if (!rdn) {
      err++;
    }
  }
  if (!err) {
    rdn[0] = '\0';
    if(sscanf(client, "%d.%d.%d.%d", &o1, &o2, &o3, &o4) != 4) {
      syslog(LOG_NOTICE, "Invalid client address '%s'", client);
      err++;
    }
  }
  if (!err) {
    sprintf(rdn, "%d.%d.%d.%d", o4, o3, o2, o1);
    dbg("Reverse client address: '%s'", rdn);
    for (rbl = rblist; rbl && score < 100; rbl = rbl->next) {
      sprintf(rqname, "%s.%s", rdn, rbl->rbldomain);
      dbg("Lookup '%s'", rqname);
      h = gethostbyname(rqname);
      if (h) {
	if (h->h_addrtype == AF_INET) {
	  char **addrlist = h->h_addr_list;
	  for (; *addrlist != NULL; addrlist++) {
	    inet_ntop(AF_INET, *addrlist, result, 127);
	    dbg("'%s' = %s", rqname, result);
	    if (strncmp(result, "127.", 4) == 0) {
	      dbg("%s found in %s", client, rbl->rbldomain);
	      if (reply) {
		replen += 2+strlen(rbl->rbldomain);
		reply = realloc(reply, replen);
		strcat(reply, ", ");
	      } else {
		replen += strlen(rbl->rbldomain)+1;
		reply = malloc(replen);
		reply[0] = '\0';
	      }
	      strcat(reply, rbl->rbldomain);
	      score += rbl->weight;
	      break;
	    } /* endif (127.0.0.x) */
	  } /* end foreach(host) */
	} /* endif AF_INET */
      } /* endif h */
      if (score >= 100) {
	break;
      }
    } /* end foreach(rbl) */
  } /* endif(!err) */
  if (!err && score >= 100) {
    dbg("Reply: 'action=REJECT Blocked through %s\\r\\n\\r\\n'", reply);
    write(conn, "action=REJECT Blocked through ", 30);
    write(conn, reply, strlen(reply));
    // write(conn, "\r\n\r\n", 4);
    write(conn, "\n\n", 2);
  } else {
    dbg("Reply: 'action=DUNNO\\n\\n'");
    write(conn, "action=DUNNO", 12);
    // write(conn, "\r\n\r\n", 4);
    write(conn, "\n\n", 2);
  }
  if (request) {
    free(request);
  }
  if (client) {
    free(client);
  }
  if (rdn) {
    free(rdn);
  }
  if (reply) {
    free(reply);
  }
  close(conn);
}

