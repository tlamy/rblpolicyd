#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#if HAVE_STDARG_H
#include <stdarg.h>
#endif
#if HAVE_LIMITS_H
#include <limits.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_STRINGS_H
#include <strings.h>
#endif

#include "globals.h"
#include "cfgfile.h"


void dbg(const char *fmt, ...) {
  va_list args;
  char buf[4096];

  if (debug) {
    va_start(args, fmt);
    vsnprintf(buf, 4095, fmt, args);
    buf[4095] = '\0';
    syslog(LOG_INFO, buf);

    va_end(args);
  }
}

/** void cfg_cleanline(char *line)
 * Clean up an input line by removing all whitespaces and
 * comments from end of line (like rtrim)
 */
static void cfg_cleanline(char *line) {
  char *x = line;
  while (*x && *x != '#') {
    x++;
  }
  *x = '\0';
  while (x >= line && (*x == '\0' || isspace(*x))) {
    *x-- = '\0';
  }
}


cfgitem_t * cfg_read(char * filename) {
  FILE *c;
  char buf[MAXLINE];
  cfgitem_t *first=NULL, *current=NULL, *item=NULL, *last = NULL;
  char temp[MAXLINE];
  long weight;
  char *x, *y;
  enum {
    ST_START,
    ST_EOW,
    ST_REM
  };
  char state = ST_START;
  int lineno = 0;

  if ((c = fopen(filename, "r")) == NULL) {
    syslog(LOG_ERR, "Can not open %s: %s\n", filename, strerror(errno));
    fprintf(stderr, "%s: Can not open %s: %s\n", progname, filename, strerror(errno));
    return NULL;
  }
  while(fgets(buf, MAXLINE-1, c)) {
    lineno++;
    cfg_cleanline(buf);
    if (*buf == '\0') {
      /* empty line */
      continue;
    }
    // dbg("Line %2d: '%s'\n", lineno, buf);
    state = ST_START;
    for (x=buf, y=temp; *x && state == ST_START; x++) {
      if (isspace(*x)) {
	if (y == temp) {
	  continue;	/* skip whitespace at beginning */
	} else {
	  *y = '\0';
	  state = ST_EOW;
	  break;
	}
      } else {
	*y++ = *x;
      }
    }
    if (!*x) {
      syslog(LOG_ERR, "%s line %d: Premature end of line\n", filename, lineno);
      fprintf(stderr, "%s: %s line %d: Premature end of line\n", progname, filename, lineno);
      goto err_cleanup;
    }
    // dbg("> domain='%s'\n", temp);
    
    if ((current = calloc(1, sizeof(cfgitem_t))) == NULL) {
      syslog(LOG_ERR, "Out of memory at %s:%d allocating %d bytes\n", __FILE__, __LINE__, sizeof(cfgitem_t));
      fprintf(stderr, "%s: Out of memory at %s:%d allocating %d bytes\n", progname, __FILE__, __LINE__, sizeof(cfgitem_t));
      goto err_cleanup;
    }
    current->rbldomain = strdup(temp);
    while(*x && isspace(*x)) {
      x++;
    }
    // dbg("> weightstr='%s'\n", x);
    weight = strtol(x, &y, 0);
    if (*y != '\0') {
      syslog(LOG_ERR, "%s line %d: argument '%s' is not numeric\n", filename, lineno, x);
      fprintf(stderr, "%s: %s line %d: argument '%s' is not numeric\n", progname, filename, lineno, x);
      goto err_cleanup;
    }
    if (weight <= 0 || weight > SHRT_MAX) {
      syslog(LOG_ERR, "%s line %d: argument '%s' is outside allowed range\n", filename, lineno, x);
      fprintf(stderr, "%s: %s line %d: argument '%s' is outside allowed range\n", progname, filename, lineno, x);
      goto err_cleanup;
    }
    current->weight = (short) weight;

    last = NULL;
    for(item = first; item && item->weight > current->weight; item = item->next) {
      last = item;
    }
    if (last) {
      last->next = current;
    }
    current->next = item;
    if (!first) {
      first = current;
    }
  }
  fclose(c);
  return first;
err_cleanup:
  if (c) {
    fclose(c);
  }
  cfg_free(first);
  return NULL;
}

void cfg_free(cfgitem_t *item) {
  cfgitem_t *next;
  while (item) {
    next = item->next;
    if (item->rbldomain) {
      free (item->rbldomain);
    }
    free(item);
    item = next;
  }
}

void cfg_dump(cfgitem_t *item) {
  printf("<rblservers>\n");
  while(item) {
    printf("%-40s %3hd\n", item->rbldomain, item->weight);
    item = item->next;
  }
  printf("</rblservers>\n");
}
