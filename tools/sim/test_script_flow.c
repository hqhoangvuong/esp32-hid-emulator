#include "rhythm.h"
#include "script.h"
#include "sim.h"
#include "strutil.h"
#include "test_util.h"

#define MAX_TR 100000
static script_event_t tr[MAX_TR];
static int ntr;

static void on_trace(const script_event_t *e, void *ctx)
{
    (void)ctx;
    CHECK(ntr < MAX_TR);
    if (ntr < MAX_TR) {
        tr[ntr++] = *e;
    }
}

static bool same(const script_event_t *a, const script_event_t *b)
{
    return a->len == b->len && memcmp(a->name, b->name, a->len) == 0;
}

static void load(uint64_t seed, const char *dev)
{
    sim_reset(seed, 0);
    rhythm_init();
    rhythm_personality_t p = {240, 0, 100, 100};
    rhythm_set_personality(&p);
    ntr = 0;
    script_set_trace(on_trace, NULL);
    CHECK(script_load(dev, strlen(dev), "", 0));
}

static int count(script_trace_t k, const char *name)
{
    int c = 0;
    for (int i = 0; i < ntr; i++) {
        c += tr[i].kind == k && str_ieq(tr[i].name, tr[i].len, name);
    }
    return c;
}

static void test_choose_alternates(void)
{
    load(1, "DEF MAIN\nREPEAT 1000 1000\nCHOOSE\n1 a\n1 b\nEND\nEND\nEND\nDEF a\nEND\nDEF b\nEND\n");
    script_call("MAIN");
    const script_event_t *prev = NULL;
    int picks = 0;
    for (int i = 0; i < ntr; i++) {
        if (tr[i].kind == TRACE_PICK) {
            if (prev) {
                CHECK(!same(&tr[i], prev));
            }
            prev = &tr[i];
            picks++;
        }
    }
    CHECK(picks == 1000);
}

static void test_choose_compensated(void)
{
    load(2, "DEF MAIN\nREPEAT 20000 20000\nCHOOSE\n40 a\n12 b\n13 c\n8 d\n15 e\n12 f\nEND\nEND\nEND\n"
            "DEF a\nEND\nDEF b\nEND\nDEF c\nEND\nDEF d\nEND\nDEF e\nEND\nDEF f\nEND\n");
    script_call("MAIN");
    const char *names[] = {"a", "b", "c", "d", "e", "f"};
    int w[] = {40, 12, 13, 8, 15, 12};
    for (int i = 0; i < 6; i++) {
        int pct = count(TRACE_PICK, names[i]) * 100 / 20000;
        CHECK(pct >= w[i] - 2 && pct <= w[i] + 2);
        printf("  %s %d%% (weight %d)\n", names[i], pct, w[i]);
    }
    for (int i = 0, last = -1; i < ntr; i++) {
        if (tr[i].kind == TRACE_PICK) {
            CHECK(last < 0 || !same(&tr[i], &tr[last]));
            last = i;
        }
    }
}

static void test_pick_trace_depth(void)
{
    load(3, "DEF MAIN\nCHOOSE\n1 a\nEND\nEND\nDEF a\nCALL b\nEND\nDEF b\nEND\n");
    script_call("MAIN");
    CHECK(ntr == 3);
    CHECK(tr[0].kind == TRACE_PICK && tr[0].depth == 1 && str_ieq(tr[0].name, tr[0].len, "a"));
    CHECK(tr[1].kind == TRACE_CALL && tr[1].depth == 3 && str_ieq(tr[1].name, tr[1].len, "b"));
    CHECK(tr[2].kind == TRACE_PICK_END && tr[2].depth == 1);
}

static void test_maybe_repeat_when(void)
{
    load(3, "DEF MAIN\nREPEAT 2000 2000\nMAYBE 30\nCALL hit\nEND\nEND\nEND\nDEF hit\nEND\n");
    script_call("MAIN");
    int h = count(TRACE_CALL, "hit");
    CHECK(h >= 520 && h <= 680);
    load(4, "DEF MAIN\nREPEAT 300 300\nCALL r\nEND\nEND\nDEF r\nREPEAT 2 4\nCALL x\nEND\nEND\nDEF x\nEND\n");
    script_call("MAIN");
    int x = count(TRACE_CALL, "x");
    CHECK(x > 600 && x < 1200);
    load(5, "DEF MAIN\nLINES 5\nWHEN LINES > 3\nCALL yes\nEND\nLINES 2\nLINES +1\nWHEN LINES > 3\nCALL no\nEND\nEND\n"
            "DEF yes\nEND\nDEF no\nEND\n");
    script_call("MAIN");
    CHECK(count(TRACE_CALL, "yes") == 1 && count(TRACE_CALL, "no") == 0 && script_state()->lines == 3);
}

static void test_idle_and_wait(void)
{
    rhythm_personality_t slow = {240, 0, 100, 125};
    load(6, "DEF MAIN\nIDLE 1000 1000\nEND\n");
    script_call("MAIN");
    CHECK(hid_out_now_ms() == 1000);
    load(7, "DEF MAIN\nWAIT 500\nEND\n");
    rhythm_set_personality(&slow);
    script_call("MAIN");
    CHECK(hid_out_now_ms() == 500);
    load(8, "DEF MAIN\nIDLE 1000 1000\nEND\n");
    rhythm_set_personality(&slow);
    script_call("MAIN");
    CHECK(hid_out_now_ms() == 1250);
}

static void test_depth_limit(void)
{
    load(9, "DEF MAIN\nCALL d2\nEND\nDEF d2\nCALL d3\nEND\nDEF d3\nCALL d4\nEND\nDEF d4\nCALL d5\nEND\nDEF d5\nCALL d6\nEND\n"
            "DEF d6\nCALL d7\nEND\nDEF d7\nCALL d8\nEND\nDEF d8\nCALL d9\nEND\nDEF d9\nEND\n");
    script_call("MAIN");
    CHECK(count(TRACE_CALL, "d8") == 1);
    CHECK(count(TRACE_CALL, "d9") == 0 && sim_log_count("call depth") == 1);
}

static void test_block_depth_limit(void)
{
    load(10, "DEF MAIN\nMAYBE 100\nMAYBE 100\nMAYBE 100\nMAYBE 100\nMAYBE 100\nMAYBE 100\nMAYBE 100\nMAYBE 100\n"
             "MAYBE 100\nCALL deep\nEND\nEND\nEND\nEND\nEND\nEND\nEND\nEND\nEND\nCALL after\nEND\nDEF deep\nEND\nDEF after\nEND\n");
    script_call("MAIN");
    CHECK(count(TRACE_CALL, "deep") == 0 && count(TRACE_CALL, "after") == 1 && sim_log_count("block depth") == 1);
}

static uint32_t stop_at;
static bool until(void)
{
    return hid_out_now_ms() < stop_at;
}

static void test_run_boot_and_loop(void)
{
    load(10, "REGION editor +440 +115 -140 -90\nDEF MAIN\nCALL tick\nWAIT 1000\nLOOP\nEND\nDEF tick\nEND\n");
    stop_at = 30000;
    script_run(until);
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(n >= 1 && e[0].kind == 'M' && e[0].t >= 10000 && e[0].t <= 15000);
    CHECK(count(TRACE_CALL, "tick") >= 15);
}

static void test_pause_resume(void)
{
    load(11, "REGION editor +440 +115 -140 -90\nDEF MAIN\nCALL work\nLOOP\nEND\nDEF work\nWAIT 10000\nEND\nDEF RESUME\nEND\n");
    sim_pause(20000, 25000);
    stop_at = 40000;
    script_run(until);
    int r = -1;
    for (int i = 0; i < ntr; i++) {
        if (tr[i].kind == TRACE_CALL && str_ieq(tr[i].name, tr[i].len, "RESUME")) {
            r = i;
        }
    }
    CHECK(r > 0);
    bool work_after = false;
    for (int i = r + 1; i < ntr; i++) {
        work_after |= tr[i].kind == TRACE_CALL && str_ieq(tr[i].name, tr[i].len, "work");
    }
    CHECK(work_after);
}

static void test_main_choices(void)
{
    load(12, "DEF MAIN\nCALL x\nCHOOSE\n40 a\n60 b\nEND\nEND\nDEF a\nEND\nDEF b\nEND\nDEF x\nEND\n");
    script_choice_t c[4];
    CHECK(script_main_choices(c, 4) == 2);
    CHECK(c[0].weight == 40 && c[1].weight == 60 && c[1].len == 1 && c[1].name[0] == 'b');
}

int main(int argc, char **argv)
{
    RUN(test_choose_alternates);
    RUN(test_choose_compensated);
    RUN(test_pick_trace_depth);
    RUN(test_maybe_repeat_when);
    RUN(test_idle_and_wait);
    RUN(test_depth_limit);
    RUN(test_block_depth_limit);
    RUN(test_run_boot_and_loop);
    RUN(test_pause_resume);
    RUN(test_main_choices);
    return TEST_RESULT();
}
