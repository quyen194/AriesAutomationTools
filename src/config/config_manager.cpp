#include "config_manager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <filesystem>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#else
#  include <unistd.h>
#  include <limits.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string PosModeToStr(PositionMode m) {
    return m == PositionMode::Relative ? "relative" : "absolute";
}
static PositionMode StrToPosMode(const std::string& s) {
    return s == "relative" ? PositionMode::Relative : PositionMode::Absolute;
}
static std::string BtnToStr(MouseButton b) {
    switch (b) {
        case MouseButton::Right:  return "right";
        case MouseButton::Middle: return "middle";
        default:                  return "left";
    }
}
static MouseButton StrToBtn(const std::string& s) {
    if (s == "right")  return MouseButton::Right;
    if (s == "middle") return MouseButton::Middle;
    return MouseButton::Left;
}
static std::string PixelActionToStr(PixelCheckAction a) {
    switch (a) {
        case PixelCheckAction::SkipIteration: return "skip_iteration";
        case PixelCheckAction::StopWorkflow:  return "stop_workflow";
        default:                              return "retry";
    }
}
static PixelCheckAction StrToPixelAction(const std::string& s) {
    if (s == "skip_iteration") return PixelCheckAction::SkipIteration;
    if (s == "stop_workflow")  return PixelCheckAction::StopWorkflow;
    return PixelCheckAction::Retry;
}
static std::string ColorToHex(uint32_t c) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", c & 0xFFFFFF);
    return buf;
}
static uint32_t HexToColor(const std::string& s) {
    std::string hex = s;
    if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
    return (uint32_t)std::stoul(hex, nullptr, 16);
}

// ── Activity serialization ────────────────────────────────────────────────────

static json SerializeActivity(const Activity& a) {
    json j;
    j["id"]      = a.id;
    j["enabled"] = a.enabled;

    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, MouseMoveActivity>) {
            j["type"]              = "mouse_move";
            j["position_mode"]     = PosModeToStr(v.pos_mode);
            j["x"] = v.x; j["y"]  = v.y;
            j["delay_after_ms"]    = v.delay_ms;
            j["delay_random_range_ms"] = v.delay_rand_ms;

        } else if constexpr (std::is_same_v<T, MouseClickActivity>) {
            j["type"]              = "mouse_click";
            j["position_mode"]     = PosModeToStr(v.pos_mode);
            j["x"] = v.x; j["y"]  = v.y;
            j["button"]            = BtnToStr(v.button);
            j["double_click"]      = v.double_click;
            j["delay_after_ms"]    = v.delay_ms;
            j["delay_random_range_ms"] = v.delay_rand_ms;

        } else if constexpr (std::is_same_v<T, MouseDragActivity>) {
            j["type"]          = "mouse_drag";
            j["position_mode"] = PosModeToStr(v.pos_mode);
            j["from_x"] = v.from_x; j["from_y"] = v.from_y;
            j["to_x"]   = v.to_x;   j["to_y"]   = v.to_y;
            j["button"]        = BtnToStr(v.button);
            j["duration_ms"]   = v.duration_ms;
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, MouseScrollActivity>) {
            j["type"]          = "mouse_scroll";
            j["position_mode"] = PosModeToStr(v.pos_mode);
            j["x"] = v.x; j["y"] = v.y;
            j["delta_x"] = v.delta_x; j["delta_y"] = v.delta_y;
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, KeyPressActivity>) {
            j["type"]          = "key_press";
            j["key"]           = v.key;
            j["modifiers"]     = v.modifiers;
            j["delay_after_ms"]= v.delay_ms;
            j["delay_random_range_ms"] = v.delay_rand_ms;

        } else if constexpr (std::is_same_v<T, TypeStringActivity>) {
            j["type"]                    = "type_string";
            j["text"]                    = v.text;
            j["delay_between_chars_ms"]  = v.delay_between_chars_ms;
            j["delay_after_ms"]          = v.delay_ms;

        } else if constexpr (std::is_same_v<T, WaitActivity>) {
            j["type"]             = "wait";
            j["duration_ms"]      = v.duration_ms;
            j["random_range_ms"]  = v.random_range_ms;

        } else if constexpr (std::is_same_v<T, PixelCheckActivity>) {
            j["type"]               = "pixel_check";
            j["position_mode"]      = PosModeToStr(v.pos_mode);
            j["x"] = v.x; j["y"]   = v.y;
            j["color"]              = ColorToHex(v.color_rgb);
            j["tolerance"]          = v.tolerance;
            j["on_no_match"]        = PixelActionToStr(v.on_no_match);
            j["retry_interval_ms"]  = v.retry_interval_ms;
            j["retry_timeout_ms"]   = v.retry_timeout_ms;
            j["delay_after_ms"]     = v.delay_ms;

        } else if constexpr (std::is_same_v<T, RunWorkflowActivity>) {
            j["type"]          = "run_workflow";
            j["workflow_id"]   = v.workflow_id;
            j["delay_after_ms"]= v.delay_ms;
        }
    }, a.data);

    return j;
}

static Activity DeserializeActivity(const json& j) {
    Activity a;
    a.id      = j.value("id", "");
    a.enabled = j.value("enabled", true);

    std::string type = j.value("type", "");

    if (type == "mouse_move") {
        MouseMoveActivity v;
        v.pos_mode     = StrToPosMode(j.value("position_mode", "absolute"));
        v.x = j.value("x", 0); v.y = j.value("y", 0);
        v.delay_ms     = j.value("delay_after_ms", 100);
        v.delay_rand_ms= j.value("delay_random_range_ms", 0);
        a.data = v;

    } else if (type == "mouse_click") {
        MouseClickActivity v;
        v.pos_mode     = StrToPosMode(j.value("position_mode", "absolute"));
        v.x = j.value("x", 0); v.y = j.value("y", 0);
        v.button       = StrToBtn(j.value("button", "left"));
        v.double_click = j.value("double_click", false);
        v.delay_ms     = j.value("delay_after_ms", 100);
        v.delay_rand_ms= j.value("delay_random_range_ms", 0);
        a.data = v;

    } else if (type == "mouse_drag") {
        MouseDragActivity v;
        v.pos_mode   = StrToPosMode(j.value("position_mode", "absolute"));
        v.from_x = j.value("from_x", 0); v.from_y = j.value("from_y", 0);
        v.to_x   = j.value("to_x",   0); v.to_y   = j.value("to_y",   0);
        v.button     = StrToBtn(j.value("button", "left"));
        v.duration_ms= j.value("duration_ms", 300);
        v.delay_ms   = j.value("delay_after_ms", 100);
        a.data = v;

    } else if (type == "mouse_scroll") {
        MouseScrollActivity v;
        v.pos_mode = StrToPosMode(j.value("position_mode", "absolute"));
        v.x = j.value("x", 0); v.y = j.value("y", 0);
        v.delta_x  = j.value("delta_x", 0);
        v.delta_y  = j.value("delta_y", -3);
        v.delay_ms = j.value("delay_after_ms", 100);
        a.data = v;

    } else if (type == "key_press") {
        KeyPressActivity v;
        v.key          = j.value("key", "");
        v.modifiers    = j.value("modifiers", std::vector<std::string>{});
        v.delay_ms     = j.value("delay_after_ms", 100);
        v.delay_rand_ms= j.value("delay_random_range_ms", 0);
        a.data = v;

    } else if (type == "type_string") {
        TypeStringActivity v;
        v.text                   = j.value("text", "");
        v.delay_between_chars_ms = j.value("delay_between_chars_ms", 50);
        v.delay_ms               = j.value("delay_after_ms", 100);
        a.data = v;

    } else if (type == "wait") {
        WaitActivity v;
        v.duration_ms     = j.value("duration_ms", 1000);
        v.random_range_ms = j.value("random_range_ms", 0);
        a.data = v;

    } else if (type == "pixel_check") {
        PixelCheckActivity v;
        v.pos_mode          = StrToPosMode(j.value("position_mode", "absolute"));
        v.x = j.value("x", 0); v.y = j.value("y", 0);
        v.color_rgb         = HexToColor(j.value("color", "#FF0000"));
        v.tolerance         = j.value("tolerance", 10);
        v.on_no_match       = StrToPixelAction(j.value("on_no_match", "retry"));
        v.retry_interval_ms = j.value("retry_interval_ms", 500);
        v.retry_timeout_ms  = j.value("retry_timeout_ms", 10000);
        v.delay_ms          = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "run_workflow") {
        RunWorkflowActivity v;
        v.workflow_id = j.value("workflow_id", "");
        v.delay_ms    = j.value("delay_after_ms", 0);
        a.data = v;
    } else {
        // Unknown type — store as wait(0) to avoid crashing
        a.data = WaitActivity{0, 0};
    }

    return a;
}

// ── Workflow serialization ─────────────────────────────────────────────────────

static json SerializeWorkflow(const Workflow& w) {
    json j;
    j["id"]      = w.id;
    j["name"]    = w.name;
    j["enabled"] = w.enabled;
    j["repeat_interval_ms"]      = w.repeat_interval_ms;
    j["repeat_count"]            = w.repeat_count;
    j["smart_detection"]         = w.smart_detection;
    j["smart_detection_idle_ms"] = w.smart_detection_idle_ms;

    // Window target
    json win;
    switch (w.window.type) {
        case WindowTarget::Type::ByTitle:  win["type"] = "title";  win["title"]      = w.window.title;      break;
        case WindowTarget::Type::ByClass:  win["type"] = "class";  win["class_name"] = w.window.class_name; break;
        case WindowTarget::Type::ByHandle: win["type"] = "handle"; win["handle"]     = w.window.handle;     break;
        default:                           win["type"] = "global"; break;
    }
    j["window"] = win;

    // Trigger
    json trig;
    switch (w.trigger.type) {
        case StartTrigger::Type::Schedule:
            trig["type"]      = "schedule";
            trig["cron_expr"] = w.trigger.cron_expr;
            break;
        case StartTrigger::Type::Pixel:
            trig["type"]            = "pixel";
            trig["pixel_x"]         = w.trigger.pixel_x;
            trig["pixel_y"]         = w.trigger.pixel_y;
            trig["pixel_color"]     = ColorToHex(w.trigger.pixel_color);
            trig["pixel_tolerance"] = w.trigger.pixel_tolerance;
            trig["pixel_poll_ms"]   = w.trigger.pixel_poll_ms;
            break;
        default:
            trig["type"] = "manual";
            break;
    }
    j["trigger"] = trig;

    json acts = json::array();
    for (auto& a : w.activities) acts.push_back(SerializeActivity(a));
    j["activities"] = acts;

    j["hotkey_start"]  = w.hotkey_start;
    j["hotkey_stop"]   = w.hotkey_stop;
    j["hotkey_pause"]  = w.hotkey_pause;
    j["hotkey_resume"] = w.hotkey_resume;

    return j;
}

static Workflow DeserializeWorkflow(const json& j) {
    Workflow w;
    w.id                       = j.value("id", "");
    w.name                     = j.value("name", "Unnamed");
    w.enabled                  = j.value("enabled", true);
    w.repeat_interval_ms       = j.value("repeat_interval_ms", 5000);
    w.repeat_count             = j.value("repeat_count", 0);
    w.smart_detection          = j.value("smart_detection", true);
    w.smart_detection_idle_ms  = j.value("smart_detection_idle_ms", 2000);

    if (j.contains("window")) {
        auto& win = j["window"];
        std::string wt = win.value("type", "global");
        if      (wt == "title")  { w.window.type = WindowTarget::Type::ByTitle;  w.window.title      = win.value("title", ""); }
        else if (wt == "class")  { w.window.type = WindowTarget::Type::ByClass;  w.window.class_name = win.value("class_name", ""); }
        else if (wt == "handle") { w.window.type = WindowTarget::Type::ByHandle; w.window.handle     = win.value("handle", (uint64_t)0); }
        else                     { w.window.type = WindowTarget::Type::Global; }
    }

    if (j.contains("trigger")) {
        auto& trig = j["trigger"];
        std::string tt = trig.value("type", "manual");
        if (tt == "schedule") {
            w.trigger.type      = StartTrigger::Type::Schedule;
            w.trigger.cron_expr = trig.value("cron_expr", "");
        } else if (tt == "pixel") {
            w.trigger.type            = StartTrigger::Type::Pixel;
            w.trigger.pixel_x         = trig.value("pixel_x", 0);
            w.trigger.pixel_y         = trig.value("pixel_y", 0);
            w.trigger.pixel_color     = HexToColor(trig.value("pixel_color", "#000000"));
            w.trigger.pixel_tolerance = trig.value("pixel_tolerance", 10);
            w.trigger.pixel_poll_ms   = trig.value("pixel_poll_ms", 500);
        } else {
            w.trigger.type = StartTrigger::Type::Manual;
        }
    }

    if (j.contains("activities")) {
        for (auto& aj : j["activities"])
            w.activities.push_back(DeserializeActivity(aj));
    }

    w.hotkey_start  = j.value("hotkey_start",  "");
    w.hotkey_stop   = j.value("hotkey_stop",   "");
    w.hotkey_pause  = j.value("hotkey_pause",  "");
    w.hotkey_resume = j.value("hotkey_resume", "");

    return w;
}

// ── Public API ────────────────────────────────────────────────────────────────

AppConfig ConfigManager::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open config: " + path);

    json root;
    try { f >> root; }
    catch (json::parse_error& e) {
        throw std::runtime_error(std::string("JSON parse error: ") + e.what());
    }

    AppConfig cfg;
    cfg.global_hotkey       = root.value("global_hotkey",       "f9");
    // "record_hotkey" is the legacy key; "start_record_hotkey" takes precedence
    cfg.start_record_hotkey = root.value("start_record_hotkey", root.value("record_hotkey", ""));
    cfg.stop_record_hotkey  = root.value("stop_record_hotkey",  "");
    cfg.start_all_hotkey    = root.value("start_all_hotkey",    "");
    cfg.stop_all_hotkey     = root.value("stop_all_hotkey",     "");
    cfg.pause_all_hotkey    = root.value("pause_all_hotkey",    "");
    cfg.resume_all_hotkey   = root.value("resume_all_hotkey",   "");
    cfg.close_to_tray    = root.value("close_to_tray",    false);
    cfg.minimize_to_tray = root.value("minimize_to_tray", false);
    cfg.single_instance  = root.value("single_instance",  true);

    if (root.contains("workflows")) {
        for (auto& wj : root["workflows"])
            cfg.workflows.push_back(DeserializeWorkflow(wj));
    }

    return cfg;
}

void ConfigManager::Save(const AppConfig& config, const std::string& path) {
    json root;
    root["global_hotkey"]       = config.global_hotkey;
    root["start_record_hotkey"] = config.start_record_hotkey;
    root["stop_record_hotkey"]  = config.stop_record_hotkey;
    root["start_all_hotkey"]    = config.start_all_hotkey;
    root["stop_all_hotkey"]     = config.stop_all_hotkey;
    root["pause_all_hotkey"]    = config.pause_all_hotkey;
    root["resume_all_hotkey"]   = config.resume_all_hotkey;
    root["close_to_tray"]    = config.close_to_tray;
    root["minimize_to_tray"] = config.minimize_to_tray;
    root["single_instance"]  = config.single_instance;

    json wfs = json::array();
    for (auto& w : config.workflows) wfs.push_back(SerializeWorkflow(w));
    root["workflows"] = wfs;

    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot write config: " + path);
    f << root.dump(2);
}

std::string ConfigManager::DefaultPath() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string() + "/config.json";
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    _NSGetExecutablePath(buf, &size);
    return fs::path(buf).parent_path().string() + "/config.json";
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if (len != -1) buf[len] = '\0';
    return fs::path(buf).parent_path().string() + "/config.json";
#endif
}
