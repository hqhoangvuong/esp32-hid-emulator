/* Long-run simulator: runs the shipped scripts on the virtual clock and checks the invariants.
 *   sim_main --hours H --seed S [--pauses N] [--check-mix] [--verbose]
 * Exit code: 0 when there are no violations. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "invariants.h"
#include "rhythm.h"
#include "script.h"
#include "sim.h"

static uint32_t end_ms;

static bool keep_going(void)
{
    return hid_out_now_ms() < end_ms;
}

static void on_event(const sim_event_t *e, void *ctx)
{
    (void)ctx;
    inv_event(e);
}

static void on_trace(const script_event_t *ev, void *ctx)
{
    (void)ctx;
    inv_trace(ev);
}

int main(int argc, char **argv)
{
    double hours = 1;
    uint64_t seed = 1;
    int npauses = 0;
    bool check_mix = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hours") == 0 && i + 1 < argc) {
            hours = atof(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--pauses") == 0 && i + 1 < argc) {
            npauses = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--check-mix") == 0) {
            check_mix = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            sim_verbose(true);
        } else {
            printf("usage: sim_main --hours H --seed S [--pauses N] [--check-mix] [--verbose]\n");
            return 2;
        }
    }

    size_t dl, sl;
    char *dev = sim_read_file(MACROS_DIR "/developer.txt", &dl);
    char *snip = sim_read_file(MACROS_DIR "/snippets.txt", &sl);
    if (!dev || !snip) {
        printf("sim: cannot read the scripts in %s\n", MACROS_DIR);
        return 2;
    }
    sim_reset(seed, 0);
    rhythm_init();
    if (!script_load(dev, dl, snip, sl) || script_problems() > 0) {
        printf("sim: the scripts have %d problem(s)\n", script_problems());
        return 2;
    }

    const script_settings_t *s = script_settings();
    inv_config_t cfg = {s->w, s->h, {0, 0, 0, 0}, {{0}}, 0, 240000, s->searchgap_s * 1000u};
    static const char *const names[] = {"editor", "explorer", "terminal", "chrome_page"};
    for (int i = 0; i < 4; i++) {
        region_t spec;
        if (!script_region_spec(names[i], &spec)) {
            continue;
        }
        rect_t r = pointer_resolve(&spec);
        cfg.region_kind[cfg.nregions] =
            i == 2 ? INV_REGION_TERMINAL : (i == 3 ? INV_REGION_CHROME : INV_REGION_CODE);
        cfg.regions[cfg.nregions++] = r;
        if (cfg.nregions == 1) {
            cfg.safe = r;
        } else {
            cfg.safe.x0 = r.x0 < cfg.safe.x0 ? r.x0 : cfg.safe.x0;
            cfg.safe.y0 = r.y0 < cfg.safe.y0 ? r.y0 : cfg.safe.y0;
            cfg.safe.x1 = r.x1 > cfg.safe.x1 ? r.x1 : cfg.safe.x1;
            cfg.safe.y1 = r.y1 > cfg.safe.y1 ? r.y1 : cfg.safe.y1;
        }
    }
    inv_init(&cfg);

    end_ms = (uint32_t)(hours * 3600000.0);
    for (int i = 0; i < npauses; i++) {
        uint32_t slot = end_ms / (uint32_t)(npauses + 1);
        uint32_t from = slot * (uint32_t)(i + 1) + hid_out_rand32() % (slot / 2 + 1);
        uint32_t to = from + 2000 + hid_out_rand32() % 18000;
        sim_pause(from, to);
        inv_pause(from, to);
    }

    sim_set_sink(on_event, NULL);
    script_set_trace(on_trace, NULL);
    script_run(keep_going);

    printf("simulated %.1f h, seed %llu, %d pause(s)\n", hours, (unsigned long long)seed, npauses);
    int v = inv_finish(stdout);
    if (check_mix) {
        script_choice_t c[16];
        int n = script_main_choices(c, 16), sum = 0, total = inv_pick_total();
        for (int i = 0; i < n; i++) {
            sum += c[i].weight;
        }
        for (int i = 0; i < n && total > 0 && sum > 0; i++) {
            double got = 100.0 * inv_pick_count(c[i].name, c[i].len) / total;
            double want = 100.0 * c[i].weight / sum;
            if (got - want > 10.0 || want - got > 10.0) {
                printf("violation: %.*s at %.1f%%, weight %.1f%%\n", (int)c[i].len, c[i].name, got, want);
                v++;
            }
        }
    }
    return v ? 1 : 0;
}
