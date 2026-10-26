#include "rhythm.h"

#include "hid_out.h"

#define SLICE_MS        20
#define IDLE_MIN_MS     90000u
#define FATIGUE_STEP_MS 60000u

static rhythm_personality_t personality;
static uint32_t fatigue = 100;
static uint32_t fatigue_t;
static bool aborted;
static uint32_t maxgap_ms = 210000;
static bool (*idle_hook)(void);
static bool in_hook;
static uint32_t seen_report; /* last report time the guard has armed on */
static uint32_t arm_from;    /* the idle threshold counts from here */
static uint32_t threshold;

uint32_t rhythm_rand(uint32_t lo, uint32_t hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + hid_out_rand32() % (hi - lo + 1);
}

static uint32_t draw_threshold(void)
{
    return maxgap_ms < IDLE_MIN_MS ? maxgap_ms : rhythm_rand(IDLE_MIN_MS, maxgap_ms);
}

static void arm(uint32_t from)
{
    arm_from = from;
    threshold = draw_threshold();
}

void rhythm_init(void)
{
    personality.cpm = (uint16_t)rhythm_rand(200, 320);
    personality.error_permille = (uint16_t)rhythm_rand(15, 40);
    personality.mouse_pct = (uint16_t)rhythm_rand(85, 120);
    personality.pause_pct = (uint16_t)rhythm_rand(80, 125);
    fatigue = 100;
    fatigue_t = hid_out_now_ms();
    aborted = false;
    idle_hook = NULL;
    in_hook = false;
    maxgap_ms = 210000;
    seen_report = hid_out_last_report_ms();
    arm(seen_report);
}

void rhythm_set_personality(const rhythm_personality_t *p)
{
    personality = *p;
}

const rhythm_personality_t *rhythm_personality(void)
{
    return &personality;
}

uint32_t rhythm_fatigue_pct(void)
{
    uint32_t now = hid_out_now_ms();
    while ((uint32_t)(now - fatigue_t) >= FATIGUE_STEP_MS) {
        fatigue_t += FATIGUE_STEP_MS;
        fatigue = fatigue + rhythm_rand(0, 2) - 1;
        if (fatigue < 85) {
            fatigue = 85;
        } else if (fatigue > 115) {
            fatigue = 115;
        }
    }
    return fatigue;
}

uint32_t rhythm_scale(uint32_t ms, uint32_t pct)
{
    return (uint32_t)((uint64_t)ms * pct * rhythm_fatigue_pct() / 10000u);
}

static void idle_check(void)
{
    uint32_t last = hid_out_last_report_ms();
    if (last != seen_report) {
        seen_report = last;
        arm(last);
    }
    if (!idle_hook || in_hook) {
        return;
    }
    if ((uint32_t)(hid_out_now_ms() - arm_from) >= threshold) {
        in_hook = true;
        idle_hook();
        in_hook = false;
        /* re-arm from now even if the hook sent nothing */
        seen_report = hid_out_last_report_ms();
        arm(hid_out_now_ms());
    }
}

bool rhythm_sleep(uint32_t ms)
{
    if (aborted) {
        return false;
    }
    uint32_t start = hid_out_now_ms();
    for (;;) {
        if (hid_out_paused()) {
            hid_out_release_all();
            aborted = true;
            return false;
        }
        idle_check();
        if (aborted) {
            return false;
        }
        uint32_t elapsed = hid_out_now_ms() - start;
        if (elapsed >= ms) {
            return true;
        }
        uint32_t slice = ms - elapsed;
        hid_out_sleep_ms(slice > SLICE_MS ? SLICE_MS : slice);
    }
}

bool rhythm_aborted(void)
{
    return aborted;
}

void rhythm_clear_abort(void)
{
    aborted = false;
}

void rhythm_set_maxgap_s(uint32_t s)
{
    maxgap_ms = s * 1000u;
    arm(arm_from);
}

void rhythm_set_idle_hook(bool (*hook)(void))
{
    idle_hook = hook;
}
