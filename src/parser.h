/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_PARSER_H
#define MSH_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "lexer.h"

/*
 * Grammar (a small subset of POSIX sh):
 *
 *   list     := and_or (( ';' | '&' ) and_or)* [ ';' | '&' ]
 *   and_or   := pipeline (( '&&' | '||' ) pipeline)*
 *   pipeline := command ( '|' command )*
 *   command  := ( assignment | word | redirect )+
 *   redirect := [fd] ( '<' | '>' | '>>' | '<>' | '>&' ) word
 *
 * Words are stored raw; expansion happens in the executor.
 */

typedef enum {
    REDIR_IN,     /* <  */
    REDIR_OUT,    /* >  */
    REDIR_APPEND, /* >> */
    REDIR_RDWR,   /* <> */
    REDIR_DUP,    /* >& */
} RedirKind;

typedef struct {
    RedirKind kind;
    int fd;       /* fd being redirected */
    char *target; /* raw word: a path, or an fd number for REDIR_DUP */
} Redir;

typedef struct {
    char **assigns; /* NAME=value prefixes, raw */
    size_t nassigns;
    char **words;   /* command name + arguments, raw */
    size_t nwords;
    Redir *redirs;
    size_t nredirs;
} Command;

typedef struct {
    Command *cmds;
    size_t ncmds;
} Pipeline;

typedef enum { CONN_AND, CONN_OR } Connector;

typedef struct {
    Pipeline *pipes;
    Connector *conns; /* conns[i] joins pipes[i] and pipes[i + 1] */
    size_t npipes;
    bool background;
    char *source;     /* human-readable text, used for job listings */
} AndOr;

typedef struct {
    AndOr *items;
    size_t nitems;
} CommandList;

/* Parse tokens into list. Returns 0 on success, -1 on a syntax error
 * (with a message written into errbuf). */
int parse(const TokenList *tokens, CommandList *list, char *errbuf, size_t errlen);
void command_list_free(CommandList *list);

/* True if word looks like NAME=value with a valid identifier. */
bool is_assignment_word(const char *word);
bool is_valid_name(const char *s, size_t n);

#endif
