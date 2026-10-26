#pragma once

/* Run a NUL-terminated macro script (see macros/default.txt for the syntax).
 * Returns only if the script has no LOOP; otherwise runs until the task is deleted. */
void macro_run(const char *script);
