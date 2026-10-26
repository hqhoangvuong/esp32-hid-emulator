#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* HID keyboard modifier bits (left-hand keys). */
#define KMOD_CTRL  0x01
#define KMOD_SHIFT 0x02
#define KMOD_ALT   0x04
#define KMOD_GUI   0x08

/* HID usage IDs. */
#define KEY_A         0x04
#define KEY_1         0x1E
#define KEY_0         0x27
#define KEY_ENTER     0x28
#define KEY_ESC       0x29
#define KEY_BACKSPACE 0x2A
#define KEY_TAB       0x2B
#define KEY_SPACE     0x2C
#define KEY_GRAVE     0x35
#define KEY_SLASH     0x38
#define KEY_F1        0x3A
#define KEY_HOME      0x4A
#define KEY_PAGEUP    0x4B
#define KEY_DELETE    0x4C
#define KEY_END       0x4D
#define KEY_PAGEDOWN  0x4E
#define KEY_RIGHT     0x4F
#define KEY_LEFT      0x50
#define KEY_DOWN      0x51
#define KEY_UP        0x52

/* US layout. Returns false if c cannot be typed. */
bool keys_ascii(char c, uint8_t *mod, uint8_t *code);
/* Inverse of keys_ascii (only the shift bit of mod matters); 0 if none. */
char keys_to_ascii(uint8_t mod, uint8_t code);
/* Parse "CTRL+SHIFT+T" style combos from s[0..len); exactly one non-modifier key. */
bool keys_parse_combo(const char *s, size_t len, uint8_t *mod, uint8_t *code);
/* Adjacent key on the same QWERTY row, keeping case; r picks left or right. 0 if none. */
char keys_neighbour(char c, uint32_t r);
/* Touch-typing finger: 0-3 left pinky..index, 4-7 right index..pinky; -1 if unknown. */
int keys_finger(char c);
