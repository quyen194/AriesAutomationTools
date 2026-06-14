#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>
#include <memory>

// Forward declaration: enables shared_ptr<vector<Activity>> in nested block types
// before Activity is fully defined. shared_ptr only needs the type complete at
// destruction time (which happens in .cpp files where Activity IS complete).
struct Activity;

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class PositionMode { Absolute, Relative };
enum class MouseButton  { Left, Right, Middle };

enum class PixelCheckAction {
    Retry,         // keep polling until color matches (with timeout)
    SkipIteration, // skip rest of this workflow run
    StopWorkflow   // stop the workflow entirely
};

enum class SystemAction {
    Shutdown,
    Restart,
    Sleep,
    Hibernate,
    Lock,
    LogOut
};

enum class VarOp       { Set, Increment, Decrement, Random };
enum class ConditionOp { Eq, NEq, Gt, Lt, GtEq, LtEq, Contains };

// ── Condition ─────────────────────────────────────────────────────────────────

struct Condition {
    std::string lhs;
    bool        lhs_is_var = true;
    ConditionOp op         = ConditionOp::Eq;
    std::string rhs;
    bool        rhs_is_var = false;
};

// ── Activity structs ──────────────────────────────────────────────────────────

struct MouseMoveActivity {
    PositionMode pos_mode = PositionMode::Absolute;
    int x = 0, y = 0;
    int delay_ms = 100, delay_rand_ms = 0;
};

struct MouseClickActivity {
    PositionMode pos_mode = PositionMode::Absolute;
    int x = 0, y = 0;
    MouseButton button = MouseButton::Left;
    bool double_click = false;
    int delay_ms = 100, delay_rand_ms = 0;
};

struct MouseDragActivity {
    PositionMode pos_mode = PositionMode::Absolute;
    int from_x = 0, from_y = 0;
    int to_x   = 0, to_y   = 0;
    MouseButton button = MouseButton::Left;
    int duration_ms = 300;
    int delay_ms = 100;
};

struct MouseScrollActivity {
    PositionMode pos_mode = PositionMode::Absolute;
    int x = 0, y = 0;
    int delta_x = 0, delta_y = -3;
    int delay_ms = 100;
};

struct KeyPressActivity {
    std::string key;                       // e.g. "space", "f1", "a"
    std::vector<std::string> modifiers;    // e.g. ["ctrl","shift"]
    int delay_ms = 100, delay_rand_ms = 0;
};

struct TypeStringActivity {
    std::string text;
    int delay_between_chars_ms = 50;
    int delay_ms = 100;
};

struct WaitActivity {
    int duration_ms = 1000;
    int random_range_ms = 0;
};

// Index 7 — kept in variant for backwards-compat deserialization only.
// Hidden from the UI (not in kTypes[]). New configs should use PixelRangeCheckActivity.
struct PixelCheckActivity {
    PositionMode pos_mode = PositionMode::Absolute;
    int x = 0, y = 0;
    uint32_t color_rgb = 0xFF0000;   // packed 0xRRGGBB
    int tolerance = 10;
    PixelCheckAction on_no_match = PixelCheckAction::Retry;
    int retry_interval_ms = 500;
    int retry_timeout_ms  = 10000;  // 0 = no timeout
    int delay_ms = 0;
};

// Index 8 — redesigned as a block-type with match/no_match branches.
// shared_ptr bodies are initialized in .cpp files (where Activity is complete).
struct PixelRangeCheckActivity {
    std::string  name;                   // display name "Pixel Check 1", user-editable
    PositionMode pos_mode = PositionMode::Absolute;
    int x1 = 0, y1 = 0;
    int x2 = 0, y2 = 0;
    int sample_w = 0, sample_h = 0;
    std::vector<uint32_t> sample;        // packed 0xRRGGBB row-major
    int tolerance      = 10;
    int match_percent  = 95;
    // retry_timeout_ms = 0 → check once immediately, no polling
    int retry_interval_ms = 500;
    int retry_timeout_ms  = 0;
    std::shared_ptr<std::vector<Activity>> match_body;
    std::shared_ptr<std::vector<Activity>> no_match_body;
    int delay_ms = 0;
};

struct RunWorkflowActivity {
    std::string workflow_id;
    int delay_ms = 0;
};

struct SystemActionActivity {
    SystemAction action   = SystemAction::Shutdown;
    bool         force    = false;
    int          delay_ms = 0;
};

// ── New activity types (indices 11-15) ───────────────────────────────────────

// Index 11 — reuse: execute another activity in this workflow by its ID
struct RunActivityActivity {
    std::string activity_id;
    int delay_ms = 0;
};

// Index 12 — variables: set/modify a named runtime variable
struct SetVariableActivity {
    std::string name;
    std::string value;           // literal (used for Set)
    VarOp op     = VarOp::Set;
    int step     = 1;            // Inc/Dec step
    int rand_min = 0;
    int rand_max = 100;
    int delay_ms = 0;
};

// Index 13 — loop: repeat body count times (0 = infinite)
// shared_ptr body initialized in .cpp
struct LoopActivity {
    std::string name;            // display name "Loop 1", user-editable
    int         count    = 1;    // 0 = infinite
    std::string iter_var;        // optional: written with current iteration (1-based)
    std::shared_ptr<std::vector<Activity>> body;
    int delay_ms = 0;
};

// Index 14 — if/else
struct IfActivity {
    std::string name;            // display name "If 1"
    Condition   cond;
    std::shared_ptr<std::vector<Activity>> then_body;
    std::shared_ptr<std::vector<Activity>> else_body;
    int delay_ms = 0;
};

struct SwitchCase {
    std::string value;
    std::shared_ptr<std::vector<Activity>> body;
};

// Index 15 — switch/case
struct SwitchActivity {
    std::string name;            // display name "Switch 1"
    std::string var_name;        // variable to test
    std::vector<SwitchCase> cases;
    std::shared_ptr<std::vector<Activity>> default_body;
    int delay_ms = 0;
};

// Index 16 — jump: transfer control to another activity in the same body
struct JumpActivity {
    std::string target_id;   // id of the activity to jump to
    int delay_ms = 0;
};

// ── ActivityData variant ──────────────────────────────────────────────────────

using ActivityData = std::variant<
    MouseMoveActivity,        // 0
    MouseClickActivity,       // 1
    MouseDragActivity,        // 2
    MouseScrollActivity,      // 3
    KeyPressActivity,         // 4
    TypeStringActivity,       // 5
    WaitActivity,             // 6
    PixelCheckActivity,       // 7  ← compat only, hidden from UI
    PixelRangeCheckActivity,  // 8  ← redesigned block-type
    RunWorkflowActivity,      // 9
    SystemActionActivity,     // 10
    RunActivityActivity,      // 11
    SetVariableActivity,      // 12
    LoopActivity,             // 13
    IfActivity,               // 14
    SwitchActivity,           // 15
    JumpActivity              // 16
>;

struct Activity {
    std::string  id;
    bool         enabled = true;
    ActivityData data;
};

// ── Window targeting ──────────────────────────────────────────────────────────

struct WindowTarget {
    enum class Type { Global, ByTitle, ByClass, ByHandle } type = Type::Global;
    std::string title;
    std::string class_name;
    uint64_t    handle = 0;
};

// ── Start trigger ─────────────────────────────────────────────────────────────

struct StartTrigger {
    enum class Type { Manual, Schedule, Pixel } type = Type::Manual;

    // Schedule
    std::string cron_expr;

    // Pixel trigger: range-based (replaces single pixel_x/y/color fields)
    PositionMode          pixel_pos_mode      = PositionMode::Absolute;
    int                   pixel_x1            = 0;
    int                   pixel_y1            = 0;
    int                   pixel_x2            = 0;
    int                   pixel_y2            = 0;
    int                   pixel_sample_w      = 0;
    int                   pixel_sample_h      = 0;
    std::vector<uint32_t> pixel_sample;
    int                   pixel_tolerance     = 10;
    int                   pixel_match_percent = 95;
    int                   pixel_poll_ms       = 500;
};

// ── Workflow ──────────────────────────────────────────────────────────────────

struct Workflow {
    std::string id;
    std::string name;
    bool        enabled              = true;
    int         repeat_interval_ms   = 5000;
    int         repeat_count         = 0;    // 0 = infinite
    bool        smart_detection      = true;
    int         smart_detection_idle_ms           = 2000;
    int         smart_detection_start_delay_ms    = 1000;
    WindowTarget  window;
    StartTrigger  trigger;
    std::vector<Activity> activities;
    std::string hotkey_start  = "";
    std::string hotkey_stop   = "";
    std::string hotkey_pause  = "";
    std::string hotkey_resume = "";
};

// ── App config (top-level) ────────────────────────────────────────────────────

struct AppConfig {
    std::string start_record_hotkey   = "";
    std::string stop_record_hotkey    = "";
    std::string start_all_hotkey      = "";
    std::string stop_all_hotkey       = "";
    std::string pause_all_hotkey      = "";
    std::string resume_all_hotkey     = "";
    bool        close_to_tray    = false;
    bool        minimize_to_tray = false;
    bool        single_instance  = true;
    std::vector<Workflow> workflows;
};
