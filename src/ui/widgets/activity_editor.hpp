#pragma once
#include "core/workflow.hpp"
#include <functional>
#include <vector>

struct ActivityEditorWidget {
    std::function<void()> OnChanged;

    // currentStep: index of the currently executing activity (-1 = not running)
    void Render(Workflow& wf, int currentStep = -1);

private:
    int  m_editIdx   = -1;
    bool m_openModal = false;
    Activity m_draft;

    // Multi-selection for batch ops
    std::vector<bool> m_selection;
    int m_lastScrolledStep = -1;
    int m_lastClickedIdx   = -1;  // for shift-click range select

    // Coordinate picker state
    enum class PickStage { None, Single, DragFrom, DragTo };
    PickStage m_pickStage = PickStage::None;

    // Key capture state
    bool m_keyCaptureActive = false;

    // Scroll capture state
    bool  m_scrollCapture = false;
    float m_scrollAccum   = 0.f;

    void RenderPickOverlay();
    void ApplyPickedCoords(int x, int y);
    void RenderModal(Workflow& wf);
    void RenderActivityFields(ActivityData& data);
    void RenderMoveButtons(Workflow& wf, int idx);
};
