/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/),
   which allows combining different RBLs with weights.

   $Id$
   RBL lookups and thread management
   
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
#include <time.h>
#include <sys/time.h>
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
#include "xmalloc.h"

static char * read_request(int sock);
static char * parse_request(char * const req);


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

static char * parse_request(char * const req) {
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

typedef struct {
  int *			resolvers;
  pthread_mutex_t *	resolvers_;	/* Mutex for resolver count */
  pthread_cond_t *	res_ready;	/* All resolvers done condition */
  char *		hostname;	/* Hostname to resolve */
  char *		client;		/* Client addr to look up */
  cfgitem_t *		rblitem;	/* Fast lookup to RBL for statistics, used read-only by resolver */
  unsigned int		time;		/* Time needed for resolving */
  int			score;		/* resulting score */
} resdata_t;
  
 
void * worker_th(void *data) {
  char *request = NULL;
  char *client = NULL;
  char rqname[1024];
  int score = 0;
  int err = 0;
  char *rdn = NULL;
  cfgitem_t *rbl;
  char *reply = NULL;
  int replen = 0;
  int o1=-1,o2=-1,o3=-1,o4=-1;
  int conn = (int) data;
  int resolvers = 0;
  resdata_t **resdata = NULL;
  int resdata_cnt = 0;
  int i;
  struct timeval begin, end;

  pthread_mutex_t resolvers_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t res_ready_cond = PTHREAD_COND_INITIALIZER;
  
  gettimeofday(&begin, NULL);
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
    pthread_cond_init(&res_ready_cond, NULL);
    for (rbl = rblist; rbl && score < 100; rbl = rbl->next) {
      sprintf(rqname, "%s.%s", rdn, rbl->rbldomain);
      pthread_mutex_lock(&resolvers_);
      resdata = xrealloc(resdata, (resdata_cnt+2)*sizeof(resdata_t *));
      resdata[resdata_cnt+1] = NULL;
      resdata[resdata_cnt] = xmalloc(sizeof(resdata_t));
      memset(resdata[resdata_cnt], 0, sizeof(resdata_t));
      resdata[resdata_cnt]->resolvers  = &resolvers;
      resdata[resdata_cnt]->resolvers_ = &resolvers_;
      resdata[resdata_cnt]->res_ready  = &res_ready_cond;
      resdata[resdata_cnt]->hostname   = xstrdup(rqname);
      resdata[resdata_cnt]->client     = client;
      resdata[resdata_cnt]->rblitem    = rbl;
      resdata[resdata_cnt]->time       = 0;
      resdata[resdata_cnt]->score      = 0;
      resolvers++;
      rthread_create(resdata[resdata_cnt]);
      pthread_mutex_unlock(&resolvers_);
      resdata_cnt++;
    }
    /* Wait for the resolver threads to finish.
     * Each resolver signals us it's readiness through pthread_cond_signal() */
    pthread_mutex_lock(&resolvers_);
    while (resolvers) {
      syslog(LOG_INFO, "Worker waiting for %d/%d resolvers", resolvers, resdata_cnt);
      pthread_cond_wait(&res_ready_cond, &resolvers_);
    }
    pthread_cond_destroy(&res_ready_cond);
    syslog(LOG_INFO, "Worker: All resolvers ready");
    
    score=0;
    for (i=0; i < resdata_cnt; i++) {
      /* TODO: add timing stats for each rbl */
      pthread_mutex_lock(&rblist_mutex);
      resdata[i]->rblitem->questions++;
      if (resdata[i]->score) {
	resdata[i]->rblitem->positive++;
	score += resdata[i]->score;
	if (reply) {
	  replen += 2+strlen(resdata[i]->rblitem->rbldomain);
	  reply = realloc(reply, replen);
	  strcat(reply, ", ");
	} else {
	  replen += strlen(resdata[i]->rblitem->rbldomain)+1;
	  reply = malloc(replen);
	  reply[0] = '\0';
	}
	strcat(reply, resdata[i]->rblitem->rbldomain);
      }
      pthread_mutex_unlock(&rblist_mutex);
      if(resdata[i]->hostname) {
	free(resdata[i]->hostname);
      }
      free(resdata[i]);
    }
    free(resdata);
    resdata = NULL;
  } /* endif(!err) */
  if (!err && score >= 100) {
    dbg("Reply: 'action=REJECT Blocked through %s'", reply);
    write(conn, "action=REJECT Blocked through ", 30);
    write(conn, reply, strlen(reply));
    // write(conn, "\r\n\r\n", 4);
    write(conn, "\n\n", 2);
  } else {
    dbg("Reply: 'action=DUNNO'");
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
  gettimeofday(&end, NULL);
  stats_worker_time(&begin, &end);
  wthread_exit(pthread_self());
  return NULL;
}


void * solver_th(void *data) {
  resdata_t *r = data;
  struct hostent hostbuf, *hp;
  size_t hstbuflen;
  char *tmphstbuf;
  int res;
  int herr;
  char result[128];
  struct timeval begin, end;
  struct timeval res_begin, res_end;

  gettimeofday(&begin, NULL);
  syslog(LOG_INFO, "(%ld) Lookup '%s'", pthread_self(), r->hostname);

  hstbuflen = 1024;
  /* Allocate buffer, remember to free it to avoid memory leakage.  */
  tmphstbuf = malloc (hstbuflen);

  gettimeofday(&res_begin, NULL);
  while ((res = gethostbyname_r (r->hostname, &hostbuf, tmphstbuf, hstbuflen, &hp, &herr)) == ERANGE) {
    /* Enlarge the buffer.  */
    hstbuflen *= 2;
    tmphstbuf = realloc (tmphstbuf, hstbuflen);
  }
  gettimeofday(&res_end, NULL);
  if (hp) {
    if (hp->h_addrtype == AF_INET) {
      char **addrlist = hp->h_addr_list;
      for (; *addrlist != NULL; addrlist++) {
	inet_ntop(AF_INET, *addrlist, result, 127);
	dbg("'%s' = %s", r->hostname, result);
	if (strncmp(result, "127.", 4) == 0) {
	  dbg("%s found in %s", r->client, r->rblitem->rbldomain);
	  r->score = r->rblitem->weight;
	  break;
	} /* endif (127.0.0.x) */
      } /* end foreach(host) */
    } /* endif AF_INET */
  } /* endif h */

  free(tmphstbuf);
  /* Done, signal the worker thread */
  pthread_mutex_lock(r->resolvers_);
  *r->resolvers = *r->resolvers-1;
  syslog(LOG_INFO, "(%ld) Resolver for '%s' done, %d resolvers left", pthread_self(), r->hostname, *r->resolvers);
  pthread_cond_broadcast(r->res_ready);
  pthread_mutex_unlock(r->resolvers_);
  gettimeofday(&end, NULL);
  stats_solver_time(&begin, &end);
  rthread_exit(pthread_self());
  return NULL;
}

