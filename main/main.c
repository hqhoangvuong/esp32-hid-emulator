#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

#include "hid_out_usb.h"
#include "rhythm.h"
#include "script.h"

#define TOGGLE_BUTTON GPIO_NUM_0 /* BOOT button */

static const char *TAG = "hid_sim";

extern const char developer_txt_start[] asm("_binary_developer_txt_start");
extern const char snippets_txt_start[] asm("_binary_snippets_txt_start");

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

/* Absolute mouse: TinyUSB's TUD_HID_REPORT_DESC_ABSMOUSE with 3 buttons and no AC Pan, so the
 * report is buttons (3 bits + 5 padding), X, Y (0..32767) and a relative wheel: 6 bytes. */
#define HID_REPORT_DESC_ABS_MOUSE3(...)                                      \
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),                                  \
    HID_USAGE(HID_USAGE_DESKTOP_MOUSE),                                      \
    HID_COLLECTION(HID_COLLECTION_APPLICATION),                              \
        __VA_ARGS__                                                          \
        HID_USAGE(HID_USAGE_DESKTOP_POINTER),                                \
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),                             \
            HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),                           \
                HID_USAGE_MIN(1),                                            \
                HID_USAGE_MAX(3),                                            \
                HID_LOGICAL_MIN(0),                                          \
                HID_LOGICAL_MAX(1),                                          \
                HID_REPORT_COUNT(3),                                         \
                HID_REPORT_SIZE(1),                                          \
                HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),           \
                HID_REPORT_COUNT(1),                                         \
                HID_REPORT_SIZE(5),                                          \
                HID_INPUT(HID_CONSTANT),                                     \
            HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),                          \
                HID_USAGE(HID_USAGE_DESKTOP_X),                              \
                HID_USAGE(HID_USAGE_DESKTOP_Y),                              \
                HID_LOGICAL_MIN(0x00),                                       \
                HID_LOGICAL_MAX_N(0x7FFF, 2),                                \
                HID_REPORT_SIZE(16),                                         \
                HID_REPORT_COUNT(2),                                         \
                HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),           \
                HID_USAGE(HID_USAGE_DESKTOP_WHEEL),                          \
                HID_LOGICAL_MIN(0x81),                                       \
                HID_LOGICAL_MAX(0x7f),                                       \
                HID_REPORT_COUNT(1),                                         \
                HID_REPORT_SIZE(8),                                          \
                HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),           \
        HID_COLLECTION_END,                                                  \
    HID_COLLECTION_END

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_RID_KEYBOARD)),
    HID_REPORT_DESC_ABS_MOUSE3(HID_REPORT_ID(HID_RID_MOUSE)),
};

const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "Espressif",
    "ESP32-S2 HID Simulator",
    "000001",
    "HID Keyboard + Mouse",
};

/* Poll every 5 ms so the 10 ms pointer steps are never held back by the endpoint. */
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 5),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
}

static void macro_task(void *arg)
{
    while (!tud_mounted()) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGI(TAG, "USB mounted, loading the developer script");
    rhythm_init();
    if (!script_load(developer_txt_start, strlen(developer_txt_start), snippets_txt_start,
                     strlen(snippets_txt_start))) {
        ESP_LOGE(TAG, "script refused: no MAIN");
        vTaskDelete(NULL);
    }
    if (script_problems() > 0) {
        ESP_LOGW(TAG, "script has %d problem(s), see above", script_problems());
    }
    script_run(NULL);
    vTaskDelete(NULL);
}

void app_main(void)
{
    const gpio_config_t button = {
        .pin_bit_mask = BIT64(TOGGLE_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif
    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    xTaskCreate(macro_task, "macro", 8192, NULL, 5, NULL);

    /* BOOT button pauses and resumes the simulator (polled, 50 ms debounce). The macro task
     * notices within one 20 ms sleep slice and releases every key and button. */
    bool last = true;
    while (1) {
        bool level = gpio_get_level(TOGGLE_BUTTON);
        if (last && !level) {
            hid_out_usb_paused = !hid_out_usb_paused;
            ESP_LOGI(TAG, "simulator %s", hid_out_usb_paused ? "PAUSED" : "RUNNING");
        }
        last = level;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
