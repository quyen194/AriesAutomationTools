#include "key_map.hpp"
#include <unordered_map>
#include <algorithm>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__APPLE__)
#  include <Carbon/Carbon.h>
#else
#  include <X11/keysym.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Build a bidirectional table: name ↔ platform keycode
// ─────────────────────────────────────────────────────────────────────────────

struct KeyEntry { const char* name; uint32_t code; };

static const KeyEntry kTable[] = {
#if defined(_WIN32)
    // Modifiers
    {"ctrl",        VK_CONTROL}, {"lctrl",   VK_LCONTROL}, {"rctrl",   VK_RCONTROL},
    {"shift",       VK_SHIFT},   {"lshift",  VK_LSHIFT},   {"rshift",  VK_RSHIFT},
    {"alt",         VK_MENU},    {"lalt",    VK_LMENU},    {"ralt",    VK_RMENU},
    {"win",         VK_LWIN},    {"rwin",    VK_RWIN},
    // Editing
    {"backspace",   VK_BACK},    {"tab",     VK_TAB},      {"enter",   VK_RETURN},
    {"escape",      VK_ESCAPE},  {"space",   VK_SPACE},    {"delete",  VK_DELETE},
    {"insert",      VK_INSERT},  {"home",    VK_HOME},     {"end",     VK_END},
    {"pageup",      VK_PRIOR},   {"pagedown",VK_NEXT},
    // Arrows
    {"left",        VK_LEFT},    {"right",   VK_RIGHT},
    {"up",          VK_UP},      {"down",    VK_DOWN},
    // Function keys
    {"f1",VK_F1},{"f2",VK_F2},{"f3",VK_F3},{"f4",VK_F4},
    {"f5",VK_F5},{"f6",VK_F6},{"f7",VK_F7},{"f8",VK_F8},
    {"f9",VK_F9},{"f10",VK_F10},{"f11",VK_F11},{"f12",VK_F12},
    {"f13",VK_F13},{"f14",VK_F14},{"f15",VK_F15},{"f16",VK_F16},
    {"f17",VK_F17},{"f18",VK_F18},{"f19",VK_F19},{"f20",VK_F20},
    {"f21",VK_F21},{"f22",VK_F22},{"f23",VK_F23},{"f24",VK_F24},
    // Numpad
    {"num0",VK_NUMPAD0},{"num1",VK_NUMPAD1},{"num2",VK_NUMPAD2},
    {"num3",VK_NUMPAD3},{"num4",VK_NUMPAD4},{"num5",VK_NUMPAD5},
    {"num6",VK_NUMPAD6},{"num7",VK_NUMPAD7},{"num8",VK_NUMPAD8},
    {"num9",VK_NUMPAD9},{"numlock",VK_NUMLOCK},
    {"nummul",VK_MULTIPLY},{"numadd",VK_ADD},
    {"numsub",VK_SUBTRACT},{"numdiv",VK_DIVIDE},{"numdec",VK_DECIMAL},
    // Punctuation / misc
    {"capslock",    VK_CAPITAL}, {"printscreen",VK_SNAPSHOT},
    {"scrolllock",  VK_SCROLL},  {"pause",  VK_PAUSE},
    {"semicolon",   VK_OEM_1},   {"equals",  VK_OEM_PLUS},
    {"comma",       VK_OEM_COMMA},{"minus",  VK_OEM_MINUS},
    {"period",      VK_OEM_PERIOD},{"slash", VK_OEM_2},
    {"backtick",    VK_OEM_3},   {"lbracket",VK_OEM_4},
    {"backslash",   VK_OEM_5},   {"rbracket",VK_OEM_6},
    {"quote",       VK_OEM_7},
#elif defined(__APPLE__)
    {"ctrl",        kVK_Control},  {"lctrl",  kVK_Control},  {"rctrl",  kVK_RightControl},
    {"shift",       kVK_Shift},    {"lshift", kVK_Shift},    {"rshift", kVK_RightShift},
    {"alt",         kVK_Option},   {"lalt",   kVK_Option},   {"ralt",   kVK_RightOption},
    {"win",         kVK_Command},  {"rwin",   kVK_RightCommand},
    {"backspace",   kVK_Delete},   {"tab",    kVK_Tab},      {"enter",  kVK_Return},
    {"escape",      kVK_Escape},   {"space",  kVK_Space},    {"delete", kVK_ForwardDelete},
    {"insert",      0},
    {"home",        kVK_Home},     {"end",    kVK_End},
    {"pageup",      kVK_PageUp},   {"pagedown",kVK_PageDown},
    {"left",        kVK_LeftArrow},{"right",  kVK_RightArrow},
    {"up",          kVK_UpArrow},  {"down",   kVK_DownArrow},
    {"f1",kVK_F1},{"f2",kVK_F2},{"f3",kVK_F3},{"f4",kVK_F4},
    {"f5",kVK_F5},{"f6",kVK_F6},{"f7",kVK_F7},{"f8",kVK_F8},
    {"f9",kVK_F9},{"f10",kVK_F10},{"f11",kVK_F11},{"f12",kVK_F12},
    {"f13",kVK_F13},{"f14",kVK_F14},{"f15",kVK_F15},{"f16",kVK_F16},
    {"f17",kVK_F17},{"f18",kVK_F18},{"f19",kVK_F19},{"f20",kVK_F20},
    {"capslock",    kVK_CapsLock}, {"semicolon",kVK_ANSI_Semicolon},
    {"equals",      kVK_ANSI_Equal},{"comma",  kVK_ANSI_Comma},
    {"minus",       kVK_ANSI_Minus},{"period", kVK_ANSI_Period},
    {"slash",       kVK_ANSI_Slash},{"backtick",kVK_ANSI_Grave},
    {"lbracket",    kVK_ANSI_LeftBracket},{"rbracket",kVK_ANSI_RightBracket},
    {"backslash",   kVK_ANSI_Backslash},{"quote",kVK_ANSI_Quote},
#else  // Linux / X11
    {"ctrl",        XK_Control_L}, {"lctrl",  XK_Control_L}, {"rctrl",  XK_Control_R},
    {"shift",       XK_Shift_L},   {"lshift", XK_Shift_L},   {"rshift", XK_Shift_R},
    {"alt",         XK_Alt_L},     {"lalt",   XK_Alt_L},     {"ralt",   XK_Alt_R},
    {"win",         XK_Super_L},   {"rwin",   XK_Super_R},
    {"backspace",   XK_BackSpace}, {"tab",    XK_Tab},       {"enter",  XK_Return},
    {"escape",      XK_Escape},    {"space",  XK_space},     {"delete", XK_Delete},
    {"insert",      XK_Insert},
    {"home",        XK_Home},      {"end",    XK_End},
    {"pageup",      XK_Page_Up},   {"pagedown",XK_Page_Down},
    {"left",        XK_Left},      {"right",  XK_Right},
    {"up",          XK_Up},        {"down",   XK_Down},
    {"f1",XK_F1},{"f2",XK_F2},{"f3",XK_F3},{"f4",XK_F4},
    {"f5",XK_F5},{"f6",XK_F6},{"f7",XK_F7},{"f8",XK_F8},
    {"f9",XK_F9},{"f10",XK_F10},{"f11",XK_F11},{"f12",XK_F12},
    {"f13",XK_F13},{"f14",XK_F14},{"f15",XK_F15},{"f16",XK_F16},
    {"f17",XK_F17},{"f18",XK_F18},{"f19",XK_F19},{"f20",XK_F20},
    {"capslock",    XK_Caps_Lock}, {"scrolllock",XK_Scroll_Lock},
    {"printscreen", XK_Print},     {"pause",  XK_Pause},
    {"numlock",     XK_Num_Lock},
    {"num0",XK_KP_0},{"num1",XK_KP_1},{"num2",XK_KP_2},
    {"num3",XK_KP_3},{"num4",XK_KP_4},{"num5",XK_KP_5},
    {"num6",XK_KP_6},{"num7",XK_KP_7},{"num8",XK_KP_8},
    {"num9",XK_KP_9},
    {"nummul",XK_KP_Multiply},{"numadd",XK_KP_Add},
    {"numsub",XK_KP_Subtract},{"numdiv",XK_KP_Divide},{"numdec",XK_KP_Decimal},
    {"semicolon",   XK_semicolon}, {"equals",  XK_equal},
    {"comma",       XK_comma},     {"minus",   XK_minus},
    {"period",      XK_period},    {"slash",   XK_slash},
    {"backtick",    XK_grave},     {"lbracket",XK_bracketleft},
    {"backslash",   XK_backslash}, {"rbracket",XK_bracketright},
    {"quote",       XK_apostrophe},
#endif
};

// Single-character letter/digit entries are built at init time
static std::unordered_map<std::string, uint32_t> s_nameToCode;
static std::unordered_map<uint32_t, std::string> s_codeToName;

static void InitMaps() {
    static bool done = false;
    if (done) return;
    done = true;

    for (auto& e : kTable) {
        s_nameToCode[e.name] = e.code;
        if (s_codeToName.find(e.code) == s_codeToName.end())
            s_codeToName[e.code] = e.name;
    }

    // a-z
    for (char c = 'a'; c <= 'z'; ++c) {
        std::string name(1, c);
#if defined(_WIN32)
        uint32_t code = (uint32_t)std::toupper(c); // VK codes for letters are uppercase ASCII
#elif defined(__APPLE__)
        // macOS key codes for letters (ANSI layout)
        static const uint32_t alpha_codes[] = {
            kVK_ANSI_A,kVK_ANSI_B,kVK_ANSI_C,kVK_ANSI_D,kVK_ANSI_E,kVK_ANSI_F,
            kVK_ANSI_G,kVK_ANSI_H,kVK_ANSI_I,kVK_ANSI_J,kVK_ANSI_K,kVK_ANSI_L,
            kVK_ANSI_M,kVK_ANSI_N,kVK_ANSI_O,kVK_ANSI_P,kVK_ANSI_Q,kVK_ANSI_R,
            kVK_ANSI_S,kVK_ANSI_T,kVK_ANSI_U,kVK_ANSI_V,kVK_ANSI_W,kVK_ANSI_X,
            kVK_ANSI_Y,kVK_ANSI_Z
        };
        uint32_t code = alpha_codes[c - 'a'];
#else
        uint32_t code = (uint32_t)XStringToKeysym(name.c_str());
#endif
        s_nameToCode[name] = code;
        s_codeToName[code] = name;
    }

    // 0-9
    for (char c = '0'; c <= '9'; ++c) {
        std::string name(1, c);
#if defined(_WIN32)
        uint32_t code = (uint32_t)c; // VK codes for digits are ASCII values
#elif defined(__APPLE__)
        static const uint32_t digit_codes[] = {
            kVK_ANSI_0,kVK_ANSI_1,kVK_ANSI_2,kVK_ANSI_3,kVK_ANSI_4,
            kVK_ANSI_5,kVK_ANSI_6,kVK_ANSI_7,kVK_ANSI_8,kVK_ANSI_9
        };
        uint32_t code = digit_codes[c - '0'];
#else
        uint32_t code = (uint32_t)XStringToKeysym(name.c_str());
#endif
        s_nameToCode[name] = code;
        s_codeToName[code] = name;
    }
}

uint32_t KeyNameToNative(const std::string& name) {
    InitMaps();
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto it = s_nameToCode.find(lower);
    return it != s_nameToCode.end() ? it->second : 0;
}

std::string NativeToKeyName(uint32_t native_code) {
    InitMaps();
    auto it = s_codeToName.find(native_code);
    return it != s_codeToName.end() ? it->second : "";
}
