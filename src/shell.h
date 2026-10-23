/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_SHELL_H
#define MSH_SHELL_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#include "jobs.h"
#include "vars.h"

typedef struct {
    char **lines;
    size_t len;
} History;

/* All mutable interpreter state lives here; there are no other globals
 * apart from the async-signal flag in signals.c. */
typedef struct Shell {
    VarTable vars;
    JobTable jobs;
    History history;

    int last_status;   /* $? */
    pid_t last_bg_pid; /* $! */

    char *name;        /* $0 */
    char **params;     /* $1..$n */
    int nparams;

    bool interactive;  /* owns the terminal and does job control */
    int tty_fd;
    pid_t pgid;        /* the shell's own process group */

    bool exit_requested;
    int exit_code;
    bool warned_stopped_jobs;
} Shell;

void shell_init(Shell *sh, const char *name, char **envp);
void shell_destroy(Shell *sh);
void shell_set_params(Shell *sh, char **params, int n);

/* Lex, parse and run one chunk of source. Returns the resulting $?. */
int shell_run_string(Shell *sh, const char *src);

/* Run every line from fp until EOF or `exit`. */
int shell_run_stream(Shell *sh, FILE *fp);

void history_add(History *h, const char *line);

/* Called in every forked child before it runs anything. */
void shell_enter_child(Shell *sh);

#endif
