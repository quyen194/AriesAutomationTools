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

using json = nlohmann::ordered_json;
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
    if (hex.empty()) return 0;
    try { return (uint32_t)std::stoul(hex, nullptr, 16); } catch (...) { return 0; }
}

static const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string SampleToBase64(const std::vector<uint32_t>& pixels) {
    std::vector<uint8_t> bytes;
    bytes.reserve(pixels.size() * 3);
    for (uint32_t p : pixels) {
        bytes.push_back((uint8_t)((p >> 16) & 0xFF));
        bytes.push_back((uint8_t)((p >>  8) & 0xFF));
        bytes.push_back((uint8_t)( p        & 0xFF));
    }
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t n = (uint32_t)bytes[i] << 16;
        if (i + 1 < bytes.size()) n |= (uint32_t)bytes[i+1] << 8;
        if (i + 2 < bytes.size()) n |= bytes[i+2];
        out += kB64Chars[(n >> 18) & 63];
        out += kB64Chars[(n >> 12) & 63];
        out += (i + 1 < bytes.size()) ? kB64Chars[(n >> 6) & 63] : '=';
        out += (i + 2 < bytes.size()) ? kB64Chars[ n       & 63] : '=';
    }
    return out;
}

static std::vector<uint32_t> Base64ToSample(const std::string& b64) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> bytes;
    bytes.reserve(b64.size() / 4 * 3);
    int n = 0, bits = 0;
    for (char c : b64) {
        int v = val(c);
        if (v < 0) continue;
        n = (n << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back((uint8_t)((n >> bits) & 0xFF));
        }
    }
    std::vector<uint32_t> pixels;
    pixels.reserve(bytes.size() / 3);
    for (size_t i = 0; i + 2 < bytes.size(); i += 3)
        pixels.push_back(((uint32_t)bytes[i] << 16) |
                         ((uint32_t)bytes[i+1] << 8) |
                          (uint32_t)bytes[i+2]);
    return pixels;
}

static std::string SystemActionToStr(SystemAction a) {
    switch (a) {
        case SystemAction::Restart:   return "restart";
        case SystemAction::Sleep:     return "sleep";
        case SystemAction::Hibernate: return "hibernate";
        case SystemAction::Lock:      return "lock";
        case SystemAction::LogOut:    return "logout";
        default:                      return "shutdown";
    }
}
static SystemAction StrToSystemAction(const std::string& s) {
    if (s == "restart")   return SystemAction::Restart;
    if (s == "sleep")     return SystemAction::Sleep;
    if (s == "hibernate") return SystemAction::Hibernate;
    if (s == "lock")      return SystemAction::Lock;
    if (s == "logout")    return SystemAction::LogOut;
    return SystemAction::Shutdown;
}

static std::string VarOpToStr(VarOp op) {
    switch (op) {
        case VarOp::Increment: return "increment";
        case VarOp::Decrement: return "decrement";
        case VarOp::Random:    return "random";
        default:               return "set";
    }
}
static VarOp StrToVarOp(const std::string& s) {
    if (s == "increment") return VarOp::Increment;
    if (s == "decrement") return VarOp::Decrement;
    if (s == "random")    return VarOp::Random;
    return VarOp::Set;
}

static std::string CondOpToStr(ConditionOp op) {
    switch (op) {
        case ConditionOp::NEq:      return "neq";
        case ConditionOp::Gt:       return "gt";
        case ConditionOp::Lt:       return "lt";
        case ConditionOp::GtEq:     return "gteq";
        case ConditionOp::LtEq:     return "lteq";
        case ConditionOp::Contains: return "contains";
        default:                    return "eq";
    }
}
static ConditionOp StrToCondOp(const std::string& s) {
    if (s == "neq")      return ConditionOp::NEq;
    if (s == "gt")       return ConditionOp::Gt;
    if (s == "lt")       return ConditionOp::Lt;
    if (s == "gteq")     return ConditionOp::GtEq;
    if (s == "lteq")     return ConditionOp::LtEq;
    if (s == "contains") return ConditionOp::Contains;
    return ConditionOp::Eq;
}

static json SerializeCondition(const Condition& c) {
    json j;
    j["lhs"]        = c.lhs;
    j["lhs_is_var"] = c.lhs_is_var;
    j["op"]         = CondOpToStr(c.op);
    j["rhs"]        = c.rhs;
    j["rhs_is_var"] = c.rhs_is_var;
    return j;
}
static Condition DeserializeCondition(const json& j) {
    Condition c;
    c.lhs        = j.value("lhs", "");
    c.lhs_is_var = j.value("lhs_is_var", true);
    c.op         = StrToCondOp(j.value("op", "eq"));
    c.rhs        = j.value("rhs", "");
    c.rhs_is_var = j.value("rhs_is_var", false);
    return c;
}

// Forward-declare since SerializeActivity and SerializeActivityList are mutually recursive
static json SerializeActivity(const Activity& a);
static Activity DeserializeActivity(const json& j);

static json SerializeActivityList(const std::vector<Activity>& acts) {
    json arr = json::array();
    for (auto& a : acts) arr.push_back(SerializeActivity(a));
    return arr;
}
static std::vector<Activity> DeserializeActivityList(const json& arr) {
    std::vector<Activity> acts;
    if (!arr.is_array()) return acts;
    for (auto& j : arr) acts.push_back(DeserializeActivity(j));
    return acts;
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
            // Legacy type — serialize as-is so old configs can still round-trip
            j["type"]               = "pixel_check";
            j["position_mode"]      = PosModeToStr(v.pos_mode);
            j["x"] = v.x; j["y"]   = v.y;
            j["color"]              = ColorToHex(v.color_rgb);
            j["tolerance"]          = v.tolerance;
            j["on_no_match"]        = PixelActionToStr(v.on_no_match);
            j["retry_interval_ms"]  = v.retry_interval_ms;
            j["retry_timeout_ms"]   = v.retry_timeout_ms;
            j["delay_after_ms"]     = v.delay_ms;

        } else if constexpr (std::is_same_v<T, PixelRangeCheckActivity>) {
            j["type"]               = "pixel_range_check";
            j["name"]               = v.name;
            j["position_mode"]      = PosModeToStr(v.pos_mode);
            j["x1"] = v.x1; j["y1"] = v.y1;
            j["x2"] = v.x2; j["y2"] = v.y2;
            j["sample_w"]           = v.sample_w;
            j["sample_h"]           = v.sample_h;
            j["sample_b64"]         = SampleToBase64(v.sample);
            j["tolerance"]          = v.tolerance;
            j["match_percent"]      = v.match_percent;
            j["retry_interval_ms"]  = v.retry_interval_ms;
            j["retry_timeout_ms"]   = v.retry_timeout_ms;
            j["match_body"]         = SerializeActivityList(v.match_body    ? *v.match_body    : std::vector<Activity>{});
            j["no_match_body"]      = SerializeActivityList(v.no_match_body ? *v.no_match_body : std::vector<Activity>{});
            j["delay_after_ms"]     = v.delay_ms;

        } else if constexpr (std::is_same_v<T, RunWorkflowActivity>) {
            j["type"]          = "run_workflow";
            j["workflow_id"]   = v.workflow_id;
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, SystemActionActivity>) {
            j["type"]          = "system_action";
            j["action"]        = SystemActionToStr(v.action);
            j["force"]         = v.force;
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, RunActivityActivity>) {
            j["type"]          = "run_activity";
            j["activity_id"]   = v.activity_id;
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, SetVariableActivity>) {
            j["type"]          = "set_variable";
            j["name"]          = v.name;
            j["value"]         = v.value;
            j["op"]            = VarOpToStr(v.op);
            j["step"]          = v.step;
            j["rand_min"]      = v.rand_min;
            j["rand_max"]      = v.rand_max;
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, LoopActivity>) {
            j["type"]          = "loop";
            j["name"]          = v.name;
            j["count"]         = v.count;
            j["iter_var"]      = v.iter_var;
            j["body"]          = SerializeActivityList(v.body ? *v.body : std::vector<Activity>{});
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, IfActivity>) {
            j["type"]          = "if";
            j["name"]          = v.name;
            j["condition"]     = SerializeCondition(v.cond);
            j["then"]          = SerializeActivityList(v.then_body ? *v.then_body : std::vector<Activity>{});
            j["else"]          = SerializeActivityList(v.else_body ? *v.else_body : std::vector<Activity>{});
            j["delay_after_ms"]= v.delay_ms;

        } else if constexpr (std::is_same_v<T, SwitchActivity>) {
            j["type"]          = "switch";
            j["name"]          = v.name;
            j["var_name"]      = v.var_name;
            json cases = json::array();
            for (auto& sc : v.cases) {
                cases.push_back({
                    {"value", sc.value},
                    {"body",  SerializeActivityList(sc.body ? *sc.body : std::vector<Activity>{})}
                });
            }
            j["cases"]         = cases;
            j["default"]       = SerializeActivityList(v.default_body ? *v.default_body : std::vector<Activity>{});
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
        // Legacy type — deserialize as-is
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

    } else if (type == "pixel_range_check") {
        PixelRangeCheckActivity v;
        v.name              = j.value("name", "");
        v.pos_mode          = StrToPosMode(j.value("position_mode", "absolute"));
        v.x1 = j.value("x1", 0); v.y1 = j.value("y1", 0);
        v.x2 = j.value("x2", 0); v.y2 = j.value("y2", 0);
        v.sample_w          = j.value("sample_w", 0);
        v.sample_h          = j.value("sample_h", 0);
        v.sample            = Base64ToSample(j.value("sample_b64", ""));
        if ((int)v.sample.size() != v.sample_w * v.sample_h) {
            v.sample.clear(); v.sample_w = v.sample_h = 0;
        }
        v.tolerance         = j.value("tolerance", 10);
        v.match_percent     = j.value("match_percent", 95);
        v.retry_interval_ms = j.value("retry_interval_ms", 500);
        v.retry_timeout_ms  = j.value("retry_timeout_ms", 0);
        v.match_body    = std::make_shared<std::vector<Activity>>(
                              DeserializeActivityList(j.value("match_body",    json::array())));
        v.no_match_body = std::make_shared<std::vector<Activity>>(
                              DeserializeActivityList(j.value("no_match_body", json::array())));
        v.delay_ms          = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "run_workflow") {
        RunWorkflowActivity v;
        v.workflow_id = j.value("workflow_id", "");
        v.delay_ms    = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "system_action") {
        SystemActionActivity v;
        v.action   = StrToSystemAction(j.value("action", "shutdown"));
        v.force    = j.value("force", false);
        v.delay_ms = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "run_activity") {
        RunActivityActivity v;
        v.activity_id = j.value("activity_id", "");
        v.delay_ms    = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "set_variable") {
        SetVariableActivity v;
        v.name     = j.value("name", "");
        v.value    = j.value("value", "");
        v.op       = StrToVarOp(j.value("op", "set"));
        v.step     = j.value("step", 1);
        v.rand_min = j.value("rand_min", 0);
        v.rand_max = j.value("rand_max", 100);
        v.delay_ms = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "loop") {
        LoopActivity v;
        v.name     = j.value("name", "");
        v.count    = j.value("count", 1);
        v.iter_var = j.value("iter_var", "");
        v.body     = std::make_shared<std::vector<Activity>>(
                         DeserializeActivityList(j.value("body", json::array())));
        v.delay_ms = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "if") {
        IfActivity v;
        v.name      = j.value("name", "");
        v.cond      = DeserializeCondition(j.value("condition", json::object()));
        v.then_body = std::make_shared<std::vector<Activity>>(
                          DeserializeActivityList(j.value("then", json::array())));
        v.else_body = std::make_shared<std::vector<Activity>>(
                          DeserializeActivityList(j.value("else", json::array())));
        v.delay_ms  = j.value("delay_after_ms", 0);
        a.data = v;

    } else if (type == "switch") {
        SwitchActivity v;
        v.name     = j.value("name", "");
        v.var_name = j.value("var_name", "");
        if (j.contains("cases") && j["cases"].is_array()) {
            for (auto& sc : j["cases"]) {
                SwitchCase c;
                c.value = sc.value("value", "");
                c.body  = std::make_shared<std::vector<Activity>>(
                              DeserializeActivityList(sc.value("body", json::array())));
                v.cases.push_back(std::move(c));
            }
        }
        v.default_body = std::make_shared<std::vector<Activity>>(
                             DeserializeActivityList(j.value("default", json::array())));
        v.delay_ms = j.value("delay_after_ms", 0);
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
    j["smart_detection"]                  = w.smart_detection;
    j["smart_detection_idle_ms"]          = w.smart_detection_idle_ms;
    j["smart_detection_start_delay_ms"]   = w.smart_detection_start_delay_ms;

    json win;
    switch (w.window.type) {
        case WindowTarget::Type::ByTitle:  win["type"] = "title";  win["title"]      = w.window.title;      break;
        case WindowTarget::Type::ByClass:  win["type"] = "class";  win["class_name"] = w.window.class_name; break;
        case WindowTarget::Type::ByHandle: win["type"] = "handle"; win["handle"]     = w.window.handle;     break;
        default:                           win["type"] = "global"; break;
    }
    j["window"] = win;

    json trig;
    switch (w.trigger.type) {
        case StartTrigger::Type::Schedule:
            trig["type"]      = "schedule";
            trig["cron_expr"] = w.trigger.cron_expr;
            break;
        case StartTrigger::Type::Pixel:
            trig["type"]                = "pixel";
            trig["pixel_pos_mode"]      = PosModeToStr(w.trigger.pixel_pos_mode);
            trig["pixel_x1"]            = w.trigger.pixel_x1;
            trig["pixel_y1"]            = w.trigger.pixel_y1;
            trig["pixel_x2"]            = w.trigger.pixel_x2;
            trig["pixel_y2"]            = w.trigger.pixel_y2;
            trig["pixel_sample_w"]      = w.trigger.pixel_sample_w;
            trig["pixel_sample_h"]      = w.trigger.pixel_sample_h;
            trig["pixel_sample_b64"]    = SampleToBase64(w.trigger.pixel_sample);
            trig["pixel_tolerance"]     = w.trigger.pixel_tolerance;
            trig["pixel_match_percent"] = w.trigger.pixel_match_percent;
            trig["pixel_poll_ms"]       = w.trigger.pixel_poll_ms;
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
    w.smart_detection                    = j.value("smart_detection", true);
    w.smart_detection_idle_ms            = j.value("smart_detection_idle_ms", 2000);
    w.smart_detection_start_delay_ms     = j.value("smart_detection_start_delay_ms", 1000);

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
            w.trigger.type = StartTrigger::Type::Pixel;
            // New range-based fields
            w.trigger.pixel_pos_mode      = StrToPosMode(trig.value("pixel_pos_mode", "absolute"));
            w.trigger.pixel_x1            = trig.value("pixel_x1", 0);
            w.trigger.pixel_y1            = trig.value("pixel_y1", 0);
            w.trigger.pixel_x2            = trig.value("pixel_x2", 0);
            w.trigger.pixel_y2            = trig.value("pixel_y2", 0);
            w.trigger.pixel_sample_w      = trig.value("pixel_sample_w", 0);
            w.trigger.pixel_sample_h      = trig.value("pixel_sample_h", 0);
            w.trigger.pixel_sample        = Base64ToSample(trig.value("pixel_sample_b64", ""));
            if ((int)w.trigger.pixel_sample.size() != w.trigger.pixel_sample_w * w.trigger.pixel_sample_h)
            { w.trigger.pixel_sample.clear(); w.trigger.pixel_sample_w = w.trigger.pixel_sample_h = 0; }
            w.trigger.pixel_tolerance     = trig.value("pixel_tolerance", 10);
            w.trigger.pixel_match_percent = trig.value("pixel_match_percent", 95);
            w.trigger.pixel_poll_ms       = trig.value("pixel_poll_ms", 500);
            // Backwards compat: old single-pixel fields → treat as 1x1 sample
            if (w.trigger.pixel_sample.empty() && trig.contains("pixel_x") && trig.contains("pixel_color")) {
                w.trigger.pixel_x1 = trig.value("pixel_x", 0);
                w.trigger.pixel_y1 = trig.value("pixel_y", 0);
                w.trigger.pixel_x2 = w.trigger.pixel_x1;
                w.trigger.pixel_y2 = w.trigger.pixel_y1;
                w.trigger.pixel_sample_w = 1;
                w.trigger.pixel_sample_h = 1;
                w.trigger.pixel_sample   = { HexToColor(trig.value("pixel_color", "#000000")) };
                w.trigger.pixel_tolerance = trig.value("pixel_tolerance", 10);
            }
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
