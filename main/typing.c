#include "typing.h"

#include "hid_out.h"
#include "keys.h"
#include "rhythm.h"

#define LINE_MAX_CHARS 512

enum { PENDING_NONE, PENDING_NOW, PENDING_WORD, PENDING_LINE };

/* Typing state for one line: what is on screen versus what was meant. */
typedef struct {
    char line[LINE_MAX_CHARS]; /* intended text of the line (typeable characters only) */
    int llen;
    char screen[LINE_MAX_CHARS + 2];
    int slen;
    int k; /* intended characters consumed so far */
    int pending;
    unsigned flags;
    char prev;
    int burst_left;
    bool bursting;
} typer_t;

static bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_word(char c)
{
    return is_letter(c) || (c >= '0' && c <= '9') || c == '_';
}

static bool slow_key(char c)
{
    uint8_t mod, code;
    if (!keys_ascii(c, &mod, &code)) {
        return false;
    }
    return mod != 0 || (code >= KEY_1 && code <= KEY_0) || code == 0x2D || code == 0x2E;
}

uint32_t typing_nominal_ms(char prev, char cur, uint16_t cpm)
{
    uint32_t ms = 60000u / (cpm ? cpm : 1);
    int fp = keys_finger(prev), fc = keys_finger(cur);
    uint32_t factor = 85;
    if (fp >= 0 && fc >= 0) {
        if (fp == fc && prev != cur) {
            factor = 135;
        } else if ((fp < 4) == (fc < 4)) {
            factor = 100;
        }
    }
    ms = ms * factor / 100;
    if (slow_key(cur)) {
        ms = ms * 125 / 100;
    }
    return ms;
}

bool typing_key(uint8_t mod, uint8_t code, bool *pressed)
{
    if (pressed) {
        *pressed = false;
    }
    if (rhythm_aborted()) {
        return false;
    }
    if (mod) {
        hid_out_key(mod, 0);
        if (!rhythm_sleep(rhythm_rand(30, 80))) {
            return false;
        }
    }
    hid_out_key(mod, code);
    if (pressed) {
        *pressed = true;
    }
    if (!rhythm_sleep(rhythm_rand(45, 120))) {
        return false;
    }
    if (mod) {
        hid_out_key(mod, 0);
        if (!rhythm_sleep(rhythm_rand(20, 60))) {
            return false;
        }
    }
    hid_out_key(0, 0);
    return true;
}

bool typing_alt_tab(bool *tab_pressed)
{
    *tab_pressed = false;
    if (rhythm_aborted()) {
        return false;
    }
    hid_out_key(KMOD_ALT, 0);
    if (!rhythm_sleep(rhythm_rand(80, 200))) {
        return false;
    }
    hid_out_key(KMOD_ALT, KEY_TAB);
    *tab_pressed = true;
    if (!rhythm_sleep(rhythm_rand(45, 120))) {
        return false;
    }
    hid_out_key(KMOD_ALT, 0);
    if (!rhythm_sleep(rhythm_rand(100, 300))) {
        return false;
    }
    hid_out_key(0, 0);
    return rhythm_sleep(rhythm_rand(300, 700));
}

/* Delay from this key press to the next one. */
static uint32_t char_delay(typer_t *t, char c)
{
    if (t->burst_left <= 0) {
        t->bursting = rhythm_rand(0, 99) < 60;
        t->burst_left = (int)rhythm_rand(4, 12);
    }
    uint32_t d = typing_nominal_ms(t->prev, c, rhythm_personality()->cpm) * rhythm_rand(70, 130) / 100;
    if (t->bursting) {
        d = d * 80 / 100;
    }
    if (--t->burst_left == 0 && t->bursting) {
        d += rhythm_rand(150, 400); /* micro-pause after a burst */
    }
    if (c == ';' || c == '{' || c == '\n') {
        d += rhythm_rand(200, 600);
    } else if (c == ' ') {
        d += rhythm_rand(0, 60);
    }
    return rhythm_scale(d, 100);
}

static bool type_one(typer_t *t, char c)
{
    uint8_t mod, code;
    if (!keys_ascii(c, &mod, &code)) {
        return true;
    }
    uint32_t t0 = hid_out_now_ms();
    uint32_t d = char_delay(t, c);
    if (!typing_key(mod, code, NULL)) {
        return false;
    }
    t->prev = c;
    uint32_t spent = hid_out_now_ms() - t0;
    return rhythm_sleep(d > spent ? d - spent : 0);
}

/* Type c and record it on screen. */
static bool put(typer_t *t, char c)
{
    if (t->slen < (int)sizeof(t->screen)) {
        t->screen[t->slen++] = c;
    }
    return type_one(t, c);
}

static bool tap(uint8_t code)
{
    uint32_t base = 60000u / rhythm_personality()->cpm;
    return typing_key(0, code, NULL) && rhythm_sleep(rhythm_scale(base * rhythm_rand(50, 90) / 100, 100));
}

/* Make the screen match the first k intended characters again. */
static bool fix(typer_t *t)
{
    bool arrows = t->pending == PENDING_LINE;
    t->pending = PENDING_NONE;
    int p = 0;
    while (p < t->slen && p < t->k && t->screen[p] == t->line[p]) {
        p++;
    }
    int s = 0;
    if (arrows) {
        while (s < t->slen - p && s < t->k - p && t->screen[t->slen - 1 - s] == t->line[t->k - 1 - s]) {
            s++;
        }
    }
    int bs = t->slen - p - s;
    if (!rhythm_sleep(rhythm_rand(150, 400))) {
        return false;
    }
    for (int i = 0; i < s; i++) {
        if (!tap(KEY_LEFT)) {
            return false;
        }
    }
    if (bs > 0 && (t->flags & TYPE_OMNIBOX) && !tap(KEY_DELETE)) {
        return false;
    }
    for (int i = 0; i < bs; i++) {
        if (!tap(KEY_BACKSPACE)) {
            return false;
        }
    }
    for (int i = p; i < t->k - s; i++) {
        if (!type_one(t, t->line[i])) {
            return false;
        }
    }
    if (s > 0 && !tap(KEY_END)) {
        return false;
    }
    for (int i = 0; i < t->k; i++) {
        t->screen[i] = t->line[i];
    }
    t->slen = t->k;
    return true;
}

static int detect_mode(const typer_t *t)
{
    uint32_t r = rhythm_rand(0, 99);
    if (r < 50) {
        return PENDING_NOW;
    }
    if (r < 85 || !(t->flags & TYPE_ARROW_FIX)) {
        return PENDING_WORD;
    }
    return PENDING_LINE;
}

/* Type one intended line (no Enter), possibly making and fixing one mistake at a time. */
static bool type_line(typer_t *t)
{
    t->slen = 0;
    t->k = 0;
    t->pending = PENDING_NONE;
    int i = 0;
    while (i < t->llen) {
        char c = t->line[i];
        if (t->pending == PENDING_WORD && !is_word(c) && !fix(t)) {
            return false;
        }
        if (t->pending == PENDING_LINE && i == t->llen - 1 && c == '{' && !fix(t)) {
            return false;
        }
        uint32_t rate = rhythm_personality()->error_permille * (t->bursting ? 130u : 100u) / 100u;
        if (t->pending == PENDING_NONE && is_letter(c) && rhythm_rand(0, 999) < rate) {
            uint32_t kind = rhythm_rand(0, 99);
            char next = i + 1 < t->llen ? t->line[i + 1] : 0;
            char wrong = keys_neighbour(c, hid_out_rand32());
            bool ok;
            if (kind >= 50 && kind < 70 && is_letter(next) && next != c) { /* transposition */
                ok = put(t, next) && put(t, c);
                t->k = i + 2;
                i += 2;
            } else if (kind >= 70 && kind < 85) { /* omission */
                ok = true;
                t->k = i + 1;
                i++;
            } else if (kind >= 85) { /* doubled letter */
                ok = put(t, c) && put(t, c);
                t->k = i + 1;
                i++;
            } else if (wrong) { /* neighbour key */
                ok = put(t, wrong);
                t->k = i + 1;
                i++;
            } else {
                ok = put(t, c);
                t->k = i + 1;
                i++;
                if (!ok) {
                    return false;
                }
                continue;
            }
            if (!ok) {
                return false;
            }
            t->pending = detect_mode(t);
            if (t->pending == PENDING_NOW && !fix(t)) {
                return false;
            }
            continue;
        }
        if (!put(t, c)) {
            return false;
        }
        t->k = i + 1;
        i++;
    }
    return t->pending == PENDING_NONE || fix(t);
}

bool typing_text(const char *s, size_t len, unsigned flags)
{
    typer_t t = {0};
    t.flags = flags;
    size_t i = 0;
    while (i <= len) {
        t.llen = 0;
        while (i < len && s[i] != '\n') {
            uint8_t mod, code;
            if (s[i] != '\r' && keys_ascii(s[i], &mod, &code) && t.llen < LINE_MAX_CHARS) {
                t.line[t.llen++] = s[i];
            }
            i++;
        }
        if (!type_line(&t)) {
            return false;
        }
        if (i < len) { /* s[i] == '\n' */
            if (!type_one(&t, '\n')) {
                return false;
            }
        }
        i++;
    }
    return true;
}
