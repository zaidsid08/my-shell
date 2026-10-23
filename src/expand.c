/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "expand.h"

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lexer.h"
#include "parser.h"
#include "shell.h"
#include "strbuf.h"

/* Expansion builds two strings at once: the literal value, and a glob
 * pattern where every character that came from a quoted context is
 * backslash-escaped so glob() treats it literally. */
typedef struct {
    StrBuf value;
    StrBuf pattern;
    bool has_glob;
} Expansion;

static bool is_glob_meta(char c) {
    return c == '*' || c == '?' || c == '[' || c == '\\';
}

static void add_quoted(Expansion *x, const char *s, size_t n) {
    sb_putn(&x->value, s, n);
    for (size_t i = 0; i < n; i++) {
        if (is_glob_meta(s[i])) sb_putc(&x->pattern, '\\');
        sb_putc(&x->pattern, s[i]);
    }
}

static void add_unquoted_char(Expansion *x, char c) {
    sb_putc(&x->value, c);
    sb_putc(&x->pattern, c);
    if (c == '*' || c == '?' || c == '[') x->has_glob = true;
}

char *command_substitute(Shell *sh, const char *src) {
    int fds[2];
    if (pipe(fds) < 0) {
        perror("msh: pipe");
        return xstrdup("");
    }
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("msh: fork");
        close(fds[0]);
        close(fds[1]);
        return xstrdup("");
    }
    if (pid == 0) {
        close(fds[0]);
        if (fds[1] != STDOUT_FILENO) {
            dup2(fds[1], STDOUT_FILENO);
            close(fds[1]);
        }
        shell_enter_child(sh);
        int status = shell_run_string(sh, src);
        fflush(NULL);
        _exit(status);
    }

    close(fds[1]);
    StrBuf out;
    sb_init(&out);
    char buf[4096];
    for (;;) {
        ssize_t n = read(fds[0], buf, sizeof buf);
        if (n > 0) {
            sb_putn(&out, buf, (size_t)n);
        } else if (n == 0 || errno != EINTR) {
            break;
        }
    }
    close(fds[0]);

    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    sh->last_status = status_to_exit_code(status);

    while (out.len > 0 && out.data[out.len - 1] == '\n') {
        out.data[--out.len] = '\0';
    }
    return sb_take(&out);
}

/* Value of a special or positional parameter, or NULL if name isn't one.
 * Writes into scratch when it needs to format a number. */
static const char *special_param(Shell *sh, const char *name, char *scratch, size_t len) {
    if (strcmp(name, "?") == 0) {
        snprintf(scratch, len, "%d", sh->last_status);
        return scratch;
    }
    if (strcmp(name, "$") == 0) {
        snprintf(scratch, len, "%ld", (long)getpid());
        return scratch;
    }
    if (strcmp(name, "!") == 0) {
        if (sh->last_bg_pid <= 0) return "";
        snprintf(scratch, len, "%ld", (long)sh->last_bg_pid);
        return scratch;
    }
    if (strcmp(name, "#") == 0) {
        snprintf(scratch, len, "%d", sh->nparams);
        return scratch;
    }
    if (strcmp(name, "0") == 0) return sh->name;
    if (isdigit((unsigned char)name[0]) && name[1] == '\0') {
        int i = name[0] - '0';
        return i <= sh->nparams ? sh->params[i - 1] : "";
    }
    return NULL;
}

static void add_param(Shell *sh, Expansion *x, const char *name) {
    if (strcmp(name, "@") == 0 || strcmp(name, "*") == 0) {
        for (int i = 0; i < sh->nparams; i++) {
            if (i) add_quoted(x, " ", 1);
            add_quoted(x, sh->params[i], strlen(sh->params[i]));
        }
        return;
    }
    char scratch[32];
    const char *v = special_param(sh, name, scratch, sizeof scratch);
    if (!v) v = vars_get(&sh->vars, name);
    if (v) add_quoted(x, v, strlen(v));
}

/* p points at '$'. Expands it into x and returns how many chars were used. */
static size_t expand_dollar(Shell *sh, const char *p, Expansion *x) {
    if (p[1] == '(') {
        const char *end = lex_skip_command_subst(p);
        if (!end) {
            add_quoted(x, "$", 1);
            return 1;
        }
        char *inner = xstrndup(p + 2, (size_t)(end - p - 3));
        char *out = command_substitute(sh, inner);
        add_quoted(x, out, strlen(out));
        free(inner);
        free(out);
        return (size_t)(end - p);
    }

    if (p[1] == '{') {
        const char *close = strchr(p + 2, '}');
        if (!close) {
            add_quoted(x, "$", 1);
            return 1;
        }
        char *body = xstrndup(p + 2, (size_t)(close - p - 2));
        char *dflt = strstr(body, ":-");
        if (dflt) {
            *dflt = '\0';
            Expansion probe;
            sb_init(&probe.value);
            sb_init(&probe.pattern);
            probe.has_glob = false;
            add_param(sh, &probe, body);
            if (probe.value.len > 0) {
                add_quoted(x, probe.value.data, probe.value.len);
            } else {
                char *d = expand_word(sh, dflt + 2);
                add_quoted(x, d, strlen(d));
                free(d);
            }
            sb_free(&probe.value);
            sb_free(&probe.pattern);
        } else {
            add_param(sh, x, body);
        }
        free(body);
        return (size_t)(close - p + 1);
    }

    if (p[1] && strchr("?$!#@*", p[1])) {
        char name[2] = {p[1], '\0'};
        add_param(sh, x, name);
        return 2;
    }
    if (isdigit((unsigned char)p[1])) {
        char name[2] = {p[1], '\0'};
        add_param(sh, x, name);
        return 2;
    }

    size_t n = 0;
    while (isalnum((unsigned char)p[1 + n]) || p[1 + n] == '_') n++;
    if (n == 0 || !is_valid_name(p + 1, n)) {
        add_quoted(x, "$", 1);
        return 1;
    }
    char *name = xstrndup(p + 1, n);
    add_param(sh, x, name);
    free(name);
    return 1 + n;
}

static void expand_into(Shell *sh, const char *raw, Expansion *x) {
    sb_init(&x->value);
    sb_init(&x->pattern);
    x->has_glob = false;

    const char *p = raw;
    if (p[0] == '~' && (p[1] == '\0' || p[1] == '/')) {
        const char *home = vars_get(&sh->vars, "HOME");
        if (home) {
            add_quoted(x, home, strlen(home));
            p++;
        }
    }

    while (*p) {
        if (*p == '\\') {
            if (p[1]) {
                add_quoted(x, p + 1, 1);
                p += 2;
            } else {
                add_quoted(x, p, 1);
                p++;
            }
        } else if (*p == '\'') {
            const char *close = strchr(p + 1, '\'');
            size_t n = close ? (size_t)(close - p - 1) : strlen(p + 1);
            add_quoted(x, p + 1, n);
            p += n + (close ? 2 : 1);
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1] && strchr("$`\"\\", p[1])) {
                    add_quoted(x, p + 1, 1);
                    p += 2;
                } else if (*p == '$') {
                    p += expand_dollar(sh, p, x);
                } else {
                    add_quoted(x, p, 1);
                    p++;
                }
            }
            if (*p == '"') p++;
        } else if (*p == '$') {
            p += expand_dollar(sh, p, x);
        } else {
            add_unquoted_char(x, *p);
            p++;
        }
    }
}

char *expand_word(Shell *sh, const char *raw) {
    Expansion x;
    expand_into(sh, raw, &x);
    sb_free(&x.pattern);
    return sb_take(&x.value);
}

static void argv_push(char ***argv, size_t *n, size_t *cap, char *s) {
    if (*n + 2 > *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *argv = xrealloc(*argv, *cap * sizeof **argv);
    }
    (*argv)[(*n)++] = s;
    (*argv)[*n] = NULL;
}

char **expand_words(Shell *sh, char *const *words, size_t n, size_t *argc_out) {
    size_t argc = 0, cap = 8;
    char **argv = xmalloc(cap * sizeof *argv);
    argv[0] = NULL;

    for (size_t i = 0; i < n; i++) {
        Expansion x;
        expand_into(sh, words[i], &x);

        glob_t g;
        if (x.has_glob && glob(x.pattern.data, 0, NULL, &g) == 0) {
            for (size_t j = 0; j < g.gl_pathc; j++) {
                argv_push(&argv, &argc, &cap, xstrdup(g.gl_pathv[j]));
            }
            globfree(&g);
            sb_free(&x.value);
        } else {
            argv_push(&argv, &argc, &cap, sb_take(&x.value));
        }
        sb_free(&x.pattern);
    }
    if (argc_out) *argc_out = argc;
    return argv;
}

void free_argv(char **argv) {
    for (char **p = argv; p && *p; p++) free(*p);
    free(argv);
}
