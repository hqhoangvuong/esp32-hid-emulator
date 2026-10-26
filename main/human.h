#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HID_RID_KEYBOARD 1
#define HID_RID_MOUSE    2

#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

/* Cleared to make every primitive return early (and release keys/buttons). */
extern volatile bool human_enabled;

/* Block until the USB endpoint can take a report (or the host went away). */
bool human_wait_ready(void);

/* Delay that aborts early when human_enabled is cleared. */
void human_delay_ms(uint32_t ms);
/* Uniform random integer in [lo, hi]. */
uint32_t human_rand(uint32_t lo, uint32_t hi);

void human_type(const char *text);
void human_key(uint8_t modifier, uint8_t keycode);
void human_move(int dx, int dy);
void human_click(uint8_t buttons);
void human_scroll(int notches);
