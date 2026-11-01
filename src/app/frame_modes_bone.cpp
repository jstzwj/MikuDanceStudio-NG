// Bone dragging in the original frame driver: x86 475AD9..4786FA,
// x64 RVA 32F97..35EEB (MikuMikuDanceE 9.32).
// ViewportDragMode 1..6 rotates the active bone; 7..9 moves selected roots
// in the view plane; 10..12 moves them along local/world axes. A selected
// Move/Ik bone whose parent is selected inherits the parent's delta.
// World movement converts through each root's matWorld rows, while local
// axes first transform forward through the active bone's matInit columns.
// Rotation uses sin/cos of the full scaled drag angle, then the original
// D3DXQuaternionMultiply order. Keep the per-architecture float stores.
// Drags set bonePhysicsState (pending pose edits), leaving ikBackup intact;
// the press-edge undo snapshot is owned by BeginOrEndViewportToolDrag.
// With the physics editor open, these modes edit RigidRecord/JointRecord
// position, rotation or size instead, according to physicsEditorJointPage.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#if defined(_M_IX86)
#include "mikudancestudio/mmd_math.hpp"
#endif
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/bone_drag.hpp"

namespace mikudancestudio {
namespace {

constexpr double kAccDrag = 0.01;
struct NativeDragMath {
#if defined(_M_IX86)
    using Scalar = double;
    inline static constexpr Scalar Wrap = 6.28;
#else
    using Scalar = float;
    inline static constexpr Scalar Wrap = 6.28f;
#endif
    static float SinTimes(float angle, float factor) {
#if defined(_M_IX86)
        return mmd_math::sinTimes(angle, factor);
#else
        return float(std::sin(angle) * factor);
#endif
    }
    static float Cos(float angle) {
#if defined(_M_IX86)
        return mmd_math::cosine(angle);
#else
        return float(std::cos(angle));
#endif
    }
    static void Normalize(float* v) { d3dx::Get().vec3Normalize(v, v); }
    static void Multiply(float* out, const float* a, const float* b) {
        d3dx::Get().quatMultiply(out, a, b);
    }
};
using NativeBoneDrag =
    cpu_bone::DragKernel<MMDApp, mdl::ModelRecord, mdl::BoneRecord, NativeDragMath>;
void EchoRecord(HWND dialog, int id, const char* format, float value) {
    char text[256]{};
    sprintf_s(text, sizeof(text), format, value);
    SetWindowTextA(GetDlgItem(dialog, id), text);
}

bool EditModeRecord(MMDApp* app, int mode) {
    HWND dialog = app->FrameCopyOrPhysicsDialog();
    if (dialog == nullptr)
        return false;
    const int dy = app->MouseY() - app->PreviousMouseY();

    if (mode >= 4 && mode <= 6) {
        const int axis = mode - 4;
        if (app->state.physicsEditorJointPage != 0) {
            // Physics-editor joint page: drag edits the joint rotation
            // (dialog edits 754..756).
            auto* joints =
                static_cast<mikudancestudio::mdl::JointRecord*>(app->JointScratchArray());
            const int index = app->state.selectedJointIndex;
            if (joints != nullptr && index >= 0) {
                float& value = joints[index].rotation[axis];
                value = static_cast<float>(value - static_cast<double>(dy) * kAccDrag);
                EchoRecord(dialog, 754 + axis, "%3.2f",
                           value / g_AnglePiTruncated * g_AngleDegreesScale);
            }
        } else {
            // Rigid-body page: drag edits the body rotation (715..717).
            auto* bodies =
                static_cast<mikudancestudio::mdl::RigidRecord*>(app->RigidScratchArray());
            const int index = app->state.selectedRigidIndex;
            if (bodies != nullptr && index >= 0) {
                float& value = bodies[index].rotation[axis];
                value = static_cast<float>(value - static_cast<double>(dy) * kAccDrag);
                EchoRecord(dialog, 715 + axis, "%3.2f",
                           value / g_AnglePiTruncated * g_AngleDegreesScale);
            }
        }
        return true;
    }

    if (mode >= 10 && mode <= 12) {
        const int axis = mode - 10;
        constexpr double kStep = 0.05000000074505806;
        if (app->state.physicsEditorJointPage != 0) {
            // Joint position drag (744..746).
            auto* joints =
                static_cast<mikudancestudio::mdl::JointRecord*>(app->JointScratchArray());
            const int index = app->state.selectedJointIndex;
            if (joints != nullptr && index >= 0) {
                float& value = joints[index].position[axis];
                value = static_cast<float>(value - dy * kStep);
                EchoRecord(dialog, 744 + axis, "%5.4f", value);
            }
        } else {
            // Rigid body: shift-drag scales the shape size (709..711),
            // plain drag moves the body (712..714).
            auto* bodies =
                static_cast<mikudancestudio::mdl::RigidRecord*>(app->RigidScratchArray());
            const int index = app->state.selectedRigidIndex;
            if (bodies != nullptr && index >= 0) {
                const bool scale = app->ShiftModifierActive();
                float& value = scale ? bodies[index].size[axis] : bodies[index].position[axis];
                value = static_cast<float>(value - dy * kStep);
                if (scale && value < 0.1f)
                    value = 0.1f;
                EchoRecord(dialog, (scale ? 709 : 712) + axis, "%3.2f", value);
            }
        }
        return true;
    }
    return true;
}

} // namespace

void BoneLocalAxes(const mdl::ModelRecord* model, float out[16]) {
    NativeBoneDrag::BoneLocalAxes(model, out);
}

void BoneEditModes(MMDApp* app) {
    const int mode = static_cast<int>(app->state.interactionDragMode);
    if (mode < 1 || mode > 12)
        return;
    if ((mode <= 6 || mode >= 10) && app->FrameCopyOrPhysicsDialog() != nullptr) {
        EditModeRecord(app, mode);
        return;
    }
    NativeBoneDrag::Run(app, app->SelectedModel(), mode);
}
} // namespace mikudancestudio
