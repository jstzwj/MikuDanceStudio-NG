#pragma once
#include "panel_controls.hpp"
#include <array>
#include <cstdint>
#include <algorithm>
#include <vector>
namespace mikudancestudio::ui {
struct ControlRect {
    int id = 0, x = 0, y = 0, width = 0, height = 0;
};
struct EditorLayoutOptions {
    bool cameraMode = true, cameraPanelExpanded = true, lightPanelExpanded = true,
         accessoryPanelExpanded = true, bonePanelExpanded = true, morphPanelExpanded = true,
         selfShadowPanelExpanded = true;
};
// VA 00443300: extracted without changing expressions or operation order.
// Hosts apply their DPI transform after this layout in original logical pixels.
inline std::vector<ControlRect> MmdEditorControls(int side, int bottom,
                                                  const EditorLayoutOptions& options) {
    std::vector<ControlRect> result;
    auto move = [&](int id, int x, int y, int w, int h) { result.push_back({id, x, y, w, h}); };
    // --- left tool row (0x4433F5..0x443555) --------------------------------
    move(panel::kUndoButton, side / 2 - 80, 21, 70, 24);
    move(panel::kRedoButton, side / 2 + 10, 21, 70, 24);
    move(panel::kPreviousFrameButton, side / 2 - 49, 67, 20, 24);
    move(panel::kNextFrameButton, side / 2 + 29, 67, 20, 24);
    move(panel::kPreviousKeyframeButton, side / 2 - 79, 69, 20, 22);
    move(panel::kNextKeyframeButton, side / 2 + 59, 69, 20, 22);
    move(panel::kCurrentFrameEdit, side / 2 - 27, 66, 54, 24);
    move(panel::kLastFrameButton, side - 30, 69, 20, 22);
    move(panel::kFirstFrameButton, 15, 69, 20, 22);

    // --- main grid, anchored to the client bottom (0x443563..0x443B00) -----
    move(panel::kTimelineVScroll, side - 19, 162, 16, bottom - 410);
    move(panel::kTimelineHScroll, 97, bottom - 247, side - 116, 16);
    move(panel::kShowCurrentFrameButton, 8, bottom - 247, 80, 18);
    move(panel::kSelectColumnButton, side / 2 + 15, bottom - 224, 50, 18);
    move(panel::kDeleteFramesButton, side / 2 + 70, bottom - 224, 50, 18);
    move(panel::kCopyFramesButton, side / 2 - 119, bottom - 224, 40, 18);
    move(panel::kPasteButton, side / 2 - 76, bottom - 224, 40, 18);
    move(panel::kReversePasteButton, side / 2 - 33, bottom - 224, 40, 18);
    move(panel::kRangeStartEdit, side / 2 - 3, bottom - 201, 50, 14);
    move(panel::kRangeStopEdit, side / 2 + 69, bottom - 201, 50, 14);
    move(panel::kRegisterScopeCombo, side / 2 - 118, bottom - 193, 100, 400);
    move(panel::kSelectRangeButton, side / 2 - 4, bottom - 181, 60, 18);
    move(panel::kExpandShrinkButton, side / 2 + 60, bottom - 181, 60, 18);
    move(panel::kCurveLinearButton, 144, bottom - 26, 63, 18);
    move(panel::kCurvePasteButton, 144, bottom - 52, 63, 18);
    move(panel::kCurveCopyButton, 144, bottom - 78, 63, 18);
    move(panel::kInterpCurveCombo, 144, bottom - 135, 63, 120);
    move(panel::kPhysicsFrameCheckbox, 144, bottom - 105, 13, 13);
    move(panel::kMainComboModel, 225, bottom - 138, 125, 200);
    move(panel::kLoadModelButton, 224, bottom - 112, 60, 21);
    move(panel::kDeleteModelButton, 291, bottom - 112, 60, 21);
    move(panel::kRegisterModelDisplayButton, 291, bottom - 35, 60, 28);
    move(panel::kModelVisibleCheckbox, 225, bottom - 84, 13, 13);
    move(panel::kShadowCheckbox, 267, bottom - 86, 40, 18);
    move(panel::kAddBlendCheckbox, 310, bottom - 86, 41, 18);
    move(panel::kEditExternalParentButton, 328, bottom - 62, 22, 22);
    move(panel::kIkChainCombo, 225, bottom - 61, 100, 100);
    move(panel::kIkOnRadio, 227, bottom - 36, 13, 13);
    move(panel::kIkOffRadio, 227, bottom - 19, 13, 13);
    move(panel::kResetCameraButton, 368, bottom - 139, 70, 23);
    move(panel::kPerspectiveCheckbox, 443, bottom - 139, 65, 23);
    move(panel::kFovEdit, 477, bottom - 111, 30, 14);
    move(panel::kFovSlider, 363, bottom - 95, 151, 20);
    move(panel::kRegisterCameraButton, 463, bottom - 40, 45, 33);
    move(panel::kMainComboNormal, 368, bottom - 53, 90, 100);
    move(panel::kBoneRegisterCombo, 368, bottom - 28, 90, 300);
    move(panel::kExpandCameraPanelButton, 363, bottom - 155, 14, 14);
    move(panel::kCollapseCameraPanelButton, 363, bottom - 155, 14, 14);

    // --- option band 1: base bandXBase (original v52; 0x443B02..0x443D9E) --
    // bandXBase = -18, or 117 while bone mode is off (optflag[0] set) and the
    // expand toggle optflag[1] is clear.
    const auto cameraMode = options.cameraMode;
    const auto cameraPanelExpanded = options.cameraPanelExpanded;
    const auto lightPanelExpanded = options.lightPanelExpanded;
    const auto accessoryPanelExpanded = options.accessoryPanelExpanded;
    const auto bonePanelExpanded = options.bonePanelExpanded;
    const auto morphPanelExpanded = options.morphPanelExpanded;
    const auto selfShadowPanelExpanded = options.selfShadowPanelExpanded;

    std::int32_t bandXBase = -18;
    if (cameraMode != 0 && cameraPanelExpanded == 0)
        bandXBase = 117; // 0x443B1A
    move(panel::kLightColorSliderR, 514 - bandXBase, bottom - 140, 136, 18);
    move(panel::kLightColorEditR, 650 - bandXBase, bottom - 140, 23, 14);
    move(panel::kLightColorSliderG, 514 - bandXBase, bottom - 122, 136, 18);
    move(panel::kLightColorEditG, 650 - bandXBase, bottom - 122, 23, 14);
    move(panel::kLightColorSliderB, 514 - bandXBase, bottom - 104, 136, 18);
    move(panel::kLightColorEditB, 650 - bandXBase, bottom - 104, 23, 14);
    move(panel::kLightDirSliderX, 514 - bandXBase, bottom - 84, 132, 20);
    move(panel::kLightDirEditX, 646 - bandXBase, bottom - 84, 27, 14);
    move(panel::kLightDirSliderY, 514 - bandXBase, bottom - 66, 132, 20);
    move(panel::kLightDirEditY, 646 - bandXBase, bottom - 66, 27, 14);
    move(panel::kLightDirSliderZ, 514 - bandXBase, bottom - 48, 132, 20);
    move(panel::kLightDirEditZ, 646 - bandXBase, bottom - 48, 27, 14);
    move(panel::kRegisterLightButton, 590 - bandXBase, bottom - 26, 75, 20);
    move(panel::kResetLightButton, 524 - bandXBase, bottom - 26, 50, 20);
    move(panel::kExpandLightPanelButton, 501 - bandXBase, bottom - 155, 14, 14);
    move(panel::kCollapseLightPanelButton, 501 - bandXBase, bottom - 155, 14, 14);

    // --- option band 2: bandXBase += 162 with expand toggle optflag[2] (0x443DB3)
    if (cameraMode != 0 && lightPanelExpanded == 0)
        bandXBase += 162;
    move(panel::kExpandSelfShadowPanelButton, 684 - bandXBase, bottom - 155, 14, 14);
    move(panel::kCollapseSelfShadowPanelButton, 684 - bandXBase, bottom - 155, 14, 14);
    move(panel::kEditOffCheckbox, 688 - bandXBase, bottom - 135, 54, 22);
    move(panel::kEditMode1Checkbox, 745 - bandXBase, bottom - 135, 54, 22);
    move(panel::kEditMode2Checkbox, 802 - bandXBase, bottom - 135, 54, 22);
    move(panel::kSelfShadowRangeSlider, 683 - bandXBase, bottom - 80, 176, 25);
    move(panel::kSelfShadowRangeEdit, 820 - bandXBase, bottom - 100, 30, 14);
    move(panel::kRegisterSelfShadowButton, 745 - bandXBase, bottom - 36, 54, 30);

    // --- option band 3: bandXBase += 160 with expand toggle optflag[6] (0x443F19)
    if (cameraMode != 0 && selfShadowPanelExpanded == 0)
        bandXBase += 160;
    move(panel::kExpandAccessoryPanelButton, 864 - bandXBase, bottom - 155, 14, 14);
    move(panel::kCollapseAccessoryPanelButton, 864 - bandXBase, bottom - 155, 14, 14);
    move(panel::kAccessoryCombo, 868 - bandXBase, bottom - 140, 125, 150);
    move(panel::kLoadAccessoryButton, 867 - bandXBase, bottom - 116, 60, 18);
    move(panel::kDeleteAccessoryButton, 934 - bandXBase, bottom - 116, 60, 18);
    move(panel::kAccessoryVisibleCheckbox, 1000 - bandXBase, bottom - 142, 13, 13);
    move(panel::kAccessoryShadowCheckbox, 1000 - bandXBase, bottom - 127, 13, 13);
    move(panel::kAccessoryAddBlendCheckbox, 999 - bandXBase, bottom - 113, 53, 17);
    move(panel::kMainComboGround, 868 - bandXBase, bottom - 95, 90, 100);
    move(panel::kAttachBoneCombo, 962 - bandXBase, bottom - 95, 90, 300);
    move(panel::kAccPosXEdit, 877 - bandXBase, bottom - 70, 48, 14);
    move(panel::kAccPosYEdit, 940 - bandXBase, bottom - 70, 48, 14);
    move(panel::kAccPosZEdit, 1003 - bandXBase, bottom - 70, 48, 14);
    move(panel::kAccRotXEdit, 877 - bandXBase, bottom - 50, 48, 14);
    move(panel::kAccRotYEdit, 940 - bandXBase, bottom - 50, 48, 14);
    move(panel::kAccRotZEdit, 1003 - bandXBase, bottom - 50, 48, 14);
    move(panel::kAccScaleEdit, 877 - bandXBase, bottom - 30, 48, 14);
    move(panel::kAccOpacityEdit, 940 - bandXBase, bottom - 30, 48, 14);
    move(panel::kRegisterAccessoryButton, 995 - bandXBase, bottom - 32, 58, 26);

    // --- option band 4: bandXBase += 178 with expand toggle optflag[3] (0x44422B);
    // band 4 controls use fixed X -------------------------------------------
    if (cameraMode != 0 && accessoryPanelExpanded == 0)
        bandXBase += 178;
    move(panel::kExpandBonePanelButton, 363, bottom - 155, 14, 14);
    move(panel::kCollapseBonePanelButton, 363, bottom - 155, 14, 14);
    move(panel::kBoneSelectRadio, 366, bottom - 140, 60, 28);
    move(panel::kBoneRotateRadio, 430, bottom - 140, 60, 28);
    move(panel::kBoneMoveRadio, 494, bottom - 140, 60, 28);
    move(panel::kBoxSelectRadio, 366, bottom - 105, 60, 24);
    move(panel::kSelectAllBonesButton, 430, bottom - 105, 60, 24);
    move(panel::kSelectUnregisteredBonesButton, 494, bottom - 105, 60, 24);
    move(panel::kCopyBonePoseButton, 366, bottom - 70, 60, 24);
    move(panel::kBonePasteButton, 430, bottom - 70, 60, 24);
    move(panel::kBoneReversePasteButton, 494, bottom - 70, 60, 24);
    move(panel::kPhysicsCheckbox, 500, bottom - 36, 48, 28);
    move(panel::kResetBonePoseButton, 430, bottom - 32, 60, 24);
    move(panel::kRegisterFrameButton, 366, bottom - 36, 60, 28);

    // --- option band 5: second base bandXBase2 (original v110;
    // 0x44444C..0x4448BE) ----------------------------------------------------
    // optflag[4]/optflag[0] both clear -> bandXBase += 179; then bandXBase2 = bandXBase - 180
    // (bone mode on) or bandXBase + 53; optflag[5]/optflag[0] both clear -> bandXBase2 +=
    // 251; optflag[0] clear -> bandXBase2 += 13.
    if (bonePanelExpanded == 0 && cameraMode == 0)
        bandXBase += 179;                                                         // 0x444453
    std::int32_t bandXBase2 = cameraMode != 0 ? bandXBase - 180 : bandXBase + 53; // 0x44445D
    move(panel::kExpandMorphPanelButton, 598 - bandXBase2, bottom - 155, 14, 14);
    move(panel::kCollapseMorphPanelButton, 598 - bandXBase2, bottom - 155, 14, 14);
    move(panel::kMorphCombo1, 618 - bandXBase2, bottom - 120, 92, 500);
    move(panel::kMorphPreviousButton1, 602 - bandXBase2, bottom - 121, 16, 22);
    move(panel::kMorphNextButton1, 710 - bandXBase2, bottom - 121, 16, 22);
    move(panel::kMorphSlider1, 597 - bandXBase2, bottom - 100, 135, 25);
    move(panel::kMorphEdit1, 642 - bandXBase2, bottom - 138, 28, 14);
    move(panel::kMorphCombo0, 618 - bandXBase2, bottom - 50, 92, 500);
    move(panel::kMorphPreviousButton0, 602 - bandXBase2, bottom - 51, 16, 22);
    move(panel::kMorphNextButton0, 710 - bandXBase2, bottom - 51, 16, 22);
    move(panel::kMorphSlider0, 597 - bandXBase2, bottom - 30, 135, 25);
    move(panel::kMorphEdit0, 642 - bandXBase2, bottom - 68, 28, 14);
    move(panel::kMorphCombo2, 749 - bandXBase2, bottom - 120, 92, 500);
    move(panel::kMorphPreviousButton2, 733 - bandXBase2, bottom - 121, 16, 22);
    move(panel::kMorphNextButton2, 841 - bandXBase2, bottom - 121, 16, 22);
    move(panel::kMorphSlider2, 729 - bandXBase2, bottom - 100, 135, 25);
    move(panel::kMorphEdit2, 773 - bandXBase2, bottom - 138, 28, 14);
    move(panel::kMorphCombo3, 749 - bandXBase2, bottom - 50, 92, 500);
    move(panel::kMorphPreviousButton3, 733 - bandXBase2, bottom - 51, 16, 22);
    move(panel::kMorphNextButton3, 841 - bandXBase2, bottom - 51, 16, 22);
    move(panel::kMorphSlider3, 729 - bandXBase2, bottom - 30, 135, 25);
    move(panel::kMorphEdit3, 773 - bandXBase2, bottom - 68, 28, 14);
    move(panel::kRegisterMorphButton1, 675 - bandXBase2, bottom - 141, 50, 19);
    move(panel::kRegisterMorphButton0, 675 - bandXBase2, bottom - 71, 50, 19);
    move(panel::kRegisterMorphButton3, 807 - bandXBase2, bottom - 71, 50, 19);
    move(panel::kRegisterMorphButton2, 807 - bandXBase2, bottom - 141, 50, 19);

    // The original updates bandXBase2 only after the complete facial group above;
    // the adjusted base belongs exclusively to the following view/play band.
    if (morphPanelExpanded == 0 && cameraMode == 0)
        bandXBase2 += 251; // 0x4448A8
    if (cameraMode == 0)
        bandXBase2 += 13; // 0x444895
    move(panel::kViewFrontButton, 886 - bandXBase2, bottom - 141, 38, 17);
    move(panel::kViewBackButton, 925 - bandXBase2, bottom - 141, 37, 17);
    move(panel::kViewTopButton, 963 - bandXBase2, bottom - 141, 37, 17);
    move(panel::kViewLeftButton, 886 - bandXBase2, bottom - 121, 38, 17);
    move(panel::kViewRightButton, 925 - bandXBase2, bottom - 121, 37, 17);
    move(panel::kViewBottomButton, 963 - bandXBase2, bottom - 121, 37, 17);
    move(panel::kPlayButton, 887 - bandXBase2, bottom - 63, 50, 24);
    move(panel::kPlayStartFrameEdit, 888 - bandXBase2, bottom - 36, 50, 14);
    move(panel::kPlayStopFrameEdit, 952 - bandXBase2, bottom - 36, 50, 14);
    move(panel::kPlaybackLoopCheckbox, 952 - bandXBase2, bottom - 58, 13, 13);
    move(panel::kCameraRefModelCheckbox, 902 - bandXBase2, bottom - 99, 13, 13);
    move(panel::kCameraRefBoneCheckbox, 971 - bandXBase2, bottom - 99, 13, 13);
    move(panel::kPlaybackReturnCheckbox, 952 - bandXBase2, bottom - 19, 13, 13);
    move(panel::kPlaybackFromCurrentCheckbox, 888 - bandXBase2, bottom - 19, 13, 13);
    move(panel::kFrameVolumeSlider, 1017 - bandXBase2, bottom - 63, 22, 57);
    move(panel::kFollowCameraCheckbox, 1002 - bandXBase2, bottom - 141, 35, 37);

    return result;
}
inline ControlRect MmdViewport(int width, int height, int sidebar, int renderWidth,
                               int renderHeight) {
    if (renderWidth <= 0 || renderHeight <= 0)
        return {};
    int left = 0, top = 0, right = width, bottom = height;
    const int availableHeight = height - 217, availableWidth = width - sidebar - 9;
    const int fittedWidth = availableHeight * renderWidth / renderHeight;
    if (availableWidth >= fittedWidth) {
        top += 25;
        bottom -= 192;
        const int center = availableWidth / 2 + sidebar + 9;
        left = center - fittedWidth / 2;
        right = center + fittedWidth / 2;
    } else {
        const int fittedHeight = availableWidth * renderHeight / renderWidth;
        left = sidebar + 9;
        top = availableHeight / 2 - fittedHeight / 2 + 25;
        bottom = fittedHeight / 2 + availableHeight / 2 + 25;
    }
    left = std::max(0, left);
    top = std::max(0, top);
    if (right < left)
        right = left + 1;
    if (bottom < top)
        bottom = top + 1;
    return {0, left, top, right - left, bottom - top};
}
inline ControlRect MmdSeparateViewport(int width, int height, int renderWidth, int renderHeight,
                                       bool fullScreen = false) {
    if (renderWidth <= 0 || renderHeight <= 0)
        return {};
    int left = 0, top = 0, right = width, bottom = height;
    const int availableHeight = fullScreen ? height : height - 57;
    const int fittedWidth = availableHeight * renderWidth / renderHeight;
    if (width >= fittedWidth) {
        left = width / 2 - fittedWidth / 2;
        right = width / 2 + fittedWidth / 2;
        if (!fullScreen) {
            top += 25;
            bottom -= 32;
        }
    } else {
        const int fittedHeight = width * renderHeight / renderWidth;
        top = availableHeight / 2 - fittedHeight / 2 + (fullScreen ? 0 : 25);
        bottom = availableHeight / 2 + fittedHeight / 2 + (fullScreen ? 0 : 25);
    }
    return {0, left, top, right - left, bottom - top};
}
inline std::vector<ControlRect> MmdViewportControls(int sidebar, int right, ControlRect viewport) {
    std::vector<ControlRect> result;
    const int inset = sidebar + 9, stripY = viewport.y + viewport.height, rowY = viewport.y;
    auto moveLeft = [&](int id, int k, int dy, int w, int h) {
        result.push_back(inset + k <= right ? ControlRect{id, inset + k, stripY + dy, w, h}
                                            : ControlRect{id, -9000, -9000, 1, 1});
    };
    auto moveRight = [&](int id, int k, int dy, int w, int h) {
        result.push_back(right - k >= inset ? ControlRect{id, right - k, rowY + dy, w, h}
                                            : ControlRect{id, -9000, -9000, 1, 1});
    };
    moveLeft(panel::kModelEditToggle, 5, 3, 55, 27);        // 0x40CB25 model-select strip label
    moveLeft(panel::kResetPositionXButton, 140, 9, 16, 17); // X
    moveLeft(panel::kReadoutPosXEdit, 157, 11, 53, 14);     // X value
    moveLeft(panel::kResetPositionYButton, 213, 9, 16, 17); // Y
    moveLeft(panel::kReadoutPosYEdit, 230, 11, 53, 14);     // Y value
    moveLeft(panel::kResetPositionZButton, 286, 9, 16, 17); // Z
    moveLeft(panel::kReadoutPosZEdit, 303, 11, 53, 14);     // Z value
    moveLeft(panel::kResetRotationXButton, 400, 9, 16, 17); // scale label
    moveLeft(panel::kReadoutRotXEdit, 417, 11, 38, 14);
    moveLeft(panel::kResetRotationYButton, 458, 9, 16, 17);
    moveLeft(panel::kReadoutRotYEdit, 475, 11, 38, 14);
    moveLeft(panel::kResetRotationZButton, 516, 9, 16, 17);
    moveLeft(panel::kReadoutRotZEdit, 533, 11, 38, 14);
    moveLeft(panel::kCameraDistanceButton, 579, 8, 36, 21); // distance
    moveLeft(panel::kReadoutDistEdit, 616, 11, 53, 14);

    moveRight(panel::kCoordAxisCheckbox, 140, -22, 50, 20);   // 0x40D018 coordinate-axis toggle
    moveRight(panel::kFrameVolumeCheckbox, 195, -22, 50, 20); // power-save
    moveRight(panel::kInfoCheckbox, 250, -22, 50, 20);        // info display
    moveRight(panel::kSelfShadowCheckbox, 305, -22, 50, 20);
    moveRight(panel::kGoToFrameButton, 360, -22, 40, 20);
    moveRight(panel::kGotoFrameEdit, 398, -19, 35, 14); // frame edit box
    moveRight(panel::kSetGoToFrameButton, 441, -22, 40, 20);

    return result;
}
}
