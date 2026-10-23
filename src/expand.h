/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_EXPAND_H
#define MSH_EXPAND_H

#include <stddef.h>

struct Shell;

/*
 * Word expansion, applied at execution time to raw words from the parser:
 *
 *   1. tilde:      ~ and ~/path  ->  $HOME
 *   2. parameters: $NAME ${NAME} ${NAME:-default} $? $$ $! $# $0..$9 $@
 *   3. command substitution: $(command)
 *   4. quote removal: '...' "..." and backslash escapes
 *   5. pathname expansion (glob) on unquoted * ? [ characters
 *
 * Results of steps 2-3 are treated as quoted text: they are not split into
 * fields and not globbed. That keeps the rules predictable.
 */

/* Expand a list of raw words into a NULL-terminated argv. Globbing can
 * turn one word into many. Free with free_argv. */
char **expand_words(struct Shell *sh, char *const *words, size_t n, size_t *argc_out);

/* Expand one word without globbing (assignments, redirect targets). */
char *expand_word(struct Shell *sh, const char *raw);

void free_argv(char **argv);

/* Run src in a subshell and return its stdout with trailing newlines
 * removed. Exposed for tests. */
char *command_substitute(struct Shell *sh, const char *src);

#endif
