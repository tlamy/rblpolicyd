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


static thrmgr_t *workermgr = NULL;
static thrmgr_t *solvermgr = NULL;

static pthread_mutex_t worker_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t solver_init_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * A new thread has been created, update manager struct.
 * NOTE: the manager struct has to be protected by the caller!
 */
int thr_register(thrmgr_t *mgr, pthread_t tid) {
  tlist_t nthread = NULL;
  char errbuf[1024];
  int nthr;

  mgr->nthreads++;
  if ((nthread = malloc(sizeof(thrd_t))) == NULL) {
    strerror_r(errno, errbuf, 1024);
    syslog(LOG_ERR, "Failed to allocate thread info: %s", errbuf);
    return -1;
  }
  memset(nthread, 0, sizeof(thrd_t));
  gettimeofday(&nthread->start, NULL);
  nthread->tid = tid;
  nthread->next = mgr->tlist;
  if (nthread->next) {
    nthread->next->last = nthread;
  }
  nthread->last = NULL;
  mgr->tlist = nthread;
  for (nthr=0,nthread=mgr->tlist; nthread; nthread = nthread->next) {
    if (nthr && !nthread->last) {
      syslog(LOG_ERR, "LL %s corrupt at index %d (last is NULL)", mgr->name, nthr);
    }
    nthr++;
  }
  dbg("Register %s %lu (n=%d, ll=%d)\n", mgr->name, (unsigned long) tid, mgr->nthreads, nthr);
  return 0;
}

/*
 * A thread is about to cancel, remove it from the manager struct.
 */
int thr_unregister(thrmgr_t *mgr, pthread_t tid) {
  tlist_t thr;
  int nthr = 0;
  
  pthread_mutex_lock(&mgr->lock);
  for (thr = mgr->tlist; thr; thr = thr->next) {
    nthr++;
    if (pthread_equal(thr->tid, tid)) {
      if (thr->last) {
	thr->last->next = thr->next;
      } else {
	mgr->tlist = thr->next;
      }
      if(thr->next) {
	thr->next->last = thr->last;
      }
      mgr->nthreads--;
      free(thr);
      pthread_mutex_unlock(&mgr->lock);
      for (nthr=0,thr=mgr->tlist; thr; thr = thr->next) {
	nthr++;
      }
      dbg(LOG_INFO,"Unregistered %s %lu (n=%d, ll=%d)\n", mgr->name, (unsigned long) tid, mgr->nthreads, nthr);
      return 0;
    }
  }
  syslog(LOG_NOTICE, "Could not unregister %s %lu: Thread not found (%d searched)", mgr->name, tid, nthr); 
  pthread_mutex_unlock(&mgr->lock);
  return -1;
}


/*
 * Wait for all worker threads to finish in order to reload the config file.
 * Returns number of threads still alive after some time
 */
int thr_waitcomplete(void) {
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 250000;	/* 250 ms */
  int err;

  if (!workermgr) {
    syslog(LOG_NOTICE, "thr_waitcomplete() called with uninitialized workermgr");
    exit(255);
  }
  dbg("Waiting for worker threads to finish");
  /* Wait 10 seconds for all worker threads to disappear */
  for(err=400; err > 0 && workermgr->nthreads && appstate != APP_EXIT; --err) {
    dbg("Still %d worker threads busy", workermgr->nthreads);
    nanosleep(&ts, NULL);
  }
  return err;
}


/*
 * Create a new worker thread for connection conn
 */
int wthread_create(int conn) {
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 100000;	/* 100 ms */
  pthread_t tid;
  int err;
  pthread_attr_t attr;
  char errbuf[1024];

  pthread_mutex_lock(&worker_init_mutex);
  if (!workermgr) {
    workermgr = malloc(sizeof(thrmgr_t));
    pthread_mutex_init(&workermgr->lock, NULL);
    memset(workermgr, 0, sizeof(thrmgr_t));
    strcpy(workermgr->name,"worker");
    workermgr->nthreads = 0;
  }
  pthread_mutex_unlock(&worker_init_mutex);

  pthread_mutex_lock(&workermgr->lock);
  /* Try 20 times (2 seconds) until a thread becomes available */
  for(err=20; err > 0 && workermgr->nthreads >= maxthreads && appstate == APP_RUN; --err) {
    nanosleep(&ts, NULL);
  }
  if (appstate != APP_RUN) {
    /* Daemon is in shutdown or reload mode, do not allow new threads */
    pthread_mutex_unlock(&workermgr->lock);
    return -ERR_THR_APPSTATE;
  }
  if (err <= 0) {
    /* Timeout waiting for free thread */
    pthread_mutex_unlock(&workermgr->lock);
    return -ERR_THR_TOOMANY;
  }
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if ((err = pthread_create (&tid, &attr, worker_th, (void *) conn)) != 0) {
    strerror_r(errno,errbuf,1024);
    syslog(LOG_NOTICE, "Failed to create new thread: %s", errbuf);
    pthread_mutex_unlock(&workermgr->lock);
    return -ERR_THR_SYSERR;
  }
  if(thr_register(workermgr, tid) != 0) {
    pthread_cancel(tid);
    close(conn);
    pthread_mutex_unlock(&workermgr->lock);
    return -ERR_THR_SYSERR;
  }
  pthread_mutex_unlock(&workermgr->lock);
  stats_worker_thr(workermgr->nthreads);
  return 0;
}


void wthread_exit(pthread_t tid) {
  if (!workermgr) {
    syslog(LOG_NOTICE, "wthread_exit() called with uninitialized workermgr");
    exit(255);
  }
  thr_unregister(workermgr, tid);
}


/*
 * Create a new resolver thread
 */
int rthread_create(void *data) {
  pthread_t tid;
  int err;
  pthread_attr_t attr;
  char errbuf[1024];

  pthread_mutex_lock(&solver_init_mutex);
  if (!solvermgr) {
    solvermgr = malloc(sizeof(thrmgr_t));
    pthread_mutex_init(&solvermgr->lock, NULL);
    memset(solvermgr, 0, sizeof(thrmgr_t));
    strcpy(solvermgr->name,"solver");
    solvermgr->nthreads = 0;
  }
  pthread_mutex_unlock(&solver_init_mutex);

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_mutex_lock(&solvermgr->lock);
  if ((err = pthread_create (&tid, &attr, solver_th, data)) != 0) {
    strerror_r(errno,errbuf,1024);
    syslog(LOG_NOTICE, "Failed to create solver thread: %s", errbuf);
    pthread_mutex_unlock(&solvermgr->lock);
    return -ERR_THR_SYSERR;
  }
  if(thr_register(solvermgr, tid) != 0) {
    pthread_cancel(tid);
    pthread_mutex_unlock(&solvermgr->lock);
    return -ERR_THR_SYSERR;
  }
  stats_solver_thr(solvermgr->nthreads);
  pthread_mutex_unlock(&solvermgr->lock);
  return 0;
}


void rthread_exit(pthread_t tid) {
  thr_unregister(solvermgr, tid);
}


const char *thr_error(thmgr_err err) {
  switch(err) {
    case ERR_NONE:
      return "No error";
      break;
    case ERR_THR_TOOMANY:
      return "Too many threads";
      break;
    case ERR_THR_SYSERR:
      return "Fatal OS error";
      break;
    case ERR_THR_APPSTATE:
      return "Daemon is shutting down or reloading";
      break;
  }
  return "";
}

