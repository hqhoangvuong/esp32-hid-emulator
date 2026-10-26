# Developer Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the relative-mouse linear macro with the spec's behaviour engine: an absolute pointer, C# typing that follows a snippet grammar, a structured script language, rhythm with an idle-gap guard, and a PC-side simulator that proves the invariants before hardware.

**Architecture:** The portable C modules in `main/` (`keys`, `rhythm`, `pointer`, `typing`, `script`) reach the outside world only through `main/hid_out.h`. The firmware links them with `main/hid_out_usb.c`. The host build in `tools/` links the same files with `tools/sim/hid_out_sim.c`, which provides a virtual clock, a seeded PRNG and an event log. Behaviour is tested on the PC with CTest, the firmware is checked with `idf.py build`, and hardware is a final manual acceptance.

**Tech Stack:** C11; ESP-IDF v6.1; `espressif/esp_tinyusb ^2.0.1~1` (TinyUSB 0.21); host CMake 4.4 with MSVC 19.51 (VS Build Tools 2026); CTest; .NET SDK 10 (snippet compile check only).

**Spec:** `docs/superpowers/specs/2026-10-25-developer-simulator-design.md`

## Before you start

Environment facts, checked 2026-10-26:

- The project is not a git repository yet. Task 1 initialises one.
- The only host C compiler is MSVC, from VS Build Tools 2026. There is no gcc or clang on PATH. `cmake -S tools -B build-sim` selects "Visual Studio 18 2026" by itself. Run host builds in a shell **without** the ESP-IDF profile loaded.
- MSVC fails with `FTK1011` when a build path exceeds 260 characters. Keep `build-sim/` at the repo root and never build in `%TEMP%`.
- MSVC has no `strcasecmp`, `strsep`, `<strings.h>` or VLAs, so portable modules must not use them.

Host commands, used by every task:

```powershell
cmake -S tools -B build-sim                     # configure (re-run after adding a test)
cmake --build build-sim --config Debug
ctest --test-dir build-sim -C Debug --output-on-failure -R <test>
build-sim\sim\Debug\<test>.exe <function>       # run one test function
```

Firmware command, used by Task 14:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
idf.py build
```

## Decisions this plan adds to the spec

The spec is still marked "awaiting user review". The items below refine or correct it. Get the user's confirmation of items 1 and 4 before Task 12, which writes the script content.

1. **Region defaults.** The defaults change to account for the Windows 11 taskbar (48 px) and the editor gutter. The spec's values put the editor bottom (`-60`, y = 1020 at 1080p) inside the VS Code status bar (1010-1032). The chrome_page bottom (`-50`) sits 2 px from the taskbar, the explorer `x0` is 12 px from the activity bar, and the editor `x0` falls in the line-number gutter. The new defaults are `editor +440 +115 -140 -90`, `explorer +70 +115 +280 +260`, `terminal +440 -250 -60 -90` and `chrome_page +40 +130 -40 -70`. Task 13's layout model checks them.
2. **Pointer whitelist.** Resting and click positions are inside the target region. Every report is inside the *safe box*, the bounding box of all regions. Paths between regions are clamped to the safe box.
3. **Fitts constants.** The Fitts target width is W = 40 px × scale. A "long move" is D > 400 px × scale.
4. **Snippet rule 2 refined.** The line before a `}` or `};` line is not followed by Enter; otherwise every `}` gets a blank line above it. For a closer line the typer presses Down and End (plus `;` for `};`), then Enter unless the next line is also a closer. The checker additionally forbids a `{` line directly followed by a closer, and a blank line before a closer.
5. **Where arrow-key corrections run.** End-of-line corrections with the arrow keys run only in the editor (`CODE`). The terminal (PSReadLine accepts its prediction on End) and the address bar (inline autocomplete) use only immediate and end-of-word corrections. In the address bar, the typer presses Delete before each correction and before Enter.
6. **When tracked state flips.** Tracked state changes when the decisive key press is sent: Tab within Alt+Tab, Ctrl+`, Ctrl+T or Ctrl+W. A pause in the middle of a combo therefore cannot desynchronise it, because releasing Alt still completes the switch.
7. **CHOOSE compensation.** CHOOSE compensates its weights for the no-repeat rule. Without that, the long-run shares are proportional to w(1−w): "write code" at weight 40 would run about 31% of the time, close to the 10 pp limit.
8. **Script additions:**
   - `TYPE @pool` types a random item from a LINES pool, without pressing Enter.
   - `LINES n` sets the typed-line estimate and `LINES +n` adds to it.
   - `CODE pool [def]` optionally calls a pointer-only DEF once, halfway through the snippet, on 40% of runs. This implements the spec's "Midway, sometimes: scroll up, hover a symbol, return."
   - `LOOP` is valid only in MAIN and restarts MAIN.
   - `READSCROLL n` scrolls down by n notches inside the foreground region.
9. **No Ctrl+S mid-snippet.** Ctrl+S is never pressed in the middle of a snippet, because format-on-save would move the auto-closed braces the typer relies on. The 1-4 minute save cadence is something the script content aims for. The simulator reports it but does not assert it.
10. **Mouse descriptor.** The mouse uses a custom 6-byte descriptor, as the spec says, rather than TinyUSB's `TUD_HID_REPORT_DESC_ABSMOUSE` (5 buttons plus pan, 7 bytes). bInterval goes from 10 to 5 ms so that 10 ms move steps are not throttled.
11. **Scratch workspace location.** `scratch-workspace/` is copied outside the repo before use. This avoids nesting a git repository, and keeps the macro's `git` commands away from the firmware repo. It also needs one initial commit so that `git log` works.

## Global Constraints

- The target is the ESP32-S2 with ESP-IDF v6.1 and `espressif/esp_tinyusb ^2.0.1~1`.
- Only `main/main.c` and `main/hid_out_usb.c` include TinyUSB, FreeRTOS or ESP headers.
- Portable modules are C11 and build warning-free under MSVC `/W4 /WX` and under the IDF GCC defaults. They use no `strcasecmp`, `strsep`, `<strings.h>` or VLAs; use `str_ieq` from `main/strutil.h` instead.
- Code style follows the existing `main/` code: 4-space indent, snake_case, `/* */` comments, and a module prefix on public names.
- HID: keyboard report ID 1 is unchanged. Mouse report ID 2 holds buttons 1-3 (3 bits) plus 5 padding bits, X and Y as 16-bit absolute values in 0..32767, and an 8-bit relative wheel in -127..127, for 6 bytes after the ID.
- Script defaults are `SCREEN 1920 1080 100` (with a warning logged when SCREEN is absent), `MAXGAP 210` and `SEARCHGAP 360`. The idle-guard threshold is uniform between 90 s and MAXGAP. Call depth and block depth are each limited to 8.
- Values from the spec:
  - Typing speed is 200-320 cpm and the error rate is 1.5-4% per letter. Bursts are 4-12 characters.
  - The planning pause is 3-15 s and is written as a script `IDLE`.
  - Hand-off pauses are 250-900 ms between typing and pointer use, and 200-700 ms between a click and typing.
  - Fatigue is ±15%.
  - Move duration is 150 + 120·log2(D/W+1) ms, multiplied by the mouse factor and by U(0.85, 1.15).
  - 25% of long moves overshoot by 3-10%.
  - Tremor is 1-2 px. The line pitch is 19 px × scale.
- All time arithmetic uses `uint32_t` milliseconds and unsigned subtraction, so it is wrap-safe.
- Script files are ASCII, and CRLF line endings are tolerated.
- Pressing BOOT releases all keys and buttons within 100 ms.

## Review Focus

1. **CRLF line endings.** Git's autocrlf on Windows can convert script files to CRLF. They must parse identically and no `\r` may be typed. Task 9 `test_crlf`.
2. **Pause during a combo.** After a pause during Alt+Tab, Ctrl+` or Ctrl+T, the tracked state must match the host. Task 11 `test_pause_mid_combo`.
3. **POOL bodies are literal.** Lines starting with `#` (C# `#region`) and blank lines inside POOL bodies are content, not comments. Task 9 `test_pool_literal`.
4. **Screen too small for the scale.** Regions then resolve to empty rectangles. The pointer must not divide by zero or leave the screen, and the loader must log a problem. Task 4 `test_empty_region`.
5. **Uptime past 49.7 days.** The idle guard and SEARCHGAP must keep working after the `uint32` ms counter wraps. Task 3 `test_wrap`, Task 11 `test_searchgap_wrap`.

## File Structure

| Path | Responsibility |
|---|---|
| `main/hid_out.h` | Backend interface: reports, clock, sleep, RNG, pause input, log |
| `main/hid_out_usb.c` | Firmware backend (TinyUSB, FreeRTOS, esp_timer, esp_random) |
| `main/strutil.h` | `str_ieq`, a case-insensitive compare of a non-terminated token with a literal |
| `main/keys.[ch]` | HID usage constants, US ASCII map, combo parser, QWERTY neighbour, finger map |
| `main/rhythm.[ch]` | Personality, fatigue, RNG helpers, the one sleep primitive, pause abort, idle guard |
| `main/pointer.[ch]` | Screen and region maths, absolute position, moves, click/drag/scroll/hover/nudge/outline |
| `main/typing.[ch]` | Key timing, mistake model, Alt+Tab, snippet grammar typer and checker |
| `main/script.[ch]` | Loader and validator, interpreter, tracked state, trace hook, run loop |
| `main/main.c` | USB descriptors, BOOT button, macro task (modified) |
| `main/macros/developer.txt`, `main/macros/snippets.txt` | Script and pools. Replace `default.txt` |
| `tools/CMakeLists.txt`, `tools/sim/CMakeLists.txt` | Host build and CTest registration |
| `tools/sim/hid_out_sim.c`, `tools/sim/sim.h` | Virtual clock, PRNG, event log or sink, pause schedule, file loader |
| `tools/sim/test_util.h` | `CHECK` and `RUN` macros |
| `tools/sim/editor_model.[ch]` | VS Code auto-close emulation, so tests can check what ends up in the editor |
| `tools/sim/invariants.[ch]` | Host model and invariant checks over the event stream |
| `tools/sim/sim_main.c`, `tools/sim/display_matrix.c` | Long-run simulator and display-matrix check |
| `tools/sim/test_*.c` | Unit tests, one executable per module |
| `tools/check_script/check_script.c`, `compile_snippets.ps1` | Script checker and optional .NET compile check |
| `scratch-workspace/` | C# workspace the macro types into |
| `docs/setup.md` | Windows, VS Code and Chrome setup (spec section 11) |

Deleted in Task 14: `main/human.[ch]`, `main/macro.[ch]`, `main/macros/default.txt`.

---

### Task 1: Repository and host simulator backend

**Files:**
- Create: `main/hid_out.h`, `tools/CMakeLists.txt`, `tools/sim/CMakeLists.txt`, `tools/sim/sim.h`, `tools/sim/hid_out_sim.c`, `tools/sim/test_util.h`, `tools/sim/test_sim.c`
- Modify: `.gitignore` (add `build-sim/`)

**Interfaces:**
- Produces (`main/hid_out.h`):
  ```c
  #define HID_ABS_MAX 32767
  void hid_out_key(uint8_t modifier, uint8_t keycode);      /* keycode 0 = no key held */
  void hid_out_mouse(uint8_t buttons, uint16_t x, uint16_t y, int8_t wheel); /* x,y 0..HID_ABS_MAX */
  void hid_out_release_all(void);  /* key report (0,0) and mouse buttons 0 at the last x,y */
  uint32_t hid_out_last_report_ms(void);
  uint32_t hid_out_now_ms(void);
  void hid_out_sleep_ms(uint32_t ms);
  uint32_t hid_out_rand32(void);
  bool hid_out_paused(void);
  void hid_out_log(const char *fmt, ...);
  ```
- Produces (`tools/sim/sim.h`):
  ```c
  typedef struct { uint32_t t; char kind; /* 'K' or 'M' */ uint8_t mod, code, buttons; uint16_t x, y; int8_t wheel; } sim_event_t;
  void sim_reset(uint64_t seed, uint32_t start_ms);   /* clock, log, PRNG, pauses, sink cleared */
  void sim_pause(uint32_t from_ms, uint32_t to_ms);   /* up to 64 windows, [from, to) */
  const sim_event_t *sim_events(size_t *count);       /* stored log, capacity 200000 */
  void sim_set_sink(void (*fn)(const sim_event_t *e, void *ctx), void *ctx); /* replaces storing */
  int sim_log_count(const char *substr);              /* log lines since reset containing substr */
  void sim_verbose(bool on);                          /* echo hid_out_log to stderr */
  char *sim_read_file(const char *path, size_t *len); /* malloc'd, NUL-terminated */
  ```
- Produces (`tools/sim/test_util.h`): `CHECK(cond)` prints `file:line` and counts the failure. `RUN(fn)` runs `fn` when `argc < 2` or `argv[1]` equals its name. `main` returns nonzero when anything failed.
- Produces (`tools/sim/CMakeLists.txt`): a function `sim_test(name)` that builds `test_<name>.c` against the `core` library and adds a CTest test `test_<name>`.

- [ ] **Step 1: Initialise git and commit the current tree**

```powershell
git init
git add -A
git commit -m "chore: import existing relative-mouse firmware"
```

- [ ] **Step 2: Write `tools/CMakeLists.txt`**

`project(host_tools C)` with C11 and `enable_testing()`. Set `FW` to `${CMAKE_CURRENT_SOURCE_DIR}/../main`, define `MACROS_DIR="${FW}/macros"` and `_CRT_SECURE_NO_WARNINGS`, and use `/W4 /WX` on MSVC or `-Wall -Wextra -Werror` elsewhere. Create a `core` static library from `sim/hid_out_sim.c` with include directories `${FW}` and `sim`, linking `m` when not on MSVC. Then `add_subdirectory(sim)`. Later tasks append their `main/*.c` file to `core`.

- [ ] **Step 3: Write the failing tests in `tools/sim/test_sim.c`**

```c
static void test_clock(void) { sim_reset(1, 0); hid_out_sleep_ms(250); CHECK(hid_out_now_ms() == 250); }
static void test_rand_deterministic(void) {
    uint32_t a[5], b[5];
    sim_reset(7, 0); for (int i = 0; i < 5; i++) a[i] = hid_out_rand32();
    sim_reset(7, 0); for (int i = 0; i < 5; i++) b[i] = hid_out_rand32();
    CHECK(memcmp(a, b, sizeof a) == 0);
    sim_reset(8, 0); CHECK(hid_out_rand32() != a[0]);
}
static void test_event_log(void) {
    size_t n; sim_reset(1, 0); hid_out_sleep_ms(10);
    hid_out_key(0x02, 0x04); hid_out_mouse(1, 100, 200, -1);
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 2 && e[0].kind == 'K' && e[0].mod == 2 && e[0].code == 4 && e[0].t == 10);
    CHECK(e[1].kind == 'M' && e[1].buttons == 1 && e[1].x == 100 && e[1].y == 200 && e[1].wheel == -1);
    CHECK(hid_out_last_report_ms() == 10);
}
static void test_release_all(void) {
    size_t n; sim_reset(1, 0); hid_out_mouse(1, 100, 200, 0); hid_out_key(0, 4); hid_out_release_all();
    const sim_event_t *e = sim_events(&n);
    CHECK(n == 4 && e[2].kind == 'K' && e[2].code == 0 && e[2].mod == 0);
    CHECK(e[3].kind == 'M' && e[3].buttons == 0 && e[3].x == 100 && e[3].y == 200 && e[3].wheel == 0);
}
static void test_pause_window(void) {
    sim_reset(1, 0); sim_pause(1000, 2000);
    hid_out_sleep_ms(999); CHECK(!hid_out_paused());
    hid_out_sleep_ms(1); CHECK(hid_out_paused());
    hid_out_sleep_ms(999); CHECK(hid_out_paused());
    hid_out_sleep_ms(1); CHECK(!hid_out_paused());
}
static void test_wrap_start(void) { sim_reset(1, 0xFFFFFFF0u); hid_out_sleep_ms(32); CHECK(hid_out_now_ms() == 0x10); }
```

Also write `test_sink`: with a sink set, events reach the sink and `sim_events` reports 0. And write `test_log_count`: `hid_out_log("unknown key '%s'", "X")` makes `sim_log_count("unknown key") == 1`.

- [ ] **Step 4: Run the tests to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the link fails with unresolved `hid_out_*` and `sim_*` symbols.

- [ ] **Step 5: Implement `tools/sim/hid_out_sim.c`**

Use splitmix64 seeded from `seed` for `hid_out_rand32` (the high 32 bits). Sleeping advances the virtual clock. `hid_out_paused` is true when `now` falls inside any `[from, to)` window, compared with unsigned subtraction. The log keeps a count per stored line in a ring of 256 lines × 160 characters. When the stored log exceeds 200000 events, call `abort()` with a message.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cmake --build build-sim --config Debug; ctest --test-dir build-sim -C Debug --output-on-failure -R test_sim`
Expected: `100% tests passed`

- [ ] **Step 7: Commit**

```powershell
git add .gitignore main/hid_out.h tools
git commit -m "feat(sim): host backend with virtual clock, PRNG and event log"
```

---

### Task 2: Keys

**Files:**
- Create: `main/strutil.h`, `main/keys.h`, `main/keys.c`, `tools/sim/test_keys.c`
- Modify: `tools/CMakeLists.txt` (add `keys.c` to `core`), `tools/sim/CMakeLists.txt` (`sim_test(keys)`)

**Interfaces:**
- Produces (`main/strutil.h`): `static inline bool str_ieq(const char *s, size_t n, const char *lit)`, which is true when the n characters of `s` equal `lit` ignoring ASCII case and `strlen(lit) == n`.
- Produces (`main/keys.h`):
  ```c
  #define KMOD_CTRL 0x01
  #define KMOD_SHIFT 0x02
  #define KMOD_ALT 0x04
  #define KMOD_GUI 0x08
  /* HID usage IDs */
  #define KEY_A 0x04
  #define KEY_1 0x1E
  #define KEY_0 0x27
  #define KEY_ENTER 0x28
  #define KEY_ESC 0x29
  #define KEY_BACKSPACE 0x2A
  #define KEY_TAB 0x2B
  #define KEY_SPACE 0x2C
  #define KEY_GRAVE 0x35
  #define KEY_SLASH 0x38
  #define KEY_F1 0x3A
  #define KEY_HOME 0x4A
  #define KEY_PAGEUP 0x4B
  #define KEY_DELETE 0x4C
  #define KEY_END 0x4D
  #define KEY_PAGEDOWN 0x4E
  #define KEY_RIGHT 0x4F
  #define KEY_LEFT 0x50
  #define KEY_DOWN 0x51
  #define KEY_UP 0x52
  bool keys_ascii(char c, uint8_t *mod, uint8_t *code);         /* US layout; false if untypeable */
  char keys_to_ascii(uint8_t mod, uint8_t code);                /* inverse; 0 if none */
  bool keys_parse_combo(const char *s, size_t len, uint8_t *mod, uint8_t *code); /* does not modify s */
  char keys_neighbour(char c, uint32_t r);                      /* QWERTY row neighbour, keeps case; 0 if none */
  int keys_finger(char c);                                      /* 0-3 left pinky..index, 4-7 right index..pinky; -1 */
  ```
  Combo names are those in the existing `macro.c` (`ENTER TAB ESC BACKSPACE DELETE SPACE UP DOWN LEFT RIGHT HOME END PAGEUP PAGEDOWN GRAVE`) plus `SLASH`, together with `F1`-`F12`, single letters and digits, and the modifiers `CTRL SHIFT ALT GUI`. Exactly one non-modifier key is required. Finger map: `` `1qaz `` → 0, `2wsx` → 1, `3edc` → 2, `45rtfgvb` → 3, `67yuhjnm` → 4, `8ik,` → 5, `9ol.` → 6, `0p;/-=[]'\` → 7. Shifted characters map to the finger of their base key.

- [ ] **Step 1: Write the failing tests**

```c
static void expect(char c, uint8_t m, uint8_t k) { uint8_t mm, kk; CHECK(keys_ascii(c, &mm, &kk) && mm == m && kk == k); CHECK(keys_to_ascii(m, k) == c); }
static void test_ascii(void) {
    expect('a', 0, 0x04); expect('A', KMOD_SHIFT, 0x04); expect('1', 0, 0x1E); expect('!', KMOD_SHIFT, 0x1E);
    expect('0', 0, 0x27); expect(')', KMOD_SHIFT, 0x27); expect('\n', 0, 0x28); expect(' ', 0, 0x2C);
    expect('{', KMOD_SHIFT, 0x2F); expect('"', KMOD_SHIFT, 0x34); expect('`', 0, 0x35); expect('~', KMOD_SHIFT, 0x35);
    expect('<', KMOD_SHIFT, 0x36); expect('?', KMOD_SHIFT, 0x38); expect('|', KMOD_SHIFT, 0x31); expect('\t', 0, 0x2B);
    uint8_t m, k; CHECK(!keys_ascii('\x7f', &m, &k)); CHECK(!keys_ascii((char)0xE9, &m, &k));
}
static bool combo(const char *s, uint8_t m, uint8_t k) { uint8_t mm, kk; return keys_parse_combo(s, strlen(s), &mm, &kk) && mm == m && kk == k; }
static void test_combo(void) {
    CHECK(combo("CTRL+S", KMOD_CTRL, 0x16)); CHECK(combo("shift+alt+f", KMOD_SHIFT | KMOD_ALT, 0x09));
    CHECK(combo("F12", 0, 0x45)); CHECK(combo("CTRL+GRAVE", KMOD_CTRL, KEY_GRAVE)); CHECK(combo("CTRL+SLASH", KMOD_CTRL, KEY_SLASH));
    CHECK(combo("ALT+LEFT", KMOD_ALT, KEY_LEFT)); CHECK(combo("CTRL+1", KMOD_CTRL, KEY_1)); CHECK(combo("CTRL+0", KMOD_CTRL, KEY_0));
    uint8_t m, k; const char *bad[] = {"BOGUS", "CTRL+", "F13", "CTRL", "CTRL+A+B", ""};
    for (size_t i = 0; i < 6; i++) CHECK(!keys_parse_combo(bad[i], strlen(bad[i]), &m, &k));
    char buf[] = "CTRL+S"; keys_parse_combo(buf, 6, &m, &k); CHECK(strcmp(buf, "CTRL+S") == 0);
}
static void test_neighbour(void) {
    CHECK(keys_neighbour('q', 0) == 'w' && keys_neighbour('q', 1) == 'w'); CHECK(keys_neighbour('p', 0) == 'o');
    char g0 = keys_neighbour('g', 0), g1 = keys_neighbour('g', 1); CHECK(g0 != g1 && (g0 == 'f' || g0 == 'h') && (g1 == 'f' || g1 == 'h'));
    char G = keys_neighbour('G', 0); CHECK(G == 'F' || G == 'H'); CHECK(keys_neighbour('5', 0) == 0);
}
static void test_finger(void) {
    CHECK(keys_finger('f') == 3 && keys_finger('r') == 3 && keys_finger('v') == 3 && keys_finger('j') == 4);
    CHECK(keys_finger('F') == 3 && keys_finger('{') == 7 && keys_finger('#') == 2 && keys_finger(' ') == -1);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `keys.h` is not found.

- [ ] **Step 3: Implement `main/keys.c` and `main/strutil.h`**

Use a 128-entry `{mod, code}` table for the US layout. `keys_to_ascii` searches that table. The combo parser walks `+`-separated tokens with `str_ieq`.

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_keys`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/strutil.h main/keys.h main/keys.c tools
git commit -m "feat(keys): portable US key map, combo parser and finger map"
```

---

### Task 3: Rhythm

**Files:**
- Create: `main/rhythm.h`, `main/rhythm.c`, `tools/sim/test_rhythm.c`
- Modify: `tools/CMakeLists.txt`, `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `hid_out.h` (Task 1).
- Produces:
  ```c
  typedef struct { uint16_t cpm; uint16_t error_permille; uint16_t mouse_pct; uint16_t pause_pct; } rhythm_personality_t;
  void rhythm_init(void);                       /* draws personality; fatigue = 100; clears abort and hook */
  void rhythm_set_personality(const rhythm_personality_t *p);
  const rhythm_personality_t *rhythm_personality(void);
  uint32_t rhythm_rand(uint32_t lo, uint32_t hi);   /* uniform, inclusive; lo if hi <= lo */
  uint32_t rhythm_fatigue_pct(void);                /* 85..115 */
  uint32_t rhythm_scale(uint32_t ms, uint32_t pct); /* ms * pct * fatigue / 10000 */
  bool rhythm_sleep(uint32_t ms);                   /* false once aborted */
  bool rhythm_aborted(void);
  void rhythm_clear_abort(void);
  void rhythm_set_maxgap_s(uint32_t s);             /* default 210 */
  void rhythm_set_idle_hook(bool (*hook)(void));
  ```
  - **Personality ranges:** cpm 200-320, error_permille 15-40, mouse_pct 85-120 (above 100 means slower), pause_pct 80-125.
  - **Fatigue:** a random walk evaluated lazily from `hid_out_now_ms()`. Each full 60 s of elapsed time adds a step of −1, 0 or +1 pct, clamped to 85..115.
  - **`rhythm_sleep`:** sleeps in slices of at most 20 ms.
    - Each slice first checks `hid_out_paused()`. If it is set, the function calls `hid_out_release_all()`, sets the abort flag and returns false.
    - Whenever `hid_out_last_report_ms()` changes, it draws a new idle threshold uniform in [90 s, MAXGAP]. If MAXGAP is below 90 s, the threshold is MAXGAP.
    - When `now − last_report ≥ threshold`, it calls the hook if one is set and the hook is not already running. After the hook returns, the guard is re-armed from the current time, so a hook that sends nothing is not called again before another full threshold has passed.
    - The sleep runs until `start + ms`, so time spent inside the hook counts toward it.

- [ ] **Step 1: Write the failing tests**

```c
static void test_personality_ranges(void) {
    for (uint64_t s = 1; s <= 200; s++) { sim_reset(s, 0); rhythm_init(); const rhythm_personality_t *p = rhythm_personality();
        CHECK(p->cpm >= 200 && p->cpm <= 320); CHECK(p->error_permille >= 15 && p->error_permille <= 40);
        CHECK(p->mouse_pct >= 85 && p->mouse_pct <= 120); CHECK(p->pause_pct >= 80 && p->pause_pct <= 125); }
}
static void test_sleep(void) { sim_reset(1, 0); rhythm_init(); CHECK(rhythm_sleep(1000)); CHECK(hid_out_now_ms() == 1000); }
static void test_pause_aborts_and_releases(void) {
    size_t n; sim_reset(1, 0); rhythm_init(); sim_pause(500, 5000); hid_out_key(0, 4); hid_out_mouse(1, 10, 10, 0);
    CHECK(!rhythm_sleep(2000)); CHECK(hid_out_now_ms() <= 520);
    const sim_event_t *e = sim_events(&n); CHECK(e[n - 2].kind == 'K' && e[n - 2].code == 0 && e[n - 1].buttons == 0);
    uint32_t t = hid_out_now_ms(); CHECK(!rhythm_sleep(10) && hid_out_now_ms() == t && rhythm_aborted());
    rhythm_clear_abort(); hid_out_sleep_ms(5000); CHECK(rhythm_sleep(10));
}
static int hook_calls;
static bool hook_mouse(void) { hook_calls++; hid_out_mouse(0, 1, 1, 0); return true; }
static void test_idle_guard(void) {
    size_t n; sim_reset(3, 0); rhythm_init(); rhythm_set_maxgap_s(120); rhythm_set_idle_hook(hook_mouse); hook_calls = 0;
    hid_out_mouse(0, 0, 0, 0); CHECK(rhythm_sleep(600000));
    const sim_event_t *e = sim_events(&n);
    for (size_t i = 1; i < n; i++) { uint32_t g = e[i].t - e[i - 1].t; CHECK(g >= 90000 && g <= 120020); }
    CHECK(hook_calls >= 5 && hook_calls <= 7);
}
static bool hook_reenter(void) { hook_calls++; return rhythm_sleep(200000); }
static void test_hook_not_reentrant(void) {
    sim_reset(4, 0); rhythm_init(); rhythm_set_maxgap_s(100); rhythm_set_idle_hook(hook_reenter); hook_calls = 0;
    hid_out_mouse(0, 0, 0, 0); rhythm_sleep(150000); CHECK(hook_calls == 1);
}
static void test_fatigue_bounded(void) {
    sim_reset(5, 0); rhythm_init(); bool moved = false;
    for (int i = 0; i < 24 * 60; i++) { hid_out_sleep_ms(60000); uint32_t f = rhythm_fatigue_pct(); CHECK(f >= 85 && f <= 115); moved |= f != 100; }
    CHECK(moved);
}
static void test_scale(void) {
    sim_reset(6, 0); rhythm_init(); CHECK(rhythm_fatigue_pct() == 100);
    CHECK(rhythm_scale(1000, 100) == 1000 && rhythm_scale(1000, 125) == 1250);
}
static void test_wrap(void) {   /* Review Focus 5 */
    size_t n; sim_reset(7, 0xFFFFFFFFu - 30000); rhythm_init(); rhythm_set_maxgap_s(120); rhythm_set_idle_hook(hook_mouse);
    hid_out_mouse(0, 0, 0, 0); rhythm_sleep(300000);
    const sim_event_t *e = sim_events(&n); CHECK(n >= 3);
    for (size_t i = 1; i < n; i++) CHECK((uint32_t)(e[i].t - e[i - 1].t) <= 120020);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `rhythm.h` is not found.

- [ ] **Step 3: Implement `main/rhythm.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_rhythm`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/rhythm.h main/rhythm.c tools
git commit -m "feat(rhythm): personality, fatigue, pause-aware sleep and idle guard"
```

---

### Task 4: Pointer geometry and moves

**Files:**
- Create: `main/pointer.h`, `main/pointer.c`, `tools/sim/test_pointer.c`
- Modify: `tools/CMakeLists.txt`, `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `hid_out.h`, `rhythm.h`.
- Produces:
  ```c
  typedef struct { int16_t v[4]; uint8_t from_end; } region_t; /* x0 y0 x1 y1 at 100%; bit i: v[i] from right/bottom */
  typedef struct { int32_t x0, y0, x1, y1; } rect_t;           /* screen px, inclusive */
  #define BTN_LEFT 0x01
  #define BTN_RIGHT 0x02
  #define BTN_MIDDLE 0x04
  void pointer_set_screen(uint16_t w, uint16_t h, uint16_t scale_pct);
  rect_t pointer_resolve(const region_t *r);  /* offset = v * scale / 100 (truncating); from_end: extent - offset */
  bool pointer_rect_valid(const rect_t *r);   /* x0 <= x1, y0 <= y1, inside the screen */
  void pointer_set_safe(const rect_t *r);     /* every report is clamped into it */
  uint16_t pointer_px_to_units(int32_t px, uint16_t extent); /* (px*32767 + (extent-1)/2) / (extent-1) */
  int32_t pointer_units_to_px(uint16_t u, uint16_t extent);  /* (u*(extent-1) + 16383) / 32767 */
  uint32_t pointer_fitts_ms(float dist_px, float width_px);  /* 150 + 120*log2(D/W + 1), rounded */
  void pointer_pos(int32_t *x, int32_t *y);
  void pointer_jump(const rect_t *r);         /* one report at a random point in r: the boot teleport */
  bool pointer_goto(const rect_t *r);         /* false if aborted */
  ```
- Move algorithm (`pointer_goto`):
  1. Pick a random target point in `r`.
  2. Duration is `pointer_fitts_ms(D, 40*scale/100) * mouse_pct/100 * U(0.85, 1.15)`, passed through `rhythm_scale(…, 100)`.
  3. The path is the existing `human_move` cubic Bezier: control points at 0.30 and 0.70 bowed by ±15% along the normal, with min-jerk easing, sampled every 10 ms via `rhythm_sleep(10)`.
  4. Tremor: each sample whose remaining distance is over 5 px gets an independent offset in [−2, 2] px per axis. The final sample is exactly the target.
  5. Samples are rounded to integer px and clamped to the safe box. A report is sent only when the integer position changes.
  6. Overshoot: when D > 400*scale/100, with 25% probability first move to target + (target − start) × U(3%, 10%), clamped, then make a corrective goto to the target.
  7. An invalid `r` logs `empty region` and returns true without moving.

- [ ] **Step 1: Write the failing tests**

```c
static const rect_t SAFE = {40, 110, 1880, 1010}, EDITOR = {440, 115, 1780, 990}, EXPLORER = {70, 115, 280, 260};
static void setup(uint64_t seed) { sim_reset(seed, 0); rhythm_init(); pointer_set_screen(1920, 1080, 100); pointer_set_safe(&SAFE); }
static bool in(const rect_t *r, int32_t x, int32_t y) { return x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1; }
static void test_units_roundtrip(void) {
    uint16_t ext[] = {768, 1080, 1366, 1920, 3840};
    for (int i = 0; i < 5; i++) for (int32_t p = 0; p < ext[i]; p++) CHECK(pointer_units_to_px(pointer_px_to_units(p, ext[i]), ext[i]) == p);
    CHECK(pointer_px_to_units(0, 1920) == 0 && pointer_px_to_units(1919, 1920) == 32767);
}
static void test_resolve(void) {
    region_t ed = {{440, 115, 140, 90}, 0x0C};
    pointer_set_screen(1920, 1080, 100); rect_t r = pointer_resolve(&ed); CHECK(r.x0 == 440 && r.y0 == 115 && r.x1 == 1780 && r.y1 == 990);
    pointer_set_screen(2560, 1440, 150); r = pointer_resolve(&ed); CHECK(r.x0 == 660 && r.y0 == 172 && r.x1 == 2350 && r.y1 == 1305);
}
static void test_empty_region(void) {   /* Review Focus 4 */
    setup(1); region_t ed = {{440, 115, 140, 90}, 0x0C}; pointer_set_screen(800, 600, 200);
    rect_t r = pointer_resolve(&ed); CHECK(!pointer_rect_valid(&r));
    size_t n; CHECK(pointer_goto(&r)); sim_events(&n); CHECK(n == 0); CHECK(sim_log_count("empty region") == 1);
}
static void test_fitts(void) { CHECK(pointer_fitts_ms(0, 40) == 150); CHECK(pointer_fitts_ms(1000, 40) == 714); }
static void test_jump(void) { setup(2); size_t n; pointer_jump(&EDITOR); const sim_event_t *e = sim_events(&n);
    CHECK(n == 1 && in(&EDITOR, pointer_units_to_px(e[0].x, 1920), pointer_units_to_px(e[0].y, 1080))); }
static void test_goto_paths(void) {
    setup(3); pointer_jump(&EDITOR);
    for (int i = 0; i < 200; i++) {
        const rect_t *t = (i & 1) ? &EDITOR : &EXPLORER; CHECK(pointer_goto(t));
        int32_t x, y; pointer_pos(&x, &y); CHECK(in(t, x, y));
    }
    size_t n; const sim_event_t *e = sim_events(&n); int32_t px = -1, py = -1;
    for (size_t i = 0; i < n; i++) { int32_t x = pointer_units_to_px(e[i].x, 1920), y = pointer_units_to_px(e[i].y, 1080);
        CHECK(in(&SAFE, x, y)); CHECK(e[i].buttons == 0); CHECK(x != px || y != py); px = x; py = y; }
}
static void test_overshoot_rate(void) {
    setup(4); rect_t left = {440, 500, 450, 510}, right = {1700, 500, 1710, 510}; int over = 0;
    pointer_jump(&left);
    for (int i = 0; i < 400; i++) {
        size_t n0; sim_events(&n0); const rect_t *t = (i & 1) ? &left : &right; int32_t sx, sy; pointer_pos(&sx, &sy);
        pointer_goto(t); size_t n; const sim_event_t *e = sim_events(&n); bool beyond = false;
        for (size_t k = n0; k < n; k++) { int32_t x = pointer_units_to_px(e[k].x, 1920);
            beyond |= (t == &right) ? x > right.x1 + 30 : x < left.x0 - 30; }
        over += beyond;
    }
    CHECK(over >= 60 && over <= 140);   /* 25% of 400 */
}
static void test_goto_abort(void) { setup(5); pointer_jump(&EXPLORER); sim_pause(100, 200); CHECK(!pointer_goto(&EDITOR)); }
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `pointer.h` is not found.

- [ ] **Step 3: Implement the functions above in `main/pointer.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_pointer`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/pointer.h main/pointer.c tools
git commit -m "feat(pointer): absolute pointer with regions, Fitts timing and overshoot"
```

---

### Task 5: Pointer actions

**Files:**
- Modify: `main/pointer.h`, `main/pointer.c`, `tools/sim/test_pointer.c`

**Interfaces:**
- Consumes: Task 4.
- Produces (all return false if aborted):
  ```c
  bool pointer_click(uint8_t buttons);          /* settle 80-220, press, hold 55-140, release; at current pos */
  bool pointer_dblclick(void);                  /* press 50-90, release, gap 70-130, press 50-90, release */
  bool pointer_dragselect(const rect_t *r);     /* press, goto with button held (duration x1.3, no overshoot), release */
  bool pointer_hover(const rect_t *r, uint32_t dwell_ms); /* goto, then a 1-2 px wobble every 250-600 ms, staying in r */
  bool pointer_scroll(int notches);             /* positive = up (wheel +1) */
  bool pointer_readscroll(int notches, const rect_t *within); /* notches down, pointer follows the text */
  bool pointer_nudge(const rect_t *within);     /* one of: 6-20 px drift, 800-2000 ms hover, one notch */
  bool pointer_outline(const rect_t *r);        /* corners x0y0, x1y0, x1y1, x0y1, x0y0; 1 s dwell each */
  ```
- **Scroll bursts.** A burst is 1-4 notches. The first interval in a burst is 30-60 ms, and each following interval is 1.3× the previous one. Bursts are separated by 200-700 ms. Each burst has a 10% chance to add one opposite notch followed by one extra notch in the main direction, so the net equals `notches`.
- **READSCROLL.** Between bursts, the pointer moves down by 1-3 × 19 × scale/100 px. When the next position would leave `within`, it does a `pointer_goto` into the top third of `within` instead.

- [ ] **Step 1: Write the failing tests** (append to `test_pointer.c`, reusing `setup`, `in`, `EDITOR` and `SAFE`)

```c
static void test_click(void) {
    setup(10); pointer_jump(&EDITOR); size_t n0; sim_events(&n0); uint32_t t0 = hid_out_now_ms();
    CHECK(pointer_click(BTN_LEFT)); size_t n; const sim_event_t *e = sim_events(&n);
    CHECK(n - n0 == 2 && e[n0].buttons == 1 && e[n0 + 1].buttons == 0 && e[n0].x == e[n0 + 1].x);
    CHECK(e[n0].t - t0 >= 80); uint32_t hold = e[n0 + 1].t - e[n0].t; CHECK(hold >= 55 && hold <= 140);
}
static void test_dblclick(void) {
    setup(11); pointer_jump(&EDITOR); size_t n0; sim_events(&n0); CHECK(pointer_dblclick());
    size_t n; const sim_event_t *e = sim_events(&n); CHECK(n - n0 == 4);
    uint32_t gap = e[n0 + 2].t - e[n0 + 1].t; CHECK(gap >= 70 && gap <= 130); CHECK(e[n0 + 3].t - e[n0].t < 500);
}
static void test_dragselect(void) {
    setup(12); pointer_jump(&EDITOR); size_t n0; sim_events(&n0); CHECK(pointer_dragselect(&EDITOR));
    size_t n; const sim_event_t *e = sim_events(&n); CHECK(e[n0].buttons == 1 && e[n - 1].buttons == 0);
    for (size_t i = n0; i < n - 1; i++) CHECK(e[i].buttons == 1);
    int32_t x, y; pointer_pos(&x, &y); CHECK(in(&EDITOR, x, y));
}
static int wheel_sum(size_t from, bool *reversed, int sign) { size_t n; const sim_event_t *e = sim_events(&n); int s = 0;
    for (size_t i = from; i < n; i++) { if (e[i].wheel) { CHECK(e[i].wheel == 1 || e[i].wheel == -1); if (e[i].wheel != sign) *reversed = true; } s += e[i].wheel; } return s; }
static void test_scroll(void) {
    setup(13); pointer_jump(&EDITOR); int rev = 0;
    for (int i = 0; i < 200; i++) { size_t n0; sim_events(&n0); bool r = false; int k = (i & 1) ? 5 : -3;
        CHECK(pointer_scroll(k)); CHECK(wheel_sum(n0, &r, k > 0 ? 1 : -1) == k); rev += r; }
    CHECK(rev >= 10 && rev <= 80);
}
static void test_readscroll(void) {
    setup(14); pointer_jump(&EDITOR); size_t n0; sim_events(&n0); bool r = false;
    CHECK(pointer_readscroll(6, &EDITOR)); CHECK(wheel_sum(n0, &r, -1) == -6);
    size_t n; const sim_event_t *e = sim_events(&n);
    for (size_t i = n0; i < n; i++) CHECK(in(&SAFE, pointer_units_to_px(e[i].x, 1920), pointer_units_to_px(e[i].y, 1080)));
}
static void test_hover(void) {
    setup(15); pointer_jump(&EXPLORER); CHECK(pointer_hover(&EDITOR, 2000)); int32_t x, y; pointer_pos(&x, &y); CHECK(in(&EDITOR, x, y));
}
static void test_nudge_kinds(void) {
    setup(16); pointer_jump(&EDITOR); int drift = 0, wheel = 0, hover = 0;
    for (int i = 0; i < 100; i++) { size_t n0; sim_events(&n0); int32_t x0, y0; pointer_pos(&x0, &y0); uint32_t t0 = hid_out_now_ms();
        CHECK(pointer_nudge(&EDITOR)); size_t n; const sim_event_t *e = sim_events(&n); int32_t x, y; pointer_pos(&x, &y);
        bool w = false; for (size_t k = n0; k < n; k++) w |= e[k].wheel != 0;
        if (w) wheel++; else if (hid_out_now_ms() - t0 >= 800) hover++; else drift++;
        CHECK(in(&EDITOR, x, y)); CHECK(n > n0); }
    CHECK(drift > 0 && wheel > 0 && hover > 0);
}
static void test_outline(void) {
    setup(17); rect_t r = {500, 300, 900, 600}; pointer_jump(&r); CHECK(pointer_outline(&r)); int32_t x, y; pointer_pos(&x, &y);
    CHECK(x == 500 && y == 300);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build-sim --config Debug`
Expected: the build fails because `pointer_click` and the other new functions are undefined.

- [ ] **Step 3: Implement the functions above in `main/pointer.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_pointer`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/pointer.h main/pointer.c tools/sim/test_pointer.c
git commit -m "feat(pointer): click, double-click, drag, hover, scroll, read-scroll, nudge, outline"
```

---

### Task 6: Editor model (test helper)

**Files:**
- Create: `tools/sim/editor_model.h`, `tools/sim/editor_model.c`, `tools/sim/test_editor_model.c`
- Modify: `tools/CMakeLists.txt` (add `sim/editor_model.c` to `core`), `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `sim_event_t`, `keys_to_ascii` (Task 2).
- Produces:
  ```c
  void ed_reset(void);                    /* one empty line; cursor 0,0 */
  void ed_feed(const sim_event_t *e);     /* acts on key-down transitions: code != 0 and != the previous code */
  const char *ed_text(void);              /* lines joined by '\n' */
  void ed_cursor(int *line, int *col);
  ```
- Emulated VS Code behaviour (C# language configuration, defaults), with no indentation modelled:
  - Typing `(`, `[`, `{` or `"` auto-inserts its closer. This happens only when the next character is end of line, a space, or one of `;:.,=}])>`. `"` is not auto-closed on a line starting with `//`, and neither is it auto-closed when the cursor is inside a string literal; in that case `"` overtypes a closing auto-quote.
  - Typing a closer that equals the *tracked auto-inserted* closer at the cursor moves over it instead of inserting.
  - Tracking is per line and is cleared by any Left, Right, Home, End, Up or Down, and by any line change.
  - Backspace between an opener and its tracked closer deletes both.
  - Enter between `{` and its closer splits into three lines with the cursor on the empty middle line.
  - Up and Down keep the column, clamped to the line length. Delete removes the character at the cursor.

- [ ] **Step 1: Write the failing tests**

```c
static void type(const char *s) { for (; *s; s++) { uint8_t m, k; keys_ascii(*s, &m, &k);
    sim_event_t d = {0, 'K', m, k, 0, 0, 0, 0}, u = {0, 'K', 0, 0, 0, 0, 0, 0}; ed_feed(&d); ed_feed(&u); } }
static void key(uint8_t m, uint8_t k) { sim_event_t d = {0, 'K', m, k, 0, 0, 0, 0}, u = {0, 'K', 0, 0, 0, 0, 0, 0}; ed_feed(&d); ed_feed(&u); }
static void test_plain(void) { ed_reset(); type("abc"); CHECK(strcmp(ed_text(), "abc") == 0); }
static void test_overtype(void) { ed_reset(); type("f(x)"); CHECK(strcmp(ed_text(), "f(x)") == 0); }
static void test_quote_in_comment(void) { ed_reset(); type("// \"a\""); CHECK(strcmp(ed_text(), "// \"a\"") == 0); }
static void test_quote_in_code(void) { ed_reset(); type("s = \"a\";"); CHECK(strcmp(ed_text(), "s = \"a\";") == 0); }
static void test_brace_enter(void) { ed_reset(); type("c {\n"); int l, c; ed_cursor(&l, &c); CHECK(strcmp(ed_text(), "c {\n\n}") == 0 && l == 1 && c == 0); }
static void test_pair_backspace(void) { ed_reset(); type("("); key(0, KEY_BACKSPACE); CHECK(strcmp(ed_text(), "") == 0); }
static void test_tracking_lost(void) { ed_reset(); type("("); key(0, KEY_LEFT); key(0, KEY_RIGHT); type(")"); CHECK(strcmp(ed_text(), "())") == 0); }
static void test_down_end(void) { ed_reset(); type("a {\nx"); key(0, KEY_DOWN); key(0, KEY_END); type("\n"); CHECK(strcmp(ed_text(), "a {\nx\n}\n") == 0); }
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `editor_model.h` is not found.

- [ ] **Step 3: Implement `tools/sim/editor_model.c`**

Store up to 1024 lines of 256 characters each in a static array.

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_editor_model`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add tools
git commit -m "test(sim): VS Code auto-close editor model"
```

---

### Task 7: Typing timing and mistakes

**Files:**
- Create: `main/typing.h`, `main/typing.c`, `tools/sim/test_typing.c`
- Modify: `tools/CMakeLists.txt`, `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `keys.h`, `rhythm.h`, `hid_out.h`; `editor_model.h` in tests.
- Produces:
  ```c
  #define TYPE_ARROW_FIX 0x01   /* allow end-of-line fixes with Left/End (editor only) */
  #define TYPE_OMNIBOX   0x02   /* press Delete before each correction (Chrome address bar) */
  uint32_t typing_nominal_ms(char prev, char cur, uint16_t cpm);
  bool typing_key(uint8_t mod, uint8_t code, bool *pressed);   /* pressed may be NULL */
  bool typing_alt_tab(bool *tab_pressed);
  bool typing_text(const char *s, size_t len, unsigned flags); /* no Enter appended; '\n' in s is typed as Enter */
  ```
- **`typing_nominal_ms`:** base is 60000/cpm. Multiply by 1.35 when prev and cur are different characters on the same finger, by 1.0 on the same hand, and by 0.85 when the hands alternate or a finger is unknown. Multiply again by 1.25 when cur needs Shift or sits on the number row.
- **Actual interval:** nominal × U(0.7, 1.3), then × 0.8 inside a burst. Bursts are 4-12 characters, followed by a 150-400 ms micro-pause. Add 200-600 ms after `;`, `{` and Enter, and 0-60 ms after a space. Pass everything through `rhythm_scale(…, 100)`.
- **`typing_key`:** with a modifier, send (mod,0), wait 30-80 ms, send (mod,code), hold 45-120 ms, send (mod,0), wait 20-60 ms, send (0,0). Without a modifier, send (0,code), hold 45-120 ms, send (0,0). `*pressed` is set right after the (mod,code) report.
- **`typing_alt_tab`:** send (ALT,0), wait 80-200 ms, send (ALT,TAB), hold 45-120 ms, send (ALT,0), wait 100-300 ms, send (0,0), then wait 300-700 ms. `*tab_pressed` is set after (ALT,TAB).
- **Mistakes:**
  - Only letters a-z and A-Z get mistakes. The probability is `error_permille`, × 1.3 inside a burst, and only one error may be outstanding at a time.
  - Kinds: neighbour 50%, transposition 20% (only when the next character is also a letter), omission 15%, doubled letter 15%.
  - Detection: immediate 50%, end of word 35% (just before the next non-word character is typed; word characters are `[A-Za-z0-9_]`), end of line 15%. End-of-line detection happens just before Enter, or before the final `{` of a line. Without `TYPE_ARROW_FIX`, end-of-line detection becomes end of word.
  - Wait 150-400 ms before a fix. Fixes are computed from the typed ("screen") line against the intended line, as below.

  ```c
  /* p = common prefix length, s = common suffix length (p + s <= min lengths).
   * Immediate / end-of-word: s = 0. Press Backspace (screen_len - p) times, type intended[p..intended_len).
   * End-of-line (arrows):   press Left s times, Backspace (screen_len - p - s) times,
   *                         type intended[p..intended_len - s), press End.
   * TYPE_OMNIBOX: press Delete once before the Backspaces. */
  ```

- [ ] **Step 1: Write the failing tests**

```c
static void feed_all(void) { size_t n; const sim_event_t *e = sim_events(&n); ed_reset(); for (size_t i = 0; i < n; i++) if (e[i].kind == 'K') ed_feed(&e[i]); }
static void setup(uint64_t seed, uint16_t err) { sim_reset(seed, 0); rhythm_init(); rhythm_personality_t p = {240, err, 100, 100}; rhythm_set_personality(&p); }
static const char *FOX = "the quick brown fox jumps over the lazy dog";
static void test_nominal(void) {
    uint32_t sf = typing_nominal_ms('f', 'r', 240), sh = typing_nominal_ms('a', 's', 240), alt = typing_nominal_ms('f', 'j', 240);
    CHECK(sf == 337 && sh == 250 && alt == 212); CHECK(typing_nominal_ms('j', 'F', 240) == 265);
}
static void test_exact_no_errors(void) {
    setup(1, 0); CHECK(typing_text(FOX, strlen(FOX), 0)); feed_all(); CHECK(strcmp(ed_text(), FOX) == 0);
    size_t n; const sim_event_t *e = sim_events(&n); CHECK(e[n - 1].code == 0 && e[n - 1].mod == 0);
}
static void test_errors_corrected(void) {
    int backspaces = 0;
    for (uint64_t s = 1; s <= 50; s++) { setup(s, 300); CHECK(typing_text(FOX, strlen(FOX), 0)); feed_all();
        CHECK(strcmp(ed_text(), FOX) == 0); size_t n; const sim_event_t *e = sim_events(&n);
        for (size_t i = 0; i < n; i++) { backspaces += e[i].code == KEY_BACKSPACE; CHECK(e[i].code != KEY_LEFT); } }
    CHECK(backspaces > 0);
}
static void test_arrow_fix(void) {
    int lefts = 0;
    for (uint64_t s = 1; s <= 50; s++) { setup(s, 300); CHECK(typing_text(FOX, strlen(FOX), TYPE_ARROW_FIX)); feed_all();
        CHECK(strcmp(ed_text(), FOX) == 0); size_t n; const sim_event_t *e = sim_events(&n);
        for (size_t i = 0; i < n; i++) lefts += e[i].code == KEY_LEFT; }
    CHECK(lefts > 0);
}
static void test_omnibox_delete_first(void) {
    for (uint64_t s = 1; s <= 30; s++) { setup(s, 300); typing_text(FOX, strlen(FOX), TYPE_OMNIBOX);
        size_t n; const sim_event_t *e = sim_events(&n); bool armed = false; uint8_t prev = 0;
        for (size_t i = 0; i < n; i++) { if (e[i].code == prev) continue; prev = e[i].code;
            if (e[i].code == KEY_DELETE) armed = true; else if (e[i].code == KEY_BACKSPACE) CHECK(armed);
            else if (e[i].code) armed = false; } }
}
static void test_speed(void) {
    char buf[401]; for (int i = 0; i < 400; i++) buf[i] = FOX[i % strlen(FOX)]; buf[400] = 0;
    setup(9, 0); typing_text(buf, 400, 0); uint32_t cpm = 400u * 60000u / hid_out_now_ms(); CHECK(cpm >= 150 && cpm <= 330);
}
static void test_key_combo(void) {
    setup(2, 0); bool pressed = false; CHECK(typing_key(KMOD_CTRL, 0x16, &pressed)); CHECK(pressed);
    size_t n; const sim_event_t *e = sim_events(&n); CHECK(n == 4);
    CHECK(e[0].mod == KMOD_CTRL && e[0].code == 0 && e[1].code == 0x16 && e[2].code == 0 && e[3].mod == 0);
}
static void test_key_abort_before_press(void) {
    setup(3, 0); sim_pause(1, 100000); bool pressed = false; CHECK(!typing_key(KMOD_CTRL, 0x16, &pressed)); CHECK(!pressed);
}
static void test_alt_tab(void) {
    setup(4, 0); bool tab = false; CHECK(typing_alt_tab(&tab)); CHECK(tab); size_t n; const sim_event_t *e = sim_events(&n);
    CHECK(n == 4 && e[1].mod == KMOD_ALT && e[1].code == KEY_TAB && e[3].mod == 0);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `typing.h` is not found.

- [ ] **Step 3: Implement `main/typing.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_typing`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/typing.h main/typing.c tools
git commit -m "feat(typing): key timing, bursts and self-correcting mistakes"
```

---

### Task 8: Snippet grammar

**Files:**
- Modify: `main/typing.h`, `main/typing.c`, `tools/sim/test_typing.c`

**Interfaces:**
- Consumes: Task 7.
- Produces:
  ```c
  bool typing_check_line(const char *line, size_t len, const char **why);
  bool typing_check_snippet(const char *s, size_t len, int *bad_line, const char **why); /* 1-based line */
  bool typing_snippet(const char *s, size_t len, void (*midway)(void *ctx), void *ctx);
  ```
- **Line rules:**
  - Printable ASCII only, no tab, no leading or trailing space, no `///`.
  - The line is exactly `}` or `};`; or it ends in `{` and everything before that `{` is balanced; or it is fully balanced.
  - "Balanced" means `()` `[]` `{}` nest correctly. Outside `//` comments, unescaped `"` occur in pairs, and `'` occur in pairs outside strings.
- **Snippet rules:**
  - Every line passes the line rules.
  - The first and last lines are not blank.
  - No blank line directly precedes a closer line, and no `{` line is directly followed by a closer line.
  - Brace depth never goes negative and ends at 0.
- **Typer**, for each line (`\r` stripped):
  - A closer line: press Down and End, type `;` for `};`, then press Enter unless the next line is a closer.
  - Any other line: type it with `TYPE_ARROW_FIX`, with end-of-line fixes made before a final `{`. Press Enter unless the next line is a closer.
  - A blank line is just Enter.
  - The last line always gets Enter.
  - `midway`, when not NULL, is called once at the line boundary after line `nlines/2`.

- [ ] **Step 1: Write the failing tests** (append; reuse `setup` and `feed_all`)

```c
static bool line_ok(const char *s) { const char *why; return typing_check_line(s, strlen(s), &why); }
static void test_check_line(void) {
    const char *ok[] = {"int x = 1;", "if (a) {", "}", "};", "// don't panic", "var s = $\"a{b}\";", "var l = new List<int> { 1, 2 };", "#region Data"};
    const char *bad[] = {" int x;", "int x; ", "/// doc", "} else {", "foo(", "\tx = 1;", "x = \"abc;", "caf\xc3\xa9"};
    for (int i = 0; i < 8; i++) { CHECK(line_ok(ok[i])); CHECK(!line_ok(bad[i])); }
}
static bool snip_ok(const char *s) { int l; const char *w; return typing_check_snippet(s, strlen(s), &l, &w); }
static void test_check_snippet(void) {
    CHECK(snip_ok("public class A {\npublic int X { get; set; }\n}"));
    CHECK(!snip_ok("void F() {\n}")); CHECK(!snip_ok("class A {\nint x;\n\n}")); CHECK(!snip_ok("}\nclass A {"));
    CHECK(!snip_ok("class A {\nint x;")); CHECK(!snip_ok("\nclass A { }"));
}
static const char *NESTED =
    "public class Cache {\n// keep it small\nprivate readonly Dictionary<string, int> _items = new();\n"
    "public int Get(string key) {\nif (_items.TryGetValue(key, out var v)) {\nreturn v;\n}\nreturn -1;\n}\n}";
static void test_snippet_typed_exactly(void) {
    char expect[512]; snprintf(expect, sizeof expect, "%s\n", NESTED);
    for (uint64_t s = 1; s <= 30; s++) { setup(s, 300); CHECK(typing_snippet(NESTED, strlen(NESTED), NULL, NULL)); feed_all();
        CHECK(strcmp(ed_text(), expect) == 0); }
}
static int midway_calls; static void midway(void *ctx) { (void)ctx; midway_calls++; }
static void test_midway_once(void) { setup(5, 0); midway_calls = 0; typing_snippet(NESTED, strlen(NESTED), midway, NULL); CHECK(midway_calls == 1); }
static void test_snippet_crlf(void) {
    const char *crlf = "class A {\r\nint x;\r\n}"; setup(6, 0); typing_snippet(crlf, strlen(crlf), NULL, NULL); feed_all();
    CHECK(strcmp(ed_text(), "class A {\nint x;\n}\n") == 0);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build build-sim --config Debug`
Expected: the build fails because `typing_check_line` is undefined.

- [ ] **Step 3: Implement the functions above in `main/typing.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_typing`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/typing.h main/typing.c tools/sim/test_typing.c
git commit -m "feat(typing): snippet grammar typer and checker for VS Code auto-close"
```

---

### Task 9: Script loader and validator

**Files:**
- Create: `main/script.h`, `main/script.c`, `tools/sim/test_script_load.c`
- Modify: `tools/CMakeLists.txt`, `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `pointer.h` (`region_t`, `pointer_set_screen`, `pointer_resolve`, `pointer_rect_valid`, `pointer_set_safe`), `rhythm_set_maxgap_s`, `keys_parse_combo`, `typing_check_snippet`, `str_ieq`.
- Produces:
  ```c
  typedef struct { uint16_t w, h, scale; uint32_t maxgap_s, searchgap_s; } script_settings_t;
  bool script_load(const char *dev, size_t dev_len, const char *snip, size_t snip_len); /* false only if MAIN missing */
  int script_problems(void);
  const script_settings_t *script_settings(void);
  bool script_region_spec(const char *name, region_t *out);
  int script_pool_total(void);
  const char *script_pool_name(int i, size_t *len);
  int script_pool_count(const char *name);
  bool script_pool_item(const char *name, int i, const char **s, size_t *len); /* no trailing '\n'; may hold '\r' */
  bool script_pool_is_lines(const char *name);
  ```
- **Loader behaviour:**
  - Lines may end in `\n` or `\r\n`. Outside POOL bodies, leading whitespace is ignored and lines starting with `#` are comments. Commands are case-insensitive.
  - In `developer.txt`, only `SCREEN`, `REGION`, `MAXGAP`, `SEARCHGAP` and `DEF … END` may appear at top level. In `snippets.txt`, only `POOL … END`.
  - A POOL body is literal up to a line that is exactly `END`, after stripping `\r`. Items are separated by lines that are exactly `---`. `POOL name LINES` makes every non-blank line one item.
  - At the end of loading:
    - Apply SCREEN, or the default with the warning `SCREEN missing, using 1920 1080 100`.
    - Resolve every region and log `empty region <name>` for invalid ones.
    - Call `pointer_set_safe` with the bounding box of all valid regions, and apply MAXGAP.
  - Table capacities: DEF 48, POOL 24, REGION 8. Nothing from the scripts is copied, so tables store pointers into the text.
- **Problems** each produce one `hid_out_log` line containing the phrase quoted below:
  - Structure: `unbalanced END`, `unknown command`, `missing MAIN`, `LOOP outside MAIN`.
  - References: `unknown CALL target`, `unknown pool`, `unknown region`, `CODE needs a snippet pool`, `TYPE @ needs a LINES pool`.
  - Syntax: `bad WHEN` (only `WHEN LINES > <n>` is accepted), `unknown key`, `bad number`.
  - CHOOSE weights: `CHOOSE weight over 50%`.
  - Midway DEF (the second CODE argument): `midway DEF may only move` when it contains anything except `GOTO HOVER SCROLL READSCROLL IDLE WAIT`.

  The command set validated here is the full set from spec section 7 plus the additions in "Decisions" item 8.

- [ ] **Step 1: Write the failing tests**

```c
static const char *SNIP = "POOL words LINES\nalpha\nbeta\n\nEND\nPOOL types\nclass A {\n#region R\nint x;\n#endregion\n}\n---\nclass B { }\nEND\n";
static bool load(const char *dev) { sim_reset(1, 0); rhythm_init(); return script_load(dev, strlen(dev), SNIP, strlen(SNIP)); }
static void test_minimal(void) { CHECK(load("DEF MAIN\nWAIT 10\nEND\n")); CHECK(script_problems() == 0); CHECK(sim_log_count("SCREEN missing") == 1);
    const script_settings_t *s = script_settings(); CHECK(s->w == 1920 && s->h == 1080 && s->scale == 100 && s->maxgap_s == 210 && s->searchgap_s == 360); }
static void test_missing_main(void) { CHECK(!load("DEF other\nEND\n")); CHECK(sim_log_count("missing MAIN") == 1); }
static void test_problems(void) {
    struct { const char *dev, *phrase; } c[] = {
        {"DEF MAIN\nREPEAT 1 2\nWAIT 1\nEND\n", "unbalanced END"}, {"DEF MAIN\nCALL nope\nEND\n", "unknown CALL target"},
        {"DEF MAIN\nCODE nope\nEND\n", "unknown pool"}, {"DEF MAIN\nGOTO nowhere\nEND\n", "unknown region"},
        {"DEF MAIN\nEND\nDEF x\nLOOP\nEND\n", "LOOP outside MAIN"}, {"DEF MAIN\nCODE words\nEND\n", "CODE needs a snippet pool"},
        {"DEF MAIN\nTYPE @types\nEND\n", "TYPE @ needs a LINES pool"}, {"DEF MAIN\nFROB\nEND\n", "unknown command"},
        {"DEF MAIN\nWHEN LINES < 3\nEND\nEND\n", "bad WHEN"}, {"DEF MAIN\nKEY CTRL+BOGUS\nEND\n", "unknown key"},
        {"DEF MAIN\nCHOOSE\n60 a\n40 b\nEND\nEND\nDEF a\nEND\nDEF b\nEND\n", "CHOOSE weight over 50%"},
        {"DEF MAIN\nCODE types m\nEND\nDEF m\nCLICK\nEND\n", "midway DEF may only move"} };
    for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) { load(c[i].dev); CHECK(script_problems() >= 1); CHECK(sim_log_count(c[i].phrase) >= 1); }
}
static void test_region_and_settings(void) {
    CHECK(load("SCREEN 2560 1440 150\nMAXGAP 150\nSEARCHGAP 400\nREGION editor +440 +115 -140 -90\nDEF MAIN\nEND\n"));
    region_t r; CHECK(script_region_spec("editor", &r)); CHECK(r.v[0] == 440 && r.v[3] == 90 && r.from_end == 0x0C);
    CHECK(script_settings()->maxgap_s == 150 && script_settings()->searchgap_s == 400 && script_settings()->scale == 150);
}
static void test_pool_literal(void) {   /* Review Focus 3 */
    load("DEF MAIN\nEND\n"); CHECK(script_pool_count("types") == 2 && script_pool_count("words") == 2 && script_pool_is_lines("words"));
    const char *s; size_t n; CHECK(script_pool_item("types", 0, &s, &n)); CHECK(n == strlen("class A {\n#region R\nint x;\n#endregion\n}") && memcmp(s, "class A {\n#region R", 19) == 0);
}
static void test_crlf(void) {   /* Review Focus 1 */
    const char *dev = "REGION editor +440 +115 -140 -90\r\nDEF MAIN\r\n# comment\r\n  WAIT 10\r\nEND\r\n";
    const char *snip = "POOL words LINES\r\nalpha\r\nbeta\r\nEND\r\n";
    sim_reset(1, 0); rhythm_init(); CHECK(script_load(dev, strlen(dev), snip, strlen(snip))); CHECK(script_problems() == 0);
    const char *s; size_t n; CHECK(script_pool_item("words", 1, &s, &n)); CHECK(n == 4 && memcmp(s, "beta", 4) == 0);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `script.h` is not found.

- [ ] **Step 3: Implement the loader and validator in `main/script.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_script_load`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/script.h main/script.c tools
git commit -m "feat(script): loader, tables and validation pass"
```

---

### Task 10: Interpreter flow control and run loop

**Files:**
- Modify: `main/script.h`, `main/script.c`
- Create: `tools/sim/test_script_flow.c`

**Interfaces:**
- Consumes: Tasks 3 and 9.
- Produces:
  ```c
  typedef enum { APP_CODE, APP_CHROME } script_app_t;
  typedef struct { script_app_t fg; bool terminal_open, search_open, searched; uint32_t last_search_ms; int lines; } script_state_t;
  typedef enum { TRACE_CALL, TRACE_PICK, TRACE_PICK_END, TRACE_SNIPPET } script_trace_t;
  typedef struct { script_trace_t kind; const char *name; size_t len; int depth; int index; } script_event_t;
  typedef struct { const char *name; size_t len; int weight; } script_choice_t;
  void script_set_trace(void (*fn)(const script_event_t *ev, void *ctx), void *ctx);
  bool script_call(const char *name);           /* runs one DEF at depth 1; false if aborted */
  void script_run(bool (*keep_going)(void));    /* NULL = forever */
  const script_state_t *script_state(void);
  int script_main_choices(script_choice_t *out, int max); /* entries of the first CHOOSE in MAIN */
  ```
- **Semantics:**
  - `CALL` emits TRACE_CALL with depth = call depth. Beyond depth 8 it logs `call depth` and skips the call. Blocks nested deeper than 8 log `block depth` and are skipped.
  - `CHOOSE` emits TRACE_PICK before the call and TRACE_PICK_END after it, and no TRACE_CALL. Both carry the depth of the CHOOSE's own DEF (MAIN is depth 1).
  - `script_run` emits TRACE_CALL with depth 0 for its own calls of MAIN and RESUME.
  - If no `editor` region is defined, `script_run` logs `no editor region` and skips the teleport.
  - `MAYBE pct`, `REPEAT min max` and `WHEN LINES > n` behave as the spec describes. `LINES n` sets the estimate and `LINES +n` adds to it.
  - `WAIT ms` calls `rhythm_sleep(ms)`. `IDLE a b` calls `rhythm_sleep(rhythm_scale(rhythm_rand(a, b), pause_pct))`.
  - `LOOP` returns from MAIN so that `script_run` restarts it.
- **CHOOSE pick:** each CHOOSE remembers its last pick, keyed by line offset in a table of 32 entries. It draws from compensated weights, excluding the last pick:

  ```c
  /* Shares of a no-immediate-repeat chain P(j|i) = v_j / (1 - v_i) are proportional to v_j (1 - v_j).
   * Solve v_j (1 - v_j) = k w_j, sum v = 1, by fixed-point iteration (w normalised, every w_j < 0.5). */
  for (int i = 0; i < n; i++) v[i] = w[i];
  for (int it = 0; it < 40; it++) {
      float sum = 0;
      for (int i = 0; i < n; i++) { v[i] = w[i] / (1.0f - v[i]); sum += v[i]; }
      for (int i = 0; i < n; i++) v[i] /= sum;
  }
  ```
- **`script_run`:**
  1. `rhythm_sleep(rhythm_rand(10000, 15000))`, the boot wait.
  2. `pointer_jump(editor)`.
  3. Loop: if `keep_going` is set and returns false, return. Otherwise call MAIN.
  4. When MAIN is aborted, poll `hid_out_paused()` with `hid_out_sleep_ms(50)` until it clears, call `rhythm_clear_abort()`, call RESUME if it is defined, and repeat the loop.

- [ ] **Step 1: Write the failing tests**

```c
#define MAX_TR 100000
static script_event_t tr[MAX_TR]; static int ntr;
static void on_trace(const script_event_t *e, void *ctx) { (void)ctx; CHECK(ntr < MAX_TR); if (ntr < MAX_TR) tr[ntr++] = *e; }
static bool same(const script_event_t *a, const script_event_t *b) { return a->len == b->len && memcmp(a->name, b->name, a->len) == 0; }
static void load(uint64_t seed, const char *dev) { sim_reset(seed, 0); rhythm_init(); rhythm_personality_t p = {240, 0, 100, 100};
    rhythm_set_personality(&p); ntr = 0; script_set_trace(on_trace, NULL);
    const char *snip = ""; CHECK(script_load(dev, strlen(dev), snip, 0)); }
static int count(script_trace_t k, const char *name) { int c = 0; for (int i = 0; i < ntr; i++) c += tr[i].kind == k && str_ieq(tr[i].name, tr[i].len, name); return c; }
static void test_choose_alternates(void) {
    load(1, "DEF MAIN\nREPEAT 1000 1000\nCHOOSE\n1 a\n1 b\nEND\nEND\nEND\nDEF a\nEND\nDEF b\nEND\n"); script_call("MAIN");
    const script_event_t *prev = NULL; int picks = 0;
    for (int i = 0; i < ntr; i++) if (tr[i].kind == TRACE_PICK) { if (prev) CHECK(!same(&tr[i], prev)); prev = &tr[i]; picks++; }
    CHECK(picks == 1000);
}
static void test_choose_compensated(void) {
    load(2, "DEF MAIN\nREPEAT 20000 20000\nCHOOSE\n40 a\n12 b\n13 c\n8 d\n15 e\n12 f\nEND\nEND\nEND\n"
            "DEF a\nEND\nDEF b\nEND\nDEF c\nEND\nDEF d\nEND\nDEF e\nEND\nDEF f\nEND\n"); script_call("MAIN");
    const char *names[] = {"a", "b", "c", "d", "e", "f"}; int w[] = {40, 12, 13, 8, 15, 12};
    for (int i = 0; i < 6; i++) { int pct = count(TRACE_PICK, names[i]) * 100 / 20000; CHECK(pct >= w[i] - 2 && pct <= w[i] + 2); }
    for (int i = 0, last = -1; i < ntr; i++) if (tr[i].kind == TRACE_PICK) { CHECK(last < 0 || !same(&tr[i], &tr[last])); last = i; }
}
static void test_maybe_repeat_when(void) {
    load(3, "DEF MAIN\nREPEAT 2000 2000\nMAYBE 30\nCALL hit\nEND\nEND\nEND\nDEF hit\nEND\n"); script_call("MAIN");
    int h = count(TRACE_CALL, "hit"); CHECK(h >= 520 && h <= 680);
    load(4, "DEF MAIN\nREPEAT 300 300\nCALL r\nEND\nEND\nDEF r\nREPEAT 2 4\nCALL x\nEND\nEND\nDEF x\nEND\n"); script_call("MAIN");
    int x = count(TRACE_CALL, "x"); CHECK(x > 600 && x < 1200);
    load(5, "DEF MAIN\nLINES 5\nWHEN LINES > 3\nCALL yes\nEND\nLINES 2\nLINES +1\nWHEN LINES > 3\nCALL no\nEND\nEND\nDEF yes\nEND\nDEF no\nEND\n");
    script_call("MAIN"); CHECK(count(TRACE_CALL, "yes") == 1 && count(TRACE_CALL, "no") == 0 && script_state()->lines == 3);
}
static void test_idle_and_wait(void) {
    load(6, "DEF MAIN\nIDLE 1000 1000\nEND\n"); script_call("MAIN"); CHECK(hid_out_now_ms() == 1000);
    load(7, "DEF MAIN\nWAIT 500\nEND\n"); rhythm_personality_t p = {240, 0, 100, 125}; rhythm_set_personality(&p); script_call("MAIN"); CHECK(hid_out_now_ms() == 500);
    load(8, "DEF MAIN\nIDLE 1000 1000\nEND\n"); rhythm_set_personality(&p); script_call("MAIN"); CHECK(hid_out_now_ms() == 1250);
}
static void test_depth_limit(void) {
    load(9, "DEF MAIN\nCALL d2\nEND\nDEF d2\nCALL d3\nEND\nDEF d3\nCALL d4\nEND\nDEF d4\nCALL d5\nEND\nDEF d5\nCALL d6\nEND\n"
            "DEF d6\nCALL d7\nEND\nDEF d7\nCALL d8\nEND\nDEF d8\nCALL d9\nEND\nDEF d9\nEND\n");
    script_call("MAIN"); CHECK(count(TRACE_CALL, "d9") == 0 && sim_log_count("call depth") == 1);
}
static uint32_t stop_at; static bool until(void) { return hid_out_now_ms() < stop_at; }
static void test_run_boot_and_loop(void) {
    load(10, "REGION editor +440 +115 -140 -90\nDEF MAIN\nCALL tick\nWAIT 1000\nLOOP\nEND\nDEF tick\nEND\n"); stop_at = 30000; script_run(until);
    size_t n; const sim_event_t *e = sim_events(&n); CHECK(n >= 1 && e[0].kind == 'M' && e[0].t >= 10000 && e[0].t <= 15000);
    CHECK(count(TRACE_CALL, "tick") >= 15);
}
static void test_pause_resume(void) {
    load(11, "REGION editor +440 +115 -140 -90\nDEF MAIN\nCALL work\nLOOP\nEND\nDEF work\nWAIT 10000\nEND\nDEF RESUME\nEND\n");
    sim_pause(20000, 25000); stop_at = 40000; script_run(until);
    int r = -1; for (int i = 0; i < ntr; i++) if (tr[i].kind == TRACE_CALL && str_ieq(tr[i].name, tr[i].len, "RESUME")) r = i;
    CHECK(r > 0); bool work_after = false;
    for (int i = r + 1; i < ntr; i++) work_after |= tr[i].kind == TRACE_CALL && str_ieq(tr[i].name, tr[i].len, "work");
    CHECK(work_after);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `script_call` is undefined.

- [ ] **Step 3: Implement the interpreter flow and `script_run` in `main/script.c`**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R test_script_flow`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/script.h main/script.c tools
git commit -m "feat(script): DEF/CALL/CHOOSE/MAYBE/REPEAT/WHEN, compensated weights, pause and resume"
```

---

### Task 11: Interpreter input commands and tracked state

**Files:**
- Modify: `main/script.c`
- Create: `tools/sim/test_script_cmds.c`

**Interfaces:**
- Consumes: Tasks 4, 5, 7, 8, 9 and 10.
- Produces the runtime behaviour of the remaining commands:

| Command | Behaviour |
|---|---|
| `KEY combo` | `typing_key` |
| `TYPE text` / `TYPE @pool` | `typing_text(…, 0)`. A pool item is picked with no immediate repeat |
| `CODE pool [def]` | `typing_snippet`. With 40% probability, `def` is called as the midway hook. TRACE_SNIPPET carries the pool name and index. `lines` grows by the snippet's line count even if aborted |
| `GOTO r`, `HOVER r ms` | The pointer function on `pointer_resolve(r)` |
| `CLICK [LEFT\|RIGHT\|MIDDLE]`, `DBLCLICK`, `DRAGSELECT r` | Pointer click functions. Skipped with the log `click suppressed` when `fg == APP_CHROME` |
| `SCROLL n`, `READSCROLL n` | `pointer_scroll(n)` and `pointer_readscroll(n, fg region)` |
| `OUTLINE r` | `pointer_outline` |
| `FOCUS code\|chrome` | `typing_alt_tab` only if `fg` differs. `fg` flips when Tab is pressed |
| `TERMINAL open\|close` | Ctrl+GRAVE only if the state differs. The state flips when the key is pressed |
| `SEARCH pool` | Requires `fg == APP_CHROME`, otherwise logs `SEARCH needs chrome`. Skipped while `searched && now − last_search < SEARCHGAP·1000`. Otherwise: press Ctrl+T, then set `search_open`, `searched` and `last_search` on the press. Wait 400-900 ms, call `typing_text(item, TYPE_OMNIBOX)`, press Delete, then press Enter (85%) or Down + Enter (15%) |
| `ENDSEARCH` | When `search_open`, does `FOCUS chrome` if needed and presses Ctrl+W. `search_open` clears on the press |

  - The fg region is `chrome_page` if `fg == APP_CHROME`, else `terminal` if `terminal_open`, else `editor`.
  - Hand-offs: before a pointer command whose previous input was a key, wait 250-900 ms. Before a key command whose previous input was a click, wait 200-700 ms.
  - Idle hook: `script_load` registers a hook that calls `pointer_nudge(fg region)`.

- [ ] **Step 1: Write the failing tests**

```c
static const char *REG = "REGION editor +440 +115 -140 -90\nREGION terminal +440 -250 -60 -90\nREGION chrome_page +40 +130 -40 -70\nREGION explorer +70 +115 +280 +260\n";
static const char *SNIP = "POOL q LINES\nlinq group by example\ngit rebase onto\nEND\nPOOL types\nclass A {\nint x;\n}\n---\nclass B { }\nEND\n";
static void load(uint64_t seed, const char *body) { char dev[2048]; snprintf(dev, sizeof dev, "%s%sDEF MAIN\nEND\n", REG, body);
    sim_reset(seed, 0); rhythm_init(); rhythm_personality_t p = {240, 0, 100, 100}; rhythm_set_personality(&p);
    CHECK(script_load(dev, strlen(dev), SNIP, strlen(SNIP))); CHECK(script_problems() == 0); pointer_set_screen(1920, 1080, 100); }
static int presses(uint8_t mod, uint8_t code) { size_t n; const sim_event_t *e = sim_events(&n); int c = 0; uint8_t prev = 0;
    for (size_t i = 0; i < n; i++) if (e[i].kind == 'K') { if (e[i].code && e[i].code != prev && e[i].mod == mod && e[i].code == code) c++; prev = e[i].code; } return c; }
static int buttons_down(void) { size_t n; const sim_event_t *e = sim_events(&n); int c = 0; for (size_t i = 0; i < n; i++) c += e[i].kind == 'M' && e[i].buttons; return c; }
static void test_focus_idempotent(void) { load(1, "DEF t\nFOCUS chrome\nFOCUS chrome\nEND\n"); script_call("t");
    CHECK(presses(KMOD_ALT, KEY_TAB) == 1 && script_state()->fg == APP_CHROME); }
static void test_terminal_idempotent(void) { load(2, "DEF t\nTERMINAL open\nTERMINAL open\nTERMINAL close\nEND\n"); script_call("t");
    CHECK(presses(KMOD_CTRL, KEY_GRAVE) == 2 && !script_state()->terminal_open); }
static void test_no_click_in_chrome(void) { load(3, "DEF t\nFOCUS chrome\nGOTO chrome_page\nCLICK\nDBLCLICK\nDRAGSELECT chrome_page\nEND\n"); script_call("t");
    CHECK(buttons_down() == 0 && sim_log_count("click suppressed") == 3); }
static void test_search_and_gap(void) {
    load(4, "DEF t\nFOCUS chrome\nSEARCH q\nENDSEARCH\nSEARCH q\nEND\nDEF later\nWAIT 361000\nSEARCH q\nEND\n");
    script_call("t"); CHECK(presses(KMOD_CTRL, 0x17) == 1 && presses(KMOD_CTRL, 0x1A) == 1 && !script_state()->search_open);
    CHECK(presses(0, KEY_DELETE) >= 1); script_call("later"); CHECK(presses(KMOD_CTRL, 0x17) == 2);
}
static void test_searchgap_wrap(void) {   /* Review Focus 5 */
    load(5, "DEF t\nFOCUS chrome\nSEARCH q\nWAIT 100000\nSEARCH q\nWAIT 300000\nSEARCH q\nEND\n");
    sim_reset(5, 0xFFFFFFFFu - 50000); rhythm_init(); script_call("t"); CHECK(presses(KMOD_CTRL, 0x17) == 2);
}
static void test_search_needs_chrome(void) { load(6, "DEF t\nSEARCH q\nEND\n"); script_call("t"); CHECK(presses(KMOD_CTRL, 0x17) == 0 && sim_log_count("SEARCH needs chrome") == 1); }
static void test_endsearch_focuses(void) { load(7, "DEF t\nFOCUS chrome\nSEARCH q\nFOCUS code\nENDSEARCH\nEND\n"); script_call("t");
    CHECK(presses(KMOD_ALT, KEY_TAB) == 3 && script_state()->fg == APP_CHROME && !script_state()->search_open); }
static uint32_t first_press_time(uint8_t mod, uint8_t code) { size_t n; const sim_event_t *e = sim_events(&n);
    for (size_t i = 0; i < n; i++) if (e[i].mod == mod && e[i].code == code) return e[i].t; return 0; }
static void test_pause_mid_combo(void) {   /* Review Focus 2 */
    load(8, "DEF t\nFOCUS chrome\nEND\n"); script_call("t"); uint32_t tab = first_press_time(KMOD_ALT, KEY_TAB);
    load(8, "DEF t\nFOCUS chrome\nEND\n"); sim_pause(tab + 1, tab + 5000); CHECK(!script_call("t")); CHECK(script_state()->fg == APP_CHROME);
    load(9, "DEF t\nTERMINAL open\nEND\n"); script_call("t"); uint32_t g = first_press_time(KMOD_CTRL, KEY_GRAVE);
    load(9, "DEF t\nTERMINAL open\nEND\n"); sim_pause(g + 1, g + 5000); script_call("t"); CHECK(script_state()->terminal_open);
}
static script_event_t snips[4]; static int nsnips;
static void on_snip(const script_event_t *e, void *ctx) { (void)ctx; if (e->kind == TRACE_SNIPPET && nsnips < 4) snips[nsnips++] = *e; }
static void test_code_lines_and_trace(void) {   /* pool "types" has a 3-line and a 1-line snippet; no immediate repeat */
    load(10, "DEF t\nCODE types\nCODE types\nEND\n"); nsnips = 0; script_set_trace(on_snip, NULL); script_call("t");
    CHECK(script_state()->lines == 4); CHECK(nsnips == 2 && snips[0].index != snips[1].index); }
static void test_handoff(void) {
    load(11, "DEF t\nTYPE x\nGOTO editor\nCLICK\nTYPE y\nEND\n"); script_call("t"); size_t n; const sim_event_t *e = sim_events(&n);
    uint32_t last_key = 0, first_mouse = 0, click_up = 0, next_key = 0;
    for (size_t i = 0; i < n; i++) { if (e[i].kind == 'K' && !first_mouse) last_key = e[i].t;
        if (e[i].kind == 'M' && !first_mouse) first_mouse = e[i].t;
        if (e[i].kind == 'M' && i > 0 && e[i - 1].buttons && !e[i].buttons) click_up = e[i].t;
        if (e[i].kind == 'K' && click_up && !next_key) next_key = e[i].t; }
    CHECK(first_mouse - last_key >= 250); CHECK(next_key - click_up >= 200);
}
static void test_idle_nudges(void) {
    load(12, "MAXGAP 210\nDEF t\nGOTO editor\nIDLE 300000 300000\nEND\n"); script_call("t"); size_t n; const sim_event_t *e = sim_events(&n);
    for (size_t i = 1; i < n; i++) CHECK(e[i].t - e[i - 1].t <= 210020);
}
```

`SCREEN missing` is a logged warning and does not count in `script_problems()`, so these test scripts load with zero problems.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug; ctest --test-dir build-sim -C Debug -R test_script_cmds`
Expected: FAIL, because the commands log `unknown command` or have no effect.

- [ ] **Step 3: Implement the commands above in `main/script.c`**

- [ ] **Step 4: Run all host tests**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure`
Expected: `100% tests passed`

- [ ] **Step 5: Commit**

```powershell
git add main/script.c tools
git commit -m "feat(script): input commands, tracked app/terminal/search state, hand-off pauses"
```

---

### Task 12: Script content and checker

**Files:**
- Create: `main/macros/developer.txt`, `main/macros/snippets.txt`, `tools/check_script/CMakeLists.txt`, `tools/check_script/check_script.c`, `tools/sim/test_content.c`
- Modify: `tools/CMakeLists.txt` (`add_subdirectory(check_script)`), `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 8-11, `sim_read_file`, `MACROS_DIR`.
- Produces: `check_script <developer.txt> <snippets.txt> [--emit-cs <out.cs>]`.
  - It prints each problem and exits 1 when `script_problems() > 0` or any check below fails.
  - Every non-LINES pool item must pass `typing_check_snippet`. Every LINES item must be non-empty printable ASCII.
  - These must exist:
    - Pools: `types`, `comments`, `identifiers`, `files`, `terminal_cmds`, `queries`, `find_terms`, `skeleton`.
    - DEFs: `MAIN`, `RESUME`, `open_scratch`, `reset_scratch`, `calibrate`, `write_code`, `edit_refactor`, `navigate`, `terminal_check`, `lookup`, `think`.
    - Regions: `editor`, `explorer`, `terminal`, `chrome_page`.
  - `queries` must have at least 25 items and `types` at least 20.
  - `--emit-cs` writes every `types` snippet wrapped as `namespace SnippetCheck.S<N> { … }`.
- **Content rules for `developer.txt`** (spec sections 4, 8 and 10, plus "Decisions"):
  - Header: `SCREEN 1920 1080 100`, `MAXGAP 210`, `SEARCHGAP 360`, and the four regions with the defaults from Decisions item 1. Below them, a commented-out `# CALL calibrate` line and a `calibrate` DEF that runs `OUTLINE` on each region.
  - `MAIN`: `WHEN LINES > 120` → `CALL reset_scratch`, then `CHOOSE` with `40 write_code`, `12 edit_refactor`, `13 navigate`, `8 terminal_check`, `15 lookup`, `12 think`, then `LOOP`.
  - Every activity starts with `KEY ESC` and ends with `GOTO editor` and `CLICK` while VS Code is in front, with the panel closed and no search tab open. Every activity that types into the editor first calls `open_scratch`, which is `KEY CTRL+P`, `IDLE`, `TYPE Scratch.cs`, `IDLE`, `KEY ENTER`.
  - `write_code`: `CALL open_scratch`, `KEY CTRL+END`, `KEY ENTER`, `IDLE 3000 15000`, an optional `CODE comments`, then `CODE types peek` where `peek` is a pointer-only DEF (scroll up, hover, return). Then `KEY CTRL+S`, `IDLE`, `READSCROLL`.
  - `edit_refactor`: per spec section 8, item 2, built from `DBLCLICK` + `TYPE @identifiers`, line edits with `HOME`, `SHIFT+END`, `SHIFT+ALT+DOWN`, `CTRL+SLASH`, `ALT+UP`/`ALT+DOWN` and `CTRL+SHIFT+K`, `DRAGSELECT editor` + `CTRL+C`/`CTRL+V`, and `F2` + `TYPE @identifiers` + `ENTER`, and `SHIFT+ALT+F`. Ends with `CTRL+S`. Every edit opens with `CALL open_scratch`.
  - `navigate`: `CTRL+P` + `TYPE @files` + `ENTER`, `READSCROLL`, `HOVER`, `CLICK`, `F12`, `ALT+LEFT`; `CTRL+SHIFT+F` + `TYPE @find_terms` + a few `F4` (next result) presses + `ESC`; sometimes `GOTO explorer` + `CLICK`. Always ends with `CALL open_scratch`.
  - `terminal_check`: `TERMINAL open`, `TYPE @terminal_cmds` (exactly `dotnet build`, `git status`, `git diff --stat`, `git log --oneline -5`), `KEY ENTER`, `IDLE 4000 10000`, `GOTO terminal` + `READSCROLL`, `TYPE clear`, `KEY ENTER`, `TERMINAL close`, `KEY CTRL+1`.
  - `lookup`: `FOCUS chrome`, `MAYBE` a `SEARCH queries` with `READSCROLL`/`HOVER chrome_page` + `ENDSEARCH`, then `REPEAT 1 3` of `KEY CTRL+TAB` + `READSCROLL` + `HOVER chrome_page`. Finish with `KEY CTRL+1`, `FOCUS code`. No CLICK between `FOCUS chrome` and `FOCUS code`.
  - `think`: 5-40 s, made of small `HOVER`s, one-notch `SCROLL`s and `IDLE`s, with `MAYBE 5` → `IDLE 120000 230000`.
  - `reset_scratch`: `CALL open_scratch`, `KEY CTRL+A`, `KEY DELETE`, `CODE skeleton`, `KEY CTRL+S`, `LINES 0`.
  - `RESUME`: `ENDSEARCH`, `FOCUS code`, `KEY ESC`, `TERMINAL close`, `GOTO editor`, `CLICK`.
- **Content rules for `snippets.txt`:**
  - `types`: at least 20 small C# types, 6-25 lines each, using only implicit-using namespaces (`System`, `System.Linq`, `System.Collections.Generic`, `System.IO`, `System.Threading.Tasks`).
  - `comments`: at least 15 `//` lines. `identifiers`: at least 20 C# identifiers. `files`: `Helpers.cs`, `Program.cs`, `Scratch.cs`.
  - `queries`: about 30 lowercase developer queries of 60 characters or fewer, on C#, .NET, git and VS Code, with no URLs.
  - `find_terms`: at least 8 identifiers found in the workspace. `skeleton`: one snippet, `namespace Scratch;`.

- [ ] **Step 1: Write the failing tests in `tools/sim/test_content.c`, and register `check_script` as a CTest test**

```c
static char *dev, *snip; static size_t dl, sl;
static void load(uint64_t seed) { sim_reset(seed, 0); rhythm_init();
    if (!dev) { dev = sim_read_file(MACROS_DIR "/developer.txt", &dl); snip = sim_read_file(MACROS_DIR "/snippets.txt", &sl); }
    CHECK(dev && snip && script_load(dev, dl, snip, sl)); }
static void test_loads_clean(void) { load(1); CHECK(script_problems() == 0); }
static void test_every_snippet_typed_exactly(void) {
    load(1); int n = script_pool_count("types"); CHECK(n >= 20);
    for (int i = 0; i < n; i++) for (uint64_t s = 1; s <= 3; s++) {
        load(100 * i + s); rhythm_personality_t p = {280, 200, 100, 100}; rhythm_set_personality(&p);
        const char *t; size_t len; script_pool_item("types", i, &t, &len); CHECK(typing_snippet(t, len, NULL, NULL));
        size_t ne; const sim_event_t *e = sim_events(&ne); ed_reset(); for (size_t k = 0; k < ne; k++) ed_feed(&e[k]);
        char want[4096]; size_t w = 0; for (size_t k = 0; k < len; k++) if (t[k] != '\r') want[w++] = t[k]; want[w++] = '\n'; want[w] = 0;
        CHECK(strcmp(ed_text(), want) == 0); }
}
static void test_main_weights(void) { load(1); script_choice_t c[8]; CHECK(script_main_choices(c, 8) == 6); int sum = 0; for (int i = 0; i < 6; i++) sum += c[i].weight; CHECK(sum == 100); }
```

CTest entry: `add_test(NAME check_default_scripts COMMAND check_script ${FW}/macros/developer.txt ${FW}/macros/snippets.txt)`.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug; ctest --test-dir build-sim -C Debug -R "test_content|check_default_scripts"`
Expected: FAIL, because the script files do not exist yet.

- [ ] **Step 3: Implement `check_script.c`, then write `developer.txt` and `snippets.txt` to the rules above**

- [ ] **Step 4: Run to verify they pass**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure -R "test_content|check_default_scripts"`
Expected: PASS

- [ ] **Step 5: Commit**

```powershell
git add main/macros tools
git commit -m "feat(content): developer activities, C# snippet pools and script checker"
```

---

### Task 13: Long-run simulator and display matrix

**Files:**
- Create: `tools/sim/invariants.h`, `tools/sim/invariants.c`, `tools/sim/test_invariants.c`, `tools/sim/sim_main.c`, `tools/sim/display_matrix.c`
- Modify: `tools/CMakeLists.txt` (add `sim/invariants.c` to `core`), `tools/sim/CMakeLists.txt`

**Interfaces:**
- Consumes: everything above.
- Produces:
  ```c
  typedef struct { uint16_t w, h; rect_t safe; rect_t regions[8]; int nregions; uint32_t max_gap_ms, searchgap_ms; } inv_config_t;
  void inv_init(const inv_config_t *cfg);
  void inv_event(const sim_event_t *e);            /* sim sink */
  void inv_trace(const script_event_t *ev);        /* script trace */
  void inv_pause(uint32_t from_ms, uint32_t to_ms);
  int inv_finish(FILE *report);                    /* prints stats; returns violation count */
  ```
- **Host model:** state changes on key-down transitions only.
  - (ALT, TAB) toggles the foreground app.
  - (CTRL, GRAVE) toggles the terminal while VS Code is in front.
  - (CTRL, T) while Chrome is in front adds a tab and records the search time; (CTRL, W) removes one.
- **Violations**, each printed with its time:
  1. A gap between reports exceeds `max_gap_ms` (240000), excluding paused time.
  2. A report lies outside `safe`, or a button press lies outside every region.
  3. A key or modifier is held longer than 1000 ms, or anything is still held at a depth-1 TRACE_PICK or TRACE_PICK_END.
  4. At a depth-1 TRACE_PICK or TRACE_PICK_END, the foreground is not VS Code, the terminal is open, or search tabs are not 0.
  5. A button is pressed while Chrome is in front.
  6. Two searches are closer than `searchgap_ms`.
  7. The same depth-1 pick, or the same snippet index of the same pool, occurs twice in a row.
  8. With pauses: something is still held 100 ms after a pause starts, or any press occurs during a pause.
- **Stats** (printed, not asserted):
  - Activity mix in % against the weights.
  - Typing cpm while CODE runs, Backspaces per 100 characters, and a histogram of inter-key gaps (<100, <250, <500, <1000, <3000, ≥3000 ms).
  - Ctrl+S interval min/median/max.
  - Longest gap.
- `sim_main --hours H --seed S [--pauses N] [--check-mix]`:
  - Loads both shipped files, sets the sink and the trace, runs `script_run` until H virtual hours have passed, and exits with the violation count.
  - `--pauses N` schedules N pauses of 2-20 s at random times.
  - `--check-mix` also fails when any activity is more than 10 pp from its weight.
- **`display_matrix`:** for each screen in 1366×768@100, 1920×1080@100 and @125, 2560×1440@100 and @150, 3440×1440@100, 3840×2160@150 and @200:
  1. Resolve the four shipped regions.
  2. Check each is inside its layout element with a margin of at least 20 px, using the table below with every value × s = scale/100.
  3. Run 200 `pointer_goto` and 100 `pointer_nudge` calls, and check that every pair of consecutive reports with equal buttons and wheel differs by at least 1 px.

| Element | x0 | y0 | x1 | y1 |
|---|---|---|---|---|
| editor text | 412s | 92s | w − 120s | h − 70s |
| explorer tree | 48s | 92s | 348s | h − 70s |
| terminal text | 368s | h − 335s | w − 20s | h − 70s |
| Chrome page | 0 | 85s | w − 15s | h − 48s |

- [ ] **Step 1: Write the failing tests in `test_invariants.c`**

Feed one synthetic stream per violation, and check `inv_finish(NULL) == 1` for each. Also check that a clean stream gives 0.

```c
static inv_config_t cfg(void) { inv_config_t c = {1920, 1080, {40, 110, 1880, 1010}, {{440, 115, 1780, 990}}, 1, 240000, 360000}; return c; }
static void key(uint32_t t, uint8_t m, uint8_t k) { sim_event_t e = {t, 'K', m, k, 0, 0, 0, 0}; inv_event(&e); }
static void mouse(uint32_t t, uint8_t b, int32_t x, int32_t y) { sim_event_t e = {t, 'M', 0, 0, b, pointer_px_to_units(x, 1920), pointer_px_to_units(y, 1080), 0}; inv_event(&e); }
static void test_clean(void) { inv_config_t c = cfg(); inv_init(&c); mouse(0, 0, 500, 500); key(100, 0, 4); key(180, 0, 0); CHECK(inv_finish(NULL) == 0); }
static void test_gap(void) { inv_config_t c = cfg(); inv_init(&c); key(0, 0, 4); key(80, 0, 0); key(240081, 0, 4); key(240150, 0, 0); CHECK(inv_finish(NULL) == 1); }
static void test_outside_safe(void) { inv_config_t c = cfg(); inv_init(&c); mouse(0, 0, 10, 10); CHECK(inv_finish(NULL) == 1); }
static void test_stuck_key(void) { inv_config_t c = cfg(); inv_init(&c); key(0, 0, 4); key(1500, 0, 0); CHECK(inv_finish(NULL) == 1); }
static void test_click_in_chrome(void) { inv_config_t c = cfg(); inv_init(&c); key(0, KMOD_ALT, 0); key(100, KMOD_ALT, KEY_TAB); key(180, KMOD_ALT, 0); key(300, 0, 0);
    mouse(400, 1, 500, 500); mouse(480, 0, 500, 500); CHECK(inv_finish(NULL) == 1); }
static void test_search_spacing(void) { /* Alt+Tab into chrome, Ctrl+T at t=1000 and t=200000 */ }
static void test_pick_state(void) { /* depth-1 TRACE_PICK_END while terminal open -> 1 */ }
static void test_repeat(void) { /* two depth-1 TRACE_PICK "write_code" in a row -> 1 */ }
static void test_pause_release(void) { /* inv_pause(1000, 5000); key held from 900 to 1200 -> 1 */ }
```

Write the bodies of the last four tests in the same style as the others.

- [ ] **Step 2: Run to verify they fail**

Run: `cmake -S tools -B build-sim; cmake --build build-sim --config Debug`
Expected: the build fails because `invariants.h` is not found.

- [ ] **Step 3: Implement `invariants.c`, `sim_main.c` and `display_matrix.c`, and register these CTest tests**

```cmake
add_test(NAME sim_24h      COMMAND sim_main --hours 24 --seed 1)
add_test(NAME sim_mix_8h   COMMAND sim_main --hours 8 --seed 2 --check-mix)
add_test(NAME sim_pauses   COMMAND sim_main --hours 2 --seed 3 --pauses 20)
add_test(NAME display_matrix COMMAND display_matrix)
```

- [ ] **Step 4: Run all host tests**

Run: `ctest --test-dir build-sim -C Debug --output-on-failure`
Expected: `100% tests passed`. Then run `build-sim\sim\Debug\sim_main.exe --hours 8 --seed 2` and read the stats: cpm should be 200-320 and the mix within ±10 pp. When a simulator test fails, fix the module or the script content, not the invariant.

- [ ] **Step 5: Commit**

```powershell
git add tools
git commit -m "feat(sim): long-run invariant checker, pause test and display matrix"
```

---

### Task 14: Firmware integration

**Files:**
- Create: `main/hid_out_usb.c`
- Modify: `main/main.c`, `main/CMakeLists.txt`, `CLAUDE.md`
- Delete: `main/human.c`, `main/human.h`, `main/macro.c`, `main/macro.h`, `main/macros/default.txt`

**Interfaces:**
- Consumes: `hid_out.h`, `script_load`, `script_run`, `rhythm_init`.
- Produces (`main/hid_out_usb.c`):
  - `volatile bool hid_out_usb_paused`, written by `main.c`.
  - Before every report, wait for `tud_mounted() && tud_hid_ready()`, the same 500-tick wait as the existing `human_wait_ready`.
  - Keys go out via `tud_hid_keyboard_report(1, …)`. The mouse goes out via `tud_hid_report(2, &r, 6)` with `typedef struct __attribute__((packed)) { uint8_t buttons; uint16_t x, y; int8_t wheel; } abs_mouse_report_t;` and `_Static_assert(sizeof(abs_mouse_report_t) == 6, "")`.
  - The clock is `esp_timer_get_time() / 1000`. Sleep is `vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1)`, the existing idiom. The RNG is `esp_random()`. The log uses `vprintf` with a `hid_sim: ` prefix and a newline.
  - The backend tracks the last x, y for `hid_out_release_all`.

- [ ] **Step 1: Update `main/main.c`**
  - Report descriptor: keep the keyboard as RID 1. Define `HID_REPORT_DESC_ABS_MOUSE3(...)` by copying `TUD_HID_REPORT_DESC_ABSMOUSE` (`managed_components/espressif__tinyusb/src/class/hid/hid_device.h:303-349`). Change the button usage max and report count from 5 to 3, change the padding from 3 to 5 bits, and drop the AC Pan block. Use it with `HID_REPORT_ID(2)`.
  - Change the `TUD_HID_DESCRIPTOR` poll interval from 10 to 5.
  - The BOOT loop toggles `hid_out_usb_paused` and logs `PAUSED` or `RUNNING`.
  - `macro_task`: wait for `tud_mounted()`, call `rhythm_init()`, call `script_load` with `_binary_developer_txt_start` and `_binary_snippets_txt_start` (lengths from `strlen`). If loading fails, log `script refused` and stop. Otherwise call `script_run(NULL)`. The task stack goes from 4096 to 8192.

- [ ] **Step 2: Update `main/CMakeLists.txt`**

`SRCS "main.c" "hid_out_usb.c" "keys.c" "rhythm.c" "pointer.c" "typing.c" "script.c"`, `PRIV_REQUIRES esp_driver_gpio esp_hw_support esp_timer`, `EMBED_TXTFILES "macros/developer.txt" "macros/snippets.txt"`. Delete the five obsolete files.

- [ ] **Step 3: Build the firmware**

Run: `. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1; idf.py build`
Expected: `Project build complete.` with no warnings from `main/`.

- [ ] **Step 4: Re-run the host tests**, to confirm the portable modules are unchanged

Run (in a shell without the IDF profile): `cmake --build build-sim --config Debug; ctest --test-dir build-sim -C Debug --output-on-failure`
Expected: `100% tests passed`

- [ ] **Step 5: Update `CLAUDE.md`**

Rewrite the Architecture section for the new modules and the `hid_out` split. Add the host test commands from "Before you start", including the 260-character path note. Replace the relative-mouse gotcha with: the absolute mouse maps to the primary monitor, and `SCREEN` must match the display. Remove the "Planned redesign" section.

- [ ] **Step 6: Commit**

```powershell
git add -A main CLAUDE.md
git commit -m "feat(fw): absolute mouse descriptor, USB backend, run developer script"
```

---

### Task 15: Scratch workspace, setup guide and hardware acceptance

**Files:**
- Create: `scratch-workspace/Scratch.cs`, `scratch-workspace/Helpers.cs`, `scratch-workspace/Program.cs`, `scratch-workspace/Scratch.csproj`, `scratch-workspace/.vscode/settings.json`, `scratch-workspace/.gitignore`, `tools/check_script/compile_snippets.ps1`, `docs/setup.md`

**Interfaces:**
- Consumes: `check_script --emit-cs` (Task 12), plus the pool contents (`find_terms` must name identifiers that exist in `Helpers.cs` and `Program.cs`).
- Produces:
  - `Scratch.csproj`: `net10.0`, Exe, `ImplicitUsings` enabled, `Nullable` enabled. `Scratch.cs` starts as the `skeleton` snippet followed by an empty line.
  - `.vscode/settings.json`: exactly the four settings in spec section 11. `.gitignore`: `bin/` and `obj/`.
  - `compile_snippets.ps1`: copies `scratch-workspace` to a new temp folder, runs `check_script --emit-cs <temp>\Snippets.g.cs`, runs `dotnet build`, and exits with dotnet's exit code.
  - `docs/setup.md`: spec section 11 as numbered steps. It covers copying `scratch-workspace` outside the repo, then `git init`, `git add -A` and `git commit -m init` inside it. It includes the virtual desktop and Alt+Tab setting, the Chrome profile setup, the `SCREEN` and `calibrate` procedure, and Focus Assist. It lists six documentation tabs:
    - `https://learn.microsoft.com/en-us/dotnet/csharp/`
    - `https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/`
    - `https://learn.microsoft.com/en-us/dotnet/api/system.collections.generic.list-1`
    - `https://learn.microsoft.com/en-us/dotnet/csharp/asynchronous-programming/`
    - `https://git-scm.com/docs/git-log`
    - `https://git-scm.com/book/en/v2`

    Open each URL once to confirm it loads.

- [ ] **Step 1: Write the workspace files, then verify the workspace builds**

Run: `dotnet build scratch-workspace`
Expected: `Build succeeded.`

- [ ] **Step 2: Verify every `types` snippet compiles**

Run: `pwsh tools/check_script/compile_snippets.ps1`
Expected: `Build succeeded.` with exit code 0. Fix the snippets in `snippets.txt` if it fails, then re-run Task 12's tests.

- [ ] **Step 3: Write `docs/setup.md`**

- [ ] **Step 4: Commit**

```powershell
git add scratch-workspace tools/check_script/compile_snippets.ps1 docs/setup.md
git commit -m "docs: scratch workspace, snippet compile check and setup guide"
```

- [ ] **Step 5: Hardware acceptance (manual, by the user)**

  1. Flash with `idf.py -p COMx flash monitor`.
  2. Uncomment `CALL calibrate` once, then check each outlined region against the real screen and adjust the offsets.
  3. Watch for 10 minutes, per spec success criterion 4. Look for stuck keys, doubled brackets, the pointer outside the editor, explorer, terminal or Chrome page areas, and Alt+Tab landing on the wrong app.
  4. Press BOOT mid-typing and mid-move. Keys and buttons should release at once, and resume should return to VS Code.
  5. If Windows still treats the device as a relative mouse, uninstall it in Device Manager and replug it.
