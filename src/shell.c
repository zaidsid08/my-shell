/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "exec.h"
#include "lexer.h"
#include "parser.h"
#include "signals.h"
#include "strbuf.h"

void shell_init(Shell *sh, const char *name, char **envp) {
    memset(sh, 0, sizeof *sh);
    vars_init(&sh->vars);
    vars_import_env(&sh->vars, envp);
    sh->name = xstrdup(name);
    sh->tty_fd = STDIN_FILENO;
    sh->pgid = getpgrp();
}

void shell_set_params(Shell *sh, char **params, int n) {
    for (int i = 0; i < sh->nparams; i++) free(sh->params[i]);
    free(sh->params);
    sh->params = xmalloc((size_t)(n > 0 ? n : 1) * sizeof *sh->params);
    for (int i = 0; i < n; i++) sh->params[i] = xstrdup(params[i]);
    sh->nparams = n;
}

void shell_destroy(Shell *sh) {
    vars_free(&sh->vars);
    jobs_free_all(&sh->jobs);
    for (size_t i = 0; i < sh->history.len; i++) free(sh->history.lines[i]);
    free(sh->history.lines);
    for (int i = 0; i < sh->nparams; i++) free(sh->params[i]);
    free(sh->params);
    free(sh->name);
}

void shell_enter_child(Shell *sh) {
    signals_reset_defaults();
    /* A child never does job control of its own, and must not touch the
     * parent's job table entries. */
    sh->interactive = false;
    jobs_free_all(&sh->jobs);
}

int shell_run_string(Shell *sh, const char *src) {
    TokenList tokens;
    const char *lex_err = NULL;
    if (lex(src, &tokens, &lex_err) != 0) {
        fprintf(stderr, "msh: %s\n", lex_err);
        token_list_free(&tokens);
        return sh->last_status = 2;
    }

    CommandList list;
    char err[256];
    if (parse(&tokens, &list, err, sizeof err) != 0) {
        fprintf(stderr, "msh: %s\n", err);
        command_list_free(&list);
        token_list_free(&tokens);
        return sh->last_status = 2;
    }
    token_list_free(&tokens);

    int status = exec_list(sh, &list);
    command_list_free(&list);
    return status;
}

int shell_run_stream(Shell *sh, FILE *fp) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while (!sh->exit_requested && (n = getline(&line, &cap, fp)) >= 0) {
        if (n > 0 && line[n - 1] == '\n') line[n - 1] = '\0';
        shell_run_string(sh, line);
        jobs_reap(&sh->jobs, false);
    }
    free(line);
    return sh->exit_requested ? sh->exit_code : sh->last_status;
}

void history_add(History *h, const char *line) {
    if (h->len > 0 && strcmp(h->lines[h->len - 1], line) == 0) return;
    h->lines = xrealloc(h->lines, (h->len + 1) * sizeof *h->lines);
    h->lines[h->len++] = xstrdup(line);
}
