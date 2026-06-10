#pragma once
#include "core/record_engine.hpp"
#include "core/workflow.hpp"
#include <functional>
#include <vector>

// Floating always-on-top toolbar shown during recording.
// Shows event count, a Stop button, and filter options.
struct RecordOverlayWidget {
    std::function<void(std::vector<Activity>)> OnFinished;

    void Render(RecordEngine& engine);
    // Call when stopping recording from outside the overlay (e.g. main panel [Stop Rec])
    void TriggerReview(RecordEngine& engine);

private:
    bool m_showReview      = false;
    bool m_captureTimings  = true;
    int  m_fixedDelay      = 100;

    // Filter options
    bool m_clicksOnly          = false;  // drop mouse-move and scroll events
    bool m_stopMoveMsEnabled   = false;  // only keep moves where cursor paused >= threshold
    int  m_stopMoveMsThreshold = 200;

    RecordEngine*          m_engineRef = nullptr;
    std::vector<Activity>  m_captured;
    std::vector<bool>      m_reviewSelected;  // per-activity accept checkbox

    std::vector<Activity> ApplyFilters(std::vector<Activity> acts,
                                       const std::vector<RecordedEvent>& evs) const;
};
