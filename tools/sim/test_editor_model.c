#include "editor_model.h"
#include "keys.h"
#include "sim.h"
#include "test_util.h"

static void key(uint8_t m, uint8_t k)
{
    sim_event_t d = {0, 'K', m, k, 0, 0, 0, 0}, u = {0, 'K', 0, 0, 0, 0, 0, 0};
    ed_feed(&d);
    ed_feed(&u);
}

static void type(const char *s)
{
    for (; *s; s++) {
        uint8_t m, k;
        keys_ascii(*s, &m, &k);
        key(m, k);
    }
}

static void test_plain(void)
{
    ed_reset();
    type("abc");
    CHECK(strcmp(ed_text(), "abc") == 0);
}

static void test_overtype(void)
{
    ed_reset();
    type("f(x)");
    CHECK(strcmp(ed_text(), "f(x)") == 0);
}

static void test_quote_in_comment(void)
{
    ed_reset();
    type("// \"a\"");
    CHECK(strcmp(ed_text(), "// \"a\"") == 0);
}

static void test_quote_in_code(void)
{
    ed_reset();
    type("s = \"a\";");
    CHECK(strcmp(ed_text(), "s = \"a\";") == 0);
}

static void test_brace_enter(void)
{
    ed_reset();
    type("c {\n");
    int l, c;
    ed_cursor(&l, &c);
    CHECK(strcmp(ed_text(), "c {\n\n}") == 0 && l == 1 && c == 0);
}

static void test_pair_backspace(void)
{
    ed_reset();
    type("(");
    key(0, KEY_BACKSPACE);
    CHECK(strcmp(ed_text(), "") == 0);
}

static void test_tracking_lost(void)
{
    ed_reset();
    type("(");
    key(0, KEY_LEFT);
    key(0, KEY_RIGHT);
    type(")");
    CHECK(strcmp(ed_text(), "())") == 0);
}

static void test_down_end(void)
{
    ed_reset();
    type("a {\nx");
    key(0, KEY_DOWN);
    key(0, KEY_END);
    type("\n");
    CHECK(strcmp(ed_text(), "a {\nx\n}\n") == 0);
}

static void test_no_autoclose_before_word(void)
{
    ed_reset();
    type("x");
    key(0, KEY_HOME);
    type("(");
    CHECK(strcmp(ed_text(), "(x") == 0);
}

static void test_delete_and_ctrl_ignored(void)
{
    ed_reset();
    type("ab");
    key(0, KEY_LEFT);
    key(0, KEY_DELETE);
    key(KMOD_CTRL, 0x16); /* Ctrl+S does not type */
    CHECK(strcmp(ed_text(), "a") == 0);
}

int main(int argc, char **argv)
{
    RUN(test_plain);
    RUN(test_overtype);
    RUN(test_quote_in_comment);
    RUN(test_quote_in_code);
    RUN(test_brace_enter);
    RUN(test_pair_backspace);
    RUN(test_tracking_lost);
    RUN(test_down_end);
    RUN(test_no_autoclose_before_word);
    RUN(test_delete_and_ctrl_ignored);
    return TEST_RESULT();
}
