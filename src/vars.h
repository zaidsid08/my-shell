/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#ifndef MSH_VARS_H
#define MSH_VARS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Shell variable store: a hash table (FNV-1a, separate chaining) that
 * doubles its bucket array when the load factor passes 0.75.
 * Exported variables are passed to child processes as the environment.
 */

typedef struct VarEntry {
    char *name;
    char *value;
    bool exported;
    struct VarEntry *next;
} VarEntry;

typedef struct {
    VarEntry **buckets;
    size_t nbuckets;
    size_t count;
} VarTable;

void vars_init(VarTable *t);
void vars_free(VarTable *t);

/* Import every NAME=value from envp as an exported variable. */
void vars_import_env(VarTable *t, char **envp);

const char *vars_get(const VarTable *t, const char *name);
void vars_set(VarTable *t, const char *name, const char *value);
void vars_export(VarTable *t, const char *name);
bool vars_unset(VarTable *t, const char *name);

/* NULL-terminated "NAME=value" array of exported vars; free with
 * vars_free_env. */
char **vars_build_env(const VarTable *t);
void vars_free_env(char **env);

/* Visit every variable (unordered). */
typedef void (*VarVisitor)(const VarEntry *e, void *ctx);
void vars_foreach(const VarTable *t, VarVisitor fn, void *ctx);

#endif
