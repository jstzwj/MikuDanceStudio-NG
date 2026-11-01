// ===========================================================================
// VA 0x00443300 - HandleWindowSize  (original: sub_443300, ~0x19B1 bytes)
// ===========================================================================
// WM_SIZE layout engine.  Called from MainWndProc (0x4C3A10 WM_SIZE), the
// command dispatch (0x47E8A0) and the scene loaders (0x458F80 / 0x460430 /
// 0x460B30).  After a resize the 168 child controls of the main window are
// fully repositioned:
//
//   1. 0x44331B  clamp: GetWindowRect; if width  > 2560 ->
//      SetWindowPos(cx=2560, SWP_NOZORDER); if height > 1920 ->
//      SetWindowPos(cy=1920, SWP_NOZORDER).  Position is preserved.
//   2. 0x4433A4  sidebar width (this+0xA06C8): GetClientRect; when the
//      accessory-column gate (this+0xA0D38) is set the sidebar is client
//      width - 3 and the label sweep RebuildTimelineTrackPanel (0x42F1E0) runs;
//      otherwise sidebar = max(250, (int)(ratio @0xA4428 * client width))
//      and PanelPaint (0x414610) runs when the sidebar grew.
//   3. 0x4433F5  control layout, three coordinate families:
//      - left tool row (ids 400..559): X = sidebar/2 + k (or sidebar - k,
//        fixed 15 for 558), constant Y/W/H.
//      - main grid (ids 427..453): Y = clientBottom - k (stretched to the
//        window bottom), X = sidebar-derived or fixed; several controls get
//        W/H = clientBottom - k (e.g. 427 H, 428 W, 434/436/443 big panels).
//      - option bands (ids 455..535): X = const - bandXBase / const -
//        bandXBase2 where
//        the two bases accumulate conditional deltas from the UI option
//        flag bytes this+0x2F8..0x2FE (kByteOptflag0..6), see the body.
//   4. 0x444B3B  visibility re-toggle: five id ranges (455..469, 471..488,
//      504..528, 402..414, 531..535) get ShowWindow(SW_HIDE) followed by
//      ShowWindow(SW_SHOW) when IsWindowVisible.
//   5. 0x444C9D  refresh tail: RefreshMainWindowViewport (0x42C810) then 0x40CAC0
//      (0x40CAC0).
//
// All MoveWindow calls pass bRepaint = TRUE; every GetDlgItem handle is
// consumed by the immediately following MoveWindow, so the per-control
// sequence is rendered through one file-static helper (control order and
// parameters are unchanged).
//
// Reference: ../translated/MikuMikuDance/fcn_00443300.cpp is a partial
// auto-generated dump (coordinates/IDs mislabelled past 0x44365E); this
// port follows the live IDA disassembly/pseudocode of VA 0x00443300.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/mmd_editor_layout.hpp"
#include "mikudancestudio/sidebar_navigation.hpp"
#include <Windows.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

namespace {

// GetDlgItem + MoveWindow pair as in the original (0x44341E..0x443427 etc.).
void MoveCtrl(HWND parent, int id, int x, int y, int w, int h) {
    MoveWindow(GetDlgItem(parent, id), x, y, w, h, TRUE);
}

// Visibility re-toggle loop body (0x444B4A..0x444B6F and the four siblings).
void ToggleVisible(HWND parent, int firstId, int lastId) {
    for (int id = firstId; id <= lastId; ++id) {
        HWND ctrl = GetDlgItem(parent, id);
        if (IsWindowVisible(ctrl)) {
            ShowWindow(ctrl, SW_HIDE);
            ShowWindow(ctrl, SW_SHOW);
        }
    }
}

} // namespace

void PanelPaint(MMDApp* app);           // VA 0x00414610 (defined in ui_panel_paint.cpp)
void LayoutViewportPanels(MMDApp* app); // VA 0x0040CAC0
                                        // (ui_viewport_layout.cpp)

void HandleWindowSize(MMDApp* app) {
    auto& s = *app;
    const HWND hwnd = static_cast<HWND>(app->Hwnd()); // 657080 (0xA06B8)

    // --- clamp the window to 2560 x 1920 (0x44331B..0x443391) --------------
    RECT wr;
    GetWindowRect(hwnd, &wr);
    if (wr.right - wr.left > 2560)
        SetWindowPos(hwnd, nullptr, wr.left, wr.top, 2560, wr.bottom - wr.top, SWP_NOZORDER);
    GetWindowRect(hwnd, &wr);
    if (wr.bottom - wr.top > 1920)
        SetWindowPos(hwnd, nullptr, wr.left, wr.top, wr.right - wr.left, 1920, SWP_NOZORDER);

    // --- sidebar width (0x4433A4..0x4433F2) --------------------------------
    RECT cr1;
    GetClientRect(hwnd, &cr1); // var_10 - only .right feeds the sidebar
    std::int32_t& sidebar = s.SidebarWidth();
    if (s.FloatingWindow() != nullptr) {
        sidebar = cr1.right - 3;        // 0x4433B8
        RebuildTimelineTrackPanel(app); // 0x4433BE
    } else {
        const auto newSidebar = ui::ResizeSidebar<sizeof(void*) == 4>(s.SidebarRatio(), cr1.right);
        const bool grew = sidebar < newSidebar; // 0x4433E0
        sidebar = newSidebar;                   // 0x4433E6
        if (grew)
            PanelPaint(app); // 0x4433EC
    }
    const std::int32_t side = sidebar;
    RECT cb;                  // var_30
    GetClientRect(hwnd, &cb); // 0x443563
    const std::int32_t bottom = cb.bottom;

    const ui::EditorLayoutOptions options{
        s.CameraMode()!=0, s.PanelExpanded(UiPanel::Camera)!=0,
        s.PanelExpanded(UiPanel::Light)!=0, s.PanelExpanded(UiPanel::Accessory)!=0,
        s.PanelExpanded(UiPanel::Bone)!=0, s.PanelExpanded(UiPanel::Morph)!=0,
        s.PanelExpanded(UiPanel::SelfShadow)!=0};
    for(const auto& rect:ui::MmdEditorControls(side,bottom,options))
        MoveCtrl(hwnd,rect.id,rect.x,rect.y,rect.width,rect.height);

    // --- visibility re-toggle, five id ranges (0x444B3B..0x444C99) ---------
    ToggleVisible(hwnd, panel::kLightColorSliderR, panel::kCollapseLightPanelButton);
    ToggleVisible(hwnd, panel::kAccessoryCombo, panel::kCollapseAccessoryPanelButton);
    ToggleVisible(hwnd, panel::kMorphCombo0, panel::kCollapseMorphPanelButton);
    ToggleVisible(hwnd, panel::kViewFrontButton, panel::kPlaybackFromCurrentCheckbox);
    ToggleVisible(hwnd, panel::kCameraRefBoneCheckbox, panel::kFollowCameraCheckbox);

    // --- refresh tail (0x444C9D..0x444CB0) ---------------------------------
    RefreshMainWindowViewport(app);
    LayoutViewportPanels(app); // original tail call: `return sub_40CAC0(this);`
}

// VA 0x00442EB0 - live relayout of the sidebar-anchored
// controls (top tool row 400..417/532/533/558/559 plus the bottom frame
// grid 415..434) at the current sidebar width and client height; the
// visibility re-toggle refreshes those bands without the full WM_SIZE
// option-band pass.  Called by the sidebar-resize drag (HandleMouseMove
// 0x444CC0), the scene loaders and the mic window procs.
void RelayoutSidebarControls(MMDApp* app) {
    const HWND hwnd = static_cast<HWND>(app->Hwnd());
    const int side = app->SidebarWidth();
    RECT client{};
    GetClientRect(hwnd, &client);

    MoveCtrl(hwnd, panel::kUndoButton, side / 2 - 80, 21, 70, 24);
    MoveCtrl(hwnd, panel::kRedoButton, side / 2 + 10, 21, 70, 24);
    MoveCtrl(hwnd, panel::kPreviousFrameButton, side / 2 - 49, 67, 20, 24);
    MoveCtrl(hwnd, panel::kNextFrameButton, side / 2 + 29, 67, 20, 24);
    MoveCtrl(hwnd, panel::kPreviousKeyframeButton, side / 2 - 79, 69, 20, 22);
    MoveCtrl(hwnd, panel::kNextKeyframeButton, side / 2 + 59, 69, 20, 22);
    MoveCtrl(hwnd, panel::kCurrentFrameEdit, side / 2 - 27, 66, 54, 24);
    MoveCtrl(hwnd, panel::kLastFrameButton, side - 30, 69, 20, 22);
    MoveCtrl(hwnd, panel::kFirstFrameButton, 15, 69, 20, 22);

    const int bottom = client.bottom;
    MoveCtrl(hwnd, panel::kTimelineVScroll, side - 19, 162, 16, bottom - 410);
    MoveCtrl(hwnd, panel::kTimelineHScroll, 97, bottom - 247, side - 116, 16);
    MoveCtrl(hwnd, panel::kShowCurrentFrameButton, 8, bottom - 247, 80, 18);
    MoveCtrl(hwnd, panel::kSelectColumnButton, side / 2 + 15, bottom - 224, 50, 18);
    MoveCtrl(hwnd, panel::kDeleteFramesButton, side / 2 + 70, bottom - 224, 50, 18);
    MoveCtrl(hwnd, panel::kCopyFramesButton, side / 2 - 119, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kPasteButton, side / 2 - 76, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kReversePasteButton, side / 2 - 33, bottom - 224, 40, 18);
    MoveCtrl(hwnd, panel::kRangeStartEdit, side / 2 - 3, bottom - 201, 50, 14);
    MoveCtrl(hwnd, panel::kRangeStopEdit, side / 2 + 69, bottom - 201, 50, 14);
    MoveCtrl(hwnd, panel::kRegisterScopeCombo, side / 2 - 118, bottom - 193, 100, 400);
    MoveCtrl(hwnd, panel::kSelectRangeButton, side / 2 - 4, bottom - 181, 60, 18);
    MoveCtrl(hwnd, panel::kExpandShrinkButton, side / 2 + 60, bottom - 181, 60, 18);

    ToggleVisible(hwnd, panel::kUndoButton, panel::kRedoButton);
    ToggleVisible(hwnd, panel::kSelectRangeButton, panel::kRangeStopEdit);
    ToggleVisible(hwnd, panel::kFirstFrameButton, panel::kLastFrameButton);
    ToggleVisible(hwnd, panel::kRegisterScopeCombo, panel::kRegisterScopeCombo);
    RefreshMainWindowViewport(app);
    LayoutViewportPanels(app);
}

} // namespace mikudancestudio
