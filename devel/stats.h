/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/),
   which allows combining different RBLs with weights.

   $Id$
   Public statistics interface

   Copyright (C) 2005 Thomas Lamy

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

#ifndef __STATS_H
#define __STATS_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

extern void stats_worker_thr(int num);
extern void stats_worker_time(struct timeval *start, struct timeval *end);
extern void stats_solver_thr(int num);
extern void stats_solver_time(struct timeval *start, struct timeval *end);
extern void stats_start(void);
extern void stats_log(void);

#endif

