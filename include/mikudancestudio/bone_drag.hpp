#pragma once
#include "cpu_bone_types.hpp"
#include "bone_local_axes.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mikudancestudio::cpu_bone {
// Hosts provide their own pose storage, camera/drag inputs and math boundary.
// The x64 baseline uses binary32 drag intermediates on every portable host;
// the Windows x86 adapter explicitly retains its extended-precision policy.
template <class Host, class Model, class Bone, class Math> struct DragKernel {
    using DragScalar = typename Math::Scalar;
    inline static constexpr DragScalar kWrap = Math::Wrap;
    inline static constexpr DragScalar kAngBase = 0.005f, kCoarse = 5.0f, kFine = 0.1f;
    static float OriginalSinTimes(float angle, float factor) {
        return Math::SinTimes(angle, factor);
    }
    static float OriginalCos(float angle) { return Math::Cos(angle); }
    static float OriginalSin(float angle) { return OriginalSinTimes(angle, 1.f); }
    static float* Vec3Normalize(float v[3]) {
        Math::Normalize(v);
        return v;
    }
    static void QuatMultiply(float out[4], const float a[4], const float b[4]) {
        Math::Multiply(out, a, b);
    }
    static bool IsSelectedRoot(const Model* model, int index, bool excludeSelected) {
        const auto* selected = model->boneSelection;
        if (selected == nullptr || selected[index] == 0)
            return false;
        if (excludeSelected && index == model->selectedBone)
            return false;
        auto* bones = model->boneTable;
        const Bone& bone = bones[index];
        if (bone.type != BoneType::Move && bone.type != BoneType::Ik)
            return false;
        const int parent = bone.parent;
        return parent == -1 || selected[parent] == 0;
    }

    static void TransformBoneVector(const float matrix[16], const float in[3], float out[3]) {
        out[0] = matrix[0] * in[0] + matrix[4] * in[1] + matrix[8] * in[2];
        out[1] = matrix[1] * in[0] + matrix[5] * in[1] + matrix[9] * in[2];
        out[2] = matrix[2] * in[0] + matrix[6] * in[1] + matrix[10] * in[2];
    }

    static void BoneWorldPoint(const Bone& bone, float out[3]) {
        const float rest[3] = {bone.position[0], bone.position[1], bone.position[2]};
        TransformBoneVector(bone.matInit, rest, out);
        out[0] += bone.matInit[12];
        out[1] += bone.matInit[13];
        out[2] += bone.matInit[14];
    }

    static void WorldVectorInBoneFrame(const Bone& bone, const float world[3], float local[3]) {
        // x86 477439..4774C2 / x64 454B0D..454BDA: dot with matrix rows.
        // The forward matInit transform uses columns. Sharing that expression
        // here transposes the conversion and reverses motion on rotated bones.
        const float* matrix = bone.matWorld;
        local[0] = matrix[0] * world[0] + matrix[1] * world[1] + matrix[2] * world[2];
        local[1] = matrix[4] * world[0] + matrix[5] * world[1] + matrix[6] * world[2];
        local[2] = matrix[8] * world[0] + matrix[9] * world[1] + matrix[10] * world[2];
    }

    static void ApplySelectedRootTranslation(Model* model, const float delta[3]) {
        auto* bones = model->boneTable;
        auto* dirty = model->bonePhysicsState;
        const int count = model->boneCount;
        if (bones == nullptr)
            return;
        for (int i = 0; i < count; ++i) {
            if (!IsSelectedRoot(model, i, false))
                continue;
            Bone& bone = bones[i];
            float localDelta[3]{};
            WorldVectorInBoneFrame(bone, delta, localDelta);
            bone.trans[0] += localDelta[0];
            bone.trans[1] += localDelta[1];
            bone.trans[2] += localDelta[2];
            if (dirty != nullptr)
                dirty[i] = 1;
        }
    }

    static DragScalar PrecisionMultiplier(Host* app, DragScalar coarse) {
        if (app->ShiftModifierActive())
            return coarse;
        if (app->CtrlModifierActive())
            return kFine;
        return 1.0;
    }

    static void ApplyScreenPlaneBoneMove(Host* app, Model* model, int mode) {
        auto* render = app->Renderer();
        if (render == nullptr)
            return;
        const int viewWidth = render->screenWidth;    // wrapper+120036
        const int viewHeight = render->screenHeight;  // wrapper+120040
        const float overlayScale = render->viewScale; // wrapper+120048
        const int outputWidth = app->RenderWidth();
        const int outputHeight = app->RenderHeight();
        if (viewWidth == 0 || viewHeight == 0 || outputHeight == 0 || overlayScale == 0.0f)
            return;

        const DragScalar mouseScale = app->state.selectedClipW;
        float delta[3]{};
        if (mode == 7 || mode == 9) {
            DragScalar amount = static_cast<DragScalar>(app->MouseX() - app->PreviousMouseX());
            amount *= mouseScale;
            amount /= viewWidth;
            amount *= outputWidth;
            amount /= outputHeight;
            if (app->CameraPerspective() != 0)
                amount *= -static_cast<DragScalar>(app->CameraDistance());
            amount /= overlayScale;
            if (app->CameraPerspective() != 0)
                amount *= static_cast<DragScalar>(0.9f);
            float distance = static_cast<float>(amount);
            distance = static_cast<float>(distance * PrecisionMultiplier(app, 10.0f));
            const float yaw = app->CameraRotation()[1];
            delta[0] = OriginalCos(yaw) * distance;
            delta[2] = OriginalSin(yaw) * distance;
            ApplySelectedRootTranslation(model, delta);
        }

        if (mode == 8 || mode == 9) {
            DragScalar amount = static_cast<DragScalar>(app->PreviousMouseY() - app->MouseY());
            amount *= mouseScale;
            amount /= viewHeight;
            if (app->CameraPerspective() != 0)
                amount *= -static_cast<DragScalar>(app->CameraDistance());
            amount /= overlayScale;
            amount *= static_cast<DragScalar>(app->CameraPerspective() != 0 ? 0.95f : 1.05f);
            float distance = static_cast<float>(amount);
            distance = static_cast<float>(distance * PrecisionMultiplier(app, 10.0f));
            const float pitch = -app->CameraRotation()[0];
            const float yaw = -app->CameraRotation()[1];
            delta[0] = OriginalSin(yaw) * OriginalSin(pitch) * distance;
            delta[1] = OriginalCos(pitch) * distance;
            delta[2] = OriginalCos(yaw) * OriginalSin(pitch) * distance;
            ApplySelectedRootTranslation(model, delta);
        }
    }

    static void ApplyLocalAxisBoneMove(Host* app, Model* model, int mode) {
        const int selectedIndex = model->selectedBone;
        if (selectedIndex < 0)
            return;
        auto* bones = model->boneTable;
        if (bones == nullptr)
            return;
        Bone& selected = bones[selectedIndex];

        DragScalar step = 0.05f;
        if (app->ShiftModifierActive())
            step = 0.5;
        else if (app->CtrlModifierActive())
            step = 0.005f;
        const int mouseDelta = mode == 12 ? app->MouseY() - app->PreviousMouseY()
                                          : app->PreviousMouseY() - app->MouseY();

        float delta[3]{};
        delta[mode - 10] = static_cast<float>(mouseDelta * step);
        if (app->state.coordinateSystem != 1) {
            float basis[16]{};
            BoneLocalAxes(model, basis);
            const int axis = mode - 10;
            float local[3] = {basis[axis * 4 + 0] * delta[axis], basis[axis * 4 + 1] * delta[axis],
                              basis[axis * 4 + 2] * delta[axis]};
            TransformBoneVector(selected.matInit, local, delta);
        }
        ApplySelectedRootTranslation(model, delta);
    }

    static void BoneLocalAxes(const Model* model, float out[16]) {
        cpu_bone::LocalAxes(model, out, [](float* v) { Vec3Normalize(v); });
    }
    static void Run(Host* app, Model* model, int mode) {
        if (!model || !model->boneTable || mode < 1 || mode > 12 || model->selectedBone < 0 ||
            unsigned(model->selectedBone) >= model->boneCount)
            return;
        const int sel = model->selectedBone;
        if (mode >= 7 && mode <= 9) {
            ApplyScreenPlaneBoneMove(app, model, mode);
            return;
        }
        if (mode >= 10) {
            ApplyLocalAxisBoneMove(app, model, mode);
            return;
        }
        auto* bones = model->boneTable;
        Bone& bone = bones[sel];

        const bool selA = app->state.shiftModifierState == 3; // coarse selector
        const bool selB = app->state.ctrlModifierState == 3;  // fine selector
        const auto scaleOf = [&](float base) -> float {
            if (selA)
                return static_cast<float>(kCoarse * base);
            if (selB)
                return static_cast<float>(kFine * base);
            return base;
        };

        float ang = 0.0f;
        float axis[3] = {0.0f, 0.0f, 0.0f};
        const float* matInit = bone.matInit;
        const float* matWorld = bone.matWorld;

        if (mode == 1) {
            const float base = static_cast<float>(
                static_cast<DragScalar>(app->state.previousMouseX - app->state.mouseX) * kAngBase);
            ang = scaleOf(base);
            const float aX = -app->CameraRotation()[0];
            const float aY = -app->CameraRotation()[1];
            const float ax = OriginalCos(aX);
            const float ay = OriginalSin(aY) * OriginalSin(aX);
            const float az = OriginalCos(aY) * OriginalSin(aX);
            axis[0] = matWorld[1] * ax + matWorld[0] * ay + matWorld[2] * az;
            axis[1] = matWorld[6] * az + matWorld[4] * ay + matWorld[5] * ax;
            axis[2] = matWorld[10] * az + matWorld[9] * ax + matWorld[8] * ay;
        } else if (mode == 2) {
            const float base = static_cast<float>(
                static_cast<DragScalar>(app->state.previousMouseY - app->state.mouseY) * kAngBase);
            ang = scaleOf(base);
            // Both reference binaries use yaw here; camera pitch does not enter.
            const float yaw = app->CameraRotation()[1];
            const float ax = OriginalCos(yaw);
            const float az = OriginalSin(yaw);
            axis[0] = matWorld[0] * ax + matWorld[2] * az;
            axis[1] = matWorld[4] * ax + matWorld[6] * az;
            axis[2] = matWorld[8] * ax + matWorld[10] * az;
        } else if (mode == 3) {
            const DragScalar angCurRad =
                std::atan2(static_cast<DragScalar>(app->state.mouseX - app->state.dragOriginX),
                           static_cast<DragScalar>(app->state.mouseY - app->state.dragOriginY));
            const DragScalar angPrevRad = std::atan2(
                static_cast<DragScalar>(app->state.previousMouseX - app->state.dragOriginX),
                static_cast<DragScalar>(app->state.previousMouseY - app->state.dragOriginY));
            float ang1 = static_cast<float>(angCurRad * 0.5);
            float ang2 = static_cast<float>(angPrevRad * 0.5);
            if (kWrap < static_cast<DragScalar>(ang1) - ang2)
                ang2 = static_cast<float>(ang2 + kWrap);
            if (static_cast<DragScalar>(ang1) - ang2 < -kWrap)
                ang1 = static_cast<float>(ang1 + kWrap);
            ang = scaleOf(ang1 - ang2);
            const float aX = -app->CameraRotation()[0];
            const float aY = -app->CameraRotation()[1];
            const float ax = -OriginalSin(aX);
            const float ay = OriginalSin(aY) * OriginalCos(aX);
            const float az = OriginalCos(aY) * OriginalCos(aX);
            axis[0] = matWorld[1] * ax + matWorld[0] * ay + matWorld[2] * az;
            axis[1] = matWorld[6] * az + matWorld[4] * ay + matWorld[5] * ax;
            axis[2] = matWorld[10] * az + matWorld[9] * ax + matWorld[8] * ay;
        } else {
            // modes 4/5/6 - local axes
            const float base = static_cast<float>(
                static_cast<DragScalar>(app->state.previousMouseY - app->state.mouseY) * kAngBase);
            ang = scaleOf(base);
            // x86 476110 / x64 453591: world axes use the coordinate-system
            // selector, independently of the physics editor's body/joint page.
            if (app->state.coordinateSystem == 1) {
                const std::size_t k = static_cast<std::size_t>(mode - 4);
                axis[0] = matWorld[k];
                axis[1] = matWorld[4 + k];
                axis[2] = matWorld[8 + k];
            } else {
                float basis[16];
                BoneLocalAxes(model, basis);
                const std::size_t r = static_cast<std::size_t>(mode - 4) * 4;
                const float mx = basis[r];
                const float my = basis[r + 1];
                const float mz = basis[r + 2];
                const float mid0 = matInit[8] * mz + matInit[0] * mx + matInit[4] * my;
                const float mid1 = matInit[9] * mz + matInit[1] * mx + matInit[5] * my;
                const float mid2 = matInit[10] * mz + matInit[6] * my + matInit[2] * mx;
                axis[0] = matWorld[1] * mid1 + matWorld[0] * mid0 + matWorld[2] * mid2;
                axis[1] = matWorld[6] * mid2 + matWorld[4] * mid0 + matWorld[5] * mid1;
                axis[2] = mid2 * matWorld[10] + matWorld[9] * mid1 + matWorld[8] * mid0;
                Vec3Normalize(axis);
            }
        }

        {
            // ---- bone edit ------------------------------------------------------
            if (bone.type == BoneType::FixedAxis ||
                (bone.type == BoneType::UnderIk && (bone.flags & kBoneFlagFixedAxis))) {
                if (model->physicsMode == 2) {
                    axis[0] = bone.axis[0];
                    axis[1] = bone.axis[1];
                    axis[2] = bone.axis[2];
                } else {
                    const Bone& p = bones[bone.tailBone];
                    axis[0] = p.position[0] - bone.position[0];
                    axis[1] = p.position[1] - bone.position[1];
                    axis[2] = p.position[2] - bone.position[2];
                    Vec3Normalize(axis);
                }
            }
            unsigned char* dirty = model->bonePhysicsState;
            if (dirty != nullptr)
                dirty[sel] = 1;

            // 0x476D6D/0x476DA3: sin/cos of the FULL drag angle (the 0.5
            // half-angle multiplication does NOT exist in the instruction
            // stream; var_14D4 already carries the scaled full angle)
            float dq[4] = {OriginalSinTimes(ang, axis[0]), OriginalSinTimes(ang, axis[1]),
                           OriginalSinTimes(ang, axis[2]), OriginalCos(ang)};
            float out[4];
            QuatMultiply(out, bone.rotQuat, dq);
            std::memcpy(bone.rotQuat, out, sizeof(out));

            // Other selected roots (type Move/Ik, parent unselected) rotate
            // about the active bone's world position. Descendants of a selected
            // parent inherit its transform and must not receive the delta twice.
            {
                float piv[3]{};
                BoneWorldPoint(bone, piv);
                const float invq[4] = {-dq[0], -dq[1], -dq[2], dq[3]};
                const int n = model->boneCount;
                for (int i = 0; i < n; ++i) {
                    Bone& ch = bones[i];
                    if (!IsSelectedRoot(model, i, true))
                        continue;
                    if (dirty != nullptr)
                        dirty[i] = 1; // t4 mark (0x476FB2)
                    float point[3]{};
                    BoneWorldPoint(ch, point);
                    const float off[4] = {point[0] - piv[0], point[1] - piv[1], point[2] - piv[2],
                                          0.0f};
                    float r1[4], r2[4];
                    QuatMultiply(r1, invq, off); // 0x4770A6
                    QuatMultiply(r2, r1, dq);    // 0x4770FB sandwich
                    ch.trans[0] += r2[0] - off[0];
                    ch.trans[1] += r2[1] - off[1];
                    ch.trans[2] += r2[2] - off[2];
                    float oq[4];
                    QuatMultiply(oq, ch.rotQuat, dq); // 0x4771EB
                    std::memcpy(ch.rotQuat, oq, sizeof(oq));
                }
            }
            return;
        }
    }
};
}
