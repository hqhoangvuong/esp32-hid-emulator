#include "invariants.h"

#include <stdlib.h>
#include <string.h>

#include "keys.h"

#define KEY_T          0x17
#define KEY_W          0x1A
#define MAX_HELD_MS    1000
#define RELEASE_MS     100
#define MAX_PAUSES     64
#define MAX_NAMES      32
#define MAX_SAVES      8192
#define PRINT_LIMIT    25

static inv_config_t cfg;
static int violations;

/* last report */
static bool have_report;
static uint32_t last_t, longest_gap;

/* keyboard and buttons */
static uint8_t cur_mod, cur_code, cur_buttons;
static uint32_t mod_t, code_t;

/* host model; omnibox: Chrome's address bar has focus (from Ctrl+T until Enter) */
static bool chrome_front, terminal_open, searched, omnibox;
static int32_t cur_x = -1, cur_y = -1;
static int tabs;
static uint32_t last_search;

static struct {
    uint32_t from, to;
    bool checked;
} pauses[MAX_PAUSES];
static int npauses;

/* picks and snippets */
static struct {
    const char *name;
    size_t len;
    int count;
} picks[MAX_NAMES];
static int npicks, total_picks;
static const char *last_pick;
static size_t last_pick_len;
static struct {
    const char *pool;
    size_t len;
    int last;
} snips[MAX_NAMES];
static int nsnips;

/* typing statistics */
static uint32_t last_char_t, active_ms, chars, backspaces;
static uint32_t gap_hist[6];
static uint32_t saves[MAX_SAVES];
static int nsaves;

static void violation(uint32_t t, const char *what)
{
    if (violations < PRINT_LIMIT) {
        printf("violation at %u.%03u s: %s\n", (unsigned)(t / 1000), (unsigned)(t % 1000), what);
    }
    violations++;
}

void inv_init(const inv_config_t *c)
{
    cfg = *c;
    violations = 0;
    have_report = false;
    last_t = longest_gap = 0;
    cur_mod = cur_code = cur_buttons = 0;
    chrome_front = terminal_open = searched = omnibox = false;
    cur_x = cur_y = -1;
    tabs = 0;
    npauses = 0;
    npicks = total_picks = 0;
    last_pick = NULL;
    nsnips = 0;
    last_char_t = active_ms = chars = backspaces = 0;
    memset(gap_hist, 0, sizeof gap_hist);
    nsaves = 0;
}

void inv_pause(uint32_t from_ms, uint32_t to_ms)
{
    if (npauses < MAX_PAUSES) {
        pauses[npauses].from = from_ms;
        pauses[npauses].to = to_ms;
        pauses[npauses].checked = false;
        npauses++;
    }
}

static bool all_released(void)
{
    return cur_mod == 0 && cur_code == 0 && cur_buttons == 0;
}

static bool in_pause(uint32_t t)
{
    for (int i = 0; i < npauses; i++) {
        if (t >= pauses[i].from && t < pauses[i].to) {
            return true;
        }
    }
    return false;
}

static uint32_t paused_between(uint32_t a, uint32_t b)
{
    uint32_t sum = 0;
    for (int i = 0; i < npauses; i++) {
        uint32_t lo = pauses[i].from > a ? pauses[i].from : a;
        uint32_t hi = pauses[i].to < b ? pauses[i].to : b;
        if (hi > lo) {
            sum += hi - lo;
        }
    }
    return sum;
}

/* Everything must be up RELEASE_MS after a pause starts. */
static void check_pause_release(uint32_t t)
{
    for (int i = 0; i < npauses; i++) {
        if (!pauses[i].checked && t > pauses[i].from + RELEASE_MS) {
            pauses[i].checked = true;
            if (!all_released()) {
                violation(pauses[i].from, "keys or buttons still held 100 ms after a pause started");
            }
        }
    }
}

static bool inside(const rect_t *r, int32_t x, int32_t y)
{
    return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}

static void on_key_down(uint32_t t, uint8_t mod, uint8_t code)
{
    if ((mod & KMOD_ALT) && code == KEY_TAB) {
        chrome_front = !chrome_front;
        omnibox = false;
    } else if (mod == KMOD_CTRL && code == KEY_GRAVE && !chrome_front) {
        terminal_open = !terminal_open;
    } else if (mod == KMOD_CTRL && code == KEY_T && chrome_front) {
        if (searched && t - last_search < cfg.searchgap_ms) {
            violation(t, "two searches closer than SEARCHGAP");
        }
        searched = true;
        last_search = t;
        tabs++;
        omnibox = true;
    } else if (mod == KMOD_CTRL && code == KEY_W && chrome_front) {
        if (tabs > 0) {
            tabs--;
        } else {
            violation(t, "Ctrl+W with no search tab open (closes a documentation tab)");
        }
        omnibox = false;
    } else if (code == KEY_ENTER && chrome_front && omnibox) {
        omnibox = false;
        return;
    } else if (mod == KMOD_CTRL && code == 0x16) {
        if (nsaves < MAX_SAVES) {
            saves[nsaves++] = t;
        }
    }
    if (code == KEY_BACKSPACE) {
        backspaces++;
    }
    char c = (mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) ? 0 : keys_to_ascii(mod, code);
    if (c && c != '\n' && c != '\t' && chrome_front && !omnibox) {
        violation(t, "typing into a Chrome page (only the address bar after Ctrl+T)");
    }
    if (c && c != '\n' && c != '\t') {
        if (chars > 0) {
            uint32_t g = t - last_char_t;
            static const uint32_t edges[5] = {100, 250, 500, 1000, 3000};
            int b = 0;
            while (b < 5 && g >= edges[b]) {
                b++;
            }
            gap_hist[b]++;
            if (g < 3000) {
                active_ms += g;
            }
        }
        chars++;
        last_char_t = t;
    }
}

void inv_event(const sim_event_t *e)
{
    uint32_t t = e->t;
    check_pause_release(t);
    if (have_report) {
        uint32_t gap = t - last_t - paused_between(last_t, t);
        if (gap > longest_gap) {
            longest_gap = gap;
        }
        if (gap > cfg.max_gap_ms) {
            violation(t, "gap between reports over the limit");
        }
    }
    have_report = true;
    last_t = t;

    if (e->kind == 'K') {
        if (e->code != cur_code) {
            if (cur_code && t - code_t > MAX_HELD_MS) {
                violation(t, "key held longer than 1 s");
            }
            if (e->code) {
                code_t = t;
                if (in_pause(t)) {
                    violation(t, "key pressed during a pause");
                }
                on_key_down(t, e->mod, e->code);
            }
        }
        if ((e->mod != 0) != (cur_mod != 0)) {
            if (cur_mod && t - mod_t > MAX_HELD_MS) {
                violation(t, "modifier held longer than 1 s");
            }
            mod_t = t;
        }
        cur_code = e->code;
        cur_mod = e->mod;
        return;
    }

    int32_t x = pointer_units_to_px(e->x, cfg.w), y = pointer_units_to_px(e->y, cfg.h);
    if (!inside(&cfg.safe, x, y)) {
        violation(t, "pointer outside the safe box");
    }
    if (in_pause(t) && (e->wheel || ((x != cur_x || y != cur_y) && cur_x >= 0))) {
        violation(t, "pointer moved or scrolled during a pause");
    }
    cur_x = x;
    cur_y = y;
    if (e->buttons && !cur_buttons) {
        if (in_pause(t)) {
            violation(t, "button pressed during a pause");
        }
        if (chrome_front) {
            violation(t, "click while Chrome is in front");
        }
        /* VS Code is in front here: only its regions count, the terminal only while open */
        bool in_region = false;
        for (int i = 0; i < cfg.nregions; i++) {
            int kind = cfg.region_kind[i];
            bool usable = kind == INV_REGION_CODE || (kind == INV_REGION_TERMINAL && terminal_open);
            in_region |= usable && inside(&cfg.regions[i], x, y);
        }
        if (!chrome_front && !in_region) {
            violation(t, "button pressed outside the regions of the app in front");
        }
    }
    cur_buttons = e->buttons;
}

static int name_slot(const char *name, size_t len)
{
    for (int i = 0; i < npicks; i++) {
        if (picks[i].len == len && memcmp(picks[i].name, name, len) == 0) {
            return i;
        }
    }
    if (npicks == MAX_NAMES) {
        return -1;
    }
    picks[npicks].name = name;
    picks[npicks].len = len;
    picks[npicks].count = 0;
    return npicks++;
}

static void check_boundary(const char *what)
{
    char msg[96];
    if (!all_released()) {
        snprintf(msg, sizeof msg, "%s: something still held", what);
        violation(last_t, msg);
    }
    if (chrome_front || terminal_open || tabs != 0) {
        snprintf(msg, sizeof msg, "%s: %s", what,
                 chrome_front ? "Chrome in front" : (terminal_open ? "terminal open" : "search tab open"));
        violation(last_t, msg);
    }
}

void inv_trace(const script_event_t *ev)
{
    if (ev->kind == TRACE_SNIPPET) {
        int i = 0;
        while (i < nsnips && !(snips[i].len == ev->len && memcmp(snips[i].pool, ev->name, ev->len) == 0)) {
            i++;
        }
        if (i == nsnips && nsnips < MAX_NAMES) {
            snips[nsnips].pool = ev->name;
            snips[nsnips].len = ev->len;
            snips[nsnips].last = -1;
            nsnips++;
        }
        char pool[64];
        snprintf(pool, sizeof pool, "%.*s", (int)ev->len, ev->name);
        bool single = script_pool_count(pool) == 1; /* e.g. the skeleton: repeating is the only option */
        if (i < nsnips && !single) {
            if (snips[i].last == ev->index) {
                violation(last_t, "same snippet twice in a row");
            }
            snips[i].last = ev->index;
        }
        return;
    }
    if (ev->depth != 1 || (ev->kind != TRACE_PICK && ev->kind != TRACE_PICK_END)) {
        return;
    }
    if (ev->kind == TRACE_PICK) {
        check_boundary("activity start");
        if (last_pick && last_pick_len == ev->len && memcmp(last_pick, ev->name, ev->len) == 0) {
            violation(last_t, "same activity twice in a row");
        }
        last_pick = ev->name;
        last_pick_len = ev->len;
        int s = name_slot(ev->name, ev->len);
        if (s >= 0) {
            picks[s].count++;
        }
        total_picks++;
    } else {
        check_boundary("activity end");
    }
}

int inv_pick_total(void)
{
    return total_picks;
}

int inv_pick_count(const char *name, size_t len)
{
    for (int i = 0; i < npicks; i++) {
        if (picks[i].len == len && memcmp(picks[i].name, name, len) == 0) {
            return picks[i].count;
        }
    }
    return 0;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : (x > y);
}

int inv_finish(FILE *report)
{
    check_pause_release(UINT32_MAX);
    if (!all_released()) {
        violation(last_t, "keys or buttons still held at the end of the run");
    }
    if (!report) {
        return violations;
    }
    fprintf(report, "activities (%d picks):\n", total_picks);
    for (int i = 0; i < npicks; i++) {
        fprintf(report, "  %-16.*s %5d  %5.1f%%\n", (int)picks[i].len, picks[i].name, picks[i].count,
                total_picks ? 100.0 * picks[i].count / total_picks : 0.0);
    }
    fprintf(report, "typing: %u chars, %.0f cpm while typing, %.1f backspaces per 100 chars\n", (unsigned)chars,
            active_ms ? chars * 60000.0 / active_ms : 0.0, chars ? 100.0 * backspaces / chars : 0.0);
    fprintf(report, "inter-key gaps: <100 %u, <250 %u, <500 %u, <1000 %u, <3000 %u, >=3000 %u\n", (unsigned)gap_hist[0],
            (unsigned)gap_hist[1], (unsigned)gap_hist[2], (unsigned)gap_hist[3], (unsigned)gap_hist[4],
            (unsigned)gap_hist[5]);
    if (nsaves > 1) {
        static uint32_t d[MAX_SAVES];
        for (int i = 1; i < nsaves; i++) {
            d[i - 1] = saves[i] - saves[i - 1];
        }
        qsort(d, (size_t)(nsaves - 1), sizeof d[0], cmp_u32);
        fprintf(report, "Ctrl+S interval: min %u s, median %u s, max %u s\n", (unsigned)(d[0] / 1000),
                (unsigned)(d[(nsaves - 1) / 2] / 1000), (unsigned)(d[nsaves - 2] / 1000));
    }
    fprintf(report, "longest gap between reports: %.1f s\n", longest_gap / 1000.0);
    fprintf(report, "violations: %d\n", violations);
    return violations;
}
