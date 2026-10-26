/* Checks on the shipped macros/developer.txt and macros/snippets.txt. */

#include <stdlib.h>

#include "editor_model.h"
#include "rhythm.h"
#include "script.h"
#include "sim.h"
#include "test_util.h"
#include "typing.h"

static char *dev, *snip;
static size_t dl, sl;

static void load(uint64_t seed)
{
    sim_reset(seed, 0);
    rhythm_init();
    if (!dev) {
        dev = sim_read_file(MACROS_DIR "/developer.txt", &dl);
        snip = sim_read_file(MACROS_DIR "/snippets.txt", &sl);
    }
    CHECK(dev && snip);
    if (!dev || !snip) {
        exit(1);
    }
    CHECK(script_load(dev, dl, snip, sl));
}

static void test_loads_clean(void)
{
    sim_verbose(true);
    load(1);
    sim_verbose(false);
    CHECK(script_problems() == 0);
    CHECK(sim_log_count("SCREEN missing") == 0 && sim_log_count("empty region") == 0);
}

static void test_every_snippet_typed_exactly(void)
{
    load(1);
    int n = script_pool_count("types");
    CHECK(n >= 20);
    static char want[8192];
    for (int i = 0; i < n; i++) {
        for (uint64_t s = 1; s <= 3; s++) {
            load(100 * (uint64_t)i + s);
            rhythm_personality_t p = {280, 200, 100, 100};
            rhythm_set_personality(&p);
            const char *t;
            size_t len;
            script_pool_item("types", i, &t, &len);
            CHECK(typing_snippet(t, len, NULL, NULL));
            size_t ne;
            const sim_event_t *e = sim_events(&ne);
            ed_reset();
            for (size_t k = 0; k < ne; k++) {
                ed_feed(&e[k]);
            }
            size_t w = 0;
            for (size_t k = 0; k < len && w < sizeof want - 2; k++) {
                if (t[k] != '\r') {
                    want[w++] = t[k];
                }
            }
            want[w++] = '\n';
            want[w] = 0;
            bool ok = strcmp(ed_text(), want) == 0;
            CHECK(ok);
            if (!ok) {
                printf("  types[%d] seed %u typed as:\n%s\n", i, (unsigned)s, ed_text());
                return;
            }
        }
    }
}

static void test_main_weights(void)
{
    load(1);
    script_choice_t c[8];
    CHECK(script_main_choices(c, 8) == 6);
    int sum = 0;
    for (int i = 0; i < 6; i++) {
        sum += c[i].weight;
    }
    CHECK(sum == 100);
}

int main(int argc, char **argv)
{
    RUN(test_loads_clean);
    RUN(test_every_snippet_typed_exactly);
    RUN(test_main_weights);
    return TEST_RESULT();
}
