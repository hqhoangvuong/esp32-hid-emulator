# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ESP-IDF firmware that makes an **ESP32-S2** enumerate as a wired USB HID keyboard + mouse and replay scripted macros with human-like timing. The folder name says "esp32-c6", but the ESP32-C6 has no USB OTG/device peripheral (only fixed-function USB Serial/JTAG) and cannot do USB HID, so the project targets the ESP32-S2 (Waveshare ESP32-S2 board, no LCD). The S3 would work too; retarget with `idf.py set-target esp32s3` and update `sdkconfig.defaults`. The S2 is single-core with no FPU, so the float Bezier maths in `human_move` is software-emulated; that is fine at the 10 ms step.

## Build environment (Windows)

ESP-IDF v6.1 is at `C:\esp\v6.1\esp-idf`, installed via EIM. `export.ps1` does **not** work because its Python env is missing. Activate with the EIM profile instead:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
idf.py set-target esp32s2   # once; sdkconfig.defaults also pins the target
idf.py build
idf.py -p COMx flash monitor
```

There are no tests. A clean `idf.py build` is the verification available without hardware. `espressif/esp_tinyusb` (^2.0.1) is fetched into `managed_components/` on first configure, which needs network access.

## Architecture

Three layers in `main/`. Dependencies run `main.c` → `macro.c` → `human.c`, and `main.c` also writes `human_enabled` directly:

- `main.c` owns USB setup: the composite HID descriptors (keyboard report ID 1, mouse report ID 2, defined as `HID_RID_*` in `human.h`), the TinyUSB callbacks, and the BOOT button (GPIO0). The button toggles `human_enabled`, a global kill switch. It starts `macro_task`, which waits for `tud_mounted()` and then calls `macro_run()` on the script embedded from `main/macros/default.txt`.
- `macro.c` is a line-based interpreter (`TYPE`, `KEY`, `MOVE`, `CLICK`, `SCROLL`, `WAIT`, `IDLE`, `LOOP`). The syntax is documented at the top of `default.txt`. The script is embedded with `EMBED_TXTFILES`, so editing it requires a rebuild, and the symbol `_binary_default_txt_start` is derived from the file name.
- `human.c` has the primitives that make input look human: jittered keystroke delays, QWERTY-neighbour typos that get backspaced, and mouse moves along a randomised cubic Bezier with min-jerk easing, sent as relative int8 deltas every 10 ms.

Every primitive checks `human_enabled` and uses `human_delay_ms` (sliced, interruptible), so pausing takes effect mid-line. The exception is key and button hold times, which use plain `vTaskDelay` so that every press is always released. A pause abandons the rest of the current line, and resuming continues at the next script line. Reports go out through `human_wait_ready()`, which waits on `tud_mounted() && tud_hid_ready()`, gives up after 500 ticks, and then drops the report without any error.

## Planned redesign

`docs/superpowers/specs/2026-10-25-developer-simulator-design.md` is a design that is not implemented yet (status: awaiting user review). It replaces the relative mouse with an absolute pointer and the linear script with a behaviour engine (`DEF`/`CHOOSE`/`REGION`/...). It splits `human.c` into modules behind a `hid_out.h` abstraction so that a host-side simulator in `tools/sim` can run them, and it removes `MOVE` and `default.txt`. Read it before changing the HID descriptor, the mouse model or the script language. Once it is implemented, the Architecture section above is out of date.

## Gotchas

- The mouse is a relative-motion device. `MOVE` takes offsets, never absolute coordinates, and the firmware cannot know where the cursor is.
- `TYPE` maps characters through TinyUSB's `HID_ASCII_TO_KEYCODE`, so the host must use a US keyboard layout. It types ASCII only, cannot type a newline (use `KEY ENTER`), and loses leading and trailing whitespace because the parser trims it. Script lines longer than 255 characters are truncated.
- Unknown commands and key names produce only an `ESP_LOGW` and are skipped, so check the serial monitor after editing a script. To add a key name for `KEY`, add it to the `named_keys` table in `macro.c`.
- Adding a second HID interface needs a bump to `CONFIG_TINYUSB_HID_COUNT` in `sdkconfig.defaults`, plus matching changes to `TUSB_DESC_TOTAL_LEN` and the descriptor in `main.c`. An existing `sdkconfig` overrides new defaults, so delete it or run `idf.py fullclean` after changing them.
