/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "vars.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "strbuf.h"

#define INITIAL_BUCKETS 64

static uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

void vars_init(VarTable *t) {
    t->nbuckets = INITIAL_BUCKETS;
    t->count = 0;
    t->buckets = calloc(t->nbuckets, sizeof *t->buckets);
    if (!t->buckets) {
        abort();
    }
}

void vars_free(VarTable *t) {
    for (size_t i = 0; i < t->nbuckets; i++) {
        VarEntry *e = t->buckets[i];
        while (e) {
            VarEntry *next = e->next;
            free(e->name);
            free(e->value);
            free(e);
            e = next;
        }
    }
    free(t->buckets);
    t->buckets = NULL;
    t->nbuckets = t->count = 0;
}

static VarEntry *find(const VarTable *t, const char *name) {
    VarEntry *e = t->buckets[fnv1a(name) % t->nbuckets];
    for (; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            return e;
        }
    }
    return NULL;
}

static void rehash(VarTable *t) {
    size_t newn = t->nbuckets * 2;
    VarEntry **nb = calloc(newn, sizeof *nb);
    if (!nb) {
        abort();
    }
    for (size_t i = 0; i < t->nbuckets; i++) {
        VarEntry *e = t->buckets[i];
        while (e) {
            VarEntry *next = e->next;
            size_t idx = fnv1a(e->name) % newn;
            e->next = nb[idx];
            nb[idx] = e;
            e = next;
        }
    }
    free(t->buckets);
    t->buckets = nb;
    t->nbuckets = newn;
}

static VarEntry *find_or_create(VarTable *t, const char *name) {
    VarEntry *e = find(t, name);
    if (e) {
        return e;
    }
    if ((t->count + 1) * 4 > t->nbuckets * 3) {
        rehash(t);
    }
    e = xmalloc(sizeof *e);
    e->name = xstrdup(name);
    e->value = xstrdup("");
    e->exported = false;
    size_t idx = fnv1a(name) % t->nbuckets;
    e->next = t->buckets[idx];
    t->buckets[idx] = e;
    t->count++;
    return e;
}

const char *vars_get(const VarTable *t, const char *name) {
    VarEntry *e = find(t, name);
    return e ? e->value : NULL;
}

void vars_set(VarTable *t, const char *name, const char *value) {
    VarEntry *e = find_or_create(t, name);
    char *copy = xstrdup(value);
    free(e->value);
    e->value = copy;
}

void vars_export(VarTable *t, const char *name) {
    find_or_create(t, name)->exported = true;
}

bool vars_unset(VarTable *t, const char *name) {
    VarEntry **link = &t->buckets[fnv1a(name) % t->nbuckets];
    for (; *link; link = &(*link)->next) {
        if (strcmp((*link)->name, name) == 0) {
            VarEntry *dead = *link;
            *link = dead->next;
            free(dead->name);
            free(dead->value);
            free(dead);
            t->count--;
            return true;
        }
    }
    return false;
}

void vars_import_env(VarTable *t, char **envp) {
    for (; envp && *envp; envp++) {
        const char *eq = strchr(*envp, '=');
        if (!eq) {
            continue;
        }
        char *name = xstrndup(*envp, (size_t)(eq - *envp));
        vars_set(t, name, eq + 1);
        vars_export(t, name);
        free(name);
    }
}

char **vars_build_env(const VarTable *t) {
    char **env = xmalloc((t->count + 1) * sizeof *env);
    size_t n = 0;
    for (size_t i = 0; i < t->nbuckets; i++) {
        for (VarEntry *e = t->buckets[i]; e; e = e->next) {
            if (!e->exported) {
                continue;
            }
            size_t len = strlen(e->name) + strlen(e->value) + 2;
            env[n] = xmalloc(len);
            strcpy(env[n], e->name);
            strcat(env[n], "=");
            strcat(env[n], e->value);
            n++;
        }
    }
    env[n] = NULL;
    return env;
}

void vars_free_env(char **env) {
    for (char **p = env; p && *p; p++) {
        free(*p);
    }
    free(env);
}

void vars_foreach(const VarTable *t, VarVisitor fn, void *ctx) {
    for (size_t i = 0; i < t->nbuckets; i++) {
        for (VarEntry *e = t->buckets[i]; e; e = e->next) {
            fn(e, ctx);
        }
    }
}
