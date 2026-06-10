#pragma once
#include "core/workflow.hpp"
#include <functional>

struct ActivityEditorWidget {
    std::function<void()> OnChanged;

    void Render(Workflow& wf);

private:
    int  m_editIdx   = -1;
    bool m_openModal = false;
    Activity m_draft;

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
