/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/),
   which allows combining different RBLs with weights.

   $Id$
   thread management
   
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
#include <sys/time.h>
#include <time.h>
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

#define RINGBUFFERS	16

typedef struct {
  unsigned int count;
  unsigned int index;
  int           val[RINGBUFFERS];
} ring_t;

static int max_workers = 0;
static int max_solvers = 0;
static int num_workers = 0;
static int num_solvers = 0;
static int now_workers = 0;
static int now_solvers = 0;
static ring_t workertime;
static ring_t solvertime;
static time_t start;
static int requests;

void stats_worker_thr(int num) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);
  num_workers++;
  now_workers=num;
  if (num > max_workers) {
    max_workers = num;
  }
  pthread_mutex_unlock(&mutex);
  return;
}

  
void stats_worker_time(struct timeval *start, struct timeval *end) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  unsigned long long l_start, l_end;
  int duration;
  static char init = 0;

  if (init == 0) {
    pthread_mutex_lock(&mutex);
    memset(&workertime, 0, sizeof(workertime));
    pthread_mutex_unlock(&mutex);
    init = 1;
  }
  l_end   = end->tv_usec/1000   + end->tv_sec;
  l_start = start->tv_usec/1000 + start->tv_sec;
  duration = (l_end - l_start);
  pthread_mutex_lock(&mutex);
  workertime.val[workertime.index] = duration;
  if (workertime.count < RINGBUFFERS) {
    workertime.count++;
  }
  workertime.index = (workertime.index+1) % RINGBUFFERS;
  pthread_mutex_unlock(&mutex);
}
  

void stats_solver_thr(int num) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);
  num_solvers++;
  now_solvers=num;
  if (num > max_solvers) {
    max_solvers = num;
  }
  pthread_mutex_unlock(&mutex);
  return;
}

  
void stats_solver_time(struct timeval *start, struct timeval *end) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  unsigned long long l_start, l_end;
  int duration;
  static char init = 0;

  if (init == 0) {
    pthread_mutex_lock(&mutex);
    memset(&solvertime, 0, sizeof(solvertime));
    pthread_mutex_unlock(&mutex);
    init = 1;
  }
  l_end   = end->tv_usec/1000   + end->tv_sec;
  l_start = start->tv_usec/1000 + start->tv_sec;
  duration = (l_end - l_start);
  pthread_mutex_lock(&mutex);
  solvertime.val[solvertime.index] = duration;
  if (solvertime.count < RINGBUFFERS) {
    solvertime.count++;
  }
  solvertime.index = (solvertime.index+1) % RINGBUFFERS;
  pthread_mutex_unlock(&mutex);
}


void stats_request() {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);
  requests++;
  pthread_mutex_unlock(&mutex);
  return;
}

  
void stats_start() {
  start = time(NULL);
  return;
}


static int ring_average(ring_t * r) {
  int i;
  unsigned long long sum = 0;
  
  if (!r || !r->count) {
    return 0;
  }
  for (i=0; i < r->count; i++) {
    sum += r->val[i];
  }
  return (sum/r->count);
}


char *fmt_secs(unsigned int s) {
  unsigned int m,h,d,w;
  char buf[128];

  w = s/604800;
  s -= w * 604800;
  d = s/86400;
  s -= d*86400;
  h = s/3600;
  s -= h*3600;
  m = s/60;
  s -= m*60;

  snprintf(buf, 127, "%dw%dd%02dh%02dm%02ds", w, d, h, m, s);
  return strdup(buf);
}

void stats_log() {
  unsigned int runtime;
  time_t now = time(NULL);
  char *running;

  runtime = now - start;
  running = fmt_secs(runtime);
  if (!running) {
    syslog(LOG_NOTICE, "Out of memory in stats_log()");
    return;
  }
  syslog(LOG_INFO, "Running for %s; %d requests (%0.1f req/min); %u workers (%d ms avg, %d parallel, %d current); %u solvers (%d ms avg, %d parallel, %d current)",
      running, requests, (float) requests / ((float)runtime/(float)60),
      num_workers, ring_average(&workertime), max_workers, now_workers,
      num_solvers, ring_average(&solvertime), max_solvers, now_solvers);
  free(running);
  return;
}

