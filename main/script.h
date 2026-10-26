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
