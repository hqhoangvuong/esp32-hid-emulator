# Setting up the developer simulator

The device types blind and cannot see the screen. It assumes the exact desktop described here, so set it up once before leaving it running. Background and reasons: `docs/superpowers/specs/2026-10-25-developer-simulator-design.md`, sections 10 and 11.

## 1. Scratch workspace

1. Copy `scratch-workspace/` from this repo to a folder **outside** the repo, for example `%USERPROFILE%\scratch-workspace`. The macro runs `git status` and `git log` there, and it should not see this firmware repo.
2. In that copy, make it a git repository with one commit so `git log` has something to show:

   ```powershell
   cd $env:USERPROFILE\scratch-workspace
   git init
   git add -A
   git commit -m init
   dotnet build
   ```

3. Open the folder in VS Code (File > Open Folder) with the C# extension installed. Its `.vscode/settings.json` turns off accept-on-Enter and accept-on-commit-character, turns on format-on-save and turns off auto-save. Do not change them: the typing relies on them.
4. Leave the default layout: Explorer visible in the sidebar, terminal panel closed, VS Code zoom at its default. Open `Scratch.cs` and click in the editor.

## 2. Windows desktop

1. Set the primary monitor's resolution and scaling (Settings > System > Display), then put the same values in the `SCREEN` line of `main/macros/developer.txt`. For example 1920×1080 at 100% is `SCREEN 1920 1080 100`. If VS Code's zoom is not the default, multiply it in.
2. Create a dedicated virtual desktop with Win+Ctrl+D. Keep exactly two windows on it, both maximised on the primary monitor: VS Code and Chrome.
3. Settings > System > Multitasking > Desktops: set "On the taskbar, show all the open windows" and "Show all open windows when I press Alt+Tab" to **Only on the desktop I'm using**. The macro switches apps with Alt+Tab and must never land on another desktop.
4. Keep the taskbar visible and not auto-hidden. The default regions leave room for it.
5. Before leaving it running, turn on Focus Assist / Do not disturb, close chat apps and pause Windows Update. A third window, such as a dialog, a notification or a download bubble, can steal the blind typing.

## 3. Chrome

1. Use a dedicated profile, signed out or throwaway, with no extensions that add UI.
2. Block notifications, hide the bookmarks bar, keep zoom at 100% and dismiss any consent banner beforehand.
3. Open these documentation tabs, in this order, as the first tabs. Open each once to check it loads, and leave the first tab active.
   1. https://learn.microsoft.com/en-us/dotnet/csharp/
   2. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/
   3. https://learn.microsoft.com/en-us/dotnet/api/system.collections.generic.list-1
   4. https://learn.microsoft.com/en-us/dotnet/csharp/asynchronous-programming/
   5. https://git-scm.com/docs/git-log
   6. https://git-scm.com/book/en/v2

The macro only types into Chrome's address bar, after Ctrl+T. It never clicks while Chrome is in front.

## 4. Calibrate the regions

The pointer targets are rectangles defined by the `REGION` lines in `developer.txt`. They are written in pixels at 100% scale, measured from a screen edge.

1. Uncomment `# CALL calibrate` at the top of `DEF MAIN`, rebuild and flash.
2. Watch the pointer trace each region: editor, explorer, terminal (where the panel would be), Chrome page. Each should sit inside its element with a margin, clear of the line numbers, minimap, status bar, tab strips and taskbar.
3. Adjust the offsets if needed, comment `CALL calibrate` out again, then rebuild and flash.

Run `build-sim\check_script\Debug\check_script.exe main\macros\developer.txt main\macros\snippets.txt` after any script edit. The device only reports script problems on the serial monitor.

## 5. Start state and running

Start state: VS Code in front with `Scratch.cs` focused, the terminal panel closed and the Explorer visible, and Chrome behind it on its first documentation tab. There should be no other windows on the desktop.

Plug in the board. After USB enumerates, the device waits 10–15 s, moves the pointer once into the editor, then starts. Press **BOOT** to pause: every key and button is released at once. Before pressing BOOT again to resume, leave the same app in front that was in front when you paused. On resume the macro closes any search tab, returns to VS Code, closes the terminal and clicks into the editor.

**Press BOOT before locking the PC (Win+L).** Locking sends nothing over USB, so the device would keep typing into the sign-in screen, and every Enter would count as a failed sign-in. If the PC goes to sleep or the cable is pulled, the device notices and pauses by itself; press BOOT to resume once you are back at the unlocked desktop.

Turn off Windows' "screen saver" and "lock after sleep" for the same reason, or press BOOT before walking away.

If Windows keeps treating the board as a relative mouse after flashing this firmware, uninstall the device in Device Manager and plug it in again.
