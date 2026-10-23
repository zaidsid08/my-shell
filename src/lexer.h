/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_LEXER_H
#define MSH_LEXER_H

#include <stddef.h>

typedef enum {
    TOK_WORD,
    TOK_PIPE,     /* |  */
    TOK_AND_IF,   /* && */
    TOK_OR_IF,    /* || */
    TOK_SEMI,     /* ;  */
    TOK_AMP,      /* &  */
    TOK_LESS,     /* <  */
    TOK_GREAT,    /* >  */
    TOK_DGREAT,   /* >> */
    TOK_LESSGREAT,/* <> */
    TOK_GREATAND, /* >& */
    TOK_EOF
} TokenType;

typedef struct {
    TokenType type;
    /* For TOK_WORD: the raw word, quotes and escapes still in place.
     * Expansion and quote removal happen later, at execution time. */
    char *text;
    /* For redirection operators: explicit fd prefix (e.g. the 2 in 2>),
     * or -1 when none was given. */
    int fd;
} Token;

typedef struct {
    Token *items;
    size_t len;
    size_t cap;
} TokenList;

/* Tokenize src. Returns 0 on success; on a syntax error returns -1 and
 * stores a static message in *err. The list must be freed either way. */
int lex(const char *src, TokenList *out, const char **err);
void token_list_free(TokenList *list);
const char *token_type_name(TokenType t);

/* p points at "$(". Returns a pointer just past the matching ')', or NULL
 * if it is unterminated. Shared with the expander. */
const char *lex_skip_command_subst(const char *p);

#endif
