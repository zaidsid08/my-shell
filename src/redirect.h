/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_REDIRECT_H
#define MSH_REDIRECT_H

#include <stddef.h>

#include "parser.h"

struct Shell;

/* Remembers where an fd pointed before a redirection so a builtin running
 * inside the shell process can have its redirections undone afterwards. */
typedef struct {
    int fd;
    int saved; /* dup of the original, or -1 if fd was closed */
} SavedFd;

typedef struct {
    SavedFd *items;
    size_t len;
} FdSnapshot;

/*
 * Apply redirections in order. If snap is non-NULL, the original fds are
 * saved into it first. Returns 0 on success or -1 after printing an error.
 *
 * A target of the form /dev/tcp/HOST/PORT opens a TCP connection instead
 * of a file, so `echo hi > /dev/tcp/localhost/9000` works like in bash.
 */
int redirect_apply(struct Shell *sh, const Redir *redirs, size_t n, FdSnapshot *snap);

/* Undo redirections recorded in snap and free it. */
void redirect_restore(FdSnapshot *snap);

/* Parse "/dev/tcp/host/port" and connect. Returns a socket fd or -1, with
 * a message in errbuf. Exposed for tests. */
int tcp_connect_path(const char *path, char *errbuf, size_t errlen);

#endif
