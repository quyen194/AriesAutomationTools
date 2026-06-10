#pragma once
#include <string>
#include <vector>

enum class TrayAction {
    ShowWindow,
    Exit,
    StartAll,
    StopAll,
    PauseAll,
    ResumeAll,
    StartWorkflow,
    StopWorkflow,
    PauseWorkflow,
    ResumeWorkflow,
};

struct TrayWorkflowDesc {
    std::string id;
    std::string name;
    bool running = false;
    bool paused  = false;
};

struct TrayPendingAction {
    TrayAction  action;
    std::string wfId;  // non-empty for per-workflow actions
};

class TrayManager {
public:
    TrayManager();
    ~TrayManager();

    // iconPixels: 32x32 RGBA pixel data
    void Init(const uint8_t* iconPixels, int iconW, int iconH);
    void Shutdown();

    // Call once per frame; drains pending tray events into `out`
    void Poll(std::vector<TrayPendingAction>& out);

    // Update workflow list shown in submenus (call before Poll or when workflows change)
    void UpdateWorkflows(const std::vector<TrayWorkflowDesc>& wfs);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};
