/* xmalloc.h -- malloc with out of memory checking
   Copyright (C) 1990, 91, 92, 93, 94, 95, 96, 99 Free Software Foundation, Inc.

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
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if __STDC__
# define VOID void
#else
# define VOID char
#endif

#include <sys/types.h>

#if STDC_HEADERS
# include <stdlib.h>
#else
VOID *calloc ();
VOID *malloc ();
VOID *realloc ();
void free ();
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_STRINGS_H
#include <strings.h>
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# define textdomain(Domain)
# define _(Text) Text
#endif


/* Prototypes for functions defined here.  */
#if defined (__STDC__) && __STDC__
VOID *xmalloc (size_t n);
VOID *xcalloc (size_t n, size_t s);
VOID *xrealloc (VOID *p, size_t n);
char *xstrdup (char *p);
#endif


