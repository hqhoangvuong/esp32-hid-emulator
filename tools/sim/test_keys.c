#include "keys.h"
#include "test_util.h"

static void expect(char c, uint8_t m, uint8_t k)
{
    uint8_t mm = 0xFF, kk = 0xFF;
    bool ok = keys_ascii(c, &mm, &kk);
    CHECK(ok && mm == m && kk == k);
    CHECK(keys_to_ascii(m, k) == c);
    if (!ok || mm != m || kk != k) {
        printf("  char 0x%02x -> mod %02x code %02x\n", (unsigned char)c, mm, kk);
    }
}

static void test_ascii(void)
{
    expect('a', 0, 0x04);
    expect('A', KMOD_SHIFT, 0x04);
    expect('1', 0, 0x1E);
    expect('!', KMOD_SHIFT, 0x1E);
    expect('0', 0, 0x27);
    expect(')', KMOD_SHIFT, 0x27);
    expect('\n', 0, 0x28);
    expect(' ', 0, 0x2C);
    expect('{', KMOD_SHIFT, 0x2F);
    expect('"', KMOD_SHIFT, 0x34);
    expect('`', 0, 0x35);
    expect('~', KMOD_SHIFT, 0x35);
    expect('<', KMOD_SHIFT, 0x36);
    expect('?', KMOD_SHIFT, 0x38);
    expect('|', KMOD_SHIFT, 0x31);
    expect('\t', 0, 0x2B);
    uint8_t m, k;
    CHECK(!keys_ascii('\x7f', &m, &k));
    CHECK(!keys_ascii("\xe9"[0], &m, &k));
}

static bool combo(const char *s, uint8_t m, uint8_t k)
{
    uint8_t mm, kk;
    return keys_parse_combo(s, strlen(s), &mm, &kk) && mm == m && kk == k;
}

static void test_combo(void)
{
    CHECK(combo("CTRL+S", KMOD_CTRL, 0x16));
    CHECK(combo("shift+alt+f", KMOD_SHIFT | KMOD_ALT, 0x09));
    CHECK(combo("F12", 0, 0x45));
    CHECK(combo("CTRL+GRAVE", KMOD_CTRL, KEY_GRAVE));
    CHECK(combo("CTRL+SLASH", KMOD_CTRL, KEY_SLASH));
    CHECK(combo("ALT+LEFT", KMOD_ALT, KEY_LEFT));
    CHECK(combo("CTRL+1", KMOD_CTRL, KEY_1));
    CHECK(combo("CTRL+0", KMOD_CTRL, KEY_0));
    uint8_t m, k;
    const char *bad[] = {"BOGUS", "CTRL+", "F13", "CTRL", "CTRL+A+B", ""};
    for (size_t i = 0; i < 6; i++) {
        CHECK(!keys_parse_combo(bad[i], strlen(bad[i]), &m, &k));
    }
    char buf[] = "CTRL+S";
    keys_parse_combo(buf, 6, &m, &k);
    CHECK(strcmp(buf, "CTRL+S") == 0);
    /* the length bounds the token, not a NUL */
    CHECK(keys_parse_combo("CTRL+S trailing", 6, &m, &k) && m == KMOD_CTRL && k == 0x16);
}

static void test_neighbour(void)
{
    CHECK(keys_neighbour('q', 0) == 'w' && keys_neighbour('q', 1) == 'w');
    CHECK(keys_neighbour('p', 0) == 'o');
    char g0 = keys_neighbour('g', 0), g1 = keys_neighbour('g', 1);
    CHECK(g0 != g1 && (g0 == 'f' || g0 == 'h') && (g1 == 'f' || g1 == 'h'));
    char G = keys_neighbour('G', 0);
    CHECK(G == 'F' || G == 'H');
    CHECK(keys_neighbour('5', 0) == 0);
}

static void test_finger(void)
{
    CHECK(keys_finger('f') == 3 && keys_finger('r') == 3 && keys_finger('v') == 3 && keys_finger('j') == 4);
    CHECK(keys_finger('F') == 3 && keys_finger('{') == 7 && keys_finger('#') == 2 && keys_finger(' ') == -1);
}

int main(int argc, char **argv)
{
    RUN(test_ascii);
    RUN(test_combo);
    RUN(test_neighbour);
    RUN(test_finger);
    return TEST_RESULT();
}
