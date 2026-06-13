#pragma once
#include "core/workflow.hpp"
#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include <unordered_set>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

struct ActivityEditorWidget {
    std::function<void()> OnChanged;

    void SetWorkflows(const std::vector<Workflow>* wfs) { m_workflows = wfs; }
    void SetSDLContext(SDL_Window* window, SDL_Renderer* renderer) {
        m_sdlWindow = window; m_sdlRenderer = renderer;
    }

    // currentStep: index of the currently executing top-level activity (-1 = not running)
    void Render(Workflow& wf, int currentStep = -1);

private:
    // ── Modal / selection state ───────────────────────────────────────────────
    int      m_editIdx   = -1;
    bool     m_openModal = false;
    Activity m_draft;

    // Selection: set of selected activity IDs
    std::unordered_set<std::string> m_selectedIds;
    int m_lastScrolledStep = -1;
    std::string m_lastClickedId;

    // ── Coordinate picker state ───────────────────────────────────────────────
    enum class PickStage { None, Single, DragFrom, DragTo, RangeFrom, RangeTo };
    PickStage m_pickStage = PickStage::None;

    // ── Key capture state ─────────────────────────────────────────────────────
    bool m_keyCaptureActive = false;

    // ── Scroll capture state ──────────────────────────────────────────────────
    bool  m_scrollCapture = false;
    float m_scrollAccum   = 0.f;

    // ── Delete confirmation ───────────────────────────────────────────────────
    std::string m_confirmDeleteId;
    bool m_confirmDeleteBatch = false;
    bool m_pendingConfirmOpen = false;

    // ── Run Workflow / Run Activity filter ────────────────────────────────────
    const std::vector<Workflow>* m_workflows = nullptr;
    char m_wfFilterBuf[64]{};
    char m_actFilterBuf[64]{};

    // ── Tree expand/collapse state ────────────────────────────────────────────
    // IDs of block-type activities currently expanded
    std::unordered_set<std::string> m_expandedIds;

    // ── Tree FlatNode (built each frame) ─────────────────────────────────────
    struct FlatNode {
        enum class Kind { Normal, BlockHeader, BlockEnd };

        Activity*               act          = nullptr; // nullptr for BlockEnd
        std::vector<Activity>*  parentList   = nullptr;
        int                     indexInParent = -1;
        int                     depth        = 0;
        Kind                    kind         = Kind::Normal;
        bool                    isExpanded   = false;
        std::vector<bool>       ancestorContinues; // [d] = ancestor at depth d has more siblings
        std::string             blockId;           // for BlockEnd: matches header activity ID
        std::string             blockName;         // for BlockEnd: display label
    };

    std::vector<FlatNode> m_flatNodes;

    void CollectFlatNodes(std::vector<Activity>& list, int depth,
                          std::vector<bool> ancestorCont);
    void RebuildFlatNodes(Workflow& wf);

    // ── SDL fullscreen overlay (shared by pick + snip) ────────────────────────
    SDL_Window*   m_sdlWindow   = nullptr;
    SDL_Renderer* m_sdlRenderer = nullptr;
    int m_origWindowX = 0, m_origWindowY = 0;
    int m_origWindowW = 0, m_origWindowH = 0; // 0 = not in fullscreen mode

    void EnterFullscreenMode();
    void ExitFullscreenMode();

    // ── Snipping-tool capture state ───────────────────────────────────────────
    enum class SnipStage { None, WaitFrame, Active, Done };
    SnipStage    m_snipStage   = SnipStage::None;
    std::vector<uint32_t> m_snipPixels;
    int          m_snipW      = 0, m_snipH      = 0;
    int          m_snipX1     = 0, m_snipY1     = 0;
    int          m_snipX2     = 0, m_snipY2     = 0;
    bool         m_snipDragging = false;
    SDL_Texture* m_snipTexture = nullptr;

    // ── "No window selected" guard dialog state ───────────────────────────────
    bool m_showNoWindowDlg = false;

    // ── "Add to specific body list" target (nullptr = add to wf.activities) ──
    std::vector<Activity>* m_addTargetList = nullptr;

    // ── Private methods ───────────────────────────────────────────────────────
    void RenderPickOverlay();
    void RenderSnipOverlay(Workflow& wf);
    void ApplyPickedCoords(int x, int y);
    void RenderModal(Workflow& wf);
    void RenderActivityFields(ActivityData& data, const Workflow& wf);

    // Returns the primary editable body list of a block-type activity (nullptr if not a block)
    static std::vector<Activity>* GetPrimaryBody(Activity& a);
    // Returns the block-type color (0 if not a block type)
    static uint32_t BlockColor(const ActivityData& d);
    static std::string BlockName(const Activity& a);
    static bool IsBlockType(const ActivityData& d);
};
