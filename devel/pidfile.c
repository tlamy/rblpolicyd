/* 
   rblpolicyd - a policy daemon for Postfix (http://www.postfix.org/), which allows combining different RBLs with weights.

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
#include <sys/types.h>
#include <sys/stat.h>

#include "globals.h"
#include "pidfile.h"


pid_t pid_get(char *pidfile) {
  FILE *fp;
  int c;
  pid_t pid = 0;

  if ((fp = fopen(pidfile, "r")) == NULL) {
    return -1;
  }
  while ((c = fgetc(fp)) != EOF) {
    if (c >= '0' && c <= '9') {
      pid = 10*pid + (c-'0');
    } else {
      break;
    }
  }
  fclose(fp);
  return pid;
}

pid_t pid_check(char *pidfile) {
  struct stat st;
  pid_t pid;
  if (stat(pidfile, &st) == 0) {
    pid = pid_get(pidfile);
    if (pid <= 0) {
      return 0;
    }
    if (kill(pid, 0) == 0) {
      fprintf(stderr, "Process %d is still running\n", pid);
      return -1;
    }
    if(S_ISREG(st.st_mode)) {
      dbg("Removing stale pidfile %s\n", pidfile);
      unlink(pidfile);
    } else {
      fprintf(stderr, "Stale pidfile %s not removed: Not a regular file\n", pidfile);
      return -1;
    }
  }
  /* no pidfile */
  return 0;
}
  
int pid_write(pid_t pid, const char *pidfile) {
  FILE *fp;
  int res1 = 0, res2 = 0;
  
  if ((fp = fopen(pidfile, "w")) == NULL) {
    return -1;
  }
  res1 = fprintf(fp, "%ld", (long) pid);
  res2 = fclose(fp);
  return (res1 > 0 && res2 == 0) ? 0 : -1;
}
