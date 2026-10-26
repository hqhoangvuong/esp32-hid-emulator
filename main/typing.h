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

/* Snippet grammar, so that VS Code's auto-closed brackets end up exactly where the snippet
 * says. A line is printable ASCII with no leading/trailing space and no "///", and is either
 * exactly "}" or "};", or balanced, or balanced up to a final "{". */
bool typing_check_line(const char *line, size_t len, const char **why);
/* Every line passes typing_check_line; no blank first/last line; no blank line or "{" line
 * directly before a closer line; brace depth never negative and ends at 0. bad_line is 1-based. */
bool typing_check_snippet(const char *s, size_t len, int *bad_line, const char **why);
/* Type a snippet: closer lines are reached with Down+End instead of being typed, and the line
 * before a closer gets no Enter. midway (may be NULL) is called once halfway through. */
bool typing_snippet(const char *s, size_t len, void (*midway)(void *ctx), void *ctx);
