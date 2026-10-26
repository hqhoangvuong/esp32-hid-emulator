#include "sim.h"
#include "test_util.h"

static void test_clock(void)
{
    sim_reset(1, 0);
    hid_out_sleep_ms(250);
    CHECK(hid_out_now_ms() == 250);
}

static void test_rand_deterministic(void)
{
    uint32_t a[5], b[5];
    sim_reset(7, 0);
    for (int i = 0; i < 5; i++) {
        a[i] = hid_out_rand32();
    }
    sim_reset(7, 0);
    for (int i = 0; i < 5; i++) {
        b[i] = hid_out_rand32();
    }
    CHECK(memcmp(a, b, sizeof a) == 0);
    sim_reset(8, 0);
    CHECK(hid_out_rand32() != a[0]);
}

static void test_event_log(void)
{
    size_t n;
    sim_reset(1, 0);
    hid_out_sleep_ms(10);
    hid_out_key(0x02, 0x04);
    hid_out_mouse(1, 100, 200, -1);
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 2 && e[0].kind == 'K' && e[0].mod == 2 && e[0].code == 4 && e[0].t == 10);
    CHECK(e[1].kind == 'M' && e[1].buttons == 1 && e[1].x == 100 && e[1].y == 200 && e[1].wheel == -1);
    CHECK(hid_out_last_report_ms() == 10);
}

static void test_release_all(void)
{
    size_t n;
    sim_reset(1, 0);
    hid_out_mouse(1, 100, 200, 0);
    hid_out_key(0, 4);
    hid_out_release_all();
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 4 && e[2].kind == 'K' && e[2].code == 0 && e[2].mod == 0);
    CHECK(e[3].kind == 'M' && e[3].buttons == 0 && e[3].x == 100 && e[3].y == 200 && e[3].wheel == 0);
}

static void test_pause_window(void)
{
    sim_reset(1, 0);
    sim_pause(1000, 2000);
    hid_out_sleep_ms(999);
    CHECK(!hid_out_paused());
    hid_out_sleep_ms(1);
    CHECK(hid_out_paused());
    hid_out_sleep_ms(999);
    CHECK(hid_out_paused());
    hid_out_sleep_ms(1);
    CHECK(!hid_out_paused());
}

static void test_wrap_start(void)
{
    sim_reset(1, 0xFFFFFFF0u);
    hid_out_sleep_ms(32);
    CHECK(hid_out_now_ms() == 0x10);
}

static int sunk;
static void sink(const sim_event_t *e, void *ctx)
{
    (void)e;
    *(int *)ctx += 1;
}

static void test_sink(void)
{
    size_t n;
    sim_reset(1, 0);
    sunk = 0;
    sim_set_sink(sink, &sunk);
    hid_out_key(0, 4);
    hid_out_mouse(0, 1, 1, 0);
    sim_events(&n);
    CHECK(sunk == 2 && n == 0);
    sim_reset(1, 0);
    hid_out_key(0, 4);
    sim_events(&n);
    CHECK(sunk == 2 && n == 1);
}

static void test_log_count(void)
{
    sim_reset(1, 0);
    hid_out_log("unknown key '%s'", "X");
    CHECK(sim_log_count("unknown key") == 1);
    CHECK(sim_log_count("missing") == 0);
    sim_reset(1, 0);
    CHECK(sim_log_count("unknown key") == 0);
}

int main(int argc, char **argv)
{
    RUN(test_clock);
    RUN(test_rand_deterministic);
    RUN(test_event_log);
    RUN(test_release_all);
    RUN(test_pause_window);
    RUN(test_wrap_start);
    RUN(test_sink);
    RUN(test_log_count);
    return TEST_RESULT();
}
