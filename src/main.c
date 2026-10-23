/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jobs.h"
#include "shell.h"
#include "signals.h"
#include "strbuf.h"

extern char **environ;

static void usage(void) {
    fputs("usage: msh [-c command [arg ...]] [script [arg ...]]\n", stderr);
}

/* Put the shell in its own process group in the foreground of the
 * terminal, which is what makes job control possible. */
static void take_terminal(Shell *sh) {
    pid_t pgid;
    while ((pgid = getpgrp()) != tcgetpgrp(sh->tty_fd)) {
        kill(-pgid, SIGTTIN); /* we're in the background: wait our turn */
    }
    signals_setup_interactive();
    if (getpid() != getpgrp()) {
        setpgid(0, 0);
    }
    sh->pgid = getpgrp();
    tcsetpgrp(sh->tty_fd, sh->pgid);
}

static void build_prompt(Shell *sh, StrBuf *out) {
    sb_clear(out);
    const char *ps1 = vars_get(&sh->vars, "PS1");
    if (ps1) {
        sb_puts(out, ps1);
        return;
    }
    char cwd[PATH_MAX];
    const char *home = vars_get(&sh->vars, "HOME");
    sb_puts(out, "msh:");
    if (getcwd(cwd, sizeof cwd)) {
        size_t hl = home ? strlen(home) : 0;
        if (hl > 1 && strncmp(cwd, home, hl) == 0 && (cwd[hl] == '/' || cwd[hl] == '\0')) {
            sb_putc(out, '~');
            sb_puts(out, cwd + hl);
        } else {
            sb_puts(out, cwd);
        }
    }
    sb_puts(out, sh->last_status == 0 ? "$ " : "! ");
}

static int repl(Shell *sh) {
    char *line = NULL;
    size_t cap = 0;
    StrBuf prompt;
    sb_init(&prompt);

    while (!sh->exit_requested) {
        jobs_reap(&sh->jobs, true);
        build_prompt(sh, &prompt);
        fputs(prompt.data, stderr);
        fflush(stderr);

        errno = 0;
        g_interrupted = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            if (errno == EINTR && g_interrupted) {
                clearerr(stdin);
                fputc('\n', stderr);
                sh->last_status = 130;
                continue;
            }
            fputs("exit\n", stderr);
            break;
        }
        if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';

        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;

        history_add(&sh->history, line);
        shell_run_string(sh, line);
    }
    free(line);
    sb_free(&prompt);
    return sh->exit_requested ? sh->exit_code : sh->last_status;
}

int main(int argc, char **argv) {
    Shell sh;
    int status;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            usage();
            return 2;
        }
        /* Like sh -c: the word after the command becomes $0. */
        shell_init(&sh, argc > 3 ? argv[3] : "msh", environ);
        if (argc > 4) shell_set_params(&sh, argv + 4, argc - 4);
        status = shell_run_string(&sh, argv[2]);
        if (sh.exit_requested) status = sh.exit_code;
    } else if (argc > 1) {
        FILE *fp = fopen(argv[1], "r");
        if (!fp) {
            fprintf(stderr, "msh: %s: %s\n", argv[1], strerror(errno));
            return 127;
        }
        shell_init(&sh, argv[1], environ);
        shell_set_params(&sh, argv + 2, argc - 2);
        status = shell_run_stream(&sh, fp);
        fclose(fp);
    } else {
        shell_init(&sh, "msh", environ);
        sh.interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);
        if (sh.interactive) {
            take_terminal(&sh);
            status = repl(&sh);
        } else {
            status = shell_run_stream(&sh, stdin);
        }
    }

    shell_destroy(&sh);
    return status;
}
