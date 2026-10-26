#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

#include "human.h"
#include "macro.h"

#define TOGGLE_BUTTON GPIO_NUM_0 /* BOOT button */

static const char *TAG = "hid_sim";

extern const char default_macro_start[] asm("_binary_default_txt_start");

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_RID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_RID_MOUSE)),
};

const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "Espressif",
    "ESP32-S2 HID Simulator",
    "000001",
    "HID Keyboard + Mouse",
};

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
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
    ESP_LOGI(TAG, "USB mounted, starting macro");
    macro_run(default_macro_start);
    ESP_LOGI(TAG, "macro finished");
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

    xTaskCreate(macro_task, "macro", 4096, NULL, 5, NULL);

    /* BOOT button toggles the simulator on/off (polled, 50 ms debounce). */
    bool last = true;
    while (1) {
        bool level = gpio_get_level(TOGGLE_BUTTON);
        if (last && !level) {
            human_enabled = !human_enabled;
            ESP_LOGI(TAG, "simulator %s", human_enabled ? "RUNNING" : "PAUSED");
        }
        last = level;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
