#pragma once

#include <stdbool.h>
#include <stdint.h>

/* The only interface the portable modules use to reach the outside world. Implemented by
 * hid_out_usb.c on the device and by tools/sim/hid_out_sim.c on the PC. */

#define HID_ABS_MAX 32767

/* Keyboard report; keycode 0 means no key held. */
void hid_out_key(uint8_t modifier, uint8_t keycode);
/* Absolute mouse report; x and y in 0..HID_ABS_MAX, wheel relative. */
void hid_out_mouse(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel);
/* Key report (0,0) and mouse buttons 0 at the last reported position. */
void hid_out_release_all(void);
uint32_t hid_out_last_report_ms(void);

uint32_t hid_out_now_ms(void);
void hid_out_sleep_ms(uint32_t ms);
uint32_t hid_out_rand32(void);
/* True while the user has paused the simulator (BOOT button). */
bool hid_out_paused(void);
void hid_out_log(const char *fmt, ...);
