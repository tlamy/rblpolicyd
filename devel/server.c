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

#define __MAIN__
#include "globals.h"

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
#include "system.h"

#include "cfgfile.h"

static int server(cfgitem_t *rblist, int sock, struct sockaddr *sa, socklen_t salen);
static void process_request(cfgitem_t *list, int conn);


int policyserver(cfgitem_t *list, char *port) {
  int sock;
  struct sockaddr *sa = NULL;
  struct sockaddr_in  saip;
  struct sockaddr_un  saun;
  socklen_t salen;
  char *c;
  int res;

  if ((res = res_init()) != 0) {
    syslog(LOG_ERR, "Fatal: Cannot initialize resolver library");
    return -1;
  }

  if (port[0] == '/') {
    dbg("'%s' looks like UNIX socket...\n", port);
    sock = socket(PF_UNIX, SOCK_STREAM, 0);
    if (!sock) {
      syslog(LOG_ERR, "Could not create UNIX socket %s: %s", port, strerror(errno));
      return -1;
    }
    memset(&saun, 0, sizeof(struct sockaddr_un));
    saun.sun_family = AF_UNIX;
    strcpy(saun.sun_path, port);
    sa = (struct sockaddr *) &saun;
    salen = sizeof(struct sockaddr_un);
    dbg("Binding UNIX socket...(sa=0x%08x salen=%d)\n", &saun, salen);
    if (bind(sock, (struct sockaddr *) &saun, salen) != 0) {
      syslog(LOG_ERR, "Could not bind to UNIX socket %s: %s", port, strerror(errno));
      close(sock);
      return -1;
    }
  } else if ((c = strchr(port, '/')) != NULL) {
    /* host/port pair */
    char *hostname = NULL;
    char *portname = NULL;
    struct addrinfo hint;
    struct addrinfo *addr;
    struct addrinfo *aip;
    struct hostent *he;
    struct servent *se;
    
    dbg("'%s' looks like INET host/service pair...\n", port);
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (!sock) {
      syslog(LOG_ERR, "Could not create UNIX socket %s: %s", port, strerror(errno));
      return -1;
    }

    hostname = malloc(c-port+1);
    memcpy(hostname, port, c-port);
    hostname[c-port] = '\0';
    portname = strdup(c+1);
    
    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    if ((res = getaddrinfo(hostname, portname, &hint, &addr)) != 0) {
      syslog(LOG_ERR, "Can not resolve %s/%s: %s", hostname, portname, gai_strerror(res));
      free(hostname);
      free(portname);
      close(sock);
      return -1;
    }
#if 0
    for (aip = addr, res=0; aip; aip = aip->ai_next, res++) {
      char res_host[128], res_port[128];
      getnameinfo(aip->ai_addr, aip->ai_addrlen, res_host, 128, res_port, 128, NI_NUMERICSERV);
      printf("%d: family=%d  type=%d  proto=%d  host='%s'  port='%s'  name='%s'\n", res, aip->ai_family, aip->ai_socktype, aip->ai_protocol, res_host, res_port, inet_ntoa(((struct sockaddr_in *) (aip->ai_addr))->sin_addr));
    }
#endif
    memcpy(&saip, addr->ai_addr, addr->ai_addrlen);
    sa = (struct sockaddr *) &saip;
    salen = sizeof(saip);

    freeaddrinfo(addr);
    dbg("Binding INET socket...(sa=0x%08x salen=%d)\n", &saip, salen);
    if (bind(sock, (struct sockaddr *) &saip, salen) != 0) {
      syslog(LOG_ERR, "Could not bind to INET socket %s: %s", port, strerror(errno));
      close(sock);
      return -1;
    }
  } else {
    struct addrinfo hint;
    struct addrinfo *addr;
    struct addrinfo *aip;
    struct hostent *he;
    struct servent *se;

    dbg("'%s' looks like INET service...\n", port);
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (!sock) {
      syslog(LOG_ERR, "Could not create UNIX socket %s: %s", port, strerror(errno));
      return -1;
    }
    memset(&hint, 0, sizeof(hint));
    hint.ai_socktype = SOCK_STREAM;
    if ((res = getaddrinfo(NULL, port,  &hint, &addr)) != 0) {
      syslog(LOG_ERR, "Can not resolve service %s: %s", port, gai_strerror(res));
      close(sock);
      return -1;
    }
#if 0
    dbg("Lookup returned %d (addr=0x%08x)\n", port, addr);
    for (aip = addr, res=0; aip; aip = aip->ai_next, res++) {
      char res_host[128], res_port[128];
      if((res = getnameinfo(aip->ai_addr, aip->ai_addrlen, res_host, 128, res_port, 128, NI_NUMERICSERV)) == 0) {
	printf("%d: family=%d  type=%d  proto=%d  host='%s'  port='%s'  name='%s'\n", res, aip->ai_family, aip->ai_socktype, aip->ai_protocol, res_host, res_port, inet_ntoa(((struct sockaddr_in *) (aip->ai_addr))->sin_addr));
      } else {
	fprintf(stderr, "getnameinfo failed: %s (syserr: %s)\n", gai_strerror(res), strerror(errno));
      }
    }
#endif
    memcpy(&saip, addr->ai_addr, addr->ai_addrlen);
    freeaddrinfo(addr);
    sa = (struct sockaddr *) &saip;
    salen = sizeof(struct sockaddr_in);
    dbg("Binding INET socket...(sa=0x%08x salen=%d)\n", &saip, salen);
    if (bind(sock, (struct sockaddr *) &saip, salen) != 0) {
      syslog(LOG_ERR, "Could not bind to INET socket %s: %s", port, strerror(errno));
      close(sock);
      return -1;
    }
  }
  if (listen(sock, 255) != 0) {
    syslog(LOG_ERR, "Could not listen on UNIX socket %s: %s", port, strerror(errno));
    close(sock);
    return -1;
  }

  server(list, sock, sa, salen);
  syslog(LOG_INFO, "Shutting down listening socket");
  shutdown(sock, SHUT_RDWR);
  close(sock);
  return 0;
}

static int find_slot(pid_t *table, int max) {
  int i;
  for (i=0; i < max; i++) {
    if (table[i] == 0) {
      return i;
    }
  }
  return -1;
}

static int numchilds = 0;
static pid_t *children = NULL;

static int find_child(pid_t pid, pid_t* childs, int max) {
  int i;
  for (i=0; i < max; i++) {
    if (childs[i] == pid) {
      return i;
    }
  }
  return -1;
}

static void sigchld(int signo) {
  pid_t pid;
  int status;
  int slot;

  while ((pid = waitpid(WAIT_ANY, &status, WNOHANG)) > 0) {
    dbg("Child %lu terminated.", (unsigned long) pid);
    slot = find_child(pid, children, maxchilds);
    if (slot < 0) {
      syslog(LOG_NOTICE, "Received SIGCHLD for unknown pid %lu !!!", (unsigned long) pid);
    } else {
      children[slot] = 0;
      numchilds--;
    }
  }
  if (pid < 0 && errno != ECHILD) {
    syslog(LOG_ERR, "waitpid(): %s", strerror(errno));
  }
  signal (SIGCHLD, sigchld);
}

static void sigterm(int signo) {
  int i;
  syslog(LOG_INFO, "Received SIGTERM - reaping children");
  terminate = 1;

  for (i=0; i < maxchilds; i++) {
    if (children[i] > 0) {
      syslog(LOG_INFO,"Killing child #%d with pid %lu", i, (unsigned long) children[i]);
      kill(children[i], SIGTERM);
    }
  }
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
static int usedchilds = 0;

static void sigstatus(int sig) {
  unsigned long run = time(NULL)-start;
  if (run == 0) run = 1;
  
  syslog(LOG_INFO, "Running %lu seconds; %d requests (%.1f req/s); %d max used child slots",
      run, num_requests, ((float) num_requests / (float) run), usedchilds);
  signal(SIGUSR1, sigstatus);
}

static int server(cfgitem_t *list, int sock, struct sockaddr *sa, socklen_t salen) {
  int ret;
  pid_t pid;
  char *request = NULL;
  char *client  = NULL;
  int conn;
  int child;

  if(signal (SIGCHLD, sigchld) == SIG_ERR) {
    syslog(LOG_NOTICE, "Could not set signal handler for child control; not using children");
    maxchilds=0;
  }
  if(signal (SIGUSR1, sigstatus) == SIG_ERR) {
    syslog(LOG_NOTICE, "Could not set signal handler for status");
  }
  if(signal (SIGTERM, sigterm) == SIG_ERR) {
    syslog(LOG_NOTICE, "Could not set signal handler for termination");
  }

  if (maxchilds > 0) {
    if ((children = calloc(maxchilds+1, sizeof(pid_t))) == NULL) {
      syslog(LOG_ERR, "Could not allocate for %d children: %s", maxchilds, strerror(errno));
      return -1;
    }
    memset(children, 0, (maxchilds+1)*sizeof(pid_t));
  }
  start = time(NULL);
  while (!terminate) {
    socklen_t salen = sizeof(*sa);
    dbg("Waiting for connection");
    ret = accept(sock, sa, &salen);
    if (!terminate && ret < 0) {
      syslog(LOG_ERR, "accept() failure: %s", strerror(errno));
      close(sock);
      terminate=-1;
      break;
    }
    if (!terminate) {
      conn = ret;
      num_requests++;
      dbg("Accepted connection; sock=%d conn=%d", sock, conn);
      if (maxchilds > 0) {
	while ((child = find_slot(children, maxchilds)) < 0) {
	  syslog(LOG_NOTICE, "%d/%d children are busy; waiting", numchilds, maxchilds);
	  sleep(1);
	}
	dbg("Found child slot #%d", child);
	pid = fork();
	if (pid < 0) {
	  syslog(LOG_ERR, "Could not fork(): %s", strerror(errno));
	  return -1;
	}
	if (pid) {
	  close(conn);
	  children[child] = pid;	/* store child pid */
	  dbg("Child #%d has pid %lu", ++numchilds, (unsigned long) pid);
	  if (numchilds > usedchilds) {
	    usedchilds = numchilds;
	  }
	} else {
	  /* child */
	  close(sock);
	  process_request(list, conn);
	  exit(0);
	}
      } else {
	process_request(list, conn);
      }
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

  
 
static void process_request(cfgitem_t *list, int conn) {
  char *request = NULL;
  char *client = NULL;
  char rqname[1024];
  int score = 0;
  int err = 0;
  char *rdn = NULL, *c, *r;
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
    for (rbl = list; rbl && score < 100; rbl = rbl->next) {
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

