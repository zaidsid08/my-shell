/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "exec.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "expand.h"
#include "jobs.h"
#include "redirect.h"
#include "shell.h"
#include "strbuf.h"

/* Assign NAME=value prefixes. When export is set they also go into the
 * environment (used in children right before exec). */
static void apply_assignments(Shell *sh, const Command *cmd, bool export) {
    for (size_t i = 0; i < cmd->nassigns; i++) {
        const char *raw = cmd->assigns[i];
        const char *eq = strchr(raw, '=');
        char *name = xstrndup(raw, (size_t)(eq - raw));
        char *value = expand_word(sh, eq + 1);
        vars_set(&sh->vars, name, value);
        if (export) vars_export(&sh->vars, name);
        free(name);
        free(value);
    }
}

/* Replace the current (child) process with argv[0]. Never returns. */
static void exec_external(Shell *sh, char **argv) {
    char *path = resolve_command(sh, argv[0]);
    if (!path) {
        fprintf(stderr, "msh: %s: command not found\n", argv[0]);
        _exit(127);
    }
    char **env = vars_build_env(&sh->vars);
    execve(path, argv, env);
    int err = errno;
    fprintf(stderr, "msh: %s: %s\n", argv[0], strerror(err));
    _exit(err == EACCES ? 126 : 127);
}

/* Run a builtin inside the shell process, with its redirections applied
 * temporarily so `echo hi > f` doesn't permanently move stdout. */
static int run_builtin_in_shell(Shell *sh, const Builtin *b, const Command *cmd,
                                char **argv, size_t argc) {
    FdSnapshot snap = {0};
    fflush(NULL);
    int status;
    if (redirect_apply(sh, cmd->redirs, cmd->nredirs, &snap) != 0) {
        status = 1;
    } else {
        apply_assignments(sh, cmd, false);
        status = b->fn(sh, (int)argc, argv);
    }
    fflush(NULL);
    redirect_restore(&snap);
    return status;
}

/* Code that runs in a pipeline child after fork(). Never returns.
 * argv may be pre-expanded by the parent; otherwise it's expanded here. */
static void run_child(Shell *sh, const Command *cmd, char **argv, int in_fd, int out_fd) {
    shell_enter_child(sh);
    if (in_fd != STDIN_FILENO) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    }
    if (out_fd != STDOUT_FILENO) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }
    if (redirect_apply(sh, cmd->redirs, cmd->nredirs, NULL) != 0) {
        _exit(1);
    }
    apply_assignments(sh, cmd, true);

    if (!argv) argv = expand_words(sh, cmd->words, cmd->nwords, NULL);
    if (!argv[0]) {
        _exit(0);
    }
    size_t argc = 0;
    while (argv[argc]) argc++;
    const Builtin *b = builtin_lookup(argv[0]);
    if (b) {
        int status = b->fn(sh, (int)argc, argv);
        fflush(NULL);
        _exit(status);
    }
    exec_external(sh, argv);
}

static void give_terminal_to(Shell *sh, pid_t pgid) {
    if (sh->interactive) tcsetpgrp(sh->tty_fd, pgid);
}

/* Fork every stage of the pipeline, wire up pipes, then wait. */
static int run_forked_pipeline(Shell *sh, const Pipeline *pl, const char *source,
                               char **first_argv) {
    Job *job = job_new(source);
    pid_t pgid = 0;
    int prev_read = STDIN_FILENO;

    for (size_t i = 0; i < pl->ncmds; i++) {
        int fds[2] = {-1, STDOUT_FILENO};
        bool last = (i + 1 == pl->ncmds);
        if (!last && pipe(fds) < 0) {
            perror("msh: pipe");
            break;
        }

        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) {
            perror("msh: fork");
            if (!last) {
                close(fds[0]);
                close(fds[1]);
            }
            break;
        }
        if (pid == 0) {
            if (sh->interactive) {
                setpgid(0, pgid);
                if (pgid == 0) give_terminal_to(sh, getpid());
            }
            if (!last) close(fds[0]);
            run_child(sh, &pl->cmds[i], i == 0 ? first_argv : NULL, prev_read, fds[1]);
        }

        /* Parent: set the group here too, so there's no race with the child. */
        if (sh->interactive) {
            if (pgid == 0) pgid = pid;
            setpgid(pid, pgid);
        }
        job_add_proc(job, pid);
        if (prev_read != STDIN_FILENO) close(prev_read);
        if (!last) {
            close(fds[1]);
            prev_read = fds[0];
        }
    }
    if (prev_read != STDIN_FILENO) close(prev_read);

    if (job->nprocs == 0) {
        job_free(job);
        return 1;
    }

    job->pgid = pgid;
    give_terminal_to(sh, pgid);
    int status = job_wait(job);
    give_terminal_to(sh, sh->pgid);

    /* Ctrl-C leaves the cursor after "^C"; move the prompt to a fresh line. */
    if (sh->interactive && status == 128 + SIGINT) fputc('\n', stderr);

    if (job_state(job) == JOB_STOPPED) {
        jobs_add(&sh->jobs, job);
        putchar('\n');
        jobs_print(job, &sh->jobs);
        job->reported_stop = true;
    } else {
        job_free(job);
    }
    return status;
}

static int run_pipeline(Shell *sh, const Pipeline *pl, const char *source) {
    if (pl->ncmds == 1) {
        const Command *cmd = &pl->cmds[0];

        /* Bare assignments (FOO=bar) and bare redirections (> file). */
        if (cmd->nwords == 0) {
            apply_assignments(sh, cmd, false);
            FdSnapshot snap = {0};
            int status = redirect_apply(sh, cmd->redirs, cmd->nredirs, &snap) == 0 ? 0 : 1;
            redirect_restore(&snap);
            return status;
        }

        /* Builtins must run in the shell itself so cd/export/exit stick. */
        size_t argc;
        char **argv = expand_words(sh, cmd->words, cmd->nwords, &argc);
        const Builtin *b = argc ? builtin_lookup(argv[0]) : NULL;
        if (b) {
            int status = run_builtin_in_shell(sh, b, cmd, argv, argc);
            free_argv(argv);
            return status;
        }
        /* External command: reuse the expansion so $(...) only runs once. */
        int status = run_forked_pipeline(sh, pl, source, argv);
        free_argv(argv);
        return status;
    }
    return run_forked_pipeline(sh, pl, source, NULL);
}

static int run_and_or(Shell *sh, const AndOr *ao) {
    int status = run_pipeline(sh, &ao->pipes[0], ao->source);
    for (size_t i = 1; i < ao->npipes && !sh->exit_requested; i++) {
        sh->last_status = status;
        bool run = (ao->conns[i - 1] == CONN_AND) ? (status == 0) : (status != 0);
        if (run) status = run_pipeline(sh, &ao->pipes[i], ao->source);
    }
    return status;
}

/* `cmd &`: fork a subshell that becomes its own process group and runs the
 * whole and-or list, so `a && b &` backgrounds both as one job. */
static int run_background(Shell *sh, const AndOr *ao) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        perror("msh: fork");
        return 1;
    }
    if (pid == 0) {
        setpgid(0, 0);
        shell_enter_child(sh);
        /* Background jobs must not read the terminal. */
        if (isatty(STDIN_FILENO)) {
            int devnull = open("/dev/null", 0);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                close(devnull);
            }
        }
        int status = run_and_or(sh, ao);
        fflush(NULL);
        _exit(status);
    }
    setpgid(pid, pid);

    StrBuf text;
    sb_init(&text);
    sb_puts(&text, ao->source);
    sb_puts(&text, " &");
    Job *job = job_new(text.data);
    sb_free(&text);
    job->pgid = pid;
    job_add_proc(job, pid);
    jobs_add(&sh->jobs, job);
    sh->last_bg_pid = pid;

    if (sh->interactive) fprintf(stderr, "[%d] %ld\n", job->id, (long)pid);
    return 0;
}

int exec_list(Shell *sh, const CommandList *list) {
    for (size_t i = 0; i < list->nitems && !sh->exit_requested; i++) {
        const AndOr *ao = &list->items[i];
        sh->last_status = ao->background ? run_background(sh, ao) : run_and_or(sh, ao);
    }
    return sh->last_status;
}
