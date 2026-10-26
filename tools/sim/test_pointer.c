#include "pointer.h"
#include "rhythm.h"
#include "sim.h"
#include "test_util.h"

static const rect_t SAFE = {40, 110, 1880, 1010};
static const rect_t EDITOR = {440, 115, 1780, 990};
static const rect_t EXPLORER = {70, 115, 280, 260};

static void setup(uint64_t seed)
{
    sim_reset(seed, 0);
    rhythm_init();
    pointer_set_screen(1920, 1080, 100);
    pointer_set_safe(&SAFE);
}

static bool in(const rect_t *r, int32_t x, int32_t y)
{
    return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}

static int32_t px_x(const sim_event_t *e)
{
    return pointer_units_to_px(e->x, 1920);
}

static int32_t px_y(const sim_event_t *e)
{
    return pointer_units_to_px(e->y, 1080);
}

static void test_units_roundtrip(void)
{
    uint16_t ext[] = {768, 1080, 1366, 1920, 3840};
    for (int i = 0; i < 5; i++) {
        for (int32_t p = 0; p < ext[i]; p++) {
            CHECK(pointer_units_to_px(pointer_px_to_units(p, ext[i]), ext[i]) == p);
        }
    }
    CHECK(pointer_px_to_units(0, 1920) == 0 && pointer_px_to_units(1919, 1920) == 32767);
}

static void test_resolve(void)
{
    region_t ed = {{440, 115, 140, 90}, 0x0C};
    pointer_set_screen(1920, 1080, 100);
    rect_t r = pointer_resolve(&ed);
    CHECK(r.x0 == 440 && r.y0 == 115 && r.x1 == 1780 && r.y1 == 990);
    pointer_set_screen(2560, 1440, 150);
    r = pointer_resolve(&ed);
    CHECK(r.x0 == 660 && r.y0 == 172 && r.x1 == 2350 && r.y1 == 1305);
}

/* Review Focus 4: a screen too small for the scale */
static void test_empty_region(void)
{
    setup(1);
    region_t ed = {{440, 115, 140, 90}, 0x0C};
    pointer_set_screen(800, 600, 200);
    rect_t r = pointer_resolve(&ed);
    CHECK(!pointer_rect_valid(&r));
    size_t n;
    CHECK(pointer_goto(&r));
    sim_events(&n);
    CHECK(n == 0);
    CHECK(sim_log_count("empty region") == 1);
}

static void test_fitts(void)
{
    CHECK(pointer_fitts_ms(0, 40) == 150);
    CHECK(pointer_fitts_ms(1000, 40) == 714);
}

static void test_jump(void)
{
    setup(2);
    size_t n;
    pointer_jump(&EDITOR);
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 1 && in(&EDITOR, px_x(&e[0]), px_y(&e[0])));
}

static void test_goto_paths(void)
{
    setup(3);
    pointer_jump(&EDITOR);
    for (int i = 0; i < 200; i++) {
        const rect_t *t = (i & 1) ? &EDITOR : &EXPLORER;
        CHECK(pointer_goto(t));
        int32_t x, y;
        pointer_pos(&x, &y);
        CHECK(in(t, x, y));
    }
    size_t n;
    const sim_event_t *e = sim_events(&n);
    int32_t px = -1, py = -1;
    for (size_t i = 0; i < n; i++) {
        int32_t x = px_x(&e[i]), y = px_y(&e[i]);
        CHECK(in(&SAFE, x, y));
        CHECK(e[i].buttons == 0);
        CHECK(x != px || y != py);
        px = x;
        py = y;
    }
}

static void test_overshoot_rate(void)
{
    setup(4);
    rect_t left = {440, 500, 450, 510}, right = {1700, 500, 1710, 510};
    int over = 0;
    pointer_jump(&left);
    for (int i = 0; i < 400; i++) {
        size_t n0;
        sim_events(&n0);
        const rect_t *t = (i & 1) ? &left : &right;
        pointer_goto(t);
        size_t n;
        const sim_event_t *e = sim_events(&n);
        bool beyond = false;
        for (size_t k = n0; k < n; k++) {
            int32_t x = px_x(&e[k]);
            beyond |= (t == &right) ? x > right.x1 + 30 : x < left.x0 - 30;
        }
        over += beyond;
    }
    CHECK(over >= 60 && over <= 140); /* 25% of 400 */
}

static void test_goto_abort(void)
{
    setup(5);
    pointer_jump(&EXPLORER);
    sim_pause(100, 200);
    CHECK(!pointer_goto(&EDITOR));
}

static void test_click(void)
{
    setup(10);
    pointer_jump(&EDITOR);
    size_t n0;
    sim_events(&n0);
    uint32_t t0 = hid_out_now_ms();
    CHECK(pointer_click(BTN_LEFT));
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(n - n0 == 2 && e[n0].buttons == 1 && e[n0 + 1].buttons == 0 && e[n0].x == e[n0 + 1].x);
    CHECK(e[n0].t - t0 >= 80);
    uint32_t hold = e[n0 + 1].t - e[n0].t;
    CHECK(hold >= 55 && hold <= 140);
}

static void test_dblclick(void)
{
    setup(11);
    pointer_jump(&EDITOR);
    size_t n0;
    sim_events(&n0);
    CHECK(pointer_dblclick());
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(n - n0 == 4);
    uint32_t gap = e[n0 + 2].t - e[n0 + 1].t;
    CHECK(gap >= 70 && gap <= 130);
    CHECK(e[n0 + 3].t - e[n0].t < 500);
}

static void test_dragselect(void)
{
    setup(12);
    pointer_jump(&EDITOR);
    size_t n0;
    sim_events(&n0);
    CHECK(pointer_dragselect(&EDITOR));
    size_t n;
    const sim_event_t *e = sim_events(&n);
    CHECK(e[n0].buttons == 1 && e[n - 1].buttons == 0);
    for (size_t i = n0; i < n - 1; i++) {
        CHECK(e[i].buttons == 1);
    }
    int32_t x, y;
    pointer_pos(&x, &y);
    CHECK(in(&EDITOR, x, y));
}

static int wheel_sum(size_t from, bool *reversed, int sign)
{
    size_t n;
    const sim_event_t *e = sim_events(&n);
    int s = 0;
    for (size_t i = from; i < n; i++) {
        if (e[i].wheel) {
            CHECK(e[i].wheel == 1 || e[i].wheel == -1);
            if (e[i].wheel != sign) {
                *reversed = true;
            }
        }
        s += e[i].wheel;
    }
    return s;
}

static void test_scroll(void)
{
    setup(13);
    pointer_jump(&EDITOR);
    int rev = 0;
    for (int i = 0; i < 200; i++) {
        size_t n0;
        sim_events(&n0);
        bool r = false;
        int k = (i & 1) ? 5 : -3;
        CHECK(pointer_scroll(k));
        CHECK(wheel_sum(n0, &r, k > 0 ? 1 : -1) == k);
        rev += r;
    }
    CHECK(rev >= 10 && rev <= 80);
}

static void test_readscroll(void)
{
    setup(14);
    pointer_jump(&EDITOR);
    size_t n0;
    sim_events(&n0);
    bool r = false;
    CHECK(pointer_readscroll(6, &EDITOR));
    CHECK(wheel_sum(n0, &r, -1) == -6);
    size_t n;
    const sim_event_t *e = sim_events(&n);
    for (size_t i = n0; i < n; i++) {
        CHECK(in(&SAFE, px_x(&e[i]), px_y(&e[i])));
    }
}

static void test_hover(void)
{
    setup(15);
    pointer_jump(&EXPLORER);
    CHECK(pointer_hover(&EDITOR, 2000));
    int32_t x, y;
    pointer_pos(&x, &y);
    CHECK(in(&EDITOR, x, y));
}

static void test_nudge_kinds(void)
{
    setup(16);
    pointer_jump(&EDITOR);
    int drift = 0, wheel = 0, hover = 0;
    for (int i = 0; i < 100; i++) {
        size_t n0;
        sim_events(&n0);
        uint32_t t0 = hid_out_now_ms();
        CHECK(pointer_nudge(&EDITOR));
        size_t n;
        const sim_event_t *e = sim_events(&n);
        int32_t x, y;
        pointer_pos(&x, &y);
        bool w = false;
        for (size_t k = n0; k < n; k++) {
            w |= e[k].wheel != 0;
        }
        if (w) {
            wheel++;
        } else if (hid_out_now_ms() - t0 >= 800) {
            hover++;
        } else {
            drift++;
        }
        CHECK(in(&EDITOR, x, y));
        CHECK(n > n0);
    }
    CHECK(drift > 0 && wheel > 0 && hover > 0);
}

static void test_outline(void)
{
    setup(17);
    rect_t r = {500, 300, 900, 600};
    pointer_jump(&r);
    CHECK(pointer_outline(&r));
    int32_t x, y;
    pointer_pos(&x, &y);
    CHECK(x == 500 && y == 300);
}

int main(int argc, char **argv)
{
    RUN(test_click);
    RUN(test_dblclick);
    RUN(test_dragselect);
    RUN(test_scroll);
    RUN(test_readscroll);
    RUN(test_hover);
    RUN(test_nudge_kinds);
    RUN(test_outline);
    RUN(test_units_roundtrip);
    RUN(test_resolve);
    RUN(test_empty_region);
    RUN(test_fitts);
    RUN(test_jump);
    RUN(test_goto_paths);
    RUN(test_overshoot_rate);
    RUN(test_goto_abort);
    return TEST_RESULT();
}
