#include "script.h"

#include <string.h>

#include "hid_out.h"
#include "keys.h"
#include "rhythm.h"
#include "strutil.h"
#include "typing.h"

#define MAX_DEFS    48
#define MAX_POOLS   24
#define MAX_REGIONS 8
#define MAX_CHOICES 16

typedef struct {
    const char *p;
    size_t n;
} span_t;

/* body..end are the lines between the DEF/POOL line and its END line. */
typedef struct {
    span_t name;
    const char *body, *end;
} def_t;

typedef struct {
    span_t name;
    const char *body, *end;
    bool lines;
} pool_t;

typedef struct {
    span_t name;
    region_t spec;
    rect_t rect;
    bool valid;
} region_ent_t;

static const char *dev_text, *dev_end, *snip_text, *snip_end;
static def_t defs[MAX_DEFS];
static int ndefs;
static pool_t pools[MAX_POOLS];
static int npools;
static region_ent_t regions[MAX_REGIONS];
static int nregions;
static script_settings_t settings;
static int problems;

#define MAX_CALL_DEPTH  8
#define MAX_BLOCK_DEPTH 8
#define MAX_CHOOSE_MEM  32

static script_state_t state;
static void (*trace_fn)(const script_event_t *ev, void *ctx);
static void *trace_ctx;
static int call_depth;
/* Last pick of each CHOOSE, keyed by the CHOOSE line. */
static struct {
    const char *at;
    int last;
} choose_mem[MAX_CHOOSE_MEM];
static int nchoose_mem;
/* Last item picked from each pool, so no item repeats back to back. */
static int pool_last[MAX_POOLS];
/* What the last input was, for the keyboard/mouse hand-off pauses. */
static enum { INPUT_NONE, INPUT_KEY, INPUT_POINTER, INPUT_CLICK } last_input;

static void reset_run_state(void);
static bool idle_nudge(void);
static void trace(script_trace_t kind, span_t name, int depth, int index);
static int call_def(const def_t *d);
static int32_t arg_int(span_t *args);

/* ---- text helpers ---- */

typedef struct {
    const char *p;
    size_t n;          /* without '\n' and a trailing '\r' */
    const char *next;  /* start of the following line */
} line_t;

static bool read_line(const char *p, const char *end, line_t *l)
{
    if (!p || p >= end) {
        return false;
    }
    const char *e = memchr(p, '\n', (size_t)(end - p));
    const char *stop = e ? e : end;
    l->p = p;
    l->n = (size_t)(stop - p);
    if (l->n && p[l->n - 1] == '\r') {
        l->n--;
    }
    l->next = e ? e + 1 : end;
    return true;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static span_t trim(const char *p, size_t n)
{
    while (n && is_space(*p)) {
        p++;
        n--;
    }
    while (n && is_space(p[n - 1])) {
        n--;
    }
    span_t s = {p, n};
    return s;
}

/* First whitespace-separated token of *s; *s becomes the trimmed rest. */
static span_t take(span_t *s)
{
    size_t i = 0;
    while (i < s->n && !is_space(s->p[i])) {
        i++;
    }
    span_t t = {s->p, i};
    *s = trim(s->p + i, s->n - i);
    return t;
}

/* The command of a script line, or an empty span for blank lines and comments. */
static span_t command(const line_t *l, span_t *args)
{
    span_t r = trim(l->p, l->n);
    span_t none = {l->p, 0};
    if (r.n == 0 || r.p[0] == '#') {
        *args = none;
        return none;
    }
    span_t c = take(&r);
    *args = r;
    return c;
}

static bool is(span_t s, const char *lit)
{
    return str_ieq(s.p, s.n, lit);
}

static bool same(span_t a, span_t b)
{
    if (a.n != b.n) {
        return false;
    }
    char buf[64];
    if (b.n >= sizeof buf) {
        return false;
    }
    memcpy(buf, b.p, b.n);
    buf[b.n] = '\0';
    return str_ieq(a.p, a.n, buf);
}

static bool parse_int(span_t s, int32_t *out)
{
    size_t i = 0;
    bool neg = false;
    if (s.n && (s.p[0] == '+' || s.p[0] == '-')) {
        neg = s.p[0] == '-';
        i = 1;
    }
    if (i >= s.n) {
        return false;
    }
    int64_t v = 0;
    for (; i < s.n; i++) {
        if (s.p[i] < '0' || s.p[i] > '9') {
            return false;
        }
        v = v * 10 + (s.p[i] - '0');
        if (v > 100000000) {
            return false;
        }
    }
    *out = (int32_t)(neg ? -v : v);
    return true;
}

static bool parse_uint(span_t s, int32_t *out)
{
    return s.n && s.p[0] != '-' && s.p[0] != '+' && parse_int(s, out);
}

static int line_no(const char *at)
{
    const char *base = (at >= dev_text && at <= dev_end) ? dev_text : snip_text;
    int n = 1;
    for (const char *p = base; p && p < at; p++) {
        n += *p == '\n';
    }
    return n;
}

static void problem(const char *at, const char *msg, span_t detail)
{
    problems++;
    hid_out_log("script: %s line %d: %s '%.*s'", (at >= dev_text && at <= dev_end) ? "developer.txt" : "snippets.txt",
                line_no(at), msg, (int)detail.n, detail.p);
}

static bool is_opener(span_t c)
{
    return is(c, "CHOOSE") || is(c, "MAYBE") || is(c, "REPEAT") || is(c, "WHEN");
}

/* Start of the END line matching an opener whose body starts at p, or NULL (a DEF line or the
 * end of the text comes first). */
static const char *find_end(const char *p, const char *end)
{
    int depth = 0;
    line_t l;
    while (read_line(p, end, &l)) {
        span_t args, c = command(&l, &args);
        if (is(c, "END")) {
            if (depth == 0) {
                return l.p;
            }
            depth--;
        } else if (is(c, "DEF")) {
            return NULL;
        } else if (is_opener(c)) {
            depth++;
        }
        p = l.next;
    }
    return NULL;
}

/* Start of the next DEF line at or after p, or end. */
static const char *next_def(const char *p, const char *end)
{
    line_t l;
    while (read_line(p, end, &l)) {
        span_t args, c = command(&l, &args);
        if (is(c, "DEF")) {
            return l.p;
        }
        p = l.next;
    }
    return end;
}

static const def_t *find_def(span_t name)
{
    for (int i = 0; i < ndefs; i++) {
        if (same(defs[i].name, name)) {
            return &defs[i];
        }
    }
    return NULL;
}

static const def_t *find_def_c(const char *name)
{
    span_t s = {name, strlen(name)};
    return find_def(s);
}

static const pool_t *find_pool(span_t name)
{
    for (int i = 0; i < npools; i++) {
        if (same(pools[i].name, name)) {
            return &pools[i];
        }
    }
    return NULL;
}

static const pool_t *find_pool_c(const char *name)
{
    span_t s = {name, strlen(name)};
    return find_pool(s);
}

static const region_ent_t *find_region(span_t name)
{
    for (int i = 0; i < nregions; i++) {
        if (same(regions[i].name, name)) {
            return &regions[i];
        }
    }
    return NULL;
}

/* ---- pools ---- */

/* Walk a pool's items. Returns the number of items before stopping; when item want exists it
 * stops there, sets *s and *len and returns want + 1. */
static int pool_scan(const pool_t *pl, int want, const char **s, size_t *len)
{
    int idx = 0;
    const char *p = pl->body;
    line_t l;
    if (pl->lines) {
        while (read_line(p, pl->end, &l)) {
            span_t t = trim(l.p, l.n);
            if (t.n) {
                if (idx == want) {
                    *s = t.p;
                    *len = t.n;
                    return idx + 1;
                }
                idx++;
            }
            p = l.next;
        }
        return idx;
    }
    const char *start = p;
    for (;;) {
        bool at_end = !read_line(p, pl->end, &l);
        bool sep = !at_end && l.n == 3 && memcmp(l.p, "---", 3) == 0;
        if (at_end || sep) {
            const char *stop = at_end ? pl->end : l.p;
            size_t n = (size_t)(stop - start);
            if (n && start[n - 1] == '\n') {
                n--;
            }
            if (n && start[n - 1] == '\r') {
                n--;
            }
            if (n > 0) {
                if (idx == want) {
                    *s = start;
                    *len = n;
                    return idx + 1;
                }
                idx++;
            }
            if (at_end) {
                return idx;
            }
            start = l.next;
        }
        p = l.next;
    }
}

static int pool_count_of(const pool_t *pl)
{
    const char *s;
    size_t n;
    return pool_scan(pl, -1, &s, &n);
}

int script_pool_total(void)
{
    return npools;
}

const char *script_pool_name(int i, size_t *len)
{
    if (i < 0 || i >= npools) {
        *len = 0;
        return NULL;
    }
    *len = pools[i].name.n;
    return pools[i].name.p;
}

int script_pool_count(const char *name)
{
    const pool_t *pl = find_pool_c(name);
    return pl ? pool_count_of(pl) : 0;
}

bool script_pool_item(const char *name, int i, const char **s, size_t *len)
{
    const pool_t *pl = find_pool_c(name);
    return pl && i >= 0 && pool_scan(pl, i, s, len) > i;
}

bool script_pool_is_lines(const char *name)
{
    const pool_t *pl = find_pool_c(name);
    return pl && pl->lines;
}

/* ---- loading ---- */

static bool parse_offset(span_t t, int16_t *v, bool *from_end)
{
    int32_t x;
    if (!parse_int(t, &x) || x < -30000 || x > 30000) {
        return false;
    }
    *from_end = t.p[0] == '-';
    *v = (int16_t)(x < 0 ? -x : x);
    return true;
}

static void load_setting(const line_t *l, span_t c, span_t args)
{
    int32_t a, b, d;
    if (is(c, "SCREEN")) {
        span_t t1 = take(&args), t2 = take(&args), t3 = take(&args);
        if (parse_uint(t1, &a) && parse_uint(t2, &b) && parse_uint(t3, &d) && a >= 2 && b >= 2 && d > 0 && a < 65536 &&
            b < 65536 && d < 1000) {
            settings.w = (uint16_t)a;
            settings.h = (uint16_t)b;
            settings.scale = (uint16_t)d;
        } else {
            problem(l->p, "bad number", trim(l->p, l->n));
        }
    } else if (is(c, "MAXGAP") || is(c, "SEARCHGAP")) {
        if (parse_uint(take(&args), &a) && a > 0) {
            *(is(c, "MAXGAP") ? &settings.maxgap_s : &settings.searchgap_s) = (uint32_t)a;
        } else {
            problem(l->p, "bad number", trim(l->p, l->n));
        }
    } else { /* REGION name x0 y0 x1 y1 */
        span_t name = take(&args);
        region_ent_t r = {name, {{0, 0, 0, 0}, 0}, {0, 0, 0, 0}, false};
        bool ok = name.n > 0;
        for (int i = 0; i < 4 && ok; i++) {
            bool fe;
            ok = parse_offset(take(&args), &r.spec.v[i], &fe);
            if (fe) {
                r.spec.from_end |= (uint8_t)(1u << i);
            }
        }
        if (!ok) {
            problem(l->p, "bad number", trim(l->p, l->n));
        } else if (nregions < MAX_REGIONS) {
            regions[nregions++] = r;
        } else {
            problem(l->p, "too many regions", name);
        }
    }
}

static void scan_dev(void)
{
    const char *p = dev_text;
    line_t l;
    while (read_line(p, dev_end, &l)) {
        span_t args, c = command(&l, &args);
        p = l.next;
        if (c.n == 0) {
            continue;
        }
        if (is(c, "DEF")) {
            span_t name = take(&args);
            const char *end = find_end(l.next, dev_end);
            def_t d = {name, l.next, end};
            if (!end) {
                problem(l.p, "unbalanced END (DEF without END)", name);
                d.end = next_def(l.next, dev_end);
                p = d.end;
            } else {
                line_t endl;
                read_line(end, dev_end, &endl);
                p = endl.next;
            }
            if (find_def(name)) {
                problem(l.p, "duplicate DEF", name);
            } else if (ndefs < MAX_DEFS) {
                defs[ndefs++] = d;
            } else {
                problem(l.p, "too many DEFs", name);
            }
        } else if (is(c, "SCREEN") || is(c, "MAXGAP") || is(c, "SEARCHGAP") || is(c, "REGION")) {
            load_setting(&l, c, args);
        } else if (is(c, "END")) {
            problem(l.p, "unbalanced END", c);
        } else {
            problem(l.p, "unknown command", c);
        }
    }
}

static void scan_snip(void)
{
    const char *p = snip_text;
    line_t l;
    while (read_line(p, snip_end, &l)) {
        span_t args, c = command(&l, &args);
        p = l.next;
        if (c.n == 0) {
            continue;
        }
        if (!is(c, "POOL")) {
            problem(l.p, "unknown command", c);
            continue;
        }
        span_t name = take(&args);
        span_t kind = take(&args);
        pool_t pl = {name, l.next, NULL, is(kind, "LINES")};
        if (kind.n && !pl.lines) {
            problem(l.p, "unknown POOL kind", kind);
        }
        /* the body is literal up to a line that is exactly END */
        line_t b;
        const char *q = l.next;
        while (read_line(q, snip_end, &b)) {
            if (b.n == 3 && memcmp(b.p, "END", 3) == 0) {
                pl.end = b.p;
                p = b.next;
                break;
            }
            q = b.next;
        }
        if (!pl.end) {
            problem(l.p, "unbalanced END (POOL without END)", name);
            pl.end = snip_end;
            p = snip_end;
        }
        if (find_pool(name)) {
            problem(l.p, "duplicate POOL", name);
        } else if (npools < MAX_POOLS) {
            pools[npools++] = pl;
        } else {
            problem(l.p, "too many POOLs", name);
        }
    }
}

/* ---- validation ---- */

static bool move_only(const def_t *d)
{
    const char *p = d->body;
    line_t l;
    while (read_line(p, d->end, &l)) {
        span_t args, c = command(&l, &args);
        if (c.n && !is(c, "GOTO") && !is(c, "HOVER") && !is(c, "SCROLL") && !is(c, "READSCROLL") && !is(c, "IDLE") &&
            !is(c, "WAIT")) {
            return false;
        }
        p = l.next;
    }
    return true;
}

static void need_region(const line_t *l, span_t name)
{
    if (!find_region(name)) {
        problem(l->p, "unknown region", name);
    }
}

static void need_lines_pool(const line_t *l, span_t name, const char *msg)
{
    const pool_t *pl = find_pool(name);
    if (!pl) {
        problem(l->p, "unknown pool", name);
    } else if (!pl->lines) {
        problem(l->p, msg, name);
    }
}

static void need_numbers(const line_t *l, span_t args, int count, bool allow_sign)
{
    for (int i = 0; i < count; i++) {
        int32_t v;
        span_t t = take(&args);
        if (!(allow_sign ? parse_int(t, &v) : parse_uint(t, &v))) {
            problem(l->p, "bad number", t);
            return;
        }
    }
}

/* Validate one command line inside a DEF body. */
static void validate_cmd(const line_t *l, span_t c, span_t args, bool is_main)
{
    if (is(c, "KEY")) {
        uint8_t m, k;
        if (!keys_parse_combo(args.p, args.n, &m, &k)) {
            problem(l->p, "unknown key", args);
        }
    } else if (is(c, "TYPE")) {
        if (args.n && args.p[0] == '@') {
            span_t name = {args.p + 1, args.n - 1};
            need_lines_pool(l, name, "TYPE @ needs a LINES pool");
        }
    } else if (is(c, "CODE")) {
        span_t name = take(&args), mid = take(&args);
        const pool_t *pl = find_pool(name);
        if (!pl) {
            problem(l->p, "unknown pool", name);
        } else if (pl->lines) {
            problem(l->p, "CODE needs a snippet pool", name);
        }
        if (mid.n) {
            const def_t *d = find_def(mid);
            if (!d) {
                problem(l->p, "unknown CALL target", mid);
            } else if (!move_only(d)) {
                problem(l->p, "midway DEF may only move", mid);
            }
        }
    } else if (is(c, "GOTO") || is(c, "OUTLINE") || is(c, "DRAGSELECT")) {
        need_region(l, take(&args));
    } else if (is(c, "HOVER")) {
        need_region(l, take(&args));
        need_numbers(l, args, 1, false);
    } else if (is(c, "CLICK")) {
        if (args.n && !is(args, "LEFT") && !is(args, "RIGHT") && !is(args, "MIDDLE")) {
            problem(l->p, "unknown button", args);
        }
    } else if (is(c, "DBLCLICK") || is(c, "ENDSEARCH")) {
        /* no arguments */
    } else if (is(c, "SCROLL")) {
        need_numbers(l, args, 1, true);
    } else if (is(c, "READSCROLL") || is(c, "WAIT") || is(c, "MAYBE")) {
        need_numbers(l, args, 1, false);
    } else if (is(c, "IDLE") || is(c, "REPEAT")) {
        need_numbers(l, args, 2, false);
    } else if (is(c, "WHEN")) {
        span_t w = take(&args), op = take(&args), n = take(&args);
        int32_t v;
        if (!is(w, "LINES") || !(op.n == 1 && op.p[0] == '>') || !parse_uint(n, &v) || args.n) {
            problem(l->p, "bad WHEN (expected WHEN LINES > n)", trim(l->p, l->n));
        }
    } else if (is(c, "LINES")) {
        int32_t v;
        if (!parse_int(args, &v) || (args.p[0] == '-')) {
            problem(l->p, "bad number", args);
        }
    } else if (is(c, "TERMINAL")) {
        if (!is(args, "open") && !is(args, "close")) {
            problem(l->p, "TERMINAL needs open or close", args);
        }
    } else if (is(c, "FOCUS")) {
        if (!is(args, "code") && !is(args, "chrome")) {
            problem(l->p, "FOCUS needs code or chrome", args);
        }
    } else if (is(c, "SEARCH")) {
        need_lines_pool(l, take(&args), "SEARCH needs a LINES pool");
    } else if (is(c, "CALL")) {
        if (!find_def(args)) {
            problem(l->p, "unknown CALL target", args);
        }
    } else if (is(c, "LOOP")) {
        if (!is_main) {
            problem(l->p, "LOOP outside MAIN", c);
        }
    } else {
        problem(l->p, "unknown command", c);
    }
}

static void validate_def(const def_t *d)
{
    bool is_main = is(d->name, "MAIN");
    const char *p = d->body;
    line_t l;
    bool in_choose = false;
    const char *choose_at = NULL;
    int32_t weights[MAX_CHOICES];
    int nw = 0;
    while (read_line(p, d->end, &l)) {
        span_t args, c = command(&l, &args);
        p = l.next;
        if (c.n == 0) {
            continue;
        }
        if (in_choose) {
            if (is(c, "END")) {
                int32_t sum = 0, max = 0;
                for (int i = 0; i < nw; i++) {
                    sum += weights[i];
                    max = weights[i] > max ? weights[i] : max;
                }
                if (nw == 0 || sum == 0) {
                    problem(choose_at, "empty CHOOSE", c);
                } else if (max * 100 > sum * 50) {
                    problem(choose_at, "CHOOSE weight over 50%", c);
                }
                in_choose = false;
                continue;
            }
            int32_t w;
            span_t name = take(&args);
            if (!parse_uint(c, &w)) {
                problem(l.p, "bad number", c);
            } else if (!find_def(name)) {
                problem(l.p, "unknown CALL target", name);
            } else if (nw < MAX_CHOICES) {
                weights[nw++] = w;
            } else {
                problem(l.p, "too many CHOOSE entries", name);
            }
            continue;
        }
        if (is(c, "CHOOSE")) {
            in_choose = true;
            choose_at = l.p;
            nw = 0;
            continue;
        }
        if (is(c, "END")) {
            continue; /* closes MAYBE/REPEAT/WHEN; find_end already matched it */
        }
        validate_cmd(&l, c, args, is_main);
    }
}

static void apply_settings(void)
{
    pointer_set_screen(settings.w, settings.h, settings.scale);
    rect_t box = {0, 0, 0, 0};
    bool any = false;
    for (int i = 0; i < nregions; i++) {
        regions[i].rect = pointer_resolve(&regions[i].spec);
        regions[i].valid = pointer_rect_valid(&regions[i].rect);
        if (!regions[i].valid) {
            problems++;
            hid_out_log("script: empty region %.*s (check SCREEN)", (int)regions[i].name.n, regions[i].name.p);
            continue;
        }
        const rect_t *r = &regions[i].rect;
        if (!any) {
            box = *r;
            any = true;
        } else {
            box.x0 = r->x0 < box.x0 ? r->x0 : box.x0;
            box.y0 = r->y0 < box.y0 ? r->y0 : box.y0;
            box.x1 = r->x1 > box.x1 ? r->x1 : box.x1;
            box.y1 = r->y1 > box.y1 ? r->y1 : box.y1;
        }
    }
    if (any) {
        pointer_set_safe(&box);
    }
    rhythm_set_maxgap_s(settings.maxgap_s);
}

bool script_load(const char *dev, size_t dev_len, const char *snip, size_t snip_len)
{
    dev_text = dev;
    dev_end = dev + dev_len;
    snip_text = snip;
    snip_end = snip + snip_len;
    ndefs = npools = nregions = 0;
    problems = 0;
    reset_run_state();
    settings.w = 0;
    settings.h = 0;
    settings.scale = 0;
    settings.maxgap_s = 210;
    settings.searchgap_s = 360;

    scan_dev();
    scan_snip();
    for (int i = 0; i < ndefs; i++) {
        validate_def(&defs[i]);
    }
    if (settings.w == 0) {
        hid_out_log("script: SCREEN missing, using 1920 1080 100");
        settings.w = 1920;
        settings.h = 1080;
        settings.scale = 100;
    }
    apply_settings();
    rhythm_set_idle_hook(idle_nudge);
    if (!find_def_c("MAIN")) {
        problems++;
        hid_out_log("script: missing MAIN, refusing to run");
        return false;
    }
    return true;
}

int script_problems(void)
{
    return problems;
}

const script_settings_t *script_settings(void)
{
    return &settings;
}

bool script_has_def(const char *name)
{
    return find_def_c(name) != NULL;
}

bool script_region_spec(const char *name, region_t *out)
{
    span_t s = {name, strlen(name)};
    const region_ent_t *r = find_region(s);
    if (r) {
        *out = r->spec;
    }
    return r != NULL;
}

/* ---- interpreter ---- */

enum { RUN_OK, RUN_ABORT, RUN_LOOP };

static void reset_run_state(void)
{
    memset(&state, 0, sizeof state);
    state.fg = APP_CODE;
    nchoose_mem = 0;
    call_depth = 0;
    last_input = INPUT_NONE;
    for (int i = 0; i < MAX_POOLS; i++) {
        pool_last[i] = -1;
    }
}

/* ---- input commands ---- */

#define KEY_LETTER(c) ((uint8_t)(KEY_A + ((c) - 'a')))

static const rect_t *region_rect(span_t name)
{
    const region_ent_t *r = find_region(name);
    return r ? &r->rect : NULL;
}

static const rect_t *region_rect_c(const char *name)
{
    span_t s = {name, strlen(name)};
    return region_rect(s);
}

/* The region of whatever is in front: Chrome's page, the terminal panel, or the editor. */
static const rect_t *fg_rect(void)
{
    if (state.fg == APP_CHROME) {
        return region_rect_c("chrome_page");
    }
    if (state.terminal_open) {
        const rect_t *t = region_rect_c("terminal");
        if (t) {
            return t;
        }
    }
    return region_rect_c("editor");
}

static bool idle_nudge(void)
{
    const rect_t *r = fg_rect();
    return r ? pointer_nudge(r) : true;
}

/* Random item of a pool, never the one picked last time. */
static bool pool_pick(const pool_t *pl, const char **s, size_t *len, int *index)
{
    int n = pool_count_of(pl);
    if (n == 0) {
        return false;
    }
    int *last = &pool_last[pl - pools];
    int i = (int)rhythm_rand(0, (uint32_t)(n - 1));
    if (n > 1 && i == *last) {
        i = (i + 1 + (int)rhythm_rand(0, (uint32_t)(n - 2))) % n;
    }
    *last = i;
    *index = i;
    return pool_scan(pl, i, s, len) > i;
}

static int lines_in(const char *s, size_t n)
{
    int count = 1;
    for (size_t i = 0; i < n; i++) {
        count += s[i] == '\n';
    }
    return count;
}

/* Both return false if the pause came during the hand-off, so nothing more is sent. */
static bool handoff_to_pointer(void)
{
    return last_input != INPUT_KEY || rhythm_sleep(rhythm_rand(250, 900));
}

static bool handoff_to_keys(void)
{
    return last_input != INPUT_CLICK || rhythm_sleep(rhythm_rand(200, 700));
}

static bool focus(script_app_t app)
{
    if (state.fg == app) {
        return true;
    }
    if (!handoff_to_keys()) {
        return false;
    }
    bool tab = false;
    bool ok = typing_alt_tab(&tab);
    if (tab) {
        state.fg = app; /* releasing Alt completes the switch even if we are interrupted */
    }
    last_input = INPUT_KEY;
    return ok;
}

static bool key_flip(uint8_t mod, uint8_t code, bool *flag, bool value)
{
    if (!handoff_to_keys()) {
        return false;
    }
    bool pressed = false;
    bool ok = typing_key(mod, code, &pressed);
    if (pressed) {
        *flag = value;
    }
    last_input = INPUT_KEY;
    return ok;
}

static bool cmd_search(span_t args)
{
    if (state.fg != APP_CHROME) {
        hid_out_log("script: SEARCH needs chrome in front, skipped");
        return true;
    }
    uint32_t now = hid_out_now_ms();
    if (state.searched && (uint32_t)(now - state.last_search_ms) < settings.searchgap_s * 1000u) {
        return true;
    }
    const pool_t *pl = find_pool(take(&args));
    const char *q;
    size_t n;
    int idx;
    if (!pl || !pool_pick(pl, &q, &n, &idx)) {
        return true;
    }
    if (!handoff_to_keys()) {
        return false;
    }
    bool pressed = false;
    bool ok = typing_key(KMOD_CTRL, KEY_LETTER('t'), &pressed);
    if (pressed) {
        state.search_open = true;
        state.searched = true;
        state.last_search_ms = now;
    }
    last_input = INPUT_KEY;
    ok = ok && rhythm_sleep(rhythm_rand(400, 900)) && typing_text(q, n, TYPE_OMNIBOX);
    ok = ok && typing_key(0, KEY_DELETE, NULL); /* drop any inline autocomplete */
    if (ok && rhythm_rand(0, 99) < 15) {
        ok = rhythm_sleep(rhythm_rand(300, 800)) && typing_key(0, KEY_DOWN, NULL);
    }
    return ok && rhythm_sleep(rhythm_rand(200, 600)) && typing_key(0, KEY_ENTER, NULL);
}

typedef struct {
    const def_t *def;
} midway_ctx_t;

static void run_midway(void *ctx)
{
    const def_t *d = ((midway_ctx_t *)ctx)->def;
    if (rhythm_rand(0, 99) < 40) {
        trace(TRACE_CALL, d->name, call_depth + 1, 0);
        call_def(d);
        last_input = INPUT_KEY; /* typing resumes */
    }
}

static bool cmd_code(span_t args)
{
    span_t name = take(&args), mid = take(&args);
    const pool_t *pl = find_pool(name);
    const char *s;
    size_t n;
    int idx;
    if (!pl || !pool_pick(pl, &s, &n, &idx)) {
        return true;
    }
    trace(TRACE_SNIPPET, pl->name, call_depth, idx);
    if (!handoff_to_keys()) {
        return false;
    }
    last_input = INPUT_KEY;
    state.lines += lines_in(s, n); /* counted up front: an over-estimate is safe */
    midway_ctx_t ctx = {mid.n ? find_def(mid) : NULL};
    return typing_snippet(s, n, ctx.def ? run_midway : NULL, &ctx);
}

/* Pointer commands; clicks are never sent while Chrome is in front. */
static bool cmd_pointer(span_t c, span_t args)
{
    bool click = is(c, "CLICK") || is(c, "DBLCLICK") || is(c, "DRAGSELECT");
    if (click && state.fg == APP_CHROME) {
        hid_out_log("script: click suppressed while chrome is in front");
        return true;
    }
    if (!handoff_to_pointer()) {
        return false;
    }
    last_input = click ? INPUT_CLICK : INPUT_POINTER;
    if (is(c, "CLICK")) {
        uint8_t b = is(args, "RIGHT") ? BTN_RIGHT : (is(args, "MIDDLE") ? BTN_MIDDLE : BTN_LEFT);
        return pointer_click(b);
    }
    if (is(c, "DBLCLICK")) {
        return pointer_dblclick();
    }
    if (is(c, "SCROLL")) {
        return pointer_scroll((int)arg_int(&args));
    }
    if (is(c, "READSCROLL")) {
        const rect_t *r = fg_rect();
        return r ? pointer_readscroll((int)arg_int(&args), r) : true;
    }
    const rect_t *r = region_rect(take(&args));
    if (!r) {
        return true;
    }
    if (is(c, "GOTO")) {
        return pointer_goto(r);
    }
    if (is(c, "HOVER")) {
        return pointer_hover(r, (uint32_t)arg_int(&args));
    }
    if (is(c, "DRAGSELECT")) {
        return pointer_dragselect(r);
    }
    return pointer_outline(r); /* OUTLINE */
}

/* Keyboard and pointer commands. Returns -1 if c is not one of them, else whether it ran
 * without being aborted. */
static int exec_input(span_t c, span_t args)
{
    if (is(c, "KEY")) {
        uint8_t m, k;
        if (!keys_parse_combo(args.p, args.n, &m, &k)) {
            return 1;
        }
        if (!handoff_to_keys()) {
            return false;
        }
        last_input = INPUT_KEY;
        return typing_key(m, k, NULL);
    }
    if (is(c, "TYPE")) {
        if (!handoff_to_keys()) {
            return false;
        }
        last_input = INPUT_KEY;
        if (args.n && args.p[0] == '@') {
            span_t name = {args.p + 1, args.n - 1};
            const pool_t *pl = find_pool(name);
            const char *s;
            size_t n;
            int idx;
            return !pl || !pool_pick(pl, &s, &n, &idx) || typing_text(s, n, 0);
        }
        return typing_text(args.p, args.n, 0);
    }
    if (is(c, "CODE")) {
        return cmd_code(args);
    }
    if (is(c, "FOCUS")) {
        return focus(is(args, "chrome") ? APP_CHROME : APP_CODE);
    }
    if (is(c, "TERMINAL")) {
        bool want = is(args, "open");
        return want == state.terminal_open || key_flip(KMOD_CTRL, KEY_GRAVE, &state.terminal_open, want);
    }
    if (is(c, "SEARCH")) {
        return cmd_search(args);
    }
    if (is(c, "ENDSEARCH")) {
        if (!state.search_open) {
            return 1;
        }
        return focus(APP_CHROME) && key_flip(KMOD_CTRL, KEY_LETTER('w'), &state.search_open, false);
    }
    if (is(c, "GOTO") || is(c, "HOVER") || is(c, "CLICK") || is(c, "DBLCLICK") || is(c, "DRAGSELECT") ||
        is(c, "SCROLL") || is(c, "READSCROLL") || is(c, "OUTLINE")) {
        return cmd_pointer(c, args);
    }
    return -1;
}

void script_set_trace(void (*fn)(const script_event_t *ev, void *ctx), void *ctx)
{
    trace_fn = fn;
    trace_ctx = ctx;
}

const script_state_t *script_state(void)
{
    return &state;
}

static void trace(script_trace_t kind, span_t name, int depth, int index)
{
    if (trace_fn) {
        script_event_t ev = {kind, name.p, name.n, depth, index};
        trace_fn(&ev, trace_ctx);
    }
}

static int run_block(const char *p, const char *end, int block_depth);

/* Run a DEF one level deeper. LOOP only means something in MAIN. */
static int call_def(const def_t *d)
{
    if (call_depth >= MAX_CALL_DEPTH) {
        hid_out_log("script: call depth over %d, skipping %.*s", MAX_CALL_DEPTH, (int)d->name.n, d->name.p);
        return RUN_OK;
    }
    call_depth++;
    int r = run_block(d->body, d->end, 0);
    call_depth--;
    if (r == RUN_LOOP && !is(d->name, "MAIN")) {
        r = RUN_OK;
    }
    return r;
}

typedef struct {
    span_t name;
    int32_t weight;
} choice_t;

static int read_choices(const char *p, const char *end, choice_t *out, int max)
{
    int n = 0;
    line_t l;
    while (read_line(p, end, &l)) {
        span_t args, c = command(&l, &args);
        p = l.next;
        int32_t w;
        if (c.n && parse_uint(c, &w) && n < max) {
            out[n].name = take(&args);
            out[n].weight = w;
            n++;
        }
    }
    return n;
}

/* Weights whose no-immediate-repeat chain has long-run shares equal to the configured ones.
 * Shares of P(j|i) = v_j / (1 - v_i) are proportional to v_j (1 - v_j), so solve
 * v_j (1 - v_j) = k w_j with sum v = 1 by fixed-point iteration (every w_j < 0.5). */
static void compensate(const choice_t *c, int n, float *v)
{
    float w[MAX_CHOICES], sum = 0;
    for (int i = 0; i < n; i++) {
        sum += (float)c[i].weight;
    }
    for (int i = 0; i < n; i++) {
        w[i] = sum > 0 ? (float)c[i].weight / sum : 1.0f / (float)n;
        v[i] = w[i];
    }
    for (int it = 0; it < 40; it++) {
        float s = 0;
        for (int i = 0; i < n; i++) {
            float denom = 1.0f - v[i];
            v[i] = w[i] / (denom > 0.01f ? denom : 0.01f);
            s += v[i];
        }
        for (int i = 0; i < n; i++) {
            v[i] /= s;
        }
    }
}

static int *choose_last(const char *at)
{
    for (int i = 0; i < nchoose_mem; i++) {
        if (choose_mem[i].at == at) {
            return &choose_mem[i].last;
        }
    }
    if (nchoose_mem == MAX_CHOOSE_MEM) {
        nchoose_mem = 0; /* forget the oldest rather than fail */
    }
    choose_mem[nchoose_mem].at = at;
    choose_mem[nchoose_mem].last = -1;
    return &choose_mem[nchoose_mem++].last;
}

static int run_choose(const char *at, const char *body, const char *end)
{
    choice_t c[MAX_CHOICES];
    int n = read_choices(body, end, c, MAX_CHOICES);
    if (n == 0) {
        return RUN_OK;
    }
    int *last = choose_last(at);
    int pick = 0;
    if (n > 1) {
        float v[MAX_CHOICES], total = 0;
        compensate(c, n, v);
        for (int i = 0; i < n; i++) {
            total += i == *last ? 0 : v[i];
        }
        float r = (float)(hid_out_rand32() >> 8) / 16777216.0f * total;
        pick = -1;
        for (int i = 0; i < n; i++) {
            if (i == *last) {
                continue;
            }
            pick = i;
            if (r < v[i]) {
                break;
            }
            r -= v[i];
        }
    }
    *last = pick;
    const def_t *d = find_def(c[pick].name);
    if (!d) {
        return RUN_OK;
    }
    trace(TRACE_PICK, c[pick].name, call_depth, 0);
    int r = call_def(d);
    if (r != RUN_ABORT) { /* an activity cut short by a pause has not ended; RESUME tidies up */
        trace(TRACE_PICK_END, c[pick].name, call_depth, 0);
    }
    return r;
}

static int32_t arg_int(span_t *args)
{
    int32_t v = 0;
    parse_int(take(args), &v);
    return v;
}

/* Commands that are not blocks. */
static int exec_cmd(span_t c, span_t args)
{
    if (is(c, "WAIT")) {
        return rhythm_sleep((uint32_t)arg_int(&args)) ? RUN_OK : RUN_ABORT;
    }
    if (is(c, "IDLE")) {
        uint32_t lo = (uint32_t)arg_int(&args), hi = (uint32_t)arg_int(&args);
        uint32_t ms = rhythm_scale(rhythm_rand(lo, hi), rhythm_personality()->pause_pct);
        return rhythm_sleep(ms) ? RUN_OK : RUN_ABORT;
    }
    if (is(c, "LINES")) {
        int32_t v = 0;
        parse_int(args, &v);
        state.lines = args.n && args.p[0] == '+' ? state.lines + v : v;
        return RUN_OK;
    }
    if (is(c, "CALL")) {
        const def_t *d = find_def(args);
        if (!d) {
            return RUN_OK;
        }
        if (call_depth < MAX_CALL_DEPTH) {
            trace(TRACE_CALL, d->name, call_depth + 1, 0);
        }
        return call_def(d);
    }
    int in = exec_input(c, args);
    if (in >= 0) {
        return in ? RUN_OK : RUN_ABORT;
    }
    hid_out_log("script: unknown command '%.*s'", (int)c.n, c.p);
    return RUN_OK;
}

static int run_block(const char *p, const char *end, int block_depth)
{
    line_t l;
    while (read_line(p, end, &l)) {
        span_t args, c = command(&l, &args);
        p = l.next;
        if (c.n == 0 || is(c, "END")) {
            continue;
        }
        if (rhythm_aborted()) {
            return RUN_ABORT;
        }
        if (is_opener(c)) {
            const char *bend = find_end(l.next, end);
            const char *after = end;
            if (bend) {
                line_t el;
                read_line(bend, end, &el);
                after = el.next;
            } else {
                bend = end;
            }
            p = after;
            if (block_depth + 1 > MAX_BLOCK_DEPTH) {
                hid_out_log("script: block depth over %d, skipping %.*s", MAX_BLOCK_DEPTH, (int)c.n, c.p);
                continue;
            }
            int r = RUN_OK;
            if (is(c, "CHOOSE")) {
                r = run_choose(l.p, l.next, bend);
            } else if (is(c, "MAYBE")) {
                if ((int32_t)rhythm_rand(0, 99) < arg_int(&args)) {
                    r = run_block(l.next, bend, block_depth + 1);
                }
            } else if (is(c, "REPEAT")) {
                uint32_t lo = (uint32_t)arg_int(&args), hi = (uint32_t)arg_int(&args);
                for (uint32_t i = rhythm_rand(lo, hi); i > 0 && r == RUN_OK; i--) {
                    r = run_block(l.next, bend, block_depth + 1);
                }
            } else { /* WHEN LINES > n */
                take(&args);
                take(&args);
                if (state.lines > arg_int(&args)) {
                    r = run_block(l.next, bend, block_depth + 1);
                }
            }
            if (r != RUN_OK) {
                return r;
            }
            continue;
        }
        if (is(c, "LOOP")) {
            return RUN_LOOP;
        }
        int r = exec_cmd(c, args);
        if (r != RUN_OK) {
            return r;
        }
    }
    return rhythm_aborted() ? RUN_ABORT : RUN_OK;
}

bool script_call(const char *name)
{
    const def_t *d = find_def_c(name);
    if (!d) {
        return true;
    }
    call_depth = 0;
    return call_def(d) != RUN_ABORT;
}

static void call_top(const def_t *d)
{
    call_depth = 0;
    trace(TRACE_CALL, d->name, 0, 0);
    call_def(d);
}

void script_run(bool (*keep_going)(void))
{
    const def_t *main_def = find_def_c("MAIN");
    const def_t *resume = find_def_c("RESUME");
    if (!main_def) {
        return;
    }
    rhythm_sleep(rhythm_rand(10000, 15000));
    span_t ed = {"editor", 6};
    const region_ent_t *editor = find_region(ed);
    if (editor && editor->valid) {
        pointer_jump(&editor->rect);
    } else {
        hid_out_log("script: no editor region, skipping the first pointer jump");
    }
    for (;;) {
        if (keep_going && !keep_going()) {
            return;
        }
        if (!rhythm_aborted()) {
            uint32_t before = hid_out_now_ms();
            call_top(main_def);
            if (hid_out_now_ms() == before) {
                hid_out_sleep_ms(1); /* never spin if MAIN has no delays */
            }
        }
        if (rhythm_aborted()) {
            while (hid_out_paused() && (!keep_going || keep_going())) {
                hid_out_sleep_ms(50);
            }
            rhythm_clear_abort();
            if (resume) {
                call_top(resume);
            }
        }
    }
}

int script_main_choices(script_choice_t *out, int max)
{
    const def_t *d = find_def_c("MAIN");
    if (!d) {
        return 0;
    }
    const char *p = d->body;
    line_t l;
    while (read_line(p, d->end, &l)) {
        span_t args, c = command(&l, &args);
        if (is(c, "CHOOSE")) {
            const char *bend = find_end(l.next, d->end);
            choice_t ch[MAX_CHOICES];
            int n = read_choices(l.next, bend ? bend : d->end, ch, MAX_CHOICES);
            n = n < max ? n : max;
            for (int i = 0; i < n; i++) {
                out[i].name = ch[i].name.p;
                out[i].len = ch[i].name.n;
                out[i].weight = (int)ch[i].weight;
            }
            return n;
        }
        p = l.next;
    }
    return 0;
}
