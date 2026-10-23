/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include <stdio.h>
#include <stdlib.h>

#include "check.h"
#include "vars.h"

static void test_set_get_overwrite(void) {
    VarTable t;
    vars_init(&t);
    CHECK(vars_get(&t, "X") == NULL);
    vars_set(&t, "X", "1");
    vars_set(&t, "X", "2");
    CHECK_STR(vars_get(&t, "X"), "2");
    CHECK_INT(t.count, 1);
    vars_free(&t);
}

static void test_unset(void) {
    VarTable t;
    vars_init(&t);
    vars_set(&t, "A", "x");
    CHECK(vars_unset(&t, "A"));
    CHECK(!vars_unset(&t, "A"));
    CHECK(vars_get(&t, "A") == NULL);
    CHECK_INT(t.count, 0);
    vars_free(&t);
}

static void test_grows_past_initial_buckets(void) {
    VarTable t;
    vars_init(&t);
    size_t initial = t.nbuckets;
    char name[32], value[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof name, "VAR_%d", i);
        snprintf(value, sizeof value, "%d", i * 7);
        vars_set(&t, name, value);
    }
    CHECK(t.nbuckets > initial);
    CHECK_INT(t.count, 1000);
    int bad = 0;
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof name, "VAR_%d", i);
        const char *v = vars_get(&t, name);
        if (!v || atoi(v) != i * 7) bad++;
    }
    CHECK_INT(bad, 0);
    vars_free(&t);
}

static void test_env_only_has_exported(void) {
    VarTable t;
    vars_init(&t);
    char *envp[] = {"HOME=/home/me", "EMPTY=", NULL};
    vars_import_env(&t, envp);
    vars_set(&t, "LOCAL", "secret");
    vars_set(&t, "SHARED", "yes");
    vars_export(&t, "SHARED");

    char **env = vars_build_env(&t);
    int n = 0, saw_local = 0, saw_shared = 0;
    for (char **e = env; *e; e++, n++) {
        if (strcmp(*e, "LOCAL=secret") == 0) saw_local = 1;
        if (strcmp(*e, "SHARED=yes") == 0) saw_shared = 1;
    }
    CHECK_INT(n, 3);
    CHECK(!saw_local);
    CHECK(saw_shared);
    CHECK_STR(vars_get(&t, "EMPTY"), "");
    vars_free_env(env);
    vars_free(&t);
}

int main(void) {
    puts("vars");
    RUN(test_set_get_overwrite);
    RUN(test_unset);
    RUN(test_grows_past_initial_buckets);
    RUN(test_env_only_has_exported);
    return check_summary();
}
