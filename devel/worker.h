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

#ifndef __WORKER_H
#define __WORKER_H

#if HAVE_CONFIG_H
#include "config.h"
#endif

extern void * worker_th(void *);
extern void * solver_th(void *);

#endif

