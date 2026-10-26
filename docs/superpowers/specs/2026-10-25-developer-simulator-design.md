# Developer Simulator: design

Date: 2026-10-25, updated 2026-10-26 (Chrome activity added). Status: awaiting user review.

## 1. Goal

Replace the current linear VS Code macro with a behaviour engine that looks like a C# developer working in VS Code, mixing mouse and keyboard, and that can run unattended for hours without repeating itself visibly or leaving the editor in a broken state.

Target: Waveshare ESP32-S2 (no LCD), ESP-IDF v6.1, TinyUSB HID. Host: Windows, on a dedicated virtual desktop that holds exactly two maximised windows on the primary monitor: VS Code (default layout) and Chrome (dedicated profile). Installed extensions include AI completion (Copilot-style) and auto-format/linters.

### Decisions already made with the user

| Topic | Decision |
|---|---|
| Use | Long unattended runs (hours) |
| Mouse | Absolute pointer, replaces the relative mouse |
| Layout | Maximised, default VS Code layout |
| Typing target | Saved scratch file in a throwaway workspace |
| Language | C# |
| Activities | Typing code, terminal commands, code navigation, and looking things up in Chrome (address-bar search plus reading pre-opened documentation tabs). No debug cycle |
| Environment | A dedicated Windows virtual desktop with exactly two maximised windows: VS Code (scratch workspace) and Chrome (dedicated profile). Alt+Tab is set to show only the current desktop |
| Web clicks | None. Chrome gets keyboard input and pointer hover/scroll only; no click is ever issued while Chrome is in front |
| Longest input gap | Up to about 4 minutes |
| Display | 1920x1080 at 100%, one monitor (so `SCREEN 1920 1080 100`, the default). Resolution and effective UI scale are still entered through the `SCREEN` command so other setups work; VS Code runs on the primary monitor |
| Architecture | Hybrid: C primitives plus a text script |

### Success criteria

1. In the PC-side simulator, 24 simulated hours show: no input gap longer than 240 s, the pointer never outside a whitelisted region, every key press released, terminal open/close balanced, every activity ending with VS Code in front, no click while Chrome is in front, searches spaced by at least 6 minutes.
2. Over 8 simulated hours the activity mix is within 10 percentage points of the configured weights, and no activity or snippet repeats back to back.
3. Pressing BOOT releases all keys and buttons within 100 ms and resume returns VS Code to a known state.
4. On hardware, a 10-minute watch shows no stuck keys, doubled brackets, or pointer outside the editor, explorer, terminal or Chrome page areas, and Alt+Tab always lands on the intended app. Whether the pacing looks natural is the user's judgement, tuned by editing constants.

### Non-goals

Debug cycle, any window other than VS Code and Chrome, clicking web page content, typing into web pages, editing real project files, reading the screen, wireless control, any display support. The old relative-mouse `default.txt` is removed.

## 2. Architecture

```
main.c        USB setup, descriptors, BOOT button, task start
script.[ch]   parser + interpreter (DEF/CALL/CHOOSE/MAYBE/REPEAT/WHEN/CODE ...)
typing.[ch]   key timing, error model, snippet typing grammar
pointer.[ch]  absolute pointer state, regions, Fitts moves, tremor, click/drag/scroll
rhythm.[ch]   per-session personality, fatigue drift, delay primitive with idle-gap guard
hid_out.h     output abstraction: key report, mouse report, now_ms, sleep_ms, rand32, log
hid_out_usb.c ESP/TinyUSB implementation
hid_out_sim.c host implementation (virtual clock, seeded PRNG, event log, assertions)
```

Dependencies point downward: `script` uses `typing`, `pointer` and `rhythm`; those use only `hid_out.h`. No module other than `hid_out_usb.c` includes TinyUSB or FreeRTOS headers, so the same code compiles for the PC simulator.

Embedded text files: `main/macros/developer.txt` (regions, activities, schedule) and `main/macros/snippets.txt` (code and comment pools). Both go through `EMBED_TXTFILES`. At boot the script module scans them once and builds name → position tables for `DEF`, `POOL` and `REGION`; nothing is copied to RAM.

The interpreter keeps four pieces of state: the foreground app (`code` or `chrome`), whether the terminal panel is open, whether a Chrome search tab is open, and the typed-line estimate. Commands such as `FOCUS`, `TERMINAL` and `ENDSEARCH` act only when the tracked state differs from what they need, so they are safe to repeat. The state is tracked, not verified: the device cannot see the screen.

## 3. HID layer

The relative mouse collection is replaced by one absolute mouse collection, report ID 2:

- Buttons 1-3 (3 bits) plus 5 padding bits.
- X and Y, 16 bit each, logical 0..32767, absolute.
- Wheel, 8 bit, logical -127..127, relative.

The report is 6 bytes after the ID. Windows maps absolute coordinates onto the primary monitor, so one descriptor serves any resolution. The keyboard collection (report ID 1) is unchanged.

The firmware tracks the pointer position itself, so there is no drift. At boot the host cursor position is unknown, so the first pointer action is a jump to a point inside the editor region. This is the only teleport.

Absolute coordinates are normalised, so the pointer lands at the same fraction of the screen at any resolution or Windows display scaling. Two consequences follow:

- **Units per pixel vary.** 32767 units span the whole screen width: about 17 units per pixel at 1920 px, about 8.5 at 3840 px. A move smaller than one pixel is a no-op for the host. All motion is therefore specified in pixels and converted using the `SCREEN` setting (section 7); the firmware never emits a pointer step that rounds to less than 1 px.
- **Primary monitor only (assumed).** Microsoft's documentation says normalised absolute coordinates map to the primary monitor unless a virtual-desktop flag is set (MOUSEINPUT and RAWMOUSE remarks). Whether the in-box HID mouse driver sets that flag for this device is not documented, so VS Code must be on the primary monitor, and any multi-monitor setup has to be checked on hardware.

## 4. Regions

VS Code's chrome (activity bar, sidebar, tab strip, status bar) has a fixed size in pixels times the UI scale, so regions written as screen fractions only fit one resolution. Instead, `SCREEN width height scale%` (section 7) tells the firmware the primary monitor's resolution and the effective UI scale, and `REGION name x0 y0 x1 y1` defines each rectangle in pixels at 100% scale: `+N` is N px from the left/top edge, `-N` is N px from the right/bottom edge. The firmware multiplies offsets by the scale and converts to pointer units. "Effective UI scale" is the Windows display scaling multiplied by the VS Code zoom factor; keep the VS Code zoom at its default or include it.

Starting values for a maximised default VS Code (approximate chrome sizes: 48 px activity bar, about 300 px sidebar, about 35 px tab strip, 22 px status bar), each inside the intended element with a margin of at least 20 px:

| Region | `x0 y0 x1 y1` | Used when |
|---|---|---|
| editor | `+390 +110 -140 -60` | panel closed (the right margin keeps clear of the minimap) |
| explorer | `+60 +110 +280 +260` | explorer visible |
| terminal | `+390 -250 -60 -60` | panel open (anchored from the bottom, so independent of screen height) |
| chrome_page | `+40 +130 -40 -50` | Chrome in front: hover and scroll only |

Chrome (maximised, bookmarks bar hidden, zoom 100%) has roughly 85 px of tab strip and toolbar above the page, so `chrome_page` stays clear of them and of the scrollbar. Only hover and scroll happen there; no click is ever issued while Chrome is in front.

Every pointer target is clamped to a region. Nothing targets the title bar, window controls, status bar, taskbar or screen edges. `OUTLINE region` slowly traces a region's rectangle so it can be checked against the real screen after `SCREEN` is set; a commented-out `CALL calibrate` line in the script uses it. If the real chrome sizes differ from the approximations above, the region offsets are the only thing to adjust.

## 5. Mouse model

- Path: cubic Bezier with min-jerk easing, sent as absolute positions every 10 ms.
- Duration: Fitts-style, about 150 ms + 120 ms x log2(distance / target width + 1) with distance and width in real pixels (from `SCREEN`), scaled by the session's mouse-speed factor and randomised by roughly 15%.
- About 25% of long moves overshoot by 3-10% and then correct with a short second move.
- Tremor: constant noise of 1-2 px around the path and while resting. Never less than 1 px, because a smaller step would not change the host's pointer position at all.
- Hand-off pauses: 250-900 ms after typing ends before the pointer moves; 200-700 ms after a click before typing.
- Actions: `GOTO`, `HOVER` (dwell long enough for a tooltip), `CLICK`, `DBLCLICK`, `DRAGSELECT`, `SCROLL`, `READSCROLL`. Scrolling is bursts of notches with decaying intervals, occasionally reversing; `READSCROLL` moves the pointer down the text between bursts as if following lines (line pitch about 19 px x scale at VS Code's default font size).

## 6. Typing model

### Timing
A session base speed of about 200-320 characters per minute is drawn at boot and modulated by letter pair (same-finger pairs slower, alternating hands faster), shifted/number-row symbols (slower), and bursts of 4-12 characters followed by micro-pauses. Longer pauses follow `;`, `{` and Enter. A planning pause of 3-15 s precedes each new type. All values are initial and are tuned in the simulator and on hardware.

### Mistakes
Neighbour-key hit, transposition, omission and doubled letter, at roughly 1.5-4% per letter, higher when fast. Detection delay varies: immediate, end of word, or end of line. Short slips are fixed with Backspace. Slips noticed at end of line are fixed by arrow-key navigation, correction, and return with End.

### Snippet grammar (for VS Code auto-close and auto-indent)
Snippet lines carry no leading whitespace. Rules the typer applies, and the checker enforces:

1. A line ending in `{` is typed, then Enter. VS Code puts the auto-closed `}` on its own line below the caret.
2. A line that is exactly `}` or `};` is not typed. The typer presses Down then End (and types `;` for `};`), then Enter.
3. Any other line has balanced `()`, `[]` and quotes and contains no unbalanced braces. Inline `{ ... }` pairs are typed through; VS Code overtypes the auto-inserted closer.
4. `///` is forbidden (it triggers a doc-comment template).
5. Tab is never pressed in the editor.

### Editing habits
Ctrl+S every 1-4 minutes (format-on-save reflows the code); occasional Ctrl+Z, Ctrl+/, Alt+Up/Down, Shift+Alt+Down, Ctrl+Shift+K, copy/paste, F2 rename and Shift+Alt+F.

## 7. Script language

Existing commands stay: `KEY`, `TYPE`, `CLICK`, `SCROLL`, `WAIT`, `IDLE min max`, `LOOP`. `MOVE dx dy` is removed.

| Command | Meaning |
|---|---|
| `SCREEN width height scale` | primary monitor resolution in px and effective UI scale in percent, for example `SCREEN 1920 1080 100`. If absent the default is 1920 1080 100 and a warning is logged |
| `REGION name x0 y0 x1 y1` | named rectangle; each value is `+N` (px from the left/top edge) or `-N` (px from the right/bottom edge), at 100% scale |
| `MAXGAP seconds` | idle-gap limit, default 210 |
| `DEF name` ... `END` / `CALL name` | activity or subroutine |
| `CHOOSE` ... `END` | body lines are `weight name`; weighted pick, never the previous pick |
| `MAYBE pct` ... `END` | run block with that probability |
| `REPEAT min max` ... `END` | repeat block a random number of times |
| `WHEN LINES > n` ... `END` | run block if the typed-line estimate exceeds n |
| `POOL name` ... `END` | snippet pool, snippets separated by a line `---`; `POOL name LINES` makes every line one item (used for the search queries, about 30 one-line developer queries on C#, .NET, git and VS Code topics) |
| `CODE pool` | type a random snippet (no immediate repeat) with the grammar above |
| `GOTO region` | move to a random point in the region |
| `HOVER region ms` | move there and dwell |
| `DBLCLICK`, `DRAGSELECT region`, `READSCROLL n` | mouse actions |
| `TERMINAL open` / `TERMINAL close` | sends Ctrl+` only if the engine's panel state differs |
| `FOCUS code` / `FOCUS chrome` | bring that app to the front by holding Alt and tapping Tab, only if the tracked foreground differs |
| `SEARCH pool` | in Chrome: Ctrl+T, then type a random one-line query in the address bar and press Enter (sometimes pick an autocomplete suggestion with Down+Enter instead). Skipped as a no-op if less than `SEARCHGAP` seconds have passed since the previous search |
| `ENDSEARCH` | if a search tab is open: `FOCUS chrome` if needed, then Ctrl+W; otherwise a no-op |
| `SEARCHGAP seconds` | minimum spacing between searches, default 360 (at most about 10 an hour) |
| `OUTLINE region` | trace a region's rectangle |

Interpreter limits: call depth 8, block depth 8. At boot a validation pass logs problems (unbalanced `END`, unknown `CALL` target, missing `MAIN`) and refuses to run if `MAIN` is missing.

## 8. Scheduler and activities

`MAIN` is `CHOOSE` over the activities, wrapped in `LOOP`. Initial weights: write code 40, edit/refactor 12, navigate and read 13, terminal check 8, look something up 15, think 12.

**Invariant:** every activity starts and ends with VS Code in front, the editor focused, the panel closed, no popups open, and Chrome showing its first tab. Each begins with Esc and ends by clicking in the editor region, so any order is safe.

1. **Write code.** Ctrl+End, planning pause, `CODE types` (a whole small type appended at end of file, so the file structure cannot break). Midway, sometimes: scroll up, hover a symbol, return. Then Ctrl+S, the formatter reflows, the pointer reads along.
2. **Edit/refactor.** Double-click a word and type a replacement; or click a line, Home, Shift+End, then duplicate or toggle comment; or drag-select lines, copy, click elsewhere, paste; sometimes F2 rename or format. Ends with Ctrl+S.
3. **Navigate and read.** Ctrl+P, a few letters, Enter; `READSCROLL`; hover a method, click, F12, read, Alt+Left; sometimes Ctrl+Shift+F with a term and stepping through results then Esc; occasionally click an explorer row and return via Ctrl+P.
4. **Terminal check.** `TERMINAL open`, one of `dotnet build`, `git status`, `git diff --stat`, `git log --oneline -5`, wait for output, move into the terminal region and scroll/read, `clear`, `TERMINAL close`, Ctrl+1, click the editor.
5. **Think.** 5-40 s with small drift, a hover or a one-notch scroll; rarely a long lean-back.
6. **Look something up (Chrome).** `FOCUS chrome`, then `SEARCH queries` (a developer query in the address bar), `READSCROLL` and `HOVER` over the results inside `chrome_page`, `ENDSEARCH`; or, instead of or after a search, cycle the pre-opened documentation tabs with Ctrl+Tab, scrolling and reading each. When `SEARCH` is skipped by the spacing rule, the same reading steps run on the current documentation tab. The activity ends with Ctrl+1 (first tab), `FOCUS code`, and a click in the editor region. Typing in Chrome happens only in the address bar; nothing is ever clicked.

**Housekeeping.** `CODE` increments a typed-line estimate. A `WHEN LINES > 120` check at the top of `MAIN` runs `reset_scratch`: select all, delete, type a skeleton, save, and set the estimate to 0. This bounds the file size and any error accumulation.

`RESUME` is a script routine run after BOOT resumes the macro: `ENDSEARCH` (closes a search tab left open), `FOCUS code`, Esc, `TERMINAL close`, click the editor. It relies on the user having left the same app in front that was in front when paused.

## 9. Rhythm and the idle-gap guard

All delays go through one primitive in `rhythm`. It slices waits into short steps (so BOOT pause is responsive) and tracks the time since the last HID report. When that time reaches a random point between 90 s and `MAXGAP`, it inserts a sign of life: a 6-20 px pointer drift, a hover, or a one-notch scroll inside the foreground app's region (`editor` or `chrome_page`). The effective rule: no gap between HID reports exceeds `MAXGAP`.

At boot the RNG draws a personality: typing speed, error rate, mouse speed, pause habits. A slow bounded random walk of about +/-15% over tens of minutes scales speed and pause length (fatigue).

## 10. Controls and safety

- BOOT toggles pause/resume. Pausing releases all keys and buttons immediately, including a drag in progress. Resume runs `RESUME` and then continues at the next `CHOOSE`.
- On boot the firmware waits for USB mount, then 10-15 s so the user can focus VS Code.
- Typed text, keys and terminal commands come only from the two script files.
- **Known limitation:** typing is blind and focus is tracked, not verified. The dedicated desktop makes it safer: with exactly two windows, a stolen focus lands in VS Code or Chrome, and `FOCUS` (Alt+Tab) can only toggle between them. A third window (a dialog, a Chrome download bubble, a popped-out DevTools) can break that. Recommended before leaving it running: Windows Focus Assist on, chat apps closed, updates paused.
- Typing in Chrome happens only in the address bar, after Ctrl+T; never into page content. No click is ever issued while Chrome is in front.
- A virtual desktop is organisation, not concealment: tools that capture the screen or window titles still see it.

## 11. Environment and scratch workspace

`scratch-workspace/` is shipped in the repo and opened in VS Code by the user:

- `Scratch.cs` (types are appended here), `Helpers.cs`, `Program.cs`, a `.csproj` so `dotnet build` works.
- `.vscode/settings.json`: `editor.acceptSuggestionOnEnter: "off"`, `editor.acceptSuggestionOnCommitCharacter: false`, `editor.formatOnSave: true`, `files.autoSave: "off"`.
- The user runs `git init` inside it so the git terminal commands work.
- Start state for the macro: that workspace open, `Scratch.cs` focused, terminal panel closed, explorer visible.

### Windows and Chrome setup

- Create the desktop with Win+Ctrl+D and keep only VS Code and Chrome on it, both maximised on the primary monitor.
- In Settings > System > Multitasking > Desktops, set the Alt+Tab option to show only windows on the desktop in use, so Alt+Tab can never jump to another desktop.
- Chrome: a dedicated profile (signed out or throwaway), notifications blocked, bookmarks bar hidden, zoom 100%, any consent banner dismissed beforehand, no extensions that add UI. Pre-open 5-6 documentation pages from an allowlist (Microsoft Learn C# and .NET docs, git-scm docs; the exact list is shipped in the repo) as the first tabs, with the first tab active.
- Start state for the macro: VS Code in front, Chrome behind it showing the first documentation tab, no extra windows.

## 12. Verification

**PC-side simulator** (`tools/sim`): builds `script`, `typing`, `pointer`, `rhythm` with `hid_out_sim.c`, runs a virtual clock with a seeded PRNG for N simulated hours, and asserts: max report gap <= 240 s; pointer inside a region at every report; every key press released; terminal and foreground-app state balanced; no click while Chrome is in front; every `SEARCH` followed by `ENDSEARCH`; searches spaced by at least `SEARCHGAP`; no back-to-back repeats; activity mix near configured weights. It also runs a display matrix (1366x768 @100%, 1920x1080 @100% and 125%, 2560x1440 @100% and 150%, 3440x1440 ultrawide, 3840x2160 @150% and 200%) and asserts that every region stays inside the expected VS Code element with a margin of at least 20 px and that no pointer step or nudge is under 1 px. It prints typing statistics (characters per minute, error counts, pause histogram). Whether a host C compiler is available on this machine is not yet known and is the first thing to check when planning. If none is available the simulator is the first problem to solve.

**Script checker** (`tools/check_script`): validates both script files and the snippet grammar in section 6 (balanced delimiters, no leading whitespace, no `///`, only the allowed brace forms). If the .NET SDK is installed it can also compile each snippet inside `scratch-workspace/`.

**Hardware acceptance:** the 10-minute observation in success criterion 4, then tuning.

`idf.py build` clean remains the baseline check.

## 13. Risks

- Region offsets assume VS Code's default chrome sizes. If the sidebar or panel was resized, or the VS Code zoom is not default, the regions must be adjusted (`OUTLINE` exists for this).
- Absolute coordinates are documented to map to the primary monitor unless a virtual-desktop flag is set. VS Code must be on the primary monitor; multi-monitor behaviour is unverified on hardware.
- The device cannot read the display, so a wrong `SCREEN` value silently misplaces pointer targets and mis-sizes pixel-based motion.
- Copilot and other extensions can show popups or ghost text; the script avoids Tab and Enter-accept, but unusual extension behaviour is possible.
- Snippet grammar depends on VS Code auto-close behaviour for C#; the hardware acceptance step is the real test of it.
- ESP32-S2 has no FPU; float maths is software emulated but is only used at 10 ms steps.
- `FOCUS` assumes exactly two windows on the desktop. A third window, or the user switching windows while the macro is paused, desynchronises the tracked foreground.
- Google may show an "unusual traffic" check for automated-looking queries. The device cannot see or solve it; the spacing rule keeps the rate low, and the next `SEARCH` simply opens a new tab.
- Google's results page layout changes often. Because nothing in Chrome is ever clicked, a layout change can make the reading look less natural but cannot cause a mis-click.
