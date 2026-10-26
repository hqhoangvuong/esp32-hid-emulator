#include "editor_model.h"
#include "keys.h"
#include "rhythm.h"
#include "script.h"
#include "sim.h"
#include "test_util.h"

#define KEY_T 0x17
#define KEY_W 0x1A

static const char *REG = "REGION editor +440 +115 -140 -90\nREGION terminal +440 -250 -60 -90\n"
                         "REGION chrome_page +40 +130 -40 -70\nREGION explorer +70 +115 +280 +260\n";
static const char *SNIP = "POOL q LINES\nlinq group by example\ngit rebase onto\nEND\n"
                          "POOL types\nclass A {\nint x;\n}\n---\nclass B { }\nEND\n";

static void load(uint64_t seed, const char *body)
{
    static char dev[4096];
    snprintf(dev, sizeof dev, "%s%sDEF MAIN\nEND\n", REG, body);
    sim_reset(seed, 0);
    rhythm_init();
    rhythm_personality_t p = {240, 0, 100, 100};
    rhythm_set_personality(&p);
    script_set_trace(NULL, NULL);
    CHECK(script_load(dev, strlen(dev), SNIP, strlen(SNIP)));
    CHECK(script_problems() == 0);
}

static int presses(uint8_t mod, uint8_t code)
{
    size_t n;
    const sim_event_t *e = sim_events(&n);
    int c = 0;
    uint8_t prev = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == 'K') {
            if (e[i].code && e[i].code != prev && e[i].mod == mod && e[i].code == code) {
                c++;
            }
            prev = e[i].code;
        }
    }
    return c;
}

static int buttons_down(void)
{
    size_t n;
    const sim_event_t *e = sim_events(&n);
    int c = 0;
    for (size_t i = 0; i < n; i++) {
        c += e[i].kind == 'M' && e[i].buttons;
    }
    return c;
}

static void test_key_and_type(void)
{
    load(1, "DEF t\nKEY CTRL+S\nTYPE hello there\nEND\n");
    script_call("t");
    CHECK(presses(KMOD_CTRL, 0x16) == 1);
    size_t n;
    const sim_event_t *e = sim_events(&n);
    ed_reset();
    for (size_t i = 0; i < n; i++) {
        ed_feed(&e[i]);
    }
    CHECK(strcmp(ed_text(), "hello there") == 0);
}

static void test_type_pool_no_repeat(void)
{
    load(2, "DEF t\nREPEAT 20 20\nTYPE @q\nKEY ENTER\nEND\nEND\n");
    script_call("t");
    size_t n;
    const sim_event_t *e = sim_events(&n);
    ed_reset();
    for (size_t i = 0; i < n; i++) {
        ed_feed(&e[i]);
    }
    /* two items and no immediate repeat: strictly alternating lines */
    const char *t = ed_text();
    const char *first = strstr(t, "linq") == t ? "linq group by example\ngit rebase onto\n" : "git rebase onto\nlinq group by example\n";
    CHECK(strncmp(t, first, strlen(first)) == 0 && strncmp(t + strlen(first), first, strlen(first)) == 0);
}

static void test_focus_idempotent(void)
{
    load(3, "DEF t\nFOCUS chrome\nFOCUS chrome\nEND\n");
    script_call("t");
    CHECK(presses(KMOD_ALT, KEY_TAB) == 1 && script_state()->fg == APP_CHROME);
}

static void test_terminal_idempotent(void)
{
    load(4, "DEF t\nTERMINAL open\nTERMINAL open\nTERMINAL close\nEND\n");
    script_call("t");
    CHECK(presses(KMOD_CTRL, KEY_GRAVE) == 2 && !script_state()->terminal_open);
}

static void test_no_click_in_chrome(void)
{
    load(5, "DEF t\nFOCUS chrome\nGOTO chrome_page\nCLICK\nDBLCLICK\nDRAGSELECT chrome_page\nEND\n");
    script_call("t");
    CHECK(buttons_down() == 0 && sim_log_count("click suppressed") == 3);
}

static void test_clicks_in_code(void)
{
    load(6, "DEF t\nGOTO editor\nCLICK\nDBLCLICK\nDRAGSELECT editor\nCLICK RIGHT\nEND\n");
    script_call("t");
    CHECK(buttons_down() > 4 && sim_log_count("click suppressed") == 0);
}

static void test_search_and_gap(void)
{
    load(7, "DEF t\nFOCUS chrome\nSEARCH q\nENDSEARCH\nSEARCH q\nEND\nDEF later\nWAIT 361000\nSEARCH q\nEND\n");
    script_call("t");
    CHECK(presses(KMOD_CTRL, KEY_T) == 1 && presses(KMOD_CTRL, KEY_W) == 1 && !script_state()->search_open);
    CHECK(presses(0, KEY_DELETE) >= 1);
    script_call("later");
    CHECK(presses(KMOD_CTRL, KEY_T) == 2 && script_state()->search_open);
}

/* Review Focus 5 */
static void test_searchgap_wrap(void)
{
    load(8, "DEF t\nFOCUS chrome\nSEARCH q\nWAIT 100000\nSEARCH q\nWAIT 300000\nSEARCH q\nEND\n");
    sim_reset(8, 0xFFFFFFFFu - 50000);
    rhythm_init();
    script_call("t");
    CHECK(presses(KMOD_CTRL, KEY_T) == 2);
}

static void test_search_needs_chrome(void)
{
    load(9, "DEF t\nSEARCH q\nEND\n");
    script_call("t");
    CHECK(presses(KMOD_CTRL, KEY_T) == 0 && sim_log_count("SEARCH needs chrome") == 1);
}

static void test_endsearch_focuses(void)
{
    load(10, "DEF t\nFOCUS chrome\nSEARCH q\nFOCUS code\nENDSEARCH\nEND\n");
    script_call("t");
    CHECK(presses(KMOD_ALT, KEY_TAB) == 3 && script_state()->fg == APP_CHROME && !script_state()->search_open);
}

static uint32_t first_press_time(uint8_t mod, uint8_t code)
{
    size_t n;
    const sim_event_t *e = sim_events(&n);
    for (size_t i = 0; i < n; i++) {
        if (e[i].mod == mod && e[i].code == code) {
            return e[i].t;
        }
    }
    return 0;
}

/* Review Focus 2 */
static void test_pause_mid_combo(void)
{
    load(11, "DEF t\nFOCUS chrome\nEND\n");
    script_call("t");
    uint32_t tab = first_press_time(KMOD_ALT, KEY_TAB);
    load(11, "DEF t\nFOCUS chrome\nEND\n");
    sim_pause(tab + 1, tab + 5000);
    CHECK(!script_call("t"));
    CHECK(script_state()->fg == APP_CHROME);

    load(12, "DEF t\nTERMINAL open\nEND\n");
    script_call("t");
    uint32_t g = first_press_time(KMOD_CTRL, KEY_GRAVE);
    load(12, "DEF t\nTERMINAL open\nEND\n");
    sim_pause(g + 1, g + 5000);
    script_call("t");
    CHECK(script_state()->terminal_open);

    load(13, "DEF t\nFOCUS chrome\nSEARCH q\nEND\n");
    script_call("t");
    uint32_t ct = first_press_time(KMOD_CTRL, KEY_T);
    load(13, "DEF t\nFOCUS chrome\nSEARCH q\nEND\n");
    sim_pause(ct + 1, ct + 5000);
    script_call("t");
    CHECK(script_state()->search_open);
}

static script_event_t snips[4];
static int nsnips;
static void on_snip(const script_event_t *e, void *ctx)
{
    (void)ctx;
    if (e->kind == TRACE_SNIPPET && nsnips < 4) {
        snips[nsnips++] = *e;
    }
}

/* pool "types" has a 3-line and a 1-line snippet; no immediate repeat */
static void test_code_lines_and_trace(void)
{
    load(14, "DEF t\nCODE types\nCODE types\nEND\n");
    nsnips = 0;
    script_set_trace(on_snip, NULL);
    script_call("t");
    CHECK(script_state()->lines == 4);
    CHECK(nsnips == 2 && snips[0].index != snips[1].index);
}

static void test_code_midway(void)
{
    int calls = 0;
    for (uint64_t s = 1; s <= 40; s++) {
        load(100 + s, "DEF t\nCODE types peek\nEND\nDEF peek\nSCROLL 1\nEND\n");
        script_call("t");
        size_t n;
        const sim_event_t *e = sim_events(&n);
        for (size_t i = 0; i < n; i++) {
            calls += e[i].kind == 'M' && e[i].wheel != 0;
        }
    }
    CHECK(calls >= 4 && calls <= 40); /* about 40% of runs, one or more notches each */
}

static void test_handoff(void)
{
    load(15, "DEF t\nTYPE x\nGOTO editor\nCLICK\nTYPE y\nEND\n");
    script_call("t");
    size_t n;
    const sim_event_t *e = sim_events(&n);
    uint32_t last_key = 0, first_mouse = 0, click_up = 0, next_key = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == 'K' && !first_mouse) {
            last_key = e[i].t;
        }
        if (e[i].kind == 'M' && !first_mouse) {
            first_mouse = e[i].t;
        }
        if (e[i].kind == 'M' && i > 0 && e[i - 1].kind == 'M' && e[i - 1].buttons && !e[i].buttons) {
            click_up = e[i].t;
        }
        if (e[i].kind == 'K' && click_up && !next_key) {
            next_key = e[i].t;
        }
    }
    CHECK(first_mouse - last_key >= 250);
    CHECK(next_key - click_up >= 200);
}

static void test_readscroll_uses_fg_region(void)
{
    load(16, "DEF t\nFOCUS chrome\nREADSCROLL 6\nEND\n");
    script_call("t");
    size_t n;
    const sim_event_t *e = sim_events(&n);
    region_t spec;
    script_region_spec("chrome_page", &spec);
    rect_t r = pointer_resolve(&spec);
    int moves = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].kind == 'M' && e[i].wheel) {
            int32_t x = pointer_units_to_px(e[i].x, 1920), y = pointer_units_to_px(e[i].y, 1080);
            CHECK(x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1);
            moves++;
        }
    }
    CHECK(moves >= 6);
}

static void test_pointer_commands(void)
{
    load(17, "DEF t\nHOVER explorer 800\nSCROLL -2\nOUTLINE explorer\nEND\n");
    CHECK(script_call("t"));
    size_t n;
    sim_events(&n);
    CHECK(n > 20);
}

static void test_idle_nudges(void)
{
    load(18, "MAXGAP 210\nDEF t\nGOTO editor\nIDLE 300000 300000\nEND\n");
    script_call("t");
    size_t n;
    const sim_event_t *e = sim_events(&n);
    for (size_t i = 1; i < n; i++) {
        CHECK(e[i].t - e[i - 1].t <= 210020);
    }
    CHECK(hid_out_now_ms() > 300000 && hid_out_now_ms() - e[n - 1].t <= 210020);
}

/* A pause during the key-to-pointer hand-off must not let any pointer report through: only the
 * release report may go out once the pause has started. */
static void quiet_after_pause(const char *body)
{
    load(30, body);
    script_call("t");
    size_t n;
    const sim_event_t *e = sim_events(&n);
    uint32_t key_up = 0;
    for (size_t i = 0; i < n && e[i].kind == 'K'; i++) {
        key_up = e[i].t;
    }
    uint32_t p = key_up + 30;
    load(30, body);
    sim_pause(p, p + 5000);
    script_call("t");
    e = sim_events(&n);
    int mouse_after = 0;
    for (size_t i = 0; i < n; i++) {
        if (e[i].t >= p) {
            CHECK(e[i].buttons == 0 && e[i].code == 0 && e[i].wheel == 0);
            mouse_after += e[i].kind == 'M';
        }
    }
    CHECK(mouse_after <= 1);
    if (mouse_after > 1) {
        printf("  %s", body);
    }
}

static void test_pause_in_handoff(void)
{
    quiet_after_pause("DEF t\nKEY ESC\nDBLCLICK\nEND\n");
    quiet_after_pause("DEF t\nKEY ESC\nDRAGSELECT editor\nEND\n");
    quiet_after_pause("DEF t\nKEY ESC\nCLICK\nEND\n");
    quiet_after_pause("DEF t\nKEY ESC\nGOTO editor\nEND\n");
    quiet_after_pause("DEF t\nKEY ESC\nSCROLL 2\nEND\n");
    quiet_after_pause("DEF t\nKEY ESC\nREADSCROLL 2\nEND\n");
}

int main(int argc, char **argv)
{
    RUN(test_pause_in_handoff);
    RUN(test_key_and_type);
    RUN(test_type_pool_no_repeat);
    RUN(test_focus_idempotent);
    RUN(test_terminal_idempotent);
    RUN(test_no_click_in_chrome);
    RUN(test_clicks_in_code);
    RUN(test_search_and_gap);
    RUN(test_searchgap_wrap);
    RUN(test_search_needs_chrome);
    RUN(test_endsearch_focuses);
    RUN(test_pause_mid_combo);
    RUN(test_code_lines_and_trace);
    RUN(test_code_midway);
    RUN(test_handoff);
    RUN(test_readscroll_uses_fg_region);
    RUN(test_pointer_commands);
    RUN(test_idle_nudges);
    return TEST_RESULT();
}
