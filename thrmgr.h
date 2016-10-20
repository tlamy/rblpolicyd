/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/),
   which allows combining different RBLs with weights.

   $Id$
   Public threadmanager interface

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

#ifndef __THRMGR_H
#define __THRMGR_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

typedef enum {
    ERR_NONE = 0,
    ERR_THR_TOOMANY,
    ERR_THR_SYSERR,
    ERR_THR_APPSTATE
} thmgr_err;


/* Thread info */
typedef struct thrd {
    pthread_t tid;
    struct timeval start;
    struct thrd *next;
    struct thrd *last;
} thrd_t, *tlist_t;

/* Thread manager data */
typedef struct thrmgr {
    pthread_mutex_t lock;
    char name[32];
    unsigned int nthreads;
    tlist_t tlist;
} thrmgr_t;


int thr_register(thrmgr_t *mgr, pthread_t tid);

int thr_unregister(thrmgr_t *mgr, pthread_t tid);

int thr_waitcomplete(void);

int wthread_create(int conn);

void wthread_exit(pthread_t tid);

int rthread_create(void *data);

void rthread_exit(pthread_t tid);

const char *thr_error(thmgr_err err);

#endif

