#ifndef __MAIN__
#define __EXTERN__ extern
#else
#define __EXTERN__ /* */
#endif

typedef enum {
  APP_RUN = 0,
  APP_RELOAD,
  APP_EXIT,
  APP_ERROR
} appstate_t;

__EXTERN__	char *      progname;
__EXTERN__	char *      cfgpath;
__EXTERN__	char        verbose;
__EXTERN__	char        debug;
__EXTERN__	char        foreground;
__EXTERN__	appstate_t  appstate;
__EXTERN__	int         maxthreads;
__EXTERN__	cfgitem_t * rblist;

__EXTERN__	pthread_mutex_t rblist_mutex;

