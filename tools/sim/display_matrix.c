/* Display matrix: for a set of screens, checks that each shipped region sits inside the VS Code
 * or Chrome element it is meant for with a 20 px margin, and that pointer reports never move by
 * less than 1 px. The element sizes are the spec's approximations, scaled. */

#include <stdio.h>
#include <string.h>

#include "pointer.h"
#include "rhythm.h"
#include "script.h"
#include "sim.h"

#define MARGIN 20

typedef struct {
    uint16_t w, h, scale;
} screen_t;

static const screen_t screens[] = {
    {1366, 768, 100}, {1920, 1080, 100}, {1920, 1080, 125}, {2560, 1440, 100},
    {2560, 1440, 150}, {3440, 1440, 100}, {3840, 2160, 150}, {3840, 2160, 200},
};

static int32_t sc(int32_t px, uint16_t scale)
{
    return px * scale / 100;
}

/* Approximate element rectangles: activity bar 48, sidebar 300, title 35, tab strip 35,
 * breadcrumbs 22, status bar 22, taskbar 48, gutter 64, minimap 120, Chrome toolbar 85. */
static rect_t element(const char *name, const screen_t *s)
{
    int32_t w = s->w, h = s->h;
    uint16_t k = s->scale;
    rect_t r = {0, 0, 0, 0};
    if (strcmp(name, "editor") == 0) {
        r = (rect_t){sc(412, k), sc(92, k), w - sc(120, k), h - sc(70, k)};
    } else if (strcmp(name, "explorer") == 0) {
        r = (rect_t){sc(48, k), sc(92, k), sc(348, k), h - sc(70, k)};
    } else if (strcmp(name, "terminal") == 0) {
        r = (rect_t){sc(368, k), h - sc(335, k), w - sc(20, k), h - sc(70, k)};
    } else {
        r = (rect_t){0, sc(85, k), w - sc(15, k), h - sc(48, k)};
    }
    return r;
}

int main(void)
{
    size_t dl, sl;
    char *dev = sim_read_file(MACROS_DIR "/developer.txt", &dl);
    char *snip = sim_read_file(MACROS_DIR "/snippets.txt", &sl);
    if (!dev || !snip) {
        printf("display_matrix: cannot read the scripts\n");
        return 2;
    }
    sim_reset(1, 0);
    rhythm_init();
    script_load(dev, dl, snip, sl);

    static const char *const names[] = {"editor", "explorer", "terminal", "chrome_page"};
    int failures = 0;
    for (size_t i = 0; i < sizeof screens / sizeof screens[0]; i++) {
        const screen_t *s = &screens[i];
        pointer_set_screen(s->w, s->h, s->scale);
        rect_t rects[4], safe = {0, 0, 0, 0};
        printf("%ux%u @%u%%:", s->w, s->h, s->scale);
        for (int n = 0; n < 4; n++) {
            region_t spec;
            if (!script_region_spec(names[n], &spec)) {
                printf(" missing %s", names[n]);
                failures++;
                continue;
            }
            rect_t r = pointer_resolve(&spec), e = element(names[n], s);
            rects[n] = r;
            int32_t m = r.x0 - e.x0;
            m = r.y0 - e.y0 < m ? r.y0 - e.y0 : m;
            m = e.x1 - r.x1 < m ? e.x1 - r.x1 : m;
            m = e.y1 - r.y1 < m ? e.y1 - r.y1 : m;
            bool ok = pointer_rect_valid(&r) && m >= MARGIN;
            printf(" %s %d%s", names[n], (int)m, ok ? "" : "!");
            failures += !ok;
            if (n == 0) {
                safe = r;
            } else {
                safe.x0 = r.x0 < safe.x0 ? r.x0 : safe.x0;
                safe.y0 = r.y0 < safe.y0 ? r.y0 : safe.y0;
                safe.x1 = r.x1 > safe.x1 ? r.x1 : safe.x1;
                safe.y1 = r.y1 > safe.y1 ? r.y1 : safe.y1;
            }
        }

        /* no report may move the pointer by less than one pixel */
        sim_reset(100 + i, 0);
        rhythm_init();
        pointer_set_safe(&safe);
        pointer_jump(&rects[0]);
        for (int k = 0; k < 200; k++) {
            pointer_goto(&rects[k % 4]);
        }
        for (int k = 0; k < 100; k++) {
            pointer_nudge(&rects[0]);
        }
        size_t ne;
        const sim_event_t *e = sim_events(&ne);
        int small = 0;
        for (size_t k = 1; k < ne; k++) {
            if (e[k].wheel || e[k].buttons != e[k - 1].buttons) {
                continue;
            }
            int32_t dx = pointer_units_to_px(e[k].x, s->w) - pointer_units_to_px(e[k - 1].x, s->w);
            int32_t dy = pointer_units_to_px(e[k].y, s->h) - pointer_units_to_px(e[k - 1].y, s->h);
            small += dx == 0 && dy == 0 && e[k - 1].wheel == 0;
        }
        printf("  sub-pixel steps %d\n", small);
        failures += small;
    }
    printf(failures ? "display_matrix: %d failure(s)\n" : "display_matrix: ok\n", failures);
    return failures ? 1 : 0;
}
