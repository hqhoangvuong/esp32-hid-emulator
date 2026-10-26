#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A region as written in the script: x0 y0 x1 y1 in px at 100% scale. Bit i of from_end set
 * means v[i] counts from the right (x) or bottom (y) edge. */
typedef struct {
    int16_t v[4];
    uint8_t from_end;
} region_t;

/* Screen pixels, inclusive. */
typedef struct {
    int32_t x0, y0, x1, y1;
} rect_t;

#define BTN_LEFT   0x01
#define BTN_RIGHT  0x02
#define BTN_MIDDLE 0x04

void pointer_set_screen(uint16_t w, uint16_t h, uint16_t scale_pct);
/* offset = v * scale / 100 (truncating); from_end: extent - offset */
rect_t pointer_resolve(const region_t *r);
/* x0 <= x1, y0 <= y1 and inside the screen */
bool pointer_rect_valid(const rect_t *r);
/* Every report is clamped into this box (the whole screen until set). */
void pointer_set_safe(const rect_t *r);

uint16_t pointer_px_to_units(int32_t px, uint16_t extent);
int32_t pointer_units_to_px(uint16_t u, uint16_t extent);
/* 150 + 120 * log2(D/W + 1) ms, rounded */
uint32_t pointer_fitts_ms(float dist_px, float width_px);

void pointer_pos(int32_t *x, int32_t *y);
/* One report at a random point in r: the boot teleport, the only jump. */
void pointer_jump(const rect_t *r);
/* Human-like move to a random point in r. Returns false if aborted. */
bool pointer_goto(const rect_t *r);
