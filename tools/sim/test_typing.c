#include "editor_model.h"
#include "keys.h"
#include "rhythm.h"
#include "sim.h"
#include "test_util.h"
#include "typing.h"

static void feed_all(void)
{
    size_t n;
    const sim_event_t *e = sim_events(&n);
    ed_reset();
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == 'K') {
            ed_feed(&e[i]);
        }
    }
}

static void setup(uint64_t seed, uint16_t err)
{
    sim_reset(seed, 0);
    rhythm_init();
    rhythm_personality_t p = {240, err, 100, 100};
    rhythm_set_personality(&p);
}

static const char *FOX = "the quick brown fox jumps over the lazy dog";

static void test_nominal(void)
{
    uint32_t sf = typing_nominal_ms('f', 'r', 240), sh = typing_nominal_ms('a', 's', 240);
    uint32_t alt = typing_nominal_ms('f', 'j', 240);
    CHECK(sf == 337 && sh == 250 && alt == 212);
    CHECK(typing_nominal_ms('j', 'F', 240) == 265);
}

static void test_exact_no_errors(void)
{
    setup(1, 0);
    CHECK(typing_text(FOX, strlen(FOX), 0));
    feed_all();
    CHECK(strcmp(ed_text(), FOX) == 0);
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(e[n - 1].code == 0 && e[n - 1].mod == 0);
}

static void test_errors_corrected(void)
{
    int backspaces = 0;
    for (uint64_t s = 1; s <= 50; s++) {
        setup(s, 300);
        CHECK(typing_text(FOX, strlen(FOX), 0));
        feed_all();
        CHECK(strcmp(ed_text(), FOX) == 0);
        size_t n;
        const sim_event_t *e = sim_events(&n);
        for (size_t i = 0; i < n; i++) {
            backspaces += e[i].code == KEY_BACKSPACE;
            CHECK(e[i].code != KEY_LEFT);
        }
    }
    CHECK(backspaces > 0);
}

static void test_arrow_fix(void)
{
    int lefts = 0;
    for (uint64_t s = 1; s <= 50; s++) {
        setup(s, 300);
        CHECK(typing_text(FOX, strlen(FOX), TYPE_ARROW_FIX));
        feed_all();
        CHECK(strcmp(ed_text(), FOX) == 0);
        size_t n;
        const sim_event_t *e = sim_events(&n);
        for (size_t i = 0; i < n; i++) {
            lefts += e[i].code == KEY_LEFT;
        }
    }
    CHECK(lefts > 0);
}

static void test_omnibox_delete_first(void)
{
    for (uint64_t s = 1; s <= 30; s++) {
        setup(s, 300);
        typing_text(FOX, strlen(FOX), TYPE_OMNIBOX);
        size_t n;
        const sim_event_t *e = sim_events(&n);
        bool armed = false;
        uint8_t prev = 0;
        for (size_t i = 0; i < n; i++) {
            if (e[i].code == prev) {
                continue;
            }
            prev = e[i].code;
            if (e[i].code == KEY_DELETE) {
                armed = true;
            } else if (e[i].code == KEY_BACKSPACE) {
                CHECK(armed);
            } else if (e[i].code) {
                armed = false;
            }
        }
    }
}

static void test_multiline(void)
{
    const char *two = "first line\nsecond";
    for (uint64_t s = 1; s <= 20; s++) {
        setup(s, 300);
        CHECK(typing_text(two, strlen(two), TYPE_ARROW_FIX));
        feed_all();
        CHECK(strcmp(ed_text(), two) == 0);
    }
}

static void test_speed(void)
{
    char buf[401];
    for (int i = 0; i < 400; i++) {
        buf[i] = FOX[i % strlen(FOX)];
    }
    buf[400] = 0;
    setup(9, 0);
    typing_text(buf, 400, 0);
    uint32_t cpm = 400u * 60000u / hid_out_now_ms();
    printf("  cpm %u\n", (unsigned)cpm);
    CHECK(cpm >= 150 && cpm <= 330);
}

static void test_key_combo(void)
{
    setup(2, 0);
    bool pressed = false;
    CHECK(typing_key(KMOD_CTRL, 0x16, &pressed));
    CHECK(pressed);
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 4);
    CHECK(e[0].mod == KMOD_CTRL && e[0].code == 0 && e[1].code == 0x16 && e[2].code == 0 && e[3].mod == 0);
}

static void test_key_abort_before_press(void)
{
    setup(3, 0);
    sim_pause(1, 100000);
    bool pressed = false;
    CHECK(!typing_key(KMOD_CTRL, 0x16, &pressed));
    CHECK(!pressed);
}

static void test_alt_tab(void)
{
    setup(4, 0);
    bool tab = false;
    CHECK(typing_alt_tab(&tab));
    CHECK(tab);
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 4 && e[1].mod == KMOD_ALT && e[1].code == KEY_TAB && e[3].mod == 0);
}

int main(int argc, char **argv)
{
    RUN(test_nominal);
    RUN(test_exact_no_errors);
    RUN(test_errors_corrected);
    RUN(test_arrow_fix);
    RUN(test_omnibox_delete_first);
    RUN(test_multiline);
    RUN(test_speed);
    RUN(test_key_combo);
    RUN(test_key_abort_before_press);
    RUN(test_alt_tab);
    return TEST_RESULT();
}
