#include "pointer.h"

#include <math.h>

#include "hid_out.h"
#include "rhythm.h"

#define STEP_MS        10
#define FITTS_W_PX     40
#define LONG_MOVE_PX   400
#define OVERSHOOT_PCT  25

static uint16_t scr_w = 1920, scr_h = 1080, scr_scale = 100;
static rect_t safe = {0, 0, 1919, 1079};
static int32_t pos_x, pos_y;
static uint8_t sent_buttons;
static bool sent_any;

void pointer_set_screen(uint16_t w, uint16_t h, uint16_t scale_pct)
{
    scr_w = w;
    scr_h = h;
    scr_scale = scale_pct;
    safe.x0 = 0;
    safe.y0 = 0;
    safe.x1 = w - 1;
    safe.y1 = h - 1;
}

static int32_t scaled(int32_t px)
{
    return px * scr_scale / 100;
}

rect_t pointer_resolve(const region_t *r)
{
    int32_t out[4];
    for (int i = 0; i < 4; i++) {
        int32_t off = scaled(r->v[i]);
        int32_t extent = (i & 1) ? scr_h : scr_w;
        out[i] = (r->from_end & (1u << i)) ? extent - off : off;
    }
    rect_t res = {out[0], out[1], out[2], out[3]};
    return res;
}

bool pointer_rect_valid(const rect_t *r)
{
    return r->x0 <= r->x1 && r->y0 <= r->y1 && r->x0 >= 0 && r->y0 >= 0 && r->x1 < scr_w && r->y1 < scr_h;
}

void pointer_set_safe(const rect_t *r)
{
    safe = *r;
}

uint16_t pointer_px_to_units(int32_t px, uint16_t extent)
{
    if (extent < 2 || px <= 0) {
        return 0;
    }
    if (px >= extent - 1) {
        return HID_ABS_MAX;
    }
    return (uint16_t)((px * (int64_t)HID_ABS_MAX + (extent - 1) / 2) / (extent - 1));
}

int32_t pointer_units_to_px(uint16_t u, uint16_t extent)
{
    if (extent < 2) {
        return 0;
    }
    return (int32_t)(((int64_t)u * (extent - 1) + HID_ABS_MAX / 2) / HID_ABS_MAX);
}

uint32_t pointer_fitts_ms(float dist_px, float width_px)
{
    return (uint32_t)lroundf(150.0f + 120.0f * log2f(dist_px / width_px + 1.0f));
}

void pointer_pos(int32_t *x, int32_t *y)
{
    *x = pos_x;
    *y = pos_y;
}

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Report the position (clamped to the safe box) if anything changed, or always if force.
 * Nothing goes out once a pause has aborted the script: rhythm already released everything. */
static void report(int32_t x, int32_t y, uint8_t buttons, int8_t wheel, bool force)
{
    if (rhythm_aborted()) {
        return;
    }
    x = clampi(x, safe.x0, safe.x1);
    y = clampi(y, safe.y0, safe.y1);
    if (!force && sent_any && x == pos_x && y == pos_y && buttons == sent_buttons && wheel == 0) {
        return;
    }
    pos_x = x;
    pos_y = y;
    sent_buttons = buttons;
    sent_any = true;
    hid_out_mouse(buttons, pointer_px_to_units(x, scr_w), pointer_px_to_units(y, scr_h), wheel);
}

static void random_point(const rect_t *r, int32_t *x, int32_t *y)
{
    *x = (int32_t)rhythm_rand((uint32_t)r->x0, (uint32_t)r->x1);
    *y = (int32_t)rhythm_rand((uint32_t)r->y0, (uint32_t)r->y1);
}

/* Randomised cubic Bezier with min-jerk easing from the current position to (tx, ty),
 * one sample every STEP_MS. Tremor adds up to 2 px per axis until the last 5 px. */
static bool move_to(int32_t tx, int32_t ty, uint32_t duration_ms, uint8_t buttons, bool tremor)
{
    float sx = (float)pos_x, sy = (float)pos_y;
    float dx = (float)(tx - pos_x), dy = (float)(ty - pos_y);
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.0f) {
        return true;
    }
    float nx = -dy / dist, ny = dx / dist; /* unit normal */
    float bow1 = ((int32_t)rhythm_rand(0, 300) - 150) / 1000.0f * dist;
    float bow2 = ((int32_t)rhythm_rand(0, 300) - 150) / 1000.0f * dist;
    float p1x = dx * 0.30f + nx * bow1, p1y = dy * 0.30f + ny * bow1;
    float p2x = dx * 0.70f + nx * bow2, p2y = dy * 0.70f + ny * bow2;

    uint32_t steps = duration_ms / STEP_MS;
    if (steps < 1) {
        steps = 1;
    }
    for (uint32_t i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        float s = t * t * t * (10.0f + t * (-15.0f + 6.0f * t)); /* min-jerk easing */
        float u = 1.0f - s;
        float x = 3 * u * u * s * p1x + 3 * u * s * s * p2x + s * s * s * dx;
        float y = 3 * u * u * s * p1y + 3 * u * s * s * p2y + s * s * s * dy;
        int32_t px = (int32_t)lroundf(sx + x), py = (int32_t)lroundf(sy + y);
        if (i == steps) {
            px = tx;
            py = ty;
        } else if (tremor) {
            float rx = (float)tx - (sx + x), ry = (float)ty - (sy + y);
            if (rx * rx + ry * ry > 25.0f) {
                px += (int32_t)rhythm_rand(0, 4) - 2;
                py += (int32_t)rhythm_rand(0, 4) - 2;
            }
        }
        report(px, py, buttons, 0, false);
        if (!rhythm_sleep(STEP_MS)) {
            return false;
        }
    }
    return true;
}

/* Fitts duration for a move of dist px, with the session's mouse speed and +-15%. */
static uint32_t move_duration(float dist)
{
    float w = (float)scaled(FITTS_W_PX);
    uint32_t ms = pointer_fitts_ms(dist, w > 1.0f ? w : 1.0f);
    ms = ms * rhythm_personality()->mouse_pct / 100u;
    ms = ms * rhythm_rand(85, 115) / 100u;
    return rhythm_scale(ms, 100);
}

static float distance(int32_t x, int32_t y)
{
    float dx = (float)(x - pos_x), dy = (float)(y - pos_y);
    return sqrtf(dx * dx + dy * dy);
}

void pointer_jump(const rect_t *r)
{
    int32_t x, y;
    random_point(r, &x, &y);
    report(x, y, 0, 0, true);
}

bool pointer_goto(const rect_t *r)
{
    if (!pointer_rect_valid(r)) {
        hid_out_log("pointer: empty region");
        return true;
    }
    int32_t tx, ty;
    random_point(r, &tx, &ty);
    float dist = distance(tx, ty);
    if (dist > (float)scaled(LONG_MOVE_PX) && rhythm_rand(0, 99) < OVERSHOOT_PCT) {
        float k = rhythm_rand(3, 10) / 100.0f;
        int32_t ox = clampi(tx + (int32_t)lroundf((tx - pos_x) * k), safe.x0, safe.x1);
        int32_t oy = clampi(ty + (int32_t)lroundf((ty - pos_y) * k), safe.y0, safe.y1);
        if (!move_to(ox, oy, move_duration(distance(ox, oy)), 0, true)) {
            return false;
        }
    }
    return move_to(tx, ty, move_duration(distance(tx, ty)), 0, true);
}

static bool inside(const rect_t *r, int32_t x, int32_t y)
{
    return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}

static bool press_release(uint8_t buttons, uint32_t hold_lo, uint32_t hold_hi)
{
    report(pos_x, pos_y, buttons, 0, true);
    if (!rhythm_sleep(rhythm_rand(hold_lo, hold_hi))) {
        return false;
    }
    report(pos_x, pos_y, 0, 0, true);
    return true;
}

bool pointer_click(uint8_t buttons)
{
    return rhythm_sleep(rhythm_rand(80, 220)) && press_release(buttons, 55, 140);
}

bool pointer_dblclick(void)
{
    return press_release(BTN_LEFT, 50, 90) && rhythm_sleep(rhythm_rand(70, 130)) && press_release(BTN_LEFT, 50, 90);
}

bool pointer_dragselect(const rect_t *r)
{
    if (!pointer_rect_valid(r)) {
        hid_out_log("pointer: empty region");
        return true;
    }
    int32_t tx, ty;
    random_point(r, &tx, &ty);
    report(pos_x, pos_y, BTN_LEFT, 0, true);
    if (!rhythm_sleep(rhythm_rand(60, 150))) {
        return false;
    }
    if (!move_to(tx, ty, move_duration(distance(tx, ty)) * 13 / 10, BTN_LEFT, false)) {
        return false;
    }
    if (!rhythm_sleep(rhythm_rand(60, 150))) {
        return false;
    }
    report(pos_x, pos_y, 0, 0, true);
    return true;
}

/* A 1-2 px step that stays inside r (and the safe box) and actually moves. */
static void wobble(const rect_t *r)
{
    for (int attempt = 0; attempt < 8; attempt++) {
        int32_t dx = (int32_t)rhythm_rand(0, 4) - 2, dy = (int32_t)rhythm_rand(0, 4) - 2;
        int32_t x = pos_x + dx, y = pos_y + dy;
        if ((dx || dy) && inside(r, x, y) && inside(&safe, x, y)) {
            report(x, y, 0, 0, false);
            return;
        }
    }
}

/* Stay put for ms, wobbling every 250-600 ms. */
static bool dwell(const rect_t *r, uint32_t ms)
{
    uint32_t start = hid_out_now_ms();
    for (;;) {
        uint32_t elapsed = hid_out_now_ms() - start;
        if (elapsed >= ms) {
            return true;
        }
        uint32_t w = rhythm_rand(250, 600);
        if (w > ms - elapsed) {
            w = ms - elapsed;
        }
        if (!rhythm_sleep(w)) {
            return false;
        }
        if (hid_out_now_ms() - start < ms) {
            wobble(r);
        }
    }
}

bool pointer_hover(const rect_t *r, uint32_t dwell_ms)
{
    if (!pointer_rect_valid(r)) {
        hid_out_log("pointer: empty region");
        return true;
    }
    return pointer_goto(r) && dwell(r, dwell_ms);
}

/* One burst of count notches in dir, intervals growing 1.3x; 10% add a reversal and a
 * compensating notch so the net stays count * dir. */
static bool scroll_burst(int dir, int count)
{
    uint32_t interval = rhythm_rand(30, 60);
    bool reverse = rhythm_rand(0, 99) < 10;
    for (int i = 0; i < count + (reverse ? 2 : 0); i++) {
        int8_t w = (int8_t)((reverse && i == count) ? -dir : dir);
        report(pos_x, pos_y, 0, w, true);
        if (!rhythm_sleep(interval)) {
            return false;
        }
        interval = interval * 13 / 10;
    }
    return true;
}

bool pointer_scroll(int notches)
{
    int dir = notches >= 0 ? 1 : -1;
    int remaining = notches >= 0 ? notches : -notches;
    while (remaining > 0) {
        int burst = (int)rhythm_rand(1, 4);
        if (burst > remaining) {
            burst = remaining;
        }
        if (!scroll_burst(dir, burst)) {
            return false;
        }
        remaining -= burst;
        if (remaining > 0 && !rhythm_sleep(rhythm_rand(200, 700))) {
            return false;
        }
    }
    return true;
}

bool pointer_readscroll(int notches, const rect_t *within)
{
    if (!pointer_rect_valid(within)) {
        hid_out_log("pointer: empty region");
        return true;
    }
    if (!inside(within, pos_x, pos_y) && !pointer_goto(within)) {
        return false;
    }
    int remaining = notches;
    while (remaining > 0) {
        int burst = (int)rhythm_rand(1, 4);
        if (burst > remaining) {
            burst = remaining;
        }
        if (!scroll_burst(-1, burst)) {
            return false;
        }
        remaining -= burst;
        if (remaining <= 0) {
            break;
        }
        /* follow the text down a few lines, or go back to the top third */
        int32_t dy = (int32_t)rhythm_rand(1, 3) * scaled(19);
        bool ok;
        if (pos_y + dy > within->y1) {
            rect_t top = *within;
            top.y1 = within->y0 + (within->y1 - within->y0) / 3;
            ok = pointer_goto(&top);
        } else {
            ok = move_to(pos_x, pos_y + dy, move_duration((float)dy), 0, false);
        }
        if (!ok || !rhythm_sleep(rhythm_rand(200, 700))) {
            return false;
        }
    }
    return true;
}

bool pointer_nudge(const rect_t *within)
{
    if (!pointer_rect_valid(within)) {
        hid_out_log("pointer: empty region");
        return true;
    }
    if (!inside(within, pos_x, pos_y)) {
        return pointer_goto(within);
    }
    switch (rhythm_rand(0, 2)) {
    case 0: /* drift 6-20 px, flipping direction to stay inside */
        for (int attempt = 0; attempt < 8; attempt++) {
            int32_t d = (int32_t)rhythm_rand(6, 20);
            int32_t dx = (int32_t)rhythm_rand(0, (uint32_t)d), dy = d - dx;
            dx = (attempt & 1) ? -dx : dx;
            dy = (attempt & 2) ? -dy : dy;
            if (rhythm_rand(0, 1)) {
                dx = -dx;
                dy = -dy;
            }
            int32_t x = pos_x + dx, y = pos_y + dy;
            if (inside(within, x, y) && inside(&safe, x, y)) {
                return move_to(x, y, move_duration(distance(x, y)), 0, false);
            }
        }
        return rhythm_sleep(1) && scroll_burst(-1, 1);
    case 1:
        return dwell(within, rhythm_rand(800, 2000));
    default:
        report(pos_x, pos_y, 0, (int8_t)(rhythm_rand(0, 1) ? 1 : -1), true);
        return true;
    }
}

bool pointer_outline(const rect_t *r)
{
    const int32_t corners[5][2] = {
        {r->x0, r->y0}, {r->x1, r->y0}, {r->x1, r->y1}, {r->x0, r->y1}, {r->x0, r->y0},
    };
    for (int i = 0; i < 5; i++) {
        int32_t x = corners[i][0], y = corners[i][1];
        if (!move_to(x, y, move_duration(distance(x, y)) * 2, 0, false) || !rhythm_sleep(1000)) {
            return false;
        }
    }
    return true;
}
