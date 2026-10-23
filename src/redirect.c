/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "redirect.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "expand.h"
#include "shell.h"
#include "strbuf.h"

#define TCP_PREFIX "/dev/tcp/"
#define SAVE_FD_MIN 10 /* keep saved copies out of the way of user fds */

int tcp_connect_path(const char *path, char *errbuf, size_t errlen) {
    const char *rest = path + strlen(TCP_PREFIX);
    const char *slash = strchr(rest, '/');
    if (!slash || slash == rest || slash[1] == '\0') {
        snprintf(errbuf, errlen, "expected /dev/tcp/HOST/PORT");
        return -1;
    }
    char *host = xstrndup(rest, (size_t)(slash - rest));
    const char *port = slash + 1;

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, port, &hints, &res);
    free(host);
    if (rc != 0) {
        snprintf(errbuf, errlen, "%s", gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    int last_errno = 0;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            last_errno = errno;
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        last_errno = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        snprintf(errbuf, errlen, "%s", strerror(last_errno ? last_errno : ECONNREFUSED));
    }
    return fd;
}

static void snapshot_fd(FdSnapshot *snap, int fd) {
    for (size_t i = 0; i < snap->len; i++) {
        if (snap->items[i].fd == fd) return; /* first save wins */
    }
    snap->items = xrealloc(snap->items, (snap->len + 1) * sizeof *snap->items);
    int saved = fcntl(fd, F_DUPFD_CLOEXEC, SAVE_FD_MIN);
    snap->items[snap->len++] = (SavedFd){.fd = fd, .saved = saved};
}

static int open_target(const Redir *r, const char *target) {
    if (strncmp(target, TCP_PREFIX, strlen(TCP_PREFIX)) == 0) {
        char err[128];
        int fd = tcp_connect_path(target, err, sizeof err);
        if (fd < 0) fprintf(stderr, "msh: %s: %s\n", target, err);
        return fd;
    }

    int flags;
    switch (r->kind) {
    case REDIR_IN: flags = O_RDONLY; break;
    case REDIR_OUT: flags = O_WRONLY | O_CREAT | O_TRUNC; break;
    case REDIR_APPEND: flags = O_WRONLY | O_CREAT | O_APPEND; break;
    case REDIR_RDWR: flags = O_RDWR | O_CREAT; break;
    default: return -1;
    }
    int fd = open(target, flags | O_CLOEXEC, 0644);
    if (fd < 0) fprintf(stderr, "msh: %s: %s\n", target, strerror(errno));
    return fd;
}

static int apply_dup(const Redir *r, const char *target) {
    if (strcmp(target, "-") == 0) {
        close(r->fd);
        return 0;
    }
    char *end;
    long src = strtol(target, &end, 10);
    if (*target == '\0' || *end != '\0' || src < 0 || fcntl((int)src, F_GETFD) < 0) {
        fprintf(stderr, "msh: %s: bad file descriptor\n", target);
        return -1;
    }
    if ((int)src != r->fd && dup2((int)src, r->fd) < 0) {
        fprintf(stderr, "msh: dup2: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int redirect_apply(Shell *sh, const Redir *redirs, size_t n, FdSnapshot *snap) {
    for (size_t i = 0; i < n; i++) {
        const Redir *r = &redirs[i];
        char *target = expand_word(sh, r->target);
        if (snap) snapshot_fd(snap, r->fd);

        int rc = 0;
        if (r->kind == REDIR_DUP) {
            rc = apply_dup(r, target);
        } else {
            int fd = open_target(r, target);
            if (fd < 0) {
                rc = -1;
            } else if (fd != r->fd) {
                if (dup2(fd, r->fd) < 0) {
                    fprintf(stderr, "msh: dup2: %s\n", strerror(errno));
                    rc = -1;
                }
                close(fd);
            } else {
                /* Landed on the right fd already; just drop O_CLOEXEC. */
                fcntl(fd, F_SETFD, 0);
            }
        }
        free(target);
        if (rc < 0) return -1;
    }
    return 0;
}

void redirect_restore(FdSnapshot *snap) {
    /* Reverse order so nested redirections of the same fd unwind cleanly. */
    for (size_t i = snap->len; i-- > 0;) {
        SavedFd *s = &snap->items[i];
        if (s->saved >= 0) {
            dup2(s->saved, s->fd);
            close(s->saved);
        } else {
            close(s->fd);
        }
    }
    free(snap->items);
    snap->items = NULL;
    snap->len = 0;
}
