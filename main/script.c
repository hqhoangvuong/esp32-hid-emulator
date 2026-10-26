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
            hid_out_log("script: empty region %.*s", (int)regions[i].name.n, regions[i].name.p);
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

bool script_region_spec(const char *name, region_t *out)
{
    span_t s = {name, strlen(name)};
    const region_ent_t *r = find_region(s);
    if (r) {
        *out = r->spec;
    }
    return r != NULL;
}
