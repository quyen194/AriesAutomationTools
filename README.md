# AriesAutomationTools

A portable, cross-platform (Windows / Linux / macOS) desktop automation tool.
Define workflows of mouse, keyboard, and wait actions that run automatically on a timer, schedule, or pixel-color trigger. Useful for AFK tasks, game macros, UI testing, and repetitive desktop workflows.

---

## Features

- **Multiple workflows** — each with its own activity list, window target, trigger, and repeat settings
- **9 activity types** — mouse move, click, drag, scroll; key press, type string, wait, pixel check, run workflow
- **Window targeting** — run actions on the global screen or scoped to a specific window (by title, class, or spy-pick)
- **Smart detection** — auto-pauses when real user input is detected; resumes after a configurable idle period
- **Record mode** — global hook captures real mouse/keyboard actions, review and append to any workflow
- **3 start triggers** — Manual, Schedule (cron), or Pixel color watch
- **Global pause hotkey** — default F9, fully configurable
- **Portable single EXE** — no installer, statically linked, config stored next to the exe as `config.json`

---

## Build

### Requirements

| Tool | Minimum version |
|------|----------------|
| CMake | 3.25 |
| C++ compiler | MSVC 2019+ / GCC 11+ / Clang 14+ |
| Internet | Required on first build (CPM downloads SDL2, ImGui, nlohmann/json) |

### Windows (Visual Studio)

```bat
git clone <repo-url> AriesAutomationTools
cd AriesAutomationTools
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Executable: `build\Release\AriesAutomationTools.exe`
Config file: `build\Release\config.json` (auto-copied on build)

### Linux

```bash
# Install X11 development libraries first
sudo apt install libx11-dev libxtst-dev   # Debian/Ubuntu
sudo dnf install libX11-devel libXtst-devel  # Fedora

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Executable: `build/AriesAutomationTools`

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

No extra dependencies needed — CoreGraphics, IOKit, Carbon are linked automatically.

---

## Usage Guide

### 1. Launching

Run the executable. A `config.json` is loaded from the same folder. If it does not exist, the app starts with an empty config.

### 2. Creating a Workflow

1. Click **`+`** in the left panel to create a new workflow.
2. Type a name in the name field at the top of the right panel.
3. Check **Enabled** so it can run.

### 3. Adding Activities

Click **`+ Add`** in the Activities section to open the activity editor modal.

| Activity type | What it does |
|---------------|-------------|
| `mouse_move`   | Move the cursor to X, Y |
| `mouse_click`  | Click a mouse button at X, Y |
| `mouse_drag`   | Click-and-drag from one point to another |
| `mouse_scroll` | Scroll the wheel at X, Y by a delta |
| `key_press`    | Press a key (with optional modifiers) |
| `type_string`  | Type a string of text character by character |
| `wait`         | Pause execution for a duration (with optional random range) |
| `pixel_check`  | Wait until a screen pixel matches a color, or skip/stop on timeout |
| `run_workflow` | Trigger another workflow by ID |

**Position mode**: `absolute` = screen coordinates. `relative` = coordinates within the target window's client area.

#### Picking a screen position

Instead of typing X/Y numbers manually, click **Pick position** (or **Pick start** / **Pick end** for drag). The modal closes and a floating tooltip appears tracking your cursor with live screen coordinates. Move the cursor to the target point and press **Enter** or click **Capture**. Press **Esc** to cancel. The modal reopens with the coordinates filled in.

#### Capturing a key press

For `key_press`, click **Capture key**. The modal shows "Press any key...". Press the desired key (hold Ctrl/Shift/Alt for modifiers). The key name and modifiers are filled in automatically. Press **Esc** to cancel.

#### Capturing scroll delta

For `mouse_scroll`, click **Capture scroll**. Scroll your mouse wheel to accumulate delta, then click **Done** or press **Enter**.

#### Key name reference

Common key name strings used in `key_press`:

```
Letters:     a  b  c  ...  z
Digits:      0  1  2  ...  9
Function:    f1  f2  ...  f12
Navigation:  left  right  up  down  home  end  prior  next  insert  delete
Special:     space  enter  tab  backspace  escape
Modifiers:   ctrl  shift  alt  lctrl  rctrl  lshift  rshift  lalt  ralt
Numpad:      numpad0 ... numpad9  numpad_enter
```

Modifiers field (space-separated): `ctrl shift alt`

### 4. Window Targeting

By default, activities run on the global screen. To scope actions to a specific window:

1. Open the **Window** dropdown in the workflow panel and choose **By Title** or **By Class**.
2. Click **Pick Window** — the cursor becomes a crosshair. Hover over the target window; a tooltip shows its title, class, and handle. Left-click to confirm.
3. Mouse coordinates set to `relative` will now be measured from the top-left of that window's client area.

### 5. Recording

Recording captures real mouse and keyboard actions and converts them into activities.

1. Click **`[Rec]`** in the workflow panel.
2. The **`* Recording`** overlay appears in the top-right corner showing the event count.
3. Perform actions on your desktop — clicks, key presses, scrolls, and mouse moves (moves are throttled to record only when the cursor moves more than 10 px to avoid flooding).
4. Click **`[Stop]`** in the overlay when done.
5. The **Review Recording** screen shows all captured activities with their delays:
   - Toggle **Use captured timing** to use real elapsed time between actions, or uncheck to use a fixed delay for every activity.
   - Edit any individual delay directly in the list.
6. Click **Accept** to append all activities to the current workflow, or **Discard** to throw them away.

> **Note:** Clicks on the AriesAutomationTools window itself are automatically filtered out during recording.

### 6. Triggers

| Trigger type | How it fires |
|-------------|-------------|
| **Manual** | Only fires when you click `>> Start` |
| **Schedule** | Fires on a cron schedule (checked every 500 ms) |
| **Pixel color** | Fires whenever a screen pixel matches a configured color |

#### Schedule — cron expression format

Five space-separated fields: `minute  hour  day-of-month  month  day-of-week`

| Value | Meaning |
|-------|---------|
| `*` | Every value (always match) |
| `*/N` | Every N units (e.g. `*/5` = every 5) |
| `N` | Exactly N |

Examples:

```
*/1 * * * *     Every minute
*/5 * * * *     Every 5 minutes
0 9 * * 1       Every Monday at 09:00
30 18 * * *     Every day at 18:30
0 */2 * * *     Every 2 hours (on the hour)
0 0 1 * *       First day of every month at midnight
```

> The trigger fires each time the cron expression matches the current wall-clock minute. The workflow must also be **Enabled**.

#### Pixel color trigger

1. Set **Pixel X / Y** by typing coordinates or clicking **Pick pixel position** (same floating overlay as the activity picker — move cursor to the target pixel and press Enter).
2. Set the expected **Color** using the color picker.
3. Set **Tolerance** (0–255) — how close the actual pixel color must be to match (0 = exact match, 50 = close enough for most uses).
4. Set **Poll interval (ms)** — how often the pixel is sampled (minimum 500 ms).

### 7. Smart Detection

When **Smart detection** is enabled, all running workflows pause automatically when the tool detects real user input (mouse/keyboard activity). They resume after the configured **Idle ms** period has elapsed with no input.

This prevents the tool from fighting with your own mouse/keyboard during active use.

### 8. Running and Stopping

- **`>> Start`** — start the selected workflow.
- **`[Stop]`** — stop the selected workflow.
- **`>> Start All`** / **`[Stop All]`** — start or stop every workflow at once.
- **F9** (or your configured hotkey) — globally pause/resume all workflows without clicking.

The hotkey can be changed in the top bar. Press Enter after editing to apply.

### 9. Config file

`config.json` is auto-saved when you close the app if there are unsaved changes. To save manually: **File > Save Config**, or click the **`*`** save button in the top bar when it appears (it indicates unsaved changes).

---

## Project Structure

```
AriesAutomationTools/
├── CMakeLists.txt              # Build definition, CPM dependency management
├── cmake/
│   └── CPM.cmake               # CPM package manager bootstrap
├── assets/
│   └── config.json             # Default config (copied next to exe on build)
└── src/
    ├── main.cpp                 # SDL2 + ImGui init, main loop
    │
    ├── core/
    │   ├── workflow.hpp         # All data structs (ActivityData variant, Workflow, AppConfig)
    │   ├── engine.hpp/cpp       # WorkflowEngine: owns schedulers, smart-detection monitor thread
    │   ├── scheduler.hpp/cpp    # Per-workflow background thread, executes activity sequence
    │   ├── record_engine.hpp/cpp# Global WH_MOUSE_LL/WH_KEYBOARD_LL hook capture (Windows)
    │   └── trigger_manager.hpp/cpp # 500ms poll loop for Schedule and Pixel triggers
    │
    ├── input/
    │   ├── input_simulator.hpp  # IInputSimulator interface
    │   ├── key_map.hpp/cpp      # Bidirectional key-name <-> platform keycode table
    │   ├── windows/win_input.cpp   # SendInput implementation
    │   ├── linux/linux_input.cpp   # XTest implementation
    │   └── macos/macos_input.cpp   # CGEventPost implementation
    │
    ├── monitor/
    │   ├── activity_monitor.hpp # IActivityMonitor interface
    │   ├── windows/win_monitor.cpp  # GetLastInputInfo
    │   ├── linux/linux_monitor.cpp  # XScreenSaverQueryInfo
    │   └── macos/macos_monitor.cpp  # IOHIDSystem HIDIdleTime
    │
    ├── window/
    │   ├── window_finder.hpp    # IWindowFinder interface + WindowInfo/Rect structs
    │   ├── pixel_checker.hpp    # IPixelChecker interface + ColorsMatch helper
    │   ├── windows/win_window.cpp  # EnumWindows, GetPixel via screen DC
    │   ├── linux/linux_window.cpp  # XQueryTree, XGetImage
    │   └── macos/macos_window.cpp  # CGWindowListCopyWindowInfo, CGDisplayCreateImageForRect
    │
    ├── hotkey/
    │   ├── hotkey_manager.hpp   # IHotkeyManager interface
    │   ├── windows/win_hotkey.cpp  # RegisterHotKey / PeekMessage dispatch
    │   ├── linux/linux_hotkey.cpp  # XGrabKey
    │   └── macos/macos_hotkey.cpp  # CGEventTap
    │
    ├── config/
    │   └── config_manager.hpp/cpp  # JSON load/save (nlohmann/json), all 9 activity types
    │
    └── ui/
        ├── app_ui.hpp/cpp       # Top-level layout, wires all widgets and engine
        └── widgets/
            ├── workflow_list.hpp/cpp   # Left panel: workflow list, add/dup/delete
            ├── activity_editor.hpp/cpp # Right panel: activity list + add/edit modal
            ├── window_picker.hpp/cpp   # Crosshair spy-tool overlay
            └── record_overlay.hpp/cpp  # Floating recording toolbar + review screen
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│  AppUI  (UI thread - ImGui/SDL2 ~60 fps)                │
│  ┌──────────┐  ┌──────────────────┐  ┌───────────────┐ │
│  │Workflow  │  │ ActivityEditor   │  │ RecordOverlay │ │
│  │List      │  │ + CoordPicker    │  │               │ │
│  └──────────┘  └──────────────────┘  └───────────────┘ │
│       │                  │                   │          │
│  ┌────┴──────────────────┴───────────────────┴──────┐  │
│  │         WorkflowEngine                           │  │
│  │  ┌────────────┐  ┌──────────────────────────┐   │  │
│  │  │MonitorLoop │  │  Scheduler (per workflow) │   │  │
│  │  │thread      │  │  background thread        │   │  │
│  │  │(200ms poll)│  │  executes activity seq.   │   │  │
│  │  └────────────┘  └──────────────────────────┘   │  │
│  └──────────────────────────────────────────────────┘  │
│                                                         │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐  │
│  │TriggerManager│  │RecordEngine  │  │HotkeyManager│  │
│  │(500ms thread)│  │(global hooks)│  │(RegisterHotKey) │
│  └──────────────┘  └──────────────┘  └─────────────┘  │
└─────────────────────────────────────────────────────────┘
         │                    │
┌────────┴──────┐   ┌─────────┴────────┐
│IInputSimulator│   │IActivityMonitor  │
│IWindowFinder  │   │IPixelChecker     │
│(platform impl)│   │(platform impl)   │
└───────────────┘   └──────────────────┘
```

**Threading model:**
- **UI thread** — ImGui render loop, polls hotkeys via `PeekMessage` each frame
- **Scheduler thread** — one per workflow, sleeps between activities; interruptible in 10 ms slices so it stops instantly when requested
- **MonitorLoop thread** — polls `IActivityMonitor` every 200 ms; sets `suspended` flag on all schedulers if user input is detected within the idle threshold
- **TriggerManager thread** — polls cron and pixel conditions every 500 ms; calls `StartWorkflow` via callback

**Data flow for recording:**
`WH_MOUSE_LL / WH_KEYBOARD_LL hooks` -> `RecordEngine::m_events` -> `ToActivities()` -> appended to `Workflow::activities` -> `WorkflowEngine::SetWorkflows()` -> new Scheduler created

---

## Dependencies (auto-downloaded by CPM on first build)

| Library | Version | Purpose |
|---------|---------|---------|
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.0 | Immediate-mode UI |
| [SDL2](https://github.com/libsdl-org/SDL) | 2.30.3 | Window, renderer, input (statically linked) |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | Config JSON serialization |
