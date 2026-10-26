#pragma once

#include <stdbool.h>

#define HID_RID_KEYBOARD 1
#define HID_RID_MOUSE    2

/* Set by the BOOT button in main.c; hid_out_paused() reports it. */
extern volatile bool hid_out_usb_paused;
