/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "check.h"
#include "parser.h"

static int parse_src(const char *src, CommandList *list, char *err, size_t errlen) {
    TokenList t;
    const char *lexerr;
    if (lex(src, &t, &lexerr) != 0) {
        token_list_free(&t);
        return -1;
    }
    int rc = parse(&t, list, err, errlen);
    token_list_free(&t);
    return rc;
}

static void test_simple_command(void) {
    CommandList l;
    char err[128];
    CHECK_INT(parse_src("echo a b", &l, err, sizeof err), 0);
    CHECK_INT(l.nitems, 1);
    CHECK_INT(l.items[0].npipes, 1);
    Command *c = &l.items[0].pipes[0].cmds[0];
    CHECK_INT(c->nwords, 3);
    CHECK_STR(c->words[2], "b");
    command_list_free(&l);
}

static void test_pipeline_and_connectors(void) {
    CommandList l;
    char err[128];
    CHECK_INT(parse_src("a | b | c && d || e", &l, err, sizeof err), 0);
    AndOr *ao = &l.items[0];
    CHECK_INT(ao->npipes, 3);
    CHECK_INT(ao->pipes[0].ncmds, 3);
    CHECK_INT(ao->conns[0], CONN_AND);
    CHECK_INT(ao->conns[1], CONN_OR);
    command_list_free(&l);
}

static void test_list_and_background(void) {
    CommandList l;
    char err[128];
    CHECK_INT(parse_src("sleep 1 & echo x; echo y", &l, err, sizeof err), 0);
    CHECK_INT(l.nitems, 3);
    CHECK(l.items[0].background);
    CHECK(!l.items[1].background);
    CHECK_STR(l.items[0].source, "sleep 1");
    command_list_free(&l);
}

static void test_redirections(void) {
    CommandList l;
    char err[128];
    CHECK_INT(parse_src("sort < in > out 2>> log 2>&1", &l, err, sizeof err), 0);
    Command *c = &l.items[0].pipes[0].cmds[0];
    CHECK_INT(c->nwords, 1);
    CHECK_INT(c->nredirs, 4);
    CHECK_INT(c->redirs[0].kind, REDIR_IN);
    CHECK_INT(c->redirs[0].fd, 0);
    CHECK_INT(c->redirs[1].kind, REDIR_OUT);
    CHECK_INT(c->redirs[1].fd, 1);
    CHECK_INT(c->redirs[2].kind, REDIR_APPEND);
    CHECK_INT(c->redirs[2].fd, 2);
    CHECK_INT(c->redirs[3].kind, REDIR_DUP);
    CHECK_STR(c->redirs[3].target, "1");
    command_list_free(&l);
}

static void test_assignments_only_before_name(void) {
    CommandList l;
    char err[128];
    CHECK_INT(parse_src("A=1 B=2 env C=3", &l, err, sizeof err), 0);
    Command *c = &l.items[0].pipes[0].cmds[0];
    CHECK_INT(c->nassigns, 2);
    CHECK_INT(c->nwords, 2);
    CHECK_STR(c->words[1], "C=3");
    command_list_free(&l);
}

static void test_syntax_errors(void) {
    const char *bad[] = {"| a", "a |", "a && ", "a ; ; b", "cat <", "&& a"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CommandList l;
        char err[128] = "";
        CHECK_INT(parse_src(bad[i], &l, err, sizeof err), -1);
        CHECK(strstr(err, "syntax error") != NULL);
        command_list_free(&l);
    }
}

static void test_assignment_word(void) {
    CHECK(is_assignment_word("X=1"));
    CHECK(is_assignment_word("_a9="));
    CHECK(!is_assignment_word("9a=1"));
    CHECK(!is_assignment_word("=1"));
    CHECK(!is_assignment_word("a-b=1"));
}

int main(void) {
    puts("parser");
    RUN(test_simple_command);
    RUN(test_pipeline_and_connectors);
    RUN(test_list_and_background);
    RUN(test_redirections);
    RUN(test_assignments_only_before_name);
    RUN(test_syntax_errors);
    RUN(test_assignment_word);
    return check_summary();
}
