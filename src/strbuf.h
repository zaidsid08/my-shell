/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_STRBUF_H
#define MSH_STRBUF_H

#include <stddef.h>

/* Growable, always NUL-terminated byte buffer. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

void sb_init(StrBuf *sb);
void sb_putc(StrBuf *sb, char c);
void sb_puts(StrBuf *sb, const char *s);
void sb_putn(StrBuf *sb, const char *s, size_t n);
void sb_clear(StrBuf *sb);

/* Hand ownership of the buffer to the caller. sb must be re-initialised
 * with sb_init before it is used again. */
char *sb_take(StrBuf *sb);
void sb_free(StrBuf *sb);

/* Allocation helpers that abort on OOM, so callers don't need to check. */
void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

#endif
