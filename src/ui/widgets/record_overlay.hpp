#pragma once
#include "core/record_engine.hpp"
#include "core/workflow.hpp"
#include <functional>
#include <vector>
#include <string>

// Floating always-on-top recording toolbar.
// States: pre-start (Open()), recording in progress, review screen.
struct RecordOverlayWidget {
    std::function<void(std::vector<Activity>)> OnFinished;
    std::function<void(const std::string&)>    OnHotkeyChanged;
    std::function<void(const std::string&)>    OnStopHotkeyChanged;

    void Render(RecordEngine& engine);
    void TriggerReview(RecordEngine& engine);

    void Open();
    bool IsOpen()            const { return m_windowOpen; }
    bool IsHotkeyCapturing() const { return m_hotkeyCapture || m_stopHotkeyCapture; }
    void SetHotkey(const std::string& hk);
    void SetStopHotkey(const std::string& hk);

private:
    bool m_windowOpen      = false;
    bool m_showReview      = false;
    bool m_captureTimings  = true;
    int  m_fixedDelay      = 100;

    bool m_clicksOnly          = false;
    bool m_stopMoveMsEnabled   = false;
    int  m_stopMoveMsThreshold = 200;

    char m_hotkeyBuf[64]{};
    bool m_hotkeyCapture = false;
    char m_stopHotkeyBuf[64]{};
    bool m_stopHotkeyCapture = false;

    RecordEngine*          m_engineRef = nullptr;
    std::vector<Activity>  m_captured;
    std::vector<bool>      m_reviewSelected;

    std::vector<Activity> ApplyFilters(std::vector<Activity> acts,
                                       const std::vector<RecordedEvent>& evs) const;
};
