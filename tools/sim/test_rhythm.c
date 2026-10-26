#include "rhythm.h"
#include "sim.h"
#include "test_util.h"

static void test_personality_ranges(void)
{
    for (uint64_t s = 1; s <= 200; s++) {
        sim_reset(s, 0);
        rhythm_init();
        const rhythm_personality_t *p = rhythm_personality();
        CHECK(p->cpm >= 200 && p->cpm <= 320);
        CHECK(p->error_permille >= 15 && p->error_permille <= 40);
        CHECK(p->mouse_pct >= 85 && p->mouse_pct <= 120);
        CHECK(p->pause_pct >= 80 && p->pause_pct <= 125);
    }
}

static void test_sleep(void)
{
    sim_reset(1, 0);
    rhythm_init();
    CHECK(rhythm_sleep(1000));
    CHECK(hid_out_now_ms() == 1000);
}

static void test_pause_aborts_and_releases(void)
{
    size_t n;
    sim_reset(1, 0);
    rhythm_init();
    sim_pause(500, 5000);
    hid_out_key(0, 4);
    hid_out_mouse(1, 10, 10, 0);
    CHECK(!rhythm_sleep(2000));
    CHECK(hid_out_now_ms() <= 520);
    const sim_event_t *e = sim_events(&n);
    CHECK(e[n - 2].kind == 'K' && e[n - 2].code == 0 && e[n - 1].buttons == 0);
    uint32_t t = hid_out_now_ms();
    CHECK(!rhythm_sleep(10) && hid_out_now_ms() == t && rhythm_aborted());
    rhythm_clear_abort();
    hid_out_sleep_ms(5000);
    CHECK(rhythm_sleep(10));
}

static int hook_calls;
static bool hook_mouse(void)
{
    hook_calls++;
    hid_out_mouse(0, 1, 1, 0);
    return true;
}

static void test_idle_guard(void)
{
    size_t n;
    sim_reset(3, 0);
    rhythm_init();
    rhythm_set_maxgap_s(120);
    rhythm_set_idle_hook(hook_mouse);
    hook_calls = 0;
    hid_out_mouse(0, 0, 0, 0);
    CHECK(rhythm_sleep(600000));
    const sim_event_t *e = sim_events(&n);
    for (size_t i = 1; i < n; i++) {
        uint32_t g = e[i].t - e[i - 1].t;
        CHECK(g >= 90000 && g <= 120020);
    }
    CHECK(hook_calls >= 5 && hook_calls <= 7);
}

static bool hook_reenter(void)
{
    hook_calls++;
    return rhythm_sleep(200000);
}

static void test_hook_not_reentrant(void)
{
    sim_reset(4, 0);
    rhythm_init();
    rhythm_set_maxgap_s(100);
    rhythm_set_idle_hook(hook_reenter);
    hook_calls = 0;
    hid_out_mouse(0, 0, 0, 0);
    rhythm_sleep(150000);
    CHECK(hook_calls == 1);
}

static void test_fatigue_bounded(void)
{
    sim_reset(5, 0);
    rhythm_init();
    bool moved = false;
    for (int i = 0; i < 24 * 60; i++) {
        hid_out_sleep_ms(60000);
        uint32_t f = rhythm_fatigue_pct();
        CHECK(f >= 85 && f <= 115);
        moved |= f != 100;
    }
    CHECK(moved);
}

static void test_scale(void)
{
    sim_reset(6, 0);
    rhythm_init();
    CHECK(rhythm_fatigue_pct() == 100);
    CHECK(rhythm_scale(1000, 100) == 1000 && rhythm_scale(1000, 125) == 1250);
}

static void test_rand(void)
{
    sim_reset(8, 0);
    bool lo = false, hi = false;
    for (int i = 0; i < 1000; i++) {
        uint32_t r = rhythm_rand(3, 5);
        CHECK(r >= 3 && r <= 5);
        lo |= r == 3;
        hi |= r == 5;
    }
    CHECK(lo && hi);
    CHECK(rhythm_rand(7, 7) == 7 && rhythm_rand(9, 2) == 9);
}

/* Review Focus 5: uptime past the uint32 ms wrap */
static void test_wrap(void)
{
    size_t n;
    sim_reset(7, 0xFFFFFFFFu - 30000);
    rhythm_init();
    rhythm_set_maxgap_s(120);
    rhythm_set_idle_hook(hook_mouse);
    hid_out_mouse(0, 0, 0, 0);
    rhythm_sleep(300000);
    const sim_event_t *e = sim_events(&n);
    CHECK(n >= 3);
    for (size_t i = 1; i < n; i++) {
        CHECK((uint32_t)(e[i].t - e[i - 1].t) <= 120020);
    }
}

int main(int argc, char **argv)
{
    RUN(test_personality_ranges);
    RUN(test_sleep);
    RUN(test_pause_aborts_and_releases);
    RUN(test_idle_guard);
    RUN(test_hook_not_reentrant);
    RUN(test_fatigue_bounded);
    RUN(test_scale);
    RUN(test_rand);
    RUN(test_wrap);
    return TEST_RESULT();
}
