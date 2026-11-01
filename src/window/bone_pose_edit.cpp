// Selected bone-pose reset (control 495, x86 0x0047F3FA).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/bone_pose_reset.hpp"

namespace mikudancestudio {
namespace {
// The x64 original uses MULSS/ADDSS at each step; x86 retains its existing
// double intermediates. Do not combine the two evaluation paths.
#if defined(_M_X64) || defined(__x86_64__)
using ResetPositionScalar = float;
#else
using ResetPositionScalar = double;
#endif
} // namespace

void ResetSelectedBonePose(MMDApp* app) {
    PushBoneEditUndo(app); // prep (thiscall)
    mdl::ModelRecord* model = app->SelectedModel();
    const auto count = static_cast<std::int32_t>(model->boneCount);
    for (std::int32_t i = 0; i < count; ++i) {
        if (!model->boneSelection[i])
            continue;
        auto& bone = model->boneTable[i];
        const auto& root = model->boneTable[0];
        d3dx::D3DXMATRIXF matrix{};
        if (i != 0 && bone.parent == -1)
            d3dx::Get().matrixRotationQuaternion(&matrix, bone.rotQuat);
        editor::ResetBonePose<ResetPositionScalar>(
            unsigned(i), bone.parent, bone.position, &matrix.m[0][0], root.trans, root.rotQuat,
            bone.trans, bone.rotQuat, model->bonePhysicsState[i]);
    }
    PostViewRefresh(app);
}

} // namespace mikudancestudio
