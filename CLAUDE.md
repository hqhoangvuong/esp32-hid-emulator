# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ESP-IDF firmware that makes an **ESP32-S2** enumerate as a wired USB HID keyboard + absolute mouse and simulate a C# developer working in VS Code (with Chrome lookups), following `main/macros/developer.txt`. The folder name says "esp32-c6", but the ESP32-C6 has no USB OTG/device peripheral (only fixed-function USB Serial/JTAG) and cannot do USB HID, so the project targets the ESP32-S2 (Waveshare ESP32-S2 board, no LCD). The S3 would work too; retarget with `idf.py set-target esp32s3` and update `sdkconfig.defaults`. The S2 has no FPU, so the float pointer maths is software-emulated; that is fine at the 10 ms step.

Design: `docs/superpowers/specs/2026-10-25-developer-simulator-design.md`. Implementation plan and its spec refinements: `docs/superpowers/plans/2026-10-26-developer-simulator.md`. User setup (virtual desktop, Chrome, scratch workspace, calibration): `docs/setup.md`.

## Build and test (Windows)

ESP-IDF v6.1 is at `C:\esp\v6.1\esp-idf`, installed via EIM. `export.ps1` does **not** work because its Python env is missing. Activate with the EIM profile instead:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
idf.py build
idf.py -p COMx flash monitor
```

`espressif/esp_tinyusb` is fetched into `managed_components/` on first configure, which needs network access.

All behaviour is tested on the PC. The portable modules are built against a simulator backend with MSVC (VS Build Tools 2026) through CMake. Run these in a shell **without** the ESP-IDF profile loaded:

```powershell
cmake -S tools -B build-sim                       # re-run after adding a test
cmake --build build-sim --config Debug
ctest --test-dir build-sim -C Debug --output-on-failure            # everything
ctest --test-dir build-sim -C Debug --output-on-failure -R test_typing
build-sim\sim\Debug\test_typing.exe test_snippet_typed_exactly    # one test function
build-sim\sim\Debug\sim_main.exe --hours 8 --seed 2 --check-mix    # long run with stats
build-sim\check_script\Debug\check_script.exe main\macros\developer.txt main\macros\snippets.txt
```

MSVC fails with `FTK1011` when a build path exceeds 260 characters, so keep `build-sim/` at the repo root and never build under `%TEMP%`. MSVC has no `strcasecmp`, `strsep` or VLAs; use `str_ieq` from `main/strutil.h`. `tools/check_script/compile_snippets.ps1` compiles every C# snippet with the .NET SDK.

## Architecture

`main/` holds portable C modules that reach the outside world only through `main/hid_out.h` (key and mouse reports, clock, sleep, RNG, pause flag, log). Only `main.c` and `hid_out_usb.c` include TinyUSB, FreeRTOS or ESP headers; `tools/sim/hid_out_sim.c` implements the same interface with a virtual clock, seeded PRNG and event log, so the firmware logic runs unchanged in tests.

- `rhythm.c`: the session personality (typing speed, error rate, mouse speed, pause factor), slow fatigue drift, and `rhythm_sleep`, the only delay. It sleeps in 20 ms slices; on pause it releases everything, sets a sticky abort flag and returns false. It also runs the idle-gap hook (a pointer nudge) whenever no report has gone out for 90 s..MAXGAP.
- `keys.c`: HID usage IDs, the US-layout ASCII map, `KEY` combo parsing, QWERTY neighbours and the finger map used for timing.
- `pointer.c`: screen/region maths (regions are px offsets at 100% scale from an edge, multiplied by the UI scale), the absolute position (the firmware always knows where the cursor is), Fitts-timed Bezier moves with tremor and overshoot, clicks, drags, scrolls and hover. Every report is clamped to the safe box (bounding box of all regions).
- `typing.c`: key timing, bursts, self-corrected mistakes, Alt+Tab, and the **snippet grammar**: VS Code auto-closes brackets, so closer lines (`}`, `};`) are reached with Down+End rather than typed, and the line before a closer gets no Enter. `typing_check_snippet` enforces the rules.
- `script.c`: loads `developer.txt` (settings, REGIONs, DEFs) and `snippets.txt` (POOLs) in place, validates them, and interprets them. It tracks host state it cannot see (app in front, terminal panel, search tab, typed-line estimate). That state flips when the decisive key goes down, so a pause mid-combo cannot desynchronise it. CHOOSE compensates its weights for the no-repeat rule. `script_run` does the boot wait, the one teleport into the editor, MAIN forever, and RESUME after each pause.
- `main.c`: USB descriptors (keyboard report ID 1, 6-byte absolute mouse report ID 2), the BOOT button (GPIO0) that toggles `hid_out_usb_paused`, and `macro_task`.

`tools/sim/` also has `editor_model.c` (emulates VS Code auto-close so tests can check the typed text), `invariants.c` (models the host from the raw key stream and checks gaps, safe box, held keys, no click in Chrome, search spacing, activity-boundary state, pause release), `sim_main.c` (24 h runs) and `display_matrix.c`.

## Gotchas

- The absolute mouse maps onto the primary monitor. `SCREEN` in `developer.txt` must match its resolution and effective UI scale; a wrong value silently misplaces every target. Uncomment `CALL calibrate` once to trace the regions on the real screen.
- `TYPE` and snippets assume a US keyboard layout on the host, ASCII only.
- The scripts are embedded with `EMBED_TXTFILES`, so editing them requires a rebuild; the symbols `_binary_developer_txt_start` and `_binary_snippets_txt_start` come from the file names. Run `check_script` after every edit: the device only logs problems to the serial monitor.
- POOL bodies are literal (`#` lines and blank lines are content) up to a line that is exactly `END`. New C# snippets must pass the grammar and compile (`compile_snippets.ps1`).
- Never press Ctrl+S mid-snippet in a script: format-on-save moves the auto-closed braces the typer relies on.
- Adding a second HID interface needs a bump to `CONFIG_TINYUSB_HID_COUNT` in `sdkconfig.defaults`, plus matching changes to `TUSB_DESC_TOTAL_LEN` and the descriptor in `main.c`. An existing `sdkconfig` overrides new defaults, so delete it or run `idf.py fullclean` after changing them. After a descriptor change, Windows may need the device uninstalled in Device Manager and replugged.
