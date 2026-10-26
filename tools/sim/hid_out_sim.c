/* PC simulator backend for hid_out.h: virtual clock, seeded PRNG, event log or sink,
 * scheduled pause windows and a ring of recent log lines. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

#define LOG_CAP    200000
#define PAUSE_CAP  64
#define LINES_CAP  256
#define LINE_LEN   160

static uint32_t now_ms;
static uint64_t rng_state;
static sim_event_t events[LOG_CAP];
static size_t nevents;
static void (*sink_fn)(const sim_event_t *e, void *ctx);
static void *sink_ctx;
static struct { uint32_t from, to; } pauses[PAUSE_CAP];
static int npauses;
static char lines[LINES_CAP][LINE_LEN];
static int nlines; /* total lines logged since reset; the ring holds the last LINES_CAP */
static bool verbose;
static uint32_t last_report;
static uint16_t last_x, last_y;

void sim_reset(uint64_t seed, uint32_t start_ms)
{
    now_ms = start_ms;
    rng_state = seed;
    nevents = 0;
    sink_fn = NULL;
    sink_ctx = NULL;
    npauses = 0;
    nlines = 0;
    last_report = start_ms;
    last_x = last_y = 0;
}

void sim_pause(uint32_t from_ms, uint32_t to_ms)
{
    if (npauses < PAUSE_CAP) {
        pauses[npauses].from = from_ms;
        pauses[npauses].to = to_ms;
        npauses++;
    }
}

const sim_event_t *sim_events(size_t *count)
{
    *count = nevents;
    return events;
}

void sim_set_sink(void (*fn)(const sim_event_t *e, void *ctx), void *ctx)
{
    sink_fn = fn;
    sink_ctx = ctx;
}

int sim_log_count(const char *substr)
{
    int first = nlines > LINES_CAP ? nlines - LINES_CAP : 0;
    int count = 0;
    for (int i = first; i < nlines; i++) {
        if (strstr(lines[i % LINES_CAP], substr)) {
            count++;
        }
    }
    return count;
}

void sim_verbose(bool on)
{
    verbose = on;
}

char *sim_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (buf && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[size] = '\0';
        *len = (size_t)size;
    }
    return buf;
}

static void emit(const sim_event_t *e)
{
    last_report = now_ms;
    if (sink_fn) {
        sink_fn(e, sink_ctx);
        return;
    }
    if (nevents >= LOG_CAP) {
        fprintf(stderr, "sim: event log full (%d events)\n", LOG_CAP);
        abort();
    }
    events[nevents++] = *e;
}

void hid_out_key(uint8_t modifier, uint8_t keycode)
{
    sim_event_t e = {now_ms, 'K', modifier, keycode, 0, 0, 0, 0};
    emit(&e);
}

void hid_out_mouse(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel)
{
    sim_event_t e = {now_ms, 'M', 0, 0, buttons, x, y, wheel};
    last_x = x;
    last_y = y;
    emit(&e);
}

void hid_out_release_all(void)
{
    hid_out_key(0, 0);
    hid_out_mouse(0, last_x, last_y, 0);
}

uint32_t hid_out_last_report_ms(void)
{
    return last_report;
}

uint32_t hid_out_now_ms(void)
{
    return now_ms;
}

void hid_out_sleep_ms(uint32_t ms)
{
    now_ms += ms;
}

/* splitmix64, high 32 bits */
uint32_t hid_out_rand32(void)
{
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (uint32_t)(z >> 32);
}

bool hid_out_paused(void)
{
    for (int i = 0; i < npauses; i++) {
        if ((uint32_t)(now_ms - pauses[i].from) < (uint32_t)(pauses[i].to - pauses[i].from)) {
            return true;
        }
    }
    return false;
}

void hid_out_log(const char *fmt, ...)
{
    char *line = lines[nlines % LINES_CAP];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, LINE_LEN, fmt, ap);
    va_end(ap);
    nlines++;
    if (verbose) {
        fprintf(stderr, "[%u] %s\n", (unsigned)now_ms, line);
    }
}
