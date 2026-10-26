#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Per-session habits, drawn once at boot. */
typedef struct {
    uint16_t cpm;            /* base typing speed, characters per minute */
    uint16_t error_permille; /* mistake rate per letter */
    uint16_t mouse_pct;      /* move duration factor; above 100 is slower */
    uint16_t pause_pct;      /* IDLE length factor */
} rhythm_personality_t;

/* Draw the personality; fatigue back to 100; clears the abort flag and the idle hook. */
void rhythm_init(void);
void rhythm_set_personality(const rhythm_personality_t *p);
const rhythm_personality_t *rhythm_personality(void);

/* Uniform integer in [lo, hi]; lo if hi <= lo. */
uint32_t rhythm_rand(uint32_t lo, uint32_t hi);
/* Slow bounded random walk, 85..115, one step per elapsed minute. */
uint32_t rhythm_fatigue_pct(void);
/* ms * pct * fatigue / 10000 */
uint32_t rhythm_scale(uint32_t ms, uint32_t pct);

/* The one delay primitive: sliced so a pause is noticed within 20 ms, and runs the idle hook
 * when no report has gone out for a random 90 s..MAXGAP. On pause it releases all keys and
 * buttons, sets the abort flag and returns false; once aborted it returns false at once. */
bool rhythm_sleep(uint32_t ms);
bool rhythm_aborted(void);
void rhythm_clear_abort(void);
void rhythm_set_maxgap_s(uint32_t s);
void rhythm_set_idle_hook(bool (*hook)(void));
