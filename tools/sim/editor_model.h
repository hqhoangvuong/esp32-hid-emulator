#pragma once

#include "sim.h"

/* A small model of the VS Code editor (C# auto-close rules, no indentation) so tests can check
 * what text a stream of key reports leaves in the editor. */

/* One empty line, cursor at 0,0. */
void ed_reset(void);
/* Apply one report; only key-down transitions (code != 0 and != the previous code) act. */
void ed_feed(const sim_event_t *e);
/* Lines joined by '\n'. */
const char *ed_text(void);
void ed_cursor(int *line, int *col);
