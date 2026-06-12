#pragma once
#include "core/workflow.hpp"
#include <cstdint>
#include <functional>
#include <vector>

struct ActivityEditorWidget {
    std::function<void()> OnChanged;

    void SetWorkflows(const std::vector<Workflow>* wfs) { m_workflows = wfs; }

    // currentStep: index of the currently executing activity (-1 = not running)
    void Render(Workflow& wf, int currentStep = -1);

private:
    int  m_editIdx   = -1;
    bool m_openModal = false;
    Activity m_draft;

    // Multi-selection for batch ops
    std::vector<uint8_t> m_selection;
    int m_lastScrolledStep = -1;
    int m_lastClickedIdx   = -1;

    // Coordinate picker state
    enum class PickStage { None, Single, DragFrom, DragTo, RangeFrom, RangeTo };
    PickStage m_pickStage = PickStage::None;

    // Key capture state
    bool m_keyCaptureActive = false;

    // Scroll capture state
    bool  m_scrollCapture = false;
    float m_scrollAccum   = 0.f;

    // Delete confirmation
    int  m_confirmDeleteIdx   = -1;
    bool m_confirmDeleteBatch = false;
    bool m_pendingConfirmOpen = false;

    // Run Workflow dropdown filter
    const std::vector<Workflow>* m_workflows = nullptr;
    char m_wfFilterBuf[64]{};

    void RenderPickOverlay();
    void ApplyPickedCoords(int x, int y);
    void RenderModal(Workflow& wf);
    void RenderActivityFields(ActivityData& data);
    void RenderMoveButtons(Workflow& wf, int idx);
};
