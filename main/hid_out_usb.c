/* Device backend for hid_out.h: TinyUSB reports, FreeRTOS delays, esp_timer clock, esp_random. */

#include <stdarg.h>
#include <stdio.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#include "hid_out.h"
#include "hid_out_usb.h"

volatile bool hid_out_usb_paused;

typedef struct __attribute__((packed)) {
    uint8_t buttons;
    uint16_t x, y;
    int8_t wheel;
} abs_mouse_report_t;
_Static_assert(sizeof(abs_mouse_report_t) == 6, "mouse report must match the descriptor");

static uint32_t last_report;
static uint16_t last_x, last_y;

/* Block until the endpoint can take a report (or give up after 500 ticks: host gone). */
static bool wait_ready(void)
{
    for (int i = 0; i < 500; i++) {
        if (tud_mounted() && tud_hid_ready()) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

void hid_out_key(uint8_t modifier, uint8_t keycode)
{
    last_report = hid_out_now_ms();
    if (!wait_ready()) {
        return;
    }
    uint8_t keys[6] = {keycode};
    tud_hid_keyboard_report(HID_RID_KEYBOARD, modifier, keycode ? keys : NULL);
}

void hid_out_mouse(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel)
{
    last_report = hid_out_now_ms();
    last_x = x;
    last_y = y;
    if (!wait_ready()) {
        return;
    }
    abs_mouse_report_t r = {buttons, x, y, wheel};
    tud_hid_report(HID_RID_MOUSE, &r, sizeof r);
}

void hid_out_release_all(void)
{
    hid_out_key(0, 0);
    hid_out_mouse(0, last_x, last_y, 0);
}

uint32_t hid_out_last_report_ms(void)
{
    return last_report;
}

uint32_t hid_out_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void hid_out_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

uint32_t hid_out_rand32(void)
{
    return esp_random();
}

bool hid_out_paused(void)
{
    /* A suspended or vanished host (PC asleep, cable out) pauses the script until BOOT is
     * pressed: typing on wake would go into the sign-in screen, and tracked state would be wrong. */
    if (!hid_out_usb_paused && (tud_suspended() || !tud_mounted())) {
        hid_out_usb_paused = true;
        hid_out_log("host suspended or gone: PAUSED, press BOOT to resume");
    }
    return hid_out_usb_paused;
}

void hid_out_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("hid_sim: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
