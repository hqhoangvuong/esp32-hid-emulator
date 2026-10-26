#include "macro.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "class/hid/hid_device.h"
#include "human.h"

static const char *TAG = "macro";

static const struct { const char *name; uint8_t code; } named_keys[] = {
    {"ENTER", HID_KEY_ENTER}, {"TAB", HID_KEY_TAB}, {"ESC", HID_KEY_ESCAPE},
    {"BACKSPACE", HID_KEY_BACKSPACE}, {"DELETE", HID_KEY_DELETE}, {"SPACE", HID_KEY_SPACE},
    {"UP", HID_KEY_ARROW_UP}, {"DOWN", HID_KEY_ARROW_DOWN},
    {"LEFT", HID_KEY_ARROW_LEFT}, {"RIGHT", HID_KEY_ARROW_RIGHT},
    {"HOME", HID_KEY_HOME}, {"END", HID_KEY_END},
    {"PAGEUP", HID_KEY_PAGE_UP}, {"PAGEDOWN", HID_KEY_PAGE_DOWN},
    {"GRAVE", HID_KEY_GRAVE},
};

static const struct { const char *name; uint8_t bit; } named_mods[] = {
    {"CTRL", KEYBOARD_MODIFIER_LEFTCTRL}, {"SHIFT", KEYBOARD_MODIFIER_LEFTSHIFT},
    {"ALT", KEYBOARD_MODIFIER_LEFTALT}, {"GUI", KEYBOARD_MODIFIER_LEFTGUI},
};

/* Parse "CTRL+SHIFT+T" style combos. Returns false if the key part is unknown. */
static bool parse_combo(char *combo, uint8_t *mod, uint8_t *code)
{
    *mod = 0;
    *code = 0;
    for (char *tok = strtok(combo, "+"); tok; tok = strtok(NULL, "+")) {
        bool matched = false;
        for (size_t i = 0; i < sizeof(named_mods) / sizeof(named_mods[0]); i++) {
            if (strcasecmp(tok, named_mods[i].name) == 0) {
                *mod |= named_mods[i].bit;
                matched = true;
            }
        }
        if (matched) {
            continue;
        }
        for (size_t i = 0; i < sizeof(named_keys) / sizeof(named_keys[0]); i++) {
            if (strcasecmp(tok, named_keys[i].name) == 0) {
                *code = named_keys[i].code;
            }
        }
        if (!*code && (tok[0] == 'F' || tok[0] == 'f') && atoi(tok + 1) >= 1 && atoi(tok + 1) <= 12) {
            *code = HID_KEY_F1 + atoi(tok + 1) - 1;
        }
        if (!*code && strlen(tok) == 1 && isalpha((unsigned char)tok[0])) {
            *code = HID_KEY_A + (toupper((unsigned char)tok[0]) - 'A');
        }
        if (!*code && strlen(tok) == 1 && isdigit((unsigned char)tok[0])) {
            *code = tok[0] == '0' ? HID_KEY_0 : HID_KEY_1 + (tok[0] - '1');
        }
        if (!*code) {
            return false;
        }
    }
    return *code != 0;
}

static void exec_line(char *line)
{
    char *cmd = strsep(&line, " \t");
    char *arg = line ? line + strspn(line, " \t") : "";

    if (strcasecmp(cmd, "TYPE") == 0) {
        human_type(arg);
    } else if (strcasecmp(cmd, "KEY") == 0) {
        uint8_t mod, code;
        if (parse_combo(arg, &mod, &code)) {
            human_key(mod, code);
        } else {
            ESP_LOGW(TAG, "unknown key '%s'", arg);
        }
    } else if (strcasecmp(cmd, "MOVE") == 0) {
        int dx = 0, dy = 0;
        if (sscanf(arg, "%d %d", &dx, &dy) == 2) {
            human_move(dx, dy);
        }
    } else if (strcasecmp(cmd, "CLICK") == 0) {
        uint8_t btn = MOUSE_BTN_LEFT;
        if (strcasecmp(arg, "RIGHT") == 0) {
            btn = MOUSE_BTN_RIGHT;
        } else if (strcasecmp(arg, "MIDDLE") == 0) {
            btn = MOUSE_BTN_MIDDLE;
        }
        human_click(btn);
    } else if (strcasecmp(cmd, "SCROLL") == 0) {
        human_scroll(atoi(arg));
    } else if (strcasecmp(cmd, "WAIT") == 0) {
        human_delay_ms(atoi(arg));
    } else if (strcasecmp(cmd, "IDLE") == 0) {
        int lo = 0, hi = 0;
        if (sscanf(arg, "%d %d", &lo, &hi) == 2) {
            human_delay_ms(human_rand(lo, hi));
        }
    } else {
        ESP_LOGW(TAG, "unknown command '%s'", cmd);
    }
}

void macro_run(const char *script)
{
    const char *pos = script;
    while (*pos) {
        while (!human_enabled) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        const char *eol = strchr(pos, '\n');
        size_t len = eol ? (size_t)(eol - pos) : strlen(pos);
        char line[256];
        if (len >= sizeof(line)) {
            len = sizeof(line) - 1;
        }
        memcpy(line, pos, len);
        line[len] = '\0';
        pos = eol ? eol + 1 : pos + strlen(pos);

        /* trim trailing CR/space, skip blanks and comments */
        while (len > 0 && isspace((unsigned char)line[len - 1])) {
            line[--len] = '\0';
        }
        char *start = line + strspn(line, " \t");
        if (*start == '\0' || *start == '#') {
            continue;
        }
        if (strcasecmp(start, "LOOP") == 0) {
            pos = script;
            vTaskDelay(1); /* never spin if the script has no delays */
            continue;
        }
        exec_line(start);
    }
}
