/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_BUILTINS_H
#define MSH_BUILTINS_H

struct Shell;

typedef int (*BuiltinFn)(struct Shell *sh, int argc, char **argv);

typedef struct {
    const char *name;
    BuiltinFn fn;
    const char *usage;
} Builtin;

/* NULL if name isn't a builtin. */
const Builtin *builtin_lookup(const char *name);

/* Search $PATH for an executable. Returns a malloc'd path or NULL.
 * Names containing '/' are returned as-is if they exist. */
char *resolve_command(struct Shell *sh, const char *name);

#endif
