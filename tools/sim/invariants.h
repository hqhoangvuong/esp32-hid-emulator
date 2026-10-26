#pragma once

#include <stdio.h>

#include "pointer.h"
#include "script.h"
#include "sim.h"

/* Watches the simulator's event stream and the script trace, models what the host would do with
 * the keys (Alt+Tab, Ctrl+`, Ctrl+T, Ctrl+W), and counts violations of the spec's invariants. */

/* Which app a region belongs to, for the click check. */
enum { INV_REGION_CODE, INV_REGION_TERMINAL, INV_REGION_CHROME };

typedef struct {
    uint16_t w, h;
    rect_t safe;
    rect_t regions[8];
    int nregions;
    uint32_t max_gap_ms, searchgap_ms;
    int region_kind[8]; /* INV_REGION_*, zero (CODE) by default */
} inv_config_t;

void inv_init(const inv_config_t *cfg);
void inv_event(const sim_event_t *e);
void inv_trace(const script_event_t *ev);
void inv_pause(uint32_t from_ms, uint32_t to_ms);
/* Finish the run: print statistics to report (if not NULL) and return the violation count. */
int inv_finish(FILE *report);

int inv_pick_total(void);
int inv_pick_count(const char *name, size_t len);
