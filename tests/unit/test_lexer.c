/* msh, written October 2026 by Zaid Siddiqui.
 * From-scratch rewrite of university coursework; see README for the timeline. */

#include "check.h"
#include "lexer.h"

static TokenList lex_ok(const char *src) {
    TokenList t;
    const char *err = NULL;
    CHECK_INT(lex(src, &t, &err), 0);
    return t;
}

static void test_words_and_whitespace(void) {
    TokenList t = lex_ok("  ls   -la\t/tmp ");
    CHECK_INT(t.len, 4);
    CHECK_STR(t.items[0].text, "ls");
    CHECK_STR(t.items[1].text, "-la");
    CHECK_STR(t.items[2].text, "/tmp");
    CHECK_INT(t.items[3].type, TOK_EOF);
    token_list_free(&t);
}

static void test_operators_longest_match(void) {
    TokenList t = lex_ok("a&&b||c|d;e&");
    TokenType want[] = {TOK_WORD, TOK_AND_IF, TOK_WORD, TOK_OR_IF, TOK_WORD, TOK_PIPE,
                        TOK_WORD, TOK_SEMI,   TOK_WORD, TOK_AMP,   TOK_EOF};
    CHECK_INT(t.len, sizeof want / sizeof want[0]);
    for (size_t i = 0; i < t.len; i++) CHECK_INT(t.items[i].type, want[i]);
    token_list_free(&t);
}

static void test_quotes_stay_raw(void) {
    /* The lexer keeps quotes; quote removal is the expander's job. */
    TokenList t = lex_ok("echo 'a | b' \"c ; d\" e\\ f");
    CHECK_INT(t.len, 5);
    CHECK_STR(t.items[1].text, "'a | b'");
    CHECK_STR(t.items[2].text, "\"c ; d\"");
    CHECK_STR(t.items[3].text, "e\\ f");
    token_list_free(&t);
}

static void test_command_substitution_is_one_word(void) {
    TokenList t = lex_ok("echo $(ls | grep \"x)\" ) done");
    CHECK_INT(t.len, 4);
    CHECK_STR(t.items[1].text, "$(ls | grep \"x)\" )");
    CHECK_STR(t.items[2].text, "done");
    token_list_free(&t);
}

static void test_fd_prefix(void) {
    TokenList t = lex_ok("cmd 2>err 2>&1 3 >x");
    CHECK_INT(t.items[1].type, TOK_GREAT);
    CHECK_INT(t.items[1].fd, 2);
    CHECK_INT(t.items[3].type, TOK_GREATAND);
    CHECK_INT(t.items[3].fd, 2);
    /* "3 >x": a space means 3 is an argument, not an fd. */
    CHECK_STR(t.items[5].text, "3");
    CHECK_INT(t.items[6].fd, -1);
    token_list_free(&t);
}

static void test_comment(void) {
    TokenList t = lex_ok("echo hi # ignored | stuff");
    CHECK_INT(t.len, 3);
    token_list_free(&t);
}

static void test_errors(void) {
    const char *bad[] = {"echo 'open", "echo \"open", "echo $(open", "echo ${open"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        TokenList t;
        const char *err = NULL;
        CHECK_INT(lex(bad[i], &t, &err), -1);
        CHECK(err != NULL);
        token_list_free(&t);
    }
}

int main(void) {
    puts("lexer");
    RUN(test_words_and_whitespace);
    RUN(test_operators_longest_match);
    RUN(test_quotes_stay_raw);
    RUN(test_command_substitution_is_one_word);
    RUN(test_fd_prefix);
    RUN(test_comment);
    RUN(test_errors);
    return check_summary();
}
