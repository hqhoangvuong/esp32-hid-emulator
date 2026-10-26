#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hid_out.h"

typedef struct {
    uint32_t t;
    char kind; /* 'K' or 'M' */
    uint8_t mod, code, buttons;
    uint16_t x, y;
    int8_t wheel;
} sim_event_t;

/* Reset clock (to start_ms), event log, log lines, PRNG (seeded), pause windows and sink. */
void sim_reset(uint64_t seed, uint32_t start_ms);
/* Schedule a pause window [from_ms, to_ms); up to 64 windows. */
void sim_pause(uint32_t from_ms, uint32_t to_ms);
/* Stored event log (capacity 200000). */
const sim_event_t *sim_events(size_t *count);
/* When set, events go to the sink instead of the stored log. */
void sim_set_sink(void (*fn)(const sim_event_t *e, void *ctx), void *ctx);
/* Number of log lines since reset that contain substr. */
int sim_log_count(const char *substr);
/* Echo hid_out_log lines to stderr. */
void sim_verbose(bool on);
/* Read a whole file; malloc'd, NUL-terminated; NULL on error. */
char *sim_read_file(const char *path, size_t *len);
