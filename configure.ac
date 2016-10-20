dnl Process this file with autoconf to produce a configure script.

AC_INIT(rblpolicyd.c)
dnl Every other copy of the package version number gets its value from here
AM_INIT_AUTOMAKE(rblpolicyd, 0.2.0)

dnl create a config.h file (Automake will add -DHAVE_CONFIG_H)
AM_CONFIG_HEADER(config.h)

AC_SUBST(VERSION)

ISODATE=`date +%Y-%m-%d`
AC_SUBST(ISODATE)

AC_CANONICAL_HOST

dnl Checks for programs.
AC_PROG_INSTALL
AC_PROG_CC

dnl Checks for libraries.
resolv_lib=""
AC_CHECK_LIB(resolv, __dn_expand, resolv_lib="-lresolv",)
if test -z "$resolv_lib"; then
  AC_CHECK_LIB(resolv, dn_expand, resolv_lib="-lresolv",)
fi
AC_CHECK_HEADER(resolv.h,[LIBS="$LIBS $resolv_lib"; AC_DEFINE(HAVE_RESOLV_H,1,have resolv.h)], AC_MSG_ERROR([No (usable) resolver library found]))

pthread_lib=""
have_pthreads_h=no
AC_CHECK_LIB(pthread, pthread_create, pthread_lib="-lpthread",)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER(pthread.h,[have_pthreads_h=yes],)
if test "$pthread_lib" = "" -o "$have_pthreads_h" = "no"; then
  AC_MSG_ERROR([pthreasd are not present on your system])
fi
AC_CHECK_HEADERS(sys/param.h sys/time.h time.h fcntl.h limits.h stdarg.h ctype.h)


AC_HEADER_MAJOR

dnl Checks for typedefs, structures, and compiler characteristics.

dnl Checks for library functions.
AC_CHECK_FUNCS(snprintf vsnprintf)

LIBS="$LIBS $pthread_lib"
AC_SUBST(LIBS)
AC_OUTPUT(Makefile rblpolicyd.lsm rblpolicyd.spec)
