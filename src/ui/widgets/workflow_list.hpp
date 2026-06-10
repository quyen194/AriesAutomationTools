#pragma once
#include "core/workflow.hpp"
#include <functional>
#include <string>

// Left-panel widget: renders the workflow list and add/dup/delete controls.
struct WorkflowListWidget {
    // Called when selection changes; returns the selected workflow id.
    std::function<void(const std::string& id)> OnSelect;
    // Called when Add is clicked (caller appends to config and calls Refresh).
    std::function<void()> OnAdd;
    std::function<void(const std::string& id)> OnDuplicate;
    std::function<void(const std::string& id)> OnDelete;

    // Render the widget. Pass the full workflow list and the running-state checker.
    void Render(const std::vector<Workflow>& workflows,
                std::function<bool(const std::string&)> isRunning,
                const std::string& selectedId);
};
