/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

typedef struct {
    const Token *toks;
    size_t pos;
    char *errbuf;
    size_t errlen;
} Parser;

#define GROW(arr, n)                                                  \
    ((arr) = xrealloc((arr), ((n) + 1) * sizeof *(arr)), &(arr)[(n)++])

static const Token *peek(Parser *p) {
    return &p->toks[p->pos];
}

static const Token *next(Parser *p) {
    const Token *t = &p->toks[p->pos];
    if (t->type != TOK_EOF) {
        p->pos++;
    }
    return t;
}

static int syntax_error(Parser *p, const Token *at) {
    snprintf(p->errbuf, p->errlen, "syntax error near '%s'",
             at->type == TOK_WORD ? at->text : token_type_name(at->type));
    return -1;
}

bool is_valid_name(const char *s, size_t n) {
    if (n == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        return false;
    }
    for (size_t i = 1; i < n; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) {
            return false;
        }
    }
    return true;
}

bool is_assignment_word(const char *word) {
    const char *eq = strchr(word, '=');
    return eq && is_valid_name(word, (size_t)(eq - word));
}

static bool redirect_kind(TokenType t, RedirKind *kind, int *default_fd) {
    switch (t) {
    case TOK_LESS: *kind = REDIR_IN; *default_fd = 0; return true;
    case TOK_GREAT: *kind = REDIR_OUT; *default_fd = 1; return true;
    case TOK_DGREAT: *kind = REDIR_APPEND; *default_fd = 1; return true;
    case TOK_LESSGREAT: *kind = REDIR_RDWR; *default_fd = 0; return true;
    case TOK_GREATAND: *kind = REDIR_DUP; *default_fd = 1; return true;
    default: return false;
    }
}

static void command_free(Command *c) {
    for (size_t i = 0; i < c->nassigns; i++) free(c->assigns[i]);
    for (size_t i = 0; i < c->nwords; i++) free(c->words[i]);
    for (size_t i = 0; i < c->nredirs; i++) free(c->redirs[i].target);
    free(c->assigns);
    free(c->words);
    free(c->redirs);
}

static int parse_command(Parser *p, Command *cmd) {
    memset(cmd, 0, sizeof *cmd);
    for (;;) {
        const Token *t = peek(p);
        RedirKind kind;
        int default_fd;

        if (t->type == TOK_WORD) {
            next(p);
            /* Assignments only count before the command name. */
            if (cmd->nwords == 0 && is_assignment_word(t->text)) {
                *GROW(cmd->assigns, cmd->nassigns) = xstrdup(t->text);
            } else {
                *GROW(cmd->words, cmd->nwords) = xstrdup(t->text);
            }
        } else if (redirect_kind(t->type, &kind, &default_fd)) {
            next(p);
            const Token *target = next(p);
            if (target->type != TOK_WORD) {
                return syntax_error(p, target);
            }
            Redir *r = GROW(cmd->redirs, cmd->nredirs);
            r->kind = kind;
            r->fd = t->fd >= 0 ? t->fd : default_fd;
            r->target = xstrdup(target->text);
        } else {
            break;
        }
    }
    if (cmd->nwords == 0 && cmd->nassigns == 0 && cmd->nredirs == 0) {
        return syntax_error(p, peek(p));
    }
    return 0;
}

static int parse_pipeline(Parser *p, Pipeline *pl) {
    memset(pl, 0, sizeof *pl);
    for (;;) {
        Command *cmd = GROW(pl->cmds, pl->ncmds);
        if (parse_command(p, cmd) != 0) {
            return -1;
        }
        if (peek(p)->type != TOK_PIPE) {
            return 0;
        }
        next(p);
    }
}

static void pipeline_free(Pipeline *pl) {
    for (size_t i = 0; i < pl->ncmds; i++) command_free(&pl->cmds[i]);
    free(pl->cmds);
}

/* Rebuild a readable command line from tokens [from, to). */
static char *render_source(const Token *toks, size_t from, size_t to) {
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = from; i < to; i++) {
        if (i > from) sb_putc(&sb, ' ');
        if (toks[i].type == TOK_WORD) {
            sb_puts(&sb, toks[i].text);
        } else {
            if (toks[i].fd >= 0) {
                char num[16];
                snprintf(num, sizeof num, "%d", toks[i].fd);
                sb_puts(&sb, num);
            }
            sb_puts(&sb, token_type_name(toks[i].type));
        }
    }
    return sb_take(&sb);
}

static int parse_and_or(Parser *p, AndOr *ao) {
    memset(ao, 0, sizeof *ao);
    size_t start = p->pos;
    for (;;) {
        Pipeline *pl = GROW(ao->pipes, ao->npipes);
        if (parse_pipeline(p, pl) != 0) {
            return -1;
        }
        TokenType t = peek(p)->type;
        if (t != TOK_AND_IF && t != TOK_OR_IF) {
            break;
        }
        next(p);
        ao->conns = xrealloc(ao->conns, ao->npipes * sizeof *ao->conns);
        ao->conns[ao->npipes - 1] = (t == TOK_AND_IF) ? CONN_AND : CONN_OR;
    }
    ao->source = render_source(p->toks, start, p->pos);
    return 0;
}

static void and_or_free(AndOr *ao) {
    for (size_t i = 0; i < ao->npipes; i++) pipeline_free(&ao->pipes[i]);
    free(ao->pipes);
    free(ao->conns);
    free(ao->source);
}

int parse(const TokenList *tokens, CommandList *list, char *errbuf, size_t errlen) {
    Parser p = {.toks = tokens->items, .pos = 0, .errbuf = errbuf, .errlen = errlen};
    memset(list, 0, sizeof *list);

    while (peek(&p)->type != TOK_EOF) {
        AndOr *ao = GROW(list->items, list->nitems);
        if (parse_and_or(&p, ao) != 0) {
            return -1;
        }
        const Token *t = peek(&p);
        if (t->type == TOK_AMP) {
            ao->background = true;
            next(&p);
        } else if (t->type == TOK_SEMI) {
            next(&p);
        } else if (t->type != TOK_EOF) {
            return syntax_error(&p, t);
        }
    }
    return 0;
}

void command_list_free(CommandList *list) {
    for (size_t i = 0; i < list->nitems; i++) and_or_free(&list->items[i]);
    free(list->items);
    memset(list, 0, sizeof *list);
}
