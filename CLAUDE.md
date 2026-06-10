# AriesAutomationTools — Claude Context

This file provides session-start context for Claude Code. Read it before making any changes.

## AI behavior rules

- **Always commit after finishing any modification.** Include a detailed commit message: what files changed, what was added/fixed/refactored, and why. Use the imperative mood (e.g., "Fix …", "Add …", "Refactor …").

## Code exploration — codebase-memory-mcp

This project is indexed in the knowledge graph as **`K-git-AriesAutomationTools`** (750 nodes, 1236 edges).
**Always use these tools first** for any code exploration before falling back to Grep/Glob/Read:

| Tool | When to use |
|------|-------------|
| `search_graph(name_pattern / label / qn_pattern)` | Find a function, class, method, or enum by name |
| `get_code_snippet(qualified_name)` | Get exact source for a known symbol |
| `trace_path(function_name, mode=calls\|data_flow)` | Follow call chains or data flow |
| `get_architecture(aspects)` | Project structure, packages, dependency overview |
| `search_code(pattern)` | Graph-augmented text search (use instead of grep for code) |
| `query_graph(cypher)` | Complex relationship queries |

If the index seems stale after large refactors, run `index_repository(repo_path="k:\\git\\AriesAutomationTools", mode="moderate")`.

## What this project is

A portable C++17 desktop automation tool (Windows/Linux/macOS). Users define "workflows" of
ordered mouse/keyboard/wait activities that execute on a timer, cron schedule, or pixel-color
trigger. Built with Dear ImGui + SDL2 (UI), nlohmann/json (config), CMake + CPM.cmake (build).
Target: single portable EXE, statically linked.

## Build command (Windows)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

EXE: `build\Release\AriesAutomationTools.exe`

## Critical platform constraints

- **NOMINMAX + WIN32_LEAN_AND_MEAN** must be `#define`d before every `#include <windows.h>`.
  Forgetting causes `std::min`/`std::max` macro conflicts.
- **`/utf-8` MSVC compile flag** is set in CMakeLists.txt. Required because this system runs
  Japanese Windows (codepage 932). Without it, any non-ASCII string literal causes C3688/C2001.
- **ImGui default font (ProggyClean) only supports ASCII**. Any Unicode glyph (→ ↑ ↓ ± ✔ ✕ ● ■ ▶ ⏺)
  renders as a blank box. Use ASCII substitutes: `->`, `^`, `v`, `+/-`, `Accept`, `[Stop]`, `*`,
  `[Stop All]`, `>> Start`, `[Rec]`.
- **`std::istringstream`** requires `#include <sstream>`. Easy to miss.
- **`std::mutex`** requires `#include <mutex>` in the header (not just the .cpp).

## Architecture

```
UI thread (ImGui ~60fps)
  AppUI::Render()
    PollHotkeys()           ← must be called every frame
    WorkflowListWidget
    ActivityEditorWidget    ← has coord picker (closes modal, shows SDL overlay, reopens)
    WindowPickerWidget      ← spy-tool crosshair
    RecordOverlayWidget     ← always-on-top recording toolbar + review screen
    RenderTriggerPickOverlay()  ← pixel trigger position picker

WorkflowEngine
  MonitorLoop thread        ← polls IActivityMonitor every 200ms, sets suspended flag
  Scheduler (one per wf)   ← background thread, sleeps in 10ms slices (interruptible)

TriggerManager thread       ← polls cron + pixel every 500ms, fires StartWorkflow callback
RecordEngine                ← WH_MOUSE_LL + WH_KEYBOARD_LL hooks (Windows)
HotkeyManager               ← RegisterHotKey + PeekMessage dispatch (Windows)
```

## Key data structures (core/workflow.hpp)

```cpp
using ActivityData = std::variant<
    MouseMoveActivity, MouseClickActivity, MouseDragActivity, MouseScrollActivity,
    KeyPressActivity, TypeStringActivity, WaitActivity, PixelCheckActivity, RunWorkflowActivity
>;
struct Activity  { std::string id; bool enabled; ActivityData data; };
struct Workflow  { std::string id, name; bool enabled; int repeat_interval_ms, repeat_count;
                   bool smart_detection; int smart_detection_idle_ms;
                   WindowTarget window; StartTrigger trigger; std::vector<Activity> activities; };
struct AppConfig { std::string global_hotkey; std::vector<Workflow> workflows; };
```

All fields use `delay_ms` except `WaitActivity` which uses `duration_ms`. The activity editor
(record_overlay.cpp, getDelay lambda) must handle this with `if constexpr`.

## Recording flow

1. `[Rec]` → `RecordEngine::Start()` installs `WH_MOUSE_LL` + `WH_KEYBOARD_LL`
2. Events append to `m_events`. MouseMove throttled: only recorded if moved >10px.
3. `[Stop Rec]` (main panel) OR `[Stop]` (overlay) → `RecordEngine::Stop()` + `RecordOverlayWidget::TriggerReview()` 
4. Review screen: shows activities, delays editable, Accept/Discard
5. Accept → activities appended to selected workflow

`[Stop Rec]` in `app_ui.cpp` MUST call both `m_recorder.Stop()` AND `m_recOverlay.TriggerReview(m_recorder)`.
If only `Stop()` is called, the review screen never appears (known past bug, now fixed).

## Coordinate picker flow (ActivityEditorWidget)

When "Pick position" button is clicked inside the modal:
1. `m_pickStage = PickStage::Single` (or DragFrom/DragTo for drag)
2. `ImGui::CloseCurrentPopup()` — modal closes
3. `Render()` calls `RenderPickOverlay()` which shows floating tooltip with `SDL_GetGlobalMouseState`
4. Press Enter → `ApplyPickedCoords(x, y)` sets fields in `m_draft.data` via `std::visit`
5. `m_openModal = true` → modal reopens next frame with coordinates filled

Same pattern for pixel trigger pick: `m_trigPickActive` / `RenderTriggerPickOverlay()` in AppUI.

## Schedule trigger cron format

5 fields: `minute hour day-of-month month day-of-week`
Supports: `*` (any), `*/N` (every N), `N` (exact).
UI is a visual builder: HH/MM are InputText (accepts `*`, `*/N`, `N`), Day/Month/DOW are Combo dropdowns.
The assembled cron string is shown read-only below the controls.
Static char buffers `s_hh`/`s_mm` are reinitialized when `wfId != s_wfId` (workflow switch detection).

## Scheduler globals (scheduler.cpp)

`g_input` (`IInputSimulator*`) and `g_pixel` (`IPixelChecker*`) are set by:
- `Scheduler_SetInputSimulator()`
- `Scheduler_SetPixelChecker()`
These must be called before any workflow starts. They are wired in `WorkflowEngine::Init()`.

## Config file

`config.json` lives next to the EXE. Auto-saved on app exit if dirty. Manual save: Ctrl+S or `Save` button.
Colors stored as `"#RRGGBB"` hex strings. All activity types serialized in `config/config_manager.cpp`.

## File locations for common tasks

| Task | File |
|------|------|
| Add new activity type | `workflow.hpp` (variant), `config_manager.cpp` (JSON), `activity_editor.cpp` (UI), `scheduler.cpp` (execution) |
| Fix activity editor UI | `src/ui/widgets/activity_editor.cpp` |
| Fix recording | `src/core/record_engine.cpp`, `src/ui/widgets/record_overlay.cpp` |
| Fix trigger behavior | `src/core/trigger_manager.cpp`, `src/ui/app_ui.cpp` (RenderTriggerEditor) |
| Add new platform support | Add platform source set in `CMakeLists.txt`, implement all 4 interfaces |
| Change window layout | `src/ui/app_ui.cpp` |

## Dependencies (fetched by CPM on first build, cached in build/_deps)

- Dear ImGui v1.91.0 — `${imgui_SOURCE_DIR}` in CMakeLists
- SDL2 2.30.3 — static, `SDL2::SDL2-static` + `SDL2::SDL2main`
- nlohmann/json 3.11.3 — header-only, `nlohmann_json::nlohmann_json`

## What has been built and working

- Full data model, JSON config round-trip
- All 9 activity types executed by scheduler
- Smart detection (MonitorLoop suspends schedulers on user activity)
- Global hotkey (F9 default, configurable)
- Window targeting + spy-picker
- Record mode (WH_MOUSE_LL/WH_KEYBOARD_LL, throttled moves, review screen)
- Cron + pixel triggers
- Coordinate picker overlay in activity editor modal
- Key press capture (ImGui key scan)
- Scroll delta capture
- Pixel trigger position picker
- Schedule UI visual builder (HH/MM inputs + Day/Month/DOW combos)

## Linux / macOS

Interface stubs exist and compile. Full implementations are in:
- `src/input/linux/` `src/input/macos/`
- `src/monitor/linux/` `src/monitor/macos/`
- `src/window/linux/` `src/window/macos/`
- `src/hotkey/linux/` `src/hotkey/macos/`

XRecord (Linux recording) and CGEventTap (macOS recording) are not yet implemented in RecordEngine.
