#include "editor_model.h"

#include <string.h>

#include "keys.h"

#define MAX_LINES 1024
#define LINE_CAP  256
#define MAX_TRACK 32

static char lines[MAX_LINES][LINE_CAP];
static int nlines, cur, col;
static uint8_t prev_code;
/* Columns on the current line holding closers VS Code inserted automatically. */
static int track[MAX_TRACK];
static int ntrack;
static char text_buf[MAX_LINES * (LINE_CAP + 1)];

void ed_reset(void)
{
    nlines = 1;
    lines[0][0] = '\0';
    cur = col = 0;
    prev_code = 0;
    ntrack = 0;
}

static int len(int l)
{
    return (int)strlen(lines[l]);
}

static int tracked_at(int c)
{
    for (int i = 0; i < ntrack; i++) {
        if (track[i] == c) {
            return i;
        }
    }
    return -1;
}

static void untrack(int i)
{
    track[i] = track[--ntrack];
}

/* Keep tracked columns aligned after inserting (delta +1) or deleting (-1) at column at. */
static void shift_track(int at, int delta)
{
    for (int i = 0; i < ntrack; i++) {
        if (track[i] >= at) {
            track[i] += delta;
        }
    }
}

static void insert_char(int at, char c)
{
    char *s = lines[cur];
    int n = len(cur);
    if (n + 1 >= LINE_CAP) {
        return;
    }
    memmove(s + at + 1, s + at, (size_t)(n - at + 1));
    s[at] = c;
    shift_track(at, 1);
}

static void delete_char(int at)
{
    char *s = lines[cur];
    int t = tracked_at(at);
    if (t >= 0) {
        untrack(t);
    }
    memmove(s + at, s + at + 1, strlen(s + at + 1) + 1);
    shift_track(at + 1, -1);
}

static void insert_line(int at, const char *content)
{
    if (nlines >= MAX_LINES) {
        return;
    }
    memmove(lines[at + 1], lines[at], (size_t)(nlines - at) * LINE_CAP);
    strcpy(lines[at], content);
    nlines++;
}

static void remove_line(int at)
{
    memmove(lines[at], lines[at + 1], (size_t)(nlines - at - 1) * LINE_CAP);
    nlines--;
}

static bool in_comment(void)
{
    const char *s = lines[cur];
    while (*s == ' ') {
        s++;
    }
    return s[0] == '/' && s[1] == '/';
}

/* True when column at is inside a string literal (odd number of unescaped quotes before it). */
static bool in_string(int at)
{
    bool inside = false;
    for (int i = 0; i < at; i++) {
        if (lines[cur][i] == '"' && (i == 0 || lines[cur][i - 1] != '\\')) {
            inside = !inside;
        }
    }
    return inside;
}

static char closer_of(char c)
{
    switch (c) {
    case '(':
        return ')';
    case '[':
        return ']';
    case '{':
        return '}';
    case '"':
        return '"';
    default:
        return 0;
    }
}

static void type_char(char c)
{
    char *s = lines[cur];
    int t = tracked_at(col);
    if (t >= 0 && s[col] == c) { /* overtype an auto-inserted closer */
        untrack(t);
        col++;
        return;
    }
    char next = s[col];
    bool was_in_string = in_string(col);
    insert_char(col, c);
    col++;
    char close = closer_of(c);
    if (!close) {
        return;
    }
    bool next_ok = next == '\0' || next == ' ' || strchr(";:.,=}])>", next) != NULL;
    if (!next_ok) {
        return;
    }
    if (c == '"' && (in_comment() || was_in_string)) {
        return;
    }
    insert_char(col, close);
    if (ntrack < MAX_TRACK) {
        track[ntrack++] = col;
    }
}

static void enter(void)
{
    char *s = lines[cur];
    bool expand = col > 0 && s[col - 1] == '{' && s[col] == '}';
    char rest[LINE_CAP];
    strcpy(rest, s + col);
    s[col] = '\0';
    ntrack = 0;
    if (expand) {
        insert_line(cur + 1, "");
        insert_line(cur + 2, rest);
    } else {
        insert_line(cur + 1, rest);
    }
    cur++;
    col = 0;
}

static void backspace(void)
{
    char *s = lines[cur];
    if (col > 0) {
        char open = s[col - 1];
        int t = tracked_at(col);
        if (t >= 0 && closer_of(open) && s[col] == closer_of(open)) {
            delete_char(col); /* the pair goes together */
        }
        delete_char(col - 1);
        col--;
    } else if (cur > 0) {
        ntrack = 0;
        int prev_len = len(cur - 1);
        strncat(lines[cur - 1], s, (size_t)(LINE_CAP - 1 - prev_len));
        remove_line(cur);
        cur--;
        col = prev_len;
    }
}

static void del(void)
{
    if (col < len(cur)) {
        delete_char(col);
    } else if (cur + 1 < nlines) {
        ntrack = 0;
        strncat(lines[cur], lines[cur + 1], (size_t)(LINE_CAP - 1 - len(cur)));
        remove_line(cur + 1);
    }
}

static void move(uint8_t code)
{
    ntrack = 0;
    switch (code) {
    case KEY_LEFT:
        if (col > 0) {
            col--;
        } else if (cur > 0) {
            cur--;
            col = len(cur);
        }
        break;
    case KEY_RIGHT:
        if (col < len(cur)) {
            col++;
        } else if (cur + 1 < nlines) {
            cur++;
            col = 0;
        }
        break;
    case KEY_HOME:
        col = 0;
        break;
    case KEY_END:
        col = len(cur);
        break;
    case KEY_UP:
        if (cur > 0) {
            cur--;
        }
        break;
    case KEY_DOWN:
        if (cur + 1 < nlines) {
            cur++;
        }
        break;
    default:
        break;
    }
    if (col > len(cur)) {
        col = len(cur);
    }
}

void ed_feed(const sim_event_t *e)
{
    if (e->kind != 'K') {
        return;
    }
    if (e->code == 0) {
        prev_code = 0;
        return;
    }
    if (e->code == prev_code) {
        return;
    }
    prev_code = e->code;
    if (e->mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) {
        return;
    }
    switch (e->code) {
    case KEY_ENTER:
        enter();
        return;
    case KEY_BACKSPACE:
        backspace();
        return;
    case KEY_DELETE:
        del();
        return;
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_HOME:
    case KEY_END:
    case KEY_UP:
    case KEY_DOWN:
        move(e->code);
        return;
    default:
        break;
    }
    char c = keys_to_ascii(e->mod, e->code);
    if (c && c != '\n') {
        type_char(c);
    }
}

const char *ed_text(void)
{
    char *p = text_buf;
    for (int i = 0; i < nlines; i++) {
        size_t n = strlen(lines[i]);
        memcpy(p, lines[i], n);
        p += n;
        if (i + 1 < nlines) {
            *p++ = '\n';
        }
    }
    *p = '\0';
    return text_buf;
}

void ed_cursor(int *line, int *c)
{
    *line = cur;
    *c = col;
}
