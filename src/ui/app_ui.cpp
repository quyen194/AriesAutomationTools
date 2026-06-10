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
    RenderTriggerEditor(wf.trigger);
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
        if (ImGui::Button("[Stop Rec]")) m_recorder.Stop();
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

void AppUI::RenderTriggerEditor(StartTrigger& trig) {
    static const char* kTTypes[] = {"Manual","Schedule (cron)","Pixel color"};
    int typeIdx = (int)trig.type;
    ImGui::SetNextItemWidth(160);
    if (ImGui::Combo("Trigger##tr", &typeIdx, kTTypes, 3)) {
        trig.type = (StartTrigger::Type)typeIdx;
        m_dirty = true;
    }

    if (trig.type == StartTrigger::Type::Schedule) {
        static char cronBuf[64]{};
        strncpy(cronBuf, trig.cron_expr.c_str(), sizeof(cronBuf)-1);
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("Cron##tr", cronBuf, sizeof(cronBuf)))
            { trig.cron_expr = cronBuf; m_dirty = true; }
        ImGui::SameLine();
        ImGui::TextDisabled("e.g. */5 * * * *");

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
