/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include <stdlib.h>
#include <unistd.h>

#include "check.h"
#include "expand.h"
#include "shell.h"

static Shell sh;

static void expect(const char *raw, const char *want) {
    char *got = expand_word(&sh, raw);
    CHECK_STR(got, want);
    if (strcmp(got, want) != 0) fprintf(stderr, "    (input: %s)\n", raw);
    free(got);
}

static void test_quote_removal(void) {
    expect("'a b'", "a b");
    expect("\"a b\"", "a b");
    expect("a\\ b", "a b");
    expect("'$NAME'", "$NAME");
    expect("\"\\$NAME\"", "$NAME");
    expect("\"it's\"", "it's");
    expect("pre'mid'\"post\"", "premidpost");
}

static void test_parameters(void) {
    vars_set(&sh.vars, "NAME", "world");
    expect("$NAME", "world");
    expect("${NAME}s", "worlds");
    expect("\"hi $NAME!\"", "hi world!");
    expect("$UNSET_THING", "");
    expect("${UNSET_THING:-fallback}", "fallback");
    expect("${NAME:-fallback}", "world");
    expect("$", "$");
    expect("cost: $5", "cost: ");
}

static void test_special_parameters(void) {
    sh.last_status = 42;
    expect("$?", "42");
    char *params[] = {"one", "two"};
    shell_set_params(&sh, params, 2);
    expect("$#", "2");
    expect("$1-$2-$3", "one-two-");
    expect("\"$@\"", "one two");
}

static void test_tilde(void) {
    vars_set(&sh.vars, "HOME", "/home/me");
    expect("~", "/home/me");
    expect("~/x", "/home/me/x");
    expect("a~", "a~");
    expect("'~'", "~");
}

static void test_command_substitution(void) {
    expect("$(echo hi)", "hi");
    expect("\"[$(echo a; echo b)]\"", "[a\nb]"); /* trailing newline trimmed */
    expect("$(echo $(echo nested))", "nested");
}

static void test_glob(void) {
    char dir[] = "/tmp/msh_glob_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[128];
    const char *names[] = {"b.txt", "a.txt", "c.log"};
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        fclose(fopen(path, "w"));
    }
    CHECK_INT(chdir(dir), 0);

    char *words[] = {"*.txt", "'*.txt'", "*.none"};
    size_t argc;
    char **argv = expand_words(&sh, words, 3, &argc);
    CHECK_INT(argc, 4);
    CHECK_STR(argv[0], "a.txt"); /* glob() sorts */
    CHECK_STR(argv[1], "b.txt");
    CHECK_STR(argv[2], "*.txt"); /* quoted: literal */
    CHECK_STR(argv[3], "*.none"); /* no match: left alone */
    free_argv(argv);

    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        unlink(path);
    }
    CHECK_INT(chdir("/"), 0);
    rmdir(dir);
}

int main(void) {
    extern char **environ;
    shell_init(&sh, "msh", environ);
    puts("expand");
    RUN(test_quote_removal);
    RUN(test_parameters);
    RUN(test_special_parameters);
    RUN(test_tilde);
    RUN(test_command_substitution);
    RUN(test_glob);
    shell_destroy(&sh);
    return check_summary();
}
