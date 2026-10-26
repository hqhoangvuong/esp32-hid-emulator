#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TYPE_ARROW_FIX 0x01 /* allow end-of-line fixes with Left/End (editor only) */
#define TYPE_OMNIBOX   0x02 /* press Delete before each correction (Chrome address bar) */

/* Pre-jitter delay between prev and cur at cpm: same finger slower, alternating hands faster,
 * shifted or number-row keys slower. */
uint32_t typing_nominal_ms(char prev, char cur, uint16_t cpm);

/* Press and release one key, modifier first. *pressed (may be NULL) is set once the key itself
 * went down. Returns false if aborted. */
bool typing_key(uint8_t mod, uint8_t code, bool *pressed);
/* Hold Alt, tap Tab, release Alt. *tab_pressed is set once Tab went down. */
bool typing_alt_tab(bool *tab_pressed);
/* Type text with human timing and self-corrected mistakes; '\n' is typed as Enter and no Enter
 * is added at the end. Returns false if aborted. */
bool typing_text(const char *s, size_t len, unsigned flags);
