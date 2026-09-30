#pragma once
#include "workflow.hpp"
#include <string>
#include <vector>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <type_traits>
#include <algorithm>

// ── Variable-bindable fields ──────────────────────────────────────────────────
// Calls f(key, label, int&) or f(key, label, std::string&) for every field of the
// activity that may be bound to a runtime variable via Activity::var_bind.
// `key` is the stable identifier stored in config; `label` is for messages.
template <class F>
void ForEachBindableField(ActivityData& data, F&& f) {
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, MouseMoveActivity>) {
            f("x", "X", v.x); f("y", "Y", v.y);
            f("smooth_duration_ms", "Smooth duration", v.smooth_duration_ms);
            f("delay_ms", "Delay after", v.delay_ms);
            f("delay_rand_ms", "Random range", v.delay_rand_ms);
        } else if constexpr (std::is_same_v<T, MouseClickActivity>) {
            f("x", "X", v.x); f("y", "Y", v.y);
            f("delay_ms", "Delay after", v.delay_ms);
            f("delay_rand_ms", "Random range", v.delay_rand_ms);
        } else if constexpr (std::is_same_v<T, MouseDragActivity>) {
            f("from_x", "From X", v.from_x); f("from_y", "From Y", v.from_y);
            f("to_x", "To X", v.to_x);       f("to_y", "To Y", v.to_y);
            f("duration_ms", "Duration", v.duration_ms);
            f("delay_ms", "Delay after", v.delay_ms);
        } else if constexpr (std::is_same_v<T, MouseScrollActivity>) {
            f("x", "X", v.x); f("y", "Y", v.y);
            f("delta_x", "Delta X", v.delta_x); f("delta_y", "Delta Y", v.delta_y);
            f("delay_ms", "Delay after", v.delay_ms);
        } else if constexpr (std::is_same_v<T, KeyPressActivity>) {
            f("key", "Key", v.key);
            f("delay_ms", "Delay after", v.delay_ms);
            f("delay_rand_ms", "Random range", v.delay_rand_ms);
        } else if constexpr (std::is_same_v<T, TypeStringActivity>) {
            f("text", "Text", v.text);
            f("delay_between_chars_ms", "Char delay", v.delay_between_chars_ms);
            f("delay_ms", "Delay after", v.delay_ms);
        } else if constexpr (std::is_same_v<T, WaitActivity>) {
            f("duration_ms", "Duration", v.duration_ms);
            f("random_range_ms", "Random range", v.random_range_ms);
        } else if constexpr (std::is_same_v<T, PixelRangeCheckActivity>) {
            f("x1", "X1", v.x1); f("y1", "Y1", v.y1);
            f("x2", "X2", v.x2); f("y2", "Y2", v.y2);
            f("tolerance", "Tolerance", v.tolerance);
            f("match_percent", "Match percent", v.match_percent);
            f("retry_interval_ms", "Retry interval", v.retry_interval_ms);
            f("retry_timeout_ms", "Retry timeout", v.retry_timeout_ms);
            f("delay_ms", "Delay after", v.delay_ms);
        } else if constexpr (std::is_same_v<T, SetVariableActivity>) {
            f("value", "Value", v.value);
            f("step", "Step", v.step);
            f("rand_min", "Min", v.rand_min);
            f("rand_max", "Max", v.rand_max);
            f("delay_ms", "Delay after", v.delay_ms);
        } else if constexpr (std::is_same_v<T, LoopActivity>) {
            f("count", "Count", v.count);
            f("delay_ms", "Delay after", v.delay_ms);
        } else if constexpr (std::is_same_v<T, PixelCheckActivity>) {
            // legacy type: not bindable
        } else {
            f("delay_ms", "Delay after", v.delay_ms);
        }
    }, data);
}

// ── Strict integer parsing ────────────────────────────────────────────────────
// Accepts an optional sign followed by digits only (no spaces, no decimals).
inline bool ParseStrictInt(const std::string& s, int& out) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size()) return false;
    for (size_t k = i; k < s.size(); ++k)
        if (s[k] < '0' || s[k] > '9') return false;
    errno = 0;
    long long v = std::strtoll(s.c_str(), nullptr, 10);
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX) return false;
    out = (int)v;
    return true;
}

// ── Variable name discovery ───────────────────────────────────────────────────
// Names of every variable a workflow defines (Set Variable targets, Loop
// iteration vars, Get Mouse Position outputs), in order of first appearance.
// Activities whose id equals `excludeId` are skipped (their bodies are not).
inline void CollectVariableNames(const std::vector<Activity>& acts,
                                 std::vector<std::string>& out,
                                 const std::string& excludeId = {}) {
    auto add = [&](const std::string& n) {
        if (!n.empty() && std::find(out.begin(), out.end(), n) == out.end())
            out.push_back(n);
    };
    for (const auto& a : acts) {
        bool skip = !excludeId.empty() && a.id == excludeId;
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, SetVariableActivity>) {
                if (!skip) add(v.name);
            } else if constexpr (std::is_same_v<T, GetMousePositionActivity>) {
                if (!skip) { add(v.x_var); add(v.y_var); }
            } else if constexpr (std::is_same_v<T, LoopActivity>) {
                if (!skip) add(v.iter_var);
                if (v.body) CollectVariableNames(*v.body, out, excludeId);
            } else if constexpr (std::is_same_v<T, IfActivity>) {
                if (v.then_body) CollectVariableNames(*v.then_body, out, excludeId);
                if (v.else_body) CollectVariableNames(*v.else_body, out, excludeId);
            } else if constexpr (std::is_same_v<T, SwitchActivity>) {
                for (auto& sc : v.cases) if (sc.body) CollectVariableNames(*sc.body, out, excludeId);
                if (v.default_body) CollectVariableNames(*v.default_body, out, excludeId);
            } else if constexpr (std::is_same_v<T, PixelRangeCheckActivity>) {
                if (v.match_body)    CollectVariableNames(*v.match_body, out, excludeId);
                if (v.no_match_body) CollectVariableNames(*v.no_match_body, out, excludeId);
            }
        }, a.data);
    }
}
