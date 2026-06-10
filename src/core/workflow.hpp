#pragma once
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

// ── Enums ─────────────────────────────────────────────────────────────────────

enum class PositionMode { Absolute, Relative };
enum class MouseButton  { Left, Right, Middle };

enum class PixelCheckAction {
    Retry,         // keep polling until color matches (with timeout)
    SkipIteration, // skip rest of this workflow run
    StopWorkflow   // stop the workflow entirely
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

struct RunWorkflowActivity {
    std::string workflow_id;
    int delay_ms = 0;
};

using ActivityData = std::variant<
    MouseMoveActivity,
    MouseClickActivity,
    MouseDragActivity,
    MouseScrollActivity,
    KeyPressActivity,
    TypeStringActivity,
    WaitActivity,
    PixelCheckActivity,
    RunWorkflowActivity
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
    std::string cron_expr;          // simplified: "*/30 * * * *"

    // Pixel
    int      pixel_x         = 0;
    int      pixel_y         = 0;
    uint32_t pixel_color     = 0;
    int      pixel_tolerance = 10;
    int      pixel_poll_ms   = 500;
};

// ── Workflow ──────────────────────────────────────────────────────────────────

struct Workflow {
    std::string id;
    std::string name;
    bool        enabled              = true;
    int         repeat_interval_ms   = 5000;
    int         repeat_count         = 0;    // 0 = infinite
    bool        smart_detection      = true;
    int         smart_detection_idle_ms = 2000;
    WindowTarget  window;
    StartTrigger  trigger;
    std::vector<Activity> activities;
};

// ── App config (top-level) ────────────────────────────────────────────────────

struct AppConfig {
    std::string global_hotkey = "F9";
    std::vector<Workflow> workflows;
};
