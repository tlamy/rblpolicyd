bin_PROGRAMS=rblpolicyd
rblpolicyd_SOURCES=rblpolicyd.c rblpolicyd.1 pidfile.c pidfile.h cfgfile.c cfgfile.h xmalloc.c system.h aclocal.m4 getopt.c getopt1.c getopt.h globals.h server.c snprintf.h snprintf.c thrmgr.h thrmgr.c worker.h worker.c stats.h stats.c

#  uncomment the following if rblpolicyd requires the math library
#rblpolicyd_LDADD=-lm

EXTRA_DIST=rblpolicyd.lsm.in rblpolicyd.spec.in rblpolicyd.texinfo

#  if you write a self-test script named `chk', uncomment the
#  following and add `chk' to the EXTRA_DIST list
#TESTS=chk

#  build and install the .info pages
info_TEXINFOS = rblpolicyd.texinfo
rblpolicyd_TEXINFOS = gpl.texinfo

#  install the man pages
man_MANS=rblpolicyd.1
