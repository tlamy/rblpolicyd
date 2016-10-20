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

#ifndef __CFGFILE_H
#define __CFGFILE_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

#define DEFAULT_CFGFILE    "/etc/rbl-policyd.conf"

#define NUM_RTT    16

typedef struct _cfgitem {
    char *rbldomain;        /* RBL Domain */
    short weight;        /* Weight/Score */
    unsigned int questions;        /* How often asked */
    unsigned int positive;        /* positive answers */
    unsigned int rtt[NUM_RTT];    /* last 16 answer times */
    unsigned char rttindex;        /* Current position in ring buffer */
    struct _cfgitem *next;
} cfgitem_t;

#define MAXLINE 1024

cfgitem_t *cfg_read(char *filename);

void cfg_free(cfgitem_t **ptr);

void cfg_dump(cfgitem_t *ptr);

void dbg(const char *fmt, ...);

#endif

