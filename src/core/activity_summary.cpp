#include "activity_summary.hpp"
#include <cstdio>

// ── Short summary for list row ────────────────────────────────────────────────
// "$name" for a variable reference, the plain text for a literal
static std::string VarOrLit(const std::string& s, bool isVar) {
    return isVar ? "$" + s : s;
}

static std::string ActivitySummaryCore(const Activity& a) {
    return std::visit([&a](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        char buf[160]{};
        if constexpr (std::is_same_v<T,MouseMoveActivity>)
            snprintf(buf,sizeof(buf),"mouse_move %s (%d,%d)%s +%dms",
                v.pos_mode==PositionMode::Absolute?"abs":"rel",v.x,v.y,
                v.smooth_move?" smooth":"",v.delay_ms);
        else if constexpr (std::is_same_v<T,MouseClickActivity>)
            snprintf(buf,sizeof(buf),"mouse_click %s %s (%d,%d) +%dms",
                v.button==MouseButton::Left?"left":v.button==MouseButton::Right?"right":"mid",
                v.pos_mode==PositionMode::Absolute?"abs":"rel",v.x,v.y,v.delay_ms);
        else if constexpr (std::is_same_v<T,MouseDragActivity>)
            snprintf(buf,sizeof(buf),"mouse_drag (%d,%d)->(%d,%d) %dms",
                v.from_x,v.from_y,v.to_x,v.to_y,v.duration_ms);
        else if constexpr (std::is_same_v<T,MouseScrollActivity>)
            snprintf(buf,sizeof(buf),"mouse_scroll dy=%d +%dms",v.delta_y,v.delay_ms);
        else if constexpr (std::is_same_v<T,KeyPressActivity>) {
            std::string mods;
            for (auto& m : v.modifiers) mods += m + "+";
            snprintf(buf,sizeof(buf),"key_press %s%s +%dms",mods.c_str(),v.key.c_str(),v.delay_ms);
        } else if constexpr (std::is_same_v<T,TypeStringActivity>)
            snprintf(buf,sizeof(buf),"type_string \"%s\" +%dms",v.text.substr(0,20).c_str(),v.delay_ms);
        else if constexpr (std::is_same_v<T,WaitActivity>)
            snprintf(buf,sizeof(buf),"wait %dms +/-%dms",v.duration_ms,v.random_range_ms);
        else if constexpr (std::is_same_v<T,PixelCheckActivity>)
            snprintf(buf,sizeof(buf),"pixel_check #%06X (%d,%d)",v.color_rgb,v.x,v.y);
        else if constexpr (std::is_same_v<T,PixelRangeCheckActivity>) {
            int nBody = (v.match_body ? (int)v.match_body->size() : 0)
                      + (v.no_match_body ? (int)v.no_match_body->size() : 0);
            snprintf(buf,sizeof(buf),"%s  (%d,%d)-(%d,%d) %s %d%% [%d]",
                v.name.empty() ? "pixel_range" : v.name.c_str(),
                v.x1,v.y1,v.x2,v.y2,
                v.sample.empty()?"no sample":"sampled",v.match_percent,nBody);
        } else if constexpr (std::is_same_v<T,RunWorkflowActivity>)
            snprintf(buf,sizeof(buf),"run_workflow %s",v.workflow_id.c_str());
        else if constexpr (std::is_same_v<T,SystemActionActivity>) {
            static const char* names[] = {"shutdown","restart","sleep","hibernate","lock","logout"};
            int idx = (int)v.action;
            snprintf(buf,sizeof(buf),"system_action %s%s",
                (idx>=0&&idx<6)?names[idx]:"?",v.force?" (force)":"");
        } else if constexpr (std::is_same_v<T,RunActivityActivity>)
            snprintf(buf,sizeof(buf),"run_activity -> %.24s",v.activity_id.c_str());
        else if constexpr (std::is_same_v<T,SetVariableActivity>)
            snprintf(buf,sizeof(buf),"set %s %s %s",
                v.name.c_str(),
                v.op==VarOp::Set?"=":v.op==VarOp::Increment?"+=":
                v.op==VarOp::Decrement?"-=":"rand",
                v.op==VarOp::Set ? (v.value.empty() && !a.var_bind.count("value")
                                        ? "(clear)" : v.value.c_str())
                : v.op==VarOp::Random ? "" : std::to_string(v.step).c_str());
        else if constexpr (std::is_same_v<T,LoopActivity>) {
            int n = v.body ? (int)v.body->size() : 0;
            snprintf(buf,sizeof(buf),"%s  x%d  [%d steps]",
                v.name.empty()?"loop":v.name.c_str(), v.count, n);
        } else if constexpr (std::is_same_v<T,IfActivity>) {
            snprintf(buf,sizeof(buf),"%s  if %s %s %s",
                v.name.empty()?"if":v.name.c_str(),
                VarOrLit(v.cond.lhs, v.cond.lhs_is_var).c_str(),
                v.cond.op==ConditionOp::Eq?"==":v.cond.op==ConditionOp::NEq?"!=":
                v.cond.op==ConditionOp::Gt?">":v.cond.op==ConditionOp::Lt?"<":
                v.cond.op==ConditionOp::GtEq?">=":v.cond.op==ConditionOp::LtEq?"<=":"contains",
                VarOrLit(v.cond.rhs, v.cond.rhs_is_var).c_str());
        } else if constexpr (std::is_same_v<T,SwitchActivity>) {
            snprintf(buf,sizeof(buf),"%s  switch %s  [%d cases]",
                v.name.empty()?"switch":v.name.c_str(),
                VarOrLit(v.var_name, v.var_is_var).c_str(),(int)v.cases.size());
        } else if constexpr (std::is_same_v<T,JumpActivity>) {
            snprintf(buf,sizeof(buf),"jump -> %.32s",
                v.target_id.empty() ? "(none)" : v.target_id.substr(0,24).c_str());
        } else if constexpr (std::is_same_v<T,GetMousePositionActivity>) {
            snprintf(buf,sizeof(buf),"get_mouse_position %s -> $%s, $%s",
                v.pos_mode==PositionMode::Absolute?"abs":"rel",
                v.x_var.empty()?"?":v.x_var.c_str(), v.y_var.empty()?"?":v.y_var.c_str());
        } else if constexpr (std::is_same_v<T,ClearVariablesActivity>) {
            snprintf(buf,sizeof(buf),"clear_variables +%dms", v.delay_ms);
        }
        return buf;
    }, a.data);
}

// Summary plus the variable bindings, e.g. "mouse_click ... {x=$px, y=$py}"
std::string ActivitySummary(const Activity& a) {
    std::string s = ActivitySummaryCore(a);
    if (a.var_bind.empty()) return s;
    std::string tag;
    for (auto& [key, var] : a.var_bind) {
        if (var.empty()) continue;
        tag += (tag.empty() ? "" : ", ") + key + "=$" + var;
    }
    return tag.empty() ? s : s + "  {" + tag + "}";
}
