/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

static void push(TokenList *list, TokenType type, char *text, int fd) {
    if (list->len == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 8;
        list->items = xrealloc(list->items, list->cap * sizeof(Token));
    }
    list->items[list->len++] = (Token){.type = type, .text = text, .fd = fd};
}

static int is_operator_char(char c) {
    return c == '|' || c == '&' || c == ';' || c == '<' || c == '>';
}

static const char *skip_double_quoted(const char *p, const char **err);

/* p points at "$(". Returns a pointer just past the matching ')'. */
static const char *skip_command_subst(const char *p, const char **err) {
    int depth = 1;
    p += 2;
    while (*p) {
        if (*p == '\\' && p[1]) {
            p += 2;
        } else if (*p == '\'') {
            const char *close = strchr(p + 1, '\'');
            if (!close) {
                break;
            }
            p = close + 1;
        } else if (*p == '"') {
            p = skip_double_quoted(p, err);
            if (!p) {
                return NULL;
            }
        } else if (*p == '(') {
            depth++;
            p++;
        } else if (*p == ')') {
            if (--depth == 0) {
                return p + 1;
            }
            p++;
        } else {
            p++;
        }
    }
    *err = "unterminated $( )";
    return NULL;
}

/* p points at an opening '"'. Returns a pointer just past the closing one. */
static const char *skip_double_quoted(const char *p, const char **err) {
    p++;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p += 2;
        } else if (p[0] == '$' && p[1] == '(') {
            p = skip_command_subst(p, err);
            if (!p) {
                return NULL;
            }
        } else {
            p++;
        }
    }
    if (*p != '"') {
        *err = "unterminated double quote";
        return NULL;
    }
    return p + 1;
}

/* Scan one word starting at p. Returns the end pointer or NULL on error. */
static const char *scan_word(const char *p, const char **err) {
    while (*p && !isspace((unsigned char)*p) && !is_operator_char(*p)) {
        if (*p == '\\') {
            p += p[1] ? 2 : 1;
        } else if (*p == '\'') {
            const char *close = strchr(p + 1, '\'');
            if (!close) {
                *err = "unterminated single quote";
                return NULL;
            }
            p = close + 1;
        } else if (*p == '"') {
            p = skip_double_quoted(p, err);
            if (!p) {
                return NULL;
            }
        } else if (p[0] == '$' && p[1] == '(') {
            p = skip_command_subst(p, err);
            if (!p) {
                return NULL;
            }
        } else if (p[0] == '$' && p[1] == '{') {
            const char *close = strchr(p + 2, '}');
            if (!close) {
                *err = "unterminated ${ }";
                return NULL;
            }
            p = close + 1;
        } else {
            p++;
        }
    }
    return p;
}

static int all_digits(const char *s, size_t n) {
    if (n == 0) {
        return 0;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

/* Match an operator at p. Returns its length (0 if none) and sets *type. */
static size_t match_operator(const char *p, TokenType *type) {
    static const struct {
        const char *text;
        TokenType type;
    } ops[] = {
        /* Longest first so "||" wins over "|". */
        {"&&", TOK_AND_IF}, {"||", TOK_OR_IF},   {">>", TOK_DGREAT},
        {"<>", TOK_LESSGREAT}, {">&", TOK_GREATAND}, {"|", TOK_PIPE},
        {"&", TOK_AMP},     {";", TOK_SEMI},     {"<", TOK_LESS},
        {">", TOK_GREAT},
    };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        size_t n = strlen(ops[i].text);
        if (strncmp(p, ops[i].text, n) == 0) {
            *type = ops[i].type;
            return n;
        }
    }
    return 0;
}

static int is_redirect(TokenType t) {
    return t == TOK_LESS || t == TOK_GREAT || t == TOK_DGREAT ||
           t == TOK_LESSGREAT || t == TOK_GREATAND;
}

int lex(const char *src, TokenList *out, const char **err) {
    memset(out, 0, sizeof *out);
    const char *p = src;

    for (;;) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        if (*p == '#') {
            break; /* comment runs to end of input line */
        }

        TokenType type;
        size_t oplen = match_operator(p, &type);
        if (oplen) {
            push(out, type, NULL, -1);
            p += oplen;
            continue;
        }

        const char *end = scan_word(p, err);
        if (!end) {
            return -1;
        }
        size_t n = (size_t)(end - p);

        /* "2>file": a run of digits glued to a redirection is an fd number. */
        if (all_digits(p, n) && (*end == '<' || *end == '>')) {
            size_t len = match_operator(end, &type);
            if (len && is_redirect(type)) {
                push(out, type, NULL, atoi(p));
                p = end + len;
                continue;
            }
        }

        push(out, TOK_WORD, xstrndup(p, n), -1);
        p = end;
    }

    push(out, TOK_EOF, NULL, -1);
    return 0;
}

void token_list_free(TokenList *list) {
    for (size_t i = 0; i < list->len; i++) {
        free(list->items[i].text);
    }
    free(list->items);
    memset(list, 0, sizeof *list);
}

const char *token_type_name(TokenType t) {
    switch (t) {
    case TOK_WORD: return "word";
    case TOK_PIPE: return "|";
    case TOK_AND_IF: return "&&";
    case TOK_OR_IF: return "||";
    case TOK_SEMI: return ";";
    case TOK_AMP: return "&";
    case TOK_LESS: return "<";
    case TOK_GREAT: return ">";
    case TOK_DGREAT: return ">>";
    case TOK_LESSGREAT: return "<>";
    case TOK_GREATAND: return ">&";
    case TOK_EOF: return "end of input";
    }
    return "?";
}

const char *lex_skip_command_subst(const char *p) {
    const char *err = NULL;
    return skip_command_subst(p, &err);
}
