#include "app_ui.hpp"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <sstream>
#include <random>
#include <iomanip>

static std::string GenId() {
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> d;
    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << d(rng) << "-" << std::setw(4) << (d(rng)&0xFFFF)
       << "-" << std::setw(4) << (d(rng)&0xFFFF)
       << "-" << std::setw(4) << (d(rng)&0xFFFF)
       << "-" << std::setw(8) << d(rng) << std::setw(4) << (d(rng)&0xFFFF);
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────

AppUI::AppUI() {
    m_wfList.OnAdd       = [this]() { AddWorkflow(); };
    m_wfList.OnDuplicate = [this](const std::string& id) { DuplicateWorkflow(id); };
    m_wfList.OnDelete    = [this](const std::string& id) { DeleteWorkflow(id); };
    m_wfList.OnSelect    = [this](const std::string& id) { m_selectedId = id; };

    m_actEditor.OnChanged = [this]() {
        // Re-sync engine workflows after edit
        m_engine.SetWorkflows(m_config.workflows);
        m_dirty = true;
    };

    m_winPicker.OnPicked = [this](const WindowInfo& info) {
        auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                               [&](auto& w){ return w.id == m_selectedId; });
        if (it == m_config.workflows.end()) return;
        it->window.type       = WindowTarget::Type::ByTitle;
        it->window.title      = info.title;
        it->window.class_name = info.class_name;
        it->window.handle     = info.handle;
        m_dirty = true;
    };

    m_recOverlay.OnFinished = [this](std::vector<Activity> acts) {
        auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                               [&](auto& w){ return w.id == m_selectedId; });
        if (it == m_config.workflows.end()) return;
        for (auto& a : acts) it->activities.push_back(std::move(a));
        m_engine.SetWorkflows(m_config.workflows);
        m_dirty = true;
    };
}

void AppUI::Init(const std::string& config_path) {
    m_configPath = config_path;
    try {
        m_config = ConfigManager::Load(config_path);
    } catch (...) {
        // Start with empty config if file missing
    }

    m_engine.Init();
    m_engine.SetWorkflows(m_config.workflows);
    strncpy(m_hotkeyBuf, m_config.global_hotkey.c_str(), sizeof(m_hotkeyBuf)-1);
    m_engine.SetGlobalHotkey(m_config.global_hotkey);

    m_engine.SetTriggerCallback([this](const std::string& id) {
        m_engine.StartWorkflow(id);
    });

    m_triggers.Start(m_config.workflows,
                     nullptr, // pixel checker wired through engine internals
                     [this](const std::string& id) { m_engine.StartWorkflow(id); });

    if (!m_config.workflows.empty()) m_selectedId = m_config.workflows[0].id;
}

void AppUI::Shutdown() {
    m_triggers.Stop();
    m_engine.Shutdown();
    if (m_dirty) SaveConfig();
}

void AppUI::SaveConfig() {
    try {
        ConfigManager::Save(m_config, m_configPath);
        m_dirty = false;
    } catch (...) {}
}

void AppUI::LoadConfig(const std::string& path) {
    try {
        m_engine.StopAll();
        m_config     = ConfigManager::Load(path);
        m_configPath = path;
        m_engine.SetWorkflows(m_config.workflows);
        m_dirty = false;
        if (!m_config.workflows.empty()) m_selectedId = m_config.workflows[0].id;
    } catch (...) {}
}

// ─────────────────────────────────────────────────────────────────────────────

void AppUI::Render() {
    m_engine.PollHotkeys();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags mainFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::Begin("##main", nullptr, mainFlags);

    RenderMenuBar();
    RenderTopBar();

    float leftW = 200.0f;
    ImGui::BeginChild("##left", ImVec2(leftW, 0), true);
    m_wfList.Render(
        m_config.workflows,
        [this](const std::string& id){ return m_engine.IsRunning(id); },
        m_selectedId);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##right", ImVec2(0, 0), true);
    auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                           [&](auto& w){ return w.id == m_selectedId; });
    if (it != m_config.workflows.end()) {
        RenderWorkflowPanel(*it);
    } else {
        ImGui::TextDisabled("Select a workflow on the left.");
    }
    ImGui::EndChild();

    ImGui::End();

    if (m_trigPickActive) RenderTriggerPickOverlay();
    m_recOverlay.Render(m_recorder);
}

void AppUI::RenderMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Config", "Ctrl+S")) SaveConfig();
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void AppUI::RenderTopBar() {
    bool anyRunning = m_engine.AnyRunning();

    if (ImGui::Button(">> Start All")) m_engine.StartAll();
    ImGui::SameLine();
    if (ImGui::Button("[Stop All]"))  m_engine.StopAll();
    ImGui::SameLine();
    ImGui::Separator(); ImGui::SameLine();

    // Global hotkey config
    ImGui::Text("Hotkey:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    if (ImGui::InputText("##hk", m_hotkeyBuf, sizeof(m_hotkeyBuf),
                          ImGuiInputTextFlags_EnterReturnsTrue)) {
        m_config.global_hotkey = m_hotkeyBuf;
        m_engine.SetGlobalHotkey(m_config.global_hotkey);
        m_dirty = true;
    }
    ImGui::SameLine();

    if (m_dirty) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.7f,0,1));
        ImGui::Text("*");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unsaved changes");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save")) SaveConfig();
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 120);
    ImGui::PushStyleColor(ImGuiCol_Text,
        anyRunning ? ImVec4(0.2f,0.9f,0.2f,1.f) : ImVec4(0.5f,0.5f,0.5f,1.f));
    ImGui::Text("Status: %s", anyRunning ? "RUNNING" : "IDLE");
    ImGui::PopStyleColor();

    ImGui::Separator();
}

void AppUI::RenderWorkflowPanel(Workflow& wf) {
    // Header
    char nameBuf[128]{};
    strncpy(nameBuf, wf.name.c_str(), sizeof(nameBuf)-1);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("##wfname", nameBuf, sizeof(nameBuf)))
        { wf.name = nameBuf; m_dirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Enabled", &wf.enabled)) m_dirty = true;

    bool running = m_engine.IsRunning(wf.id);
    ImGui::SameLine();
    if (!running) {
        if (ImGui::Button(">> Start")) m_engine.StartWorkflow(wf.id);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f,0.1f,0.1f,1.f));
        if (ImGui::Button("[Stop]"))  m_engine.StopWorkflow(wf.id);
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // Window target
    RenderWindowTargetEditor(wf.window);
    ImGui::Separator();

    // Trigger
    RenderTriggerEditor(wf.trigger, wf.id);
    ImGui::Separator();

    // Timing
    if (ImGui::InputInt("Repeat interval (ms)", &wf.repeat_interval_ms))
        { wf.repeat_interval_ms = std::max(100, wf.repeat_interval_ms); m_dirty = true; }
    if (ImGui::InputInt("Repeat count (0=∞)", &wf.repeat_count))
        { wf.repeat_count = std::max(0, wf.repeat_count); m_dirty = true; }

    // Smart detection
    if (ImGui::Checkbox("Smart detection", &wf.smart_detection)) m_dirty = true;
    if (wf.smart_detection) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::InputInt("Idle ms##sd", &wf.smart_detection_idle_ms))
            { wf.smart_detection_idle_ms = std::max(100, wf.smart_detection_idle_ms); m_dirty = true; }
    }

    ImGui::Separator();

    // Record button
    if (!m_recorder.IsRecording()) {
        if (ImGui::Button("[Rec]")) {
            m_recorder.Start();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f,0.1f,0.1f,1.f));
        if (ImGui::Button("[Stop Rec]")) {
            m_recorder.Stop();
            m_recOverlay.TriggerReview(m_recorder);
        }
        ImGui::PopStyleColor();
    }

    ImGui::Separator();
    m_actEditor.Render(wf);
}

void AppUI::RenderWindowTargetEditor(WindowTarget& wt) {
    static const char* kWTypes[] = {"Global","By Title","By Class","By Handle"};
    int typeIdx = (int)wt.type;
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Window##wt", &typeIdx, kWTypes, 4)) {
        wt.type = (WindowTarget::Type)typeIdx;
        m_dirty = true;
    }
    ImGui::SameLine();
    m_winPicker.Render(m_engine.WindowFinder(), wt);

    if (wt.type == WindowTarget::Type::ByTitle) {
        static char buf[256]{};
        strncpy(buf, wt.title.c_str(), sizeof(buf)-1);
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Title##wt", buf, sizeof(buf)))
            { wt.title = buf; m_dirty = true; }

    } else if (wt.type == WindowTarget::Type::ByClass) {
        static char buf[256]{};
        strncpy(buf, wt.class_name.c_str(), sizeof(buf)-1);
        ImGui::SetNextItemWidth(300);
        if (ImGui::InputText("Class##wt", buf, sizeof(buf)))
            { wt.class_name = buf; m_dirty = true; }

    } else if (wt.type == WindowTarget::Type::ByHandle) {
        uint64_t h = wt.handle;
        ImGui::Text("Handle: %llu", (unsigned long long)h);
    }
}

void AppUI::RenderTriggerPickOverlay() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 pos = io.MousePos;
    pos.x += 16.f; pos.y += 16.f;
    if (pos.x + 220 > io.DisplaySize.x) pos.x = io.DisplaySize.x - 220;
    if (pos.y + 70  > io.DisplaySize.y) pos.y = io.DisplaySize.y - 70;

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoResize   |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("##trigpickoverlay", nullptr, flags)) {
        int gx = 0, gy = 0;
        SDL_GetGlobalMouseState(&gx, &gy);
        ImGui::TextColored(ImVec4(1,0.9f,0.3f,1), "Pick pixel pos: %d, %d", gx, gy);
        ImGui::TextDisabled("Enter = confirm   Esc = cancel");

        if (ImGui::Button("Capture##trigpick") ||
            ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            if (m_trigPickTarget) {
                m_trigPickTarget->pixel_x = gx;
                m_trigPickTarget->pixel_y = gy;
                m_dirty = true;
            }
            m_trigPickActive = false;
            m_trigPickTarget = nullptr;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_trigPickActive = false;
            m_trigPickTarget = nullptr;
        }
    }
    ImGui::End();
}

void AppUI::RenderTriggerEditor(StartTrigger& trig, const std::string& wfId) {
    static const char* kTTypes[] = {"Manual","Schedule","Pixel color"};
    int typeIdx = (int)trig.type;
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Trigger##tr", &typeIdx, kTTypes, 3)) {
        trig.type = (StartTrigger::Type)typeIdx;
        m_dirty = true;
    }

    if (trig.type == StartTrigger::Type::Schedule) {
        // Track per-workflow to reinitialize buffers on switch
        static std::string s_wfId;
        static char s_hh[16] = "*";
        static char s_mm[16] = "*";

        // Reinit text fields when switching workflows
        if (wfId != s_wfId) {
            s_wfId = wfId;
            std::string f[5] = {"*","*","*","*","*"};
            std::istringstream ss(trig.cron_expr);
            for (int i = 0; i < 5; ++i) if (!(ss >> f[i])) f[i] = "*";
            strncpy(s_mm, f[0].c_str(), sizeof(s_mm)-1);
            strncpy(s_hh, f[1].c_str(), sizeof(s_hh)-1);
        }

        // Parse cron_expr for combo fields (dropdowns always sync from source)
        std::string f[5] = {"*","*","*","*","*"};
        {
            std::istringstream ss(trig.cron_expr);
            for (int i = 0; i < 5; ++i) if (!(ss >> f[i])) f[i] = "*";
        }

        bool changed = false;

        // ── Row 1: HH and MM text inputs ─────────────────────────────────────
        ImGui::Text("HH:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(55);
        if (ImGui::InputText("##hh_tr", s_hh, sizeof(s_hh))) {
            f[1] = s_hh; changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hour (0-23, * = any, */2 = every 2 hours)");

        ImGui::SameLine(0, 12);
        ImGui::Text("MM:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(55);
        if (ImGui::InputText("##mm_tr", s_mm, sizeof(s_mm))) {
            f[0] = s_mm; changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minute (0-59, * = any, */5 = every 5 min)");

        // ── Row 2: Day / Month / Weekday combos ──────────────────────────────
        static const char* kDays[32] = {
            "*","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15",
            "16","17","18","19","20","21","22","23","24","25","26","27","28","29","30","31"
        };
        static const char* kMonths[13] = {
            "*","Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
        };
        static const char* kDOW[8] = {
            "*","Sun","Mon","Tue","Wed","Thu","Fri","Sat"
        };

        int dayIdx = 0;
        for (int i = 1; i <= 31; ++i) if (f[2] == kDays[i]) { dayIdx = i; break; }

        int monIdx = 0;
        if (f[3] != "*") { try { int n=std::stoi(f[3]); if(n>=1&&n<=12) monIdx=n; } catch(...){} }

        int dowIdx = 0;
        if (f[4] != "*") { try { int n=std::stoi(f[4]); if(n>=0&&n<=6) dowIdx=n+1; } catch(...){} }

        ImGui::Text("Day:");  ImGui::SameLine();
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##day_tr", &dayIdx, kDays, 32))
            { f[2] = kDays[dayIdx]; changed = true; }

        ImGui::SameLine(0, 12);
        ImGui::Text("Mon:");  ImGui::SameLine();
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##mon_tr", &monIdx, kMonths, 13))
            { f[3] = monIdx==0 ? "*" : std::to_string(monIdx); changed = true; }

        ImGui::SameLine(0, 12);
        ImGui::Text("DOW:");  ImGui::SameLine();
        ImGui::SetNextItemWidth(58);
        if (ImGui::Combo("##dow_tr", &dowIdx, kDOW, 8))
            { f[4] = dowIdx==0 ? "*" : std::to_string(dowIdx-1); changed = true; }

        if (changed) {
            trig.cron_expr = std::string(f[0])+" "+f[1]+" "+f[2]+" "+f[3]+" "+f[4];
            m_dirty = true;
        }

        ImGui::TextDisabled("cron: %s", trig.cron_expr.c_str());

    } else if (trig.type == StartTrigger::Type::Pixel) {
        ImGui::InputInt("Pixel X##tr", &trig.pixel_x);
        ImGui::InputInt("Pixel Y##tr", &trig.pixel_y);
        if (ImGui::Button("Pick pixel position##tr")) {
            m_trigPickActive = true;
            m_trigPickTarget = &trig;
        }

        float col[3] = {
            ((trig.pixel_color>>16)&0xFF)/255.f,
            ((trig.pixel_color>> 8)&0xFF)/255.f,
            ( trig.pixel_color     &0xFF)/255.f
        };
        if (ImGui::ColorEdit3("Color##tr", col)) {
            trig.pixel_color = ((uint32_t)(col[0]*255)<<16) |
                               ((uint32_t)(col[1]*255)<< 8) |
                                (uint32_t)(col[2]*255);
            m_dirty = true;
        }
        ImGui::InputInt("Tolerance##tr", &trig.pixel_tolerance);
        ImGui::InputInt("Poll interval (ms)##tr", &trig.pixel_poll_ms);
    }
}

void AppUI::AddWorkflow() {
    Workflow wf;
    wf.id   = GenId();
    wf.name = "New Workflow";
    m_config.workflows.push_back(wf);
    m_selectedId = wf.id;
    m_engine.SetWorkflows(m_config.workflows);
    m_dirty = true;
}

void AppUI::DuplicateWorkflow(const std::string& id) {
    auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                           [&](auto& w){ return w.id == id; });
    if (it == m_config.workflows.end()) return;
    Workflow copy = *it;
    copy.id   = GenId();
    copy.name = copy.name + " (copy)";
    m_config.workflows.insert(it + 1, copy);
    m_selectedId = copy.id;
    m_engine.SetWorkflows(m_config.workflows);
    m_dirty = true;
}

void AppUI::DeleteWorkflow(const std::string& id) {
    m_engine.StopWorkflow(id);
    auto it = std::find_if(m_config.workflows.begin(), m_config.workflows.end(),
                           [&](auto& w){ return w.id == id; });
    if (it == m_config.workflows.end()) return;
    m_config.workflows.erase(it);
    m_selectedId = m_config.workflows.empty() ? "" : m_config.workflows[0].id;
    m_engine.SetWorkflows(m_config.workflows);
    m_dirty = true;
}
