#include "rhythm.h"
#include "script.h"
#include "sim.h"
#include "test_util.h"

static const char *SNIP = "POOL words LINES\nalpha\nbeta\n\nEND\n"
                          "POOL types\nclass A {\n#region R\nint x;\n#endregion\n}\n---\nclass B { }\nEND\n";

static bool load(const char *dev)
{
    sim_reset(1, 0);
    rhythm_init();
    return script_load(dev, strlen(dev), SNIP, strlen(SNIP));
}

static void test_minimal(void)
{
    CHECK(load("DEF MAIN\nWAIT 10\nEND\n"));
    CHECK(script_problems() == 0);
    CHECK(sim_log_count("SCREEN missing") == 1);
    const script_settings_t *s = script_settings();
    CHECK(s->w == 1920 && s->h == 1080 && s->scale == 100 && s->maxgap_s == 210 && s->searchgap_s == 360);
}

static void test_missing_main(void)
{
    CHECK(!load("DEF other\nEND\n"));
    CHECK(sim_log_count("missing MAIN") == 1);
}

static void test_problems(void)
{
    struct {
        const char *dev, *phrase;
    } c[] = {
        {"DEF MAIN\nREPEAT 1 2\nWAIT 1\nEND\n", "unbalanced END"},
        {"DEF MAIN\nCALL nope\nEND\n", "unknown CALL target"},
        {"DEF MAIN\nCODE nope\nEND\n", "unknown pool"},
        {"DEF MAIN\nGOTO nowhere\nEND\n", "unknown region"},
        {"DEF MAIN\nEND\nDEF x\nLOOP\nEND\n", "LOOP outside MAIN"},
        {"DEF MAIN\nCODE words\nEND\n", "CODE needs a snippet pool"},
        {"DEF MAIN\nTYPE @types\nEND\n", "TYPE @ needs a LINES pool"},
        {"DEF MAIN\nFROB\nEND\n", "unknown command"},
        {"DEF MAIN\nWHEN LINES < 3\nEND\nEND\n", "bad WHEN"},
        {"DEF MAIN\nKEY CTRL+BOGUS\nEND\n", "unknown key"},
        {"DEF MAIN\nCHOOSE\n60 a\n40 b\nEND\nEND\nDEF a\nEND\nDEF b\nEND\n", "CHOOSE weight over 50%"},
        {"DEF MAIN\nCODE types m\nEND\nDEF m\nCLICK\nEND\n", "midway DEF may only move"},
        {"DEF MAIN\nWAIT soon\nEND\n", "bad number"},
        {"DEF MAIN\nEND\nEND\n", "unbalanced END"},
    };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
        load(c[i].dev);
        bool hit = script_problems() >= 1 && sim_log_count(c[i].phrase) >= 1;
        CHECK(hit);
        if (!hit) {
            printf("  case %u: expected '%s', problems %d\n", (unsigned)i, c[i].phrase, script_problems());
        }
    }
}

static void test_clean_commands(void)
{
    const char *dev = "REGION editor +440 +115 -140 -90\n"
                      "DEF MAIN\nWHEN LINES > 120\nCALL other\nEND\nCHOOSE\n50 other\n50 third\nEND\nLOOP\nEND\n"
                      "DEF other\nKEY CTRL+S\nTYPE hello world\nTYPE @words\nCODE types peek\nGOTO editor\nHOVER editor 900\n"
                      "CLICK\nCLICK RIGHT\nDBLCLICK\nDRAGSELECT editor\nSCROLL -2\nREADSCROLL 4\nWAIT 10\nIDLE 1 2\n"
                      "MAYBE 30\nTERMINAL open\nEND\nREPEAT 1 3\nFOCUS chrome\nSEARCH words\nENDSEARCH\nFOCUS code\nEND\n"
                      "LINES 0\nLINES +3\nOUTLINE editor\nTERMINAL close\nEND\n"
                      "DEF third\nEND\nDEF peek\nSCROLL 2\nHOVER editor 500\nREADSCROLL 2\nIDLE 1 2\nWAIT 1\nGOTO editor\nEND\n";
    CHECK(load(dev));
    CHECK(script_problems() == 0);
}

static void test_region_and_settings(void)
{
    CHECK(load("SCREEN 2560 1440 150\nMAXGAP 150\nSEARCHGAP 400\nREGION editor +440 +115 -140 -90\nDEF MAIN\nEND\n"));
    region_t r;
    CHECK(script_region_spec("editor", &r));
    CHECK(r.v[0] == 440 && r.v[3] == 90 && r.from_end == 0x0C);
    const script_settings_t *s = script_settings();
    CHECK(s->maxgap_s == 150 && s->searchgap_s == 400 && s->scale == 150 && s->w == 2560);
    CHECK(sim_log_count("SCREEN missing") == 0);
}

static void test_empty_region_logged(void)
{
    load("SCREEN 800 600 200\nREGION editor +440 +115 -140 -90\nDEF MAIN\nEND\n");
    CHECK(sim_log_count("empty region editor") == 1);
}

/* Review Focus 3: POOL bodies are literal */
static void test_pool_literal(void)
{
    load("DEF MAIN\nEND\n");
    CHECK(script_pool_count("types") == 2 && script_pool_count("words") == 2 && script_pool_is_lines("words"));
    const char *s;
    size_t n;
    CHECK(script_pool_item("types", 0, &s, &n));
    CHECK(n == strlen("class A {\n#region R\nint x;\n#endregion\n}") && memcmp(s, "class A {\n#region R", 19) == 0);
    CHECK(script_pool_item("types", 1, &s, &n) && n == 11 && memcmp(s, "class B { }", 11) == 0);
    CHECK(!script_pool_item("types", 2, &s, &n));
    CHECK(script_pool_total() == 2);
    size_t ln;
    const char *name = script_pool_name(1, &ln);
    CHECK(ln == 5 && memcmp(name, "types", 5) == 0);
}

/* Review Focus 1: CRLF line endings */
static void test_crlf(void)
{
    const char *dev = "REGION editor +440 +115 -140 -90\r\nDEF MAIN\r\n# comment\r\n  WAIT 10\r\nEND\r\n";
    const char *snip = "POOL words LINES\r\nalpha\r\nbeta\r\nEND\r\n";
    sim_reset(1, 0);
    rhythm_init();
    CHECK(script_load(dev, strlen(dev), snip, strlen(snip)));
    CHECK(script_problems() == 0);
    const char *s;
    size_t n;
    CHECK(script_pool_item("words", 1, &s, &n));
    CHECK(n == 4 && memcmp(s, "beta", 4) == 0);
}

int main(int argc, char **argv)
{
    RUN(test_minimal);
    RUN(test_missing_main);
    RUN(test_problems);
    RUN(test_clean_commands);
    RUN(test_region_and_settings);
    RUN(test_empty_region_logged);
    RUN(test_pool_literal);
    RUN(test_crlf);
    return TEST_RESULT();
}
