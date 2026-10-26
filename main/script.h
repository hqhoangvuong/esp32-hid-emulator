#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pointer.h"

/* Script interpreter for macros/developer.txt (settings, regions, DEFs) and macros/snippets.txt
 * (POOLs). The texts are used in place, never copied. */

typedef struct {
    uint16_t w, h, scale;
    uint32_t maxgap_s, searchgap_s;
} script_settings_t;

/* Scan and validate both texts and apply SCREEN, REGION and MAXGAP. Problems are logged and
 * counted; returns false only if MAIN is missing (the script must not run). */
bool script_load(const char *dev, size_t dev_len, const char *snip, size_t snip_len);
int script_problems(void);
const script_settings_t *script_settings(void);
bool script_region_spec(const char *name, region_t *out);

int script_pool_total(void);
const char *script_pool_name(int i, size_t *len);
int script_pool_count(const char *name);
/* Item i of a pool: no trailing newline, may contain '\r'. */
bool script_pool_item(const char *name, int i, const char **s, size_t *len);
bool script_pool_is_lines(const char *name);

/* ---- running ---- */

typedef enum { APP_CODE, APP_CHROME } script_app_t;

/* What the interpreter believes about the host. Tracked from the keys it sent, never verified. */
typedef struct {
    script_app_t fg;
    bool terminal_open, search_open, searched;
    uint32_t last_search_ms;
    int lines; /* typed-line estimate for the scratch file */
} script_state_t;

typedef enum { TRACE_CALL, TRACE_PICK, TRACE_PICK_END, TRACE_SNIPPET } script_trace_t;

typedef struct {
    script_trace_t kind;
    const char *name; /* DEF name, or the pool name for TRACE_SNIPPET */
    size_t len;
    int depth;        /* call depth; picks carry the depth of the CHOOSE's own DEF (MAIN = 1) */
    int index;        /* snippet index for TRACE_SNIPPET */
} script_event_t;

typedef struct {
    const char *name;
    size_t len;
    int weight;
} script_choice_t;

void script_set_trace(void (*fn)(const script_event_t *ev, void *ctx), void *ctx);
/* Run one DEF at depth 1. Returns false if aborted. */
bool script_call(const char *name);
/* Boot wait, teleport into the editor, then MAIN forever (or while keep_going() is true),
 * running RESUME after every pause. */
void script_run(bool (*keep_going)(void));
const script_state_t *script_state(void);
/* The entries of the first CHOOSE in MAIN. */
int script_main_choices(script_choice_t *out, int max);
