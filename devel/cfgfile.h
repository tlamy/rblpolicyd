
#define DEFAULT_CFGFILE	"/etc/rbl-policyd.conf"

typedef struct _cfgitem {
  char *	    rbldomain;
  short		    weight;
  struct _cfgitem * next;
} cfgitem_t;

#define MAXLINE 1024

cfgitem_t *	cfg_read(char * filename);
void		cfg_free(cfgitem_t *ptr);
void		cfg_dump(cfgitem_t *ptr);

void		dbg(const char *fmt, ...);
