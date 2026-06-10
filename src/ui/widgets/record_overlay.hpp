#pragma once
#include "core/record_engine.hpp"
#include "core/workflow.hpp"
#include <functional>
#include <vector>

// Floating always-on-top toolbar shown during recording.
// Shows event count, elapsed time, and a Stop button.
struct RecordOverlayWidget {
    std::function<void(std::vector<Activity>)> OnFinished; // called with captured activities

    void Render(RecordEngine& engine);

private:
    bool m_showReview    = false;
    bool m_captureTimings = true;
    int  m_fixedDelay    = 100;
    std::vector<Activity> m_captured;
};
