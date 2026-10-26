#include "human.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

volatile bool human_enabled = true;

static const uint8_t ascii_to_keycode[128][2] = {HID_ASCII_TO_KEYCODE};

uint32_t human_rand(uint32_t lo, uint32_t hi)
{
    if (hi <= lo) {
        return lo;
    }
    return lo + esp_random() % (hi - lo + 1);
}

void human_delay_ms(uint32_t ms)
{
    while (ms > 0 && human_enabled) {
        uint32_t slice = ms > 50 ? 50 : ms;
        vTaskDelay(pdMS_TO_TICKS(slice) ? pdMS_TO_TICKS(slice) : 1);
        ms -= slice;
    }
}

bool human_wait_ready(void)
{
    for (int i = 0; i < 500; i++) {
        if (tud_mounted() && tud_hid_ready()) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

/* ---- keyboard ---- */

static void kbd_report(uint8_t modifier, uint8_t keycode)
{
    if (!human_wait_ready()) {
        return;
    }
    uint8_t keys[6] = {keycode};
    tud_hid_keyboard_report(HID_RID_KEYBOARD, modifier, keycode ? keys : NULL);
}

void human_key(uint8_t modifier, uint8_t keycode)
{
    kbd_report(modifier, keycode);
    vTaskDelay(pdMS_TO_TICKS(human_rand(45, 120)));
    kbd_report(0, 0);
}

/* Adjacent key on a QWERTY row, used to fake a fat-finger typo. */
static char neighbour(char c)
{
    static const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    for (int r = 0; r < 3; r++) {
        const char *p = strchr(rows[r], c);
        if (p) {
            size_t len = strlen(rows[r]);
            size_t i = p - rows[r];
            if (i == 0) {
                return rows[r][1];
            }
            if (i == len - 1 || (esp_random() & 1)) {
                return rows[r][i - 1];
            }
            return rows[r][i + 1];
        }
    }
    return 0;
}

static void type_char(char c)
{
    if ((unsigned char)c >= 128) {
        return;
    }
    uint8_t shift = ascii_to_keycode[(int)c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
    uint8_t code = ascii_to_keycode[(int)c][1];
    if (code) {
        human_key(shift, code);
    }
}

void human_type(const char *text)
{
    for (; *text && human_enabled; text++) {
        char c = *text;

        /* ~3% of lowercase letters: hit a neighbour, notice, backspace, retry. */
        if (c >= 'a' && c <= 'z' && human_rand(0, 99) < 3) {
            type_char(neighbour(c));
            human_delay_ms(human_rand(150, 400));
            human_key(0, HID_KEY_BACKSPACE);
            human_delay_ms(human_rand(60, 180));
        }
        type_char(c);

        uint32_t d = human_rand(55, 170);
        if (c == ' ') {
            d += human_rand(0, 60);
        } else if (c == '.' || c == ',' || c == '\n') {
            d += human_rand(100, 350);
        }
        if (human_rand(0, 99) < 2) { /* brief "thinking" pause */
            d += human_rand(300, 900);
        }
        human_delay_ms(d);
    }
}

/* ---- mouse ---- */

static void mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    if (!human_wait_ready()) {
        return;
    }
    tud_hid_mouse_report(HID_RID_MOUSE, buttons, x, y, wheel, 0);
}

static int8_t clamp8(int v)
{
    return v > 127 ? 127 : (v < -127 ? -127 : v);
}

void human_move(int dx, int dy)
{
    float dist = sqrtf((float)dx * dx + (float)dy * dy);
    if (dist < 1.0f) {
        return;
    }

    /* Cubic Bezier from (0,0) to (dx,dy) with control points bowed sideways. */
    float nx = -dy / dist, ny = dx / dist; /* unit normal */
    float bow1 = ((int)human_rand(0, 300) - 150) / 1000.0f * dist;
    float bow2 = ((int)human_rand(0, 300) - 150) / 1000.0f * dist;
    float p1x = dx * 0.30f + nx * bow1, p1y = dy * 0.30f + ny * bow1;
    float p2x = dx * 0.70f + nx * bow2, p2y = dy * 0.70f + ny * bow2;

    uint32_t duration = 200 + (uint32_t)(dist * 1.3f);
    if (duration > 1400) {
        duration = 1400;
    }
    duration += human_rand(0, 150);
    const uint32_t step_ms = 10;
    uint32_t steps = duration / step_ms;

    int sent_x = 0, sent_y = 0;
    for (uint32_t i = 1; i <= steps && human_enabled; i++) {
        float t = (float)i / steps;
        float s = t * t * t * (10.0f + t * (-15.0f + 6.0f * t)); /* min-jerk easing */
        float u = 1.0f - s;
        float x = 3 * u * u * s * p1x + 3 * u * s * s * p2x + s * s * s * dx;
        float y = 3 * u * u * s * p1y + 3 * u * s * s * p2y + s * s * s * dy;
        int tx = (int)lroundf(x), ty = (int)lroundf(y);
        if (i == steps) {
            tx = dx;
            ty = dy;
        }
        int mx = tx - sent_x, my = ty - sent_y;
        while (mx || my) {
            int8_t sx = clamp8(mx), sy = clamp8(my);
            mouse_report(0, sx, sy, 0);
            mx -= sx;
            my -= sy;
            sent_x += sx;
            sent_y += sy;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms) ? pdMS_TO_TICKS(step_ms) : 1);
    }
}

void human_click(uint8_t buttons)
{
    human_delay_ms(human_rand(80, 220)); /* settle on target */
    mouse_report(buttons, 0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(human_rand(55, 140)));
    mouse_report(0, 0, 0, 0);
}

void human_scroll(int notches)
{
    int8_t dir = notches >= 0 ? 1 : -1;
    for (int n = abs(notches); n > 0 && human_enabled; n--) {
        mouse_report(0, 0, 0, dir);
        human_delay_ms(human_rand(40, 140));
    }
}
