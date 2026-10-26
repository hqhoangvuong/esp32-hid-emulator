#include "invariants.h"
#include "keys.h"
#include "test_util.h"

static inv_config_t cfg(void)
{
    inv_config_t c = {1920, 1080, {40, 110, 1880, 1010}, {{440, 115, 1780, 990}}, 1, 240000, 360000};
    return c;
}

static void start(void)
{
    inv_config_t c = cfg();
    inv_init(&c);
}

static void key(uint32_t t, uint8_t m, uint8_t k)
{
    sim_event_t e = {t, 'K', m, k, 0, 0, 0, 0};
    inv_event(&e);
}

static void mouse(uint32_t t, uint8_t b, int32_t x, int32_t y)
{
    sim_event_t e = {t, 'M', 0, 0, b, pointer_px_to_units(x, 1920), pointer_px_to_units(y, 1080), 0};
    inv_event(&e);
}

static void pick(script_trace_t kind, const char *name)
{
    script_event_t ev = {kind, name, strlen(name), 1, 0};
    inv_trace(&ev);
}

static void alt_tab(uint32_t t)
{
    key(t, KMOD_ALT, 0);
    key(t + 100, KMOD_ALT, KEY_TAB);
    key(t + 180, KMOD_ALT, 0);
    key(t + 300, 0, 0);
}

static void test_clean(void)
{
    start();
    mouse(0, 0, 500, 500);
    key(100, 0, 4);
    key(180, 0, 0);
    pick(TRACE_PICK, "write_code");
    pick(TRACE_PICK_END, "write_code");
    CHECK(inv_finish(NULL) == 0);
}

static void test_gap(void)
{
    start();
    key(0, 0, 4);
    key(80, 0, 0);
    key(240081, 0, 4);
    key(240150, 0, 0);
    CHECK(inv_finish(NULL) == 1);
}

static void test_gap_excludes_pause(void)
{
    start();
    inv_pause(1000, 100000);
    key(0, 0, 4);
    key(80, 0, 0);
    key(300000, 0, 4);
    key(300080, 0, 0);
    CHECK(inv_finish(NULL) == 0);
}

static void test_outside_safe(void)
{
    start();
    mouse(0, 0, 10, 10);
    CHECK(inv_finish(NULL) == 1);
}

static void test_press_outside_regions(void)
{
    start();
    mouse(0, 1, 100, 500); /* inside the safe box, outside every region */
    mouse(80, 0, 100, 500);
    CHECK(inv_finish(NULL) == 1);
}

static void test_stuck_key(void)
{
    start();
    key(0, 0, 4);
    key(1500, 0, 0);
    CHECK(inv_finish(NULL) == 1);
}

static void test_click_in_chrome(void)
{
    start();
    alt_tab(0);
    mouse(400, 1, 500, 500);
    mouse(480, 0, 500, 500);
    CHECK(inv_finish(NULL) == 1);
}

static void test_search_spacing(void)
{
    start();
    alt_tab(0);
    key(1000, KMOD_CTRL, 0x17);
    key(1080, KMOD_CTRL, 0);
    key(1100, 0, 0);
    key(200000, KMOD_CTRL, 0x17);
    key(200080, KMOD_CTRL, 0);
    key(200100, 0, 0);
    CHECK(inv_finish(NULL) == 1);
}

static void test_pick_state(void)
{
    start();
    key(0, KMOD_CTRL, 0);
    key(50, KMOD_CTRL, KEY_GRAVE);
    key(120, KMOD_CTRL, 0);
    key(150, 0, 0);
    pick(TRACE_PICK_END, "terminal_check");
    CHECK(inv_finish(NULL) == 1);
}

static void test_pick_held_key(void)
{
    start();
    key(0, 0, 4);
    pick(TRACE_PICK, "write_code");
    key(80, 0, 0);
    CHECK(inv_finish(NULL) == 1);
}

static void test_repeat(void)
{
    start();
    pick(TRACE_PICK, "write_code");
    pick(TRACE_PICK_END, "write_code");
    pick(TRACE_PICK, "write_code");
    pick(TRACE_PICK_END, "write_code");
    CHECK(inv_finish(NULL) == 1);
}

static void test_snippet_repeat(void)
{
    start();
    script_event_t a = {TRACE_SNIPPET, "types", 5, 1, 3};
    inv_trace(&a);
    inv_trace(&a);
    CHECK(inv_finish(NULL) == 1);
}

/* a one-item pool (the skeleton) cannot avoid repeating */
static void test_single_item_pool_exempt(void)
{
    const char *dev = "DEF MAIN\nEND\n", *snip = "POOL one\nnamespace Scratch;\nEND\n";
    sim_reset(1, 0);
    script_load(dev, strlen(dev), snip, strlen(snip));
    start();
    script_event_t a = {TRACE_SNIPPET, "one", 3, 1, 0};
    inv_trace(&a);
    inv_trace(&a);
    CHECK(inv_finish(NULL) == 0);
}

static void test_pause_release(void)
{
    start();
    inv_pause(1000, 5000);
    key(900, 0, 4);
    key(1200, 0, 0);
    CHECK(inv_finish(NULL) == 1);
}

static void test_press_during_pause(void)
{
    start();
    inv_pause(1000, 5000);
    key(2000, 0, 4);
    key(2050, 0, 0);
    CHECK(inv_finish(NULL) >= 1);
}

static void test_mix_counts(void)
{
    start();
    pick(TRACE_PICK, "a");
    pick(TRACE_PICK_END, "a");
    pick(TRACE_PICK, "b");
    pick(TRACE_PICK_END, "b");
    pick(TRACE_PICK, "a");
    pick(TRACE_PICK_END, "a");
    CHECK(inv_pick_total() == 3 && inv_pick_count("a", 1) == 2 && inv_pick_count("b", 1) == 1);
    CHECK(inv_finish(NULL) == 0);
}

int main(int argc, char **argv)
{
    RUN(test_clean);
    RUN(test_gap);
    RUN(test_gap_excludes_pause);
    RUN(test_outside_safe);
    RUN(test_press_outside_regions);
    RUN(test_stuck_key);
    RUN(test_click_in_chrome);
    RUN(test_search_spacing);
    RUN(test_pick_state);
    RUN(test_pick_held_key);
    RUN(test_repeat);
    RUN(test_snippet_repeat);
    RUN(test_single_item_pool_exempt);
    RUN(test_pause_release);
    RUN(test_press_during_pause);
    RUN(test_mix_counts);
    return TEST_RESULT();
}
