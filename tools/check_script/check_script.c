/* Script checker: check_script <developer.txt> <snippets.txt> [--emit-cs <out.cs>]
 * Prints every problem and exits 1 if there is any. --emit-cs writes each "types" snippet
 * wrapped in its own namespace so compile_snippets.ps1 can build them. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rhythm.h"
#include "script.h"
#include "sim.h"
#include "typing.h"

static int failures;

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("check_script: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    failures++;
}

static void check_pools(void)
{
    for (int i = 0; i < script_pool_total(); i++) {
        size_t nl;
        const char *np = script_pool_name(i, &nl);
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)nl, np);
        bool lines = script_pool_is_lines(name);
        int n = script_pool_count(name);
        if (n == 0) {
            fail("pool %s is empty", name);
        }
        for (int k = 0; k < n; k++) {
            const char *s;
            size_t len;
            script_pool_item(name, k, &s, &len);
            if (lines) {
                for (size_t c = 0; c < len; c++) {
                    if ((unsigned char)s[c] < 0x20 || (unsigned char)s[c] > 0x7E) {
                        fail("pool %s item %d is not printable ASCII", name, k);
                        break;
                    }
                }
                continue;
            }
            int bad;
            const char *why = "";
            if (!typing_check_snippet(s, len, &bad, &why)) {
                fail("pool %s item %d line %d: %s", name, k, bad, why);
            }
        }
    }
}

static void check_required(void)
{
    static const char *const pools[] = {"types", "comments", "identifiers", "files",
                                        "terminal_cmds", "queries", "find_terms", "skeleton"};
    static const char *const defs[] = {"MAIN", "RESUME", "open_scratch", "reset_scratch", "calibrate", "write_code",
                                       "edit_refactor", "navigate", "terminal_check", "lookup", "think"};
    static const char *const regions[] = {"editor", "explorer", "terminal", "chrome_page"};
    for (size_t i = 0; i < sizeof pools / sizeof pools[0]; i++) {
        if (script_pool_count(pools[i]) == 0) {
            fail("missing pool %s", pools[i]);
        }
    }
    for (size_t i = 0; i < sizeof defs / sizeof defs[0]; i++) {
        if (!script_has_def(defs[i])) {
            fail("missing DEF %s", defs[i]);
        }
    }
    for (size_t i = 0; i < sizeof regions / sizeof regions[0]; i++) {
        region_t r;
        if (!script_region_spec(regions[i], &r)) {
            fail("missing REGION %s", regions[i]);
        }
    }
    if (script_pool_count("queries") < 25) {
        fail("pool queries needs at least 25 items, has %d", script_pool_count("queries"));
    }
    if (script_pool_count("types") < 20) {
        fail("pool types needs at least 20 items, has %d", script_pool_count("types"));
    }
}

static int emit_cs(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("check_script: cannot write %s\n", path);
        return 1;
    }
    for (int i = 0; i < script_pool_count("types"); i++) {
        const char *s;
        size_t len;
        script_pool_item("types", i, &s, &len);
        fprintf(f, "namespace SnippetCheck.S%d\n{\n", i);
        for (size_t c = 0; c < len; c++) {
            if (s[c] != '\r') {
                fputc(s[c], f);
            }
        }
        fprintf(f, "\n}\n\n");
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 && !(argc == 5 && strcmp(argv[3], "--emit-cs") == 0)) {
        printf("usage: check_script <developer.txt> <snippets.txt> [--emit-cs <out.cs>]\n");
        return 2;
    }
    size_t dl, sl;
    char *dev = sim_read_file(argv[1], &dl), *snip = sim_read_file(argv[2], &sl);
    if (!dev || !snip) {
        printf("check_script: cannot read %s\n", dev ? argv[2] : argv[1]);
        return 2;
    }
    sim_reset(1, 0);
    rhythm_init();
    sim_verbose(true); /* the loader's problems go to stderr */
    script_load(dev, dl, snip, sl);
    sim_verbose(false);
    failures += script_problems();
    check_pools();
    check_required();
    if (argc == 5 && emit_cs(argv[4])) {
        failures++;
    }
    if (failures) {
        printf("check_script: %d problem(s)\n", failures);
    } else {
        printf("check_script: ok\n");
    }
    return failures ? 1 : 0;
}
