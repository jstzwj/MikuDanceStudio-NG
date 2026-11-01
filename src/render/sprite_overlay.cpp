// ===========================================================================
// VA 0x0046BD79..0x0046DB3D - dynamic HUD sprite producer (camera path)
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#if defined(_M_IX86)
#include "mikudancestudio/mmd_math.hpp"
#endif
#include <Windows.h>
#include <d3d9.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/bone_overlay.hpp"
#include "mikudancestudio/viewport_tools.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "vb_dump.hpp"

namespace mikudancestudio {
namespace {

struct SpriteVertex {
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
    float u;
    float v;
};

static_assert(sizeof(SpriteVertex) == 28);

struct LineVertex {
    float x;
    float y;
    float z;
    float rhw;
    D3DCOLOR color;
};

static_assert(sizeof(LineVertex) == 20);

struct BoneOverlayResult {
    int selectedX;
    int selectedY;
    float selectedClipW;
    std::uint32_t spritePrimitiveCount;
    std::uint32_t linePrimitiveCount;
};

static_assert(sizeof(BoneOverlayResult) == 20);

struct ClipPoint {
    float x;
    float y;
    float z;
    float w;
};

ClipPoint TransformPoint(float x, float y, float z, const D3DMATRIX& m) {
    return {
        x * m._11 + y * m._21 + z * m._31 + m._41,
        x * m._12 + y * m._22 + z * m._32 + m._42,
        x * m._13 + y * m._23 + z * m._33 + m._43,
        x * m._14 + y * m._24 + z * m._34 + m._44,
    };
}

ClipPoint TransformPoint(const ClipPoint& p, const D3DMATRIX& m) {
    return {
        p.x * m._11 + p.y * m._21 + p.z * m._31 + p.w * m._41,
        p.x * m._12 + p.y * m._22 + p.z * m._32 + p.w * m._42,
        p.x * m._13 + p.y * m._23 + p.z * m._33 + p.w * m._43,
        p.x * m._14 + p.y * m._24 + p.z * m._34 + p.w * m._44,
    };
}

// Project the selected rest/tail member through the bone and frame matrices.
// Original x64 sub_7FF7CB4E4980 uses the same order for both points.
bool ProjectBonePoint(const mdl::BoneRecord& bone, const float (&point)[3], const D3DMATRIX& world,
                      const D3DMATRIX& view, const D3DMATRIX& projection, const RECT& viewport,
                      int* screenX, int* screenY, float* clipW) {
    const auto& boneMatrix = *reinterpret_cast<const D3DMATRIX*>(bone.matInit);
    ClipPoint p = TransformPoint(point[0], point[1], point[2], boneMatrix);
    p = TransformPoint(p, world);
    p = TransformPoint(p, view);
    p = TransformPoint(p, projection);
    *clipW = p.w;
    if (p.w <= 0.0f) {
        *screenX = 393939;
        *screenY = 393939;
        return false;
    }
    const double width = static_cast<double>(viewport.right - viewport.left);
    const double height = static_cast<double>(viewport.bottom - viewport.top);
    *screenX = static_cast<int>(p.x / p.w * width * 0.5 + (viewport.left + viewport.right) / 2);
    *screenY = static_cast<int>((viewport.top + viewport.bottom) / 2 - p.y / p.w * height * 0.5);
    return true;
}

__declspec(noinline) float BoneVCoordinate(int numerator, int origin, float lengthValue,
                                           float scale) {
#if defined(_M_IX86)
    return mikudancestudio::mmd_math::boneVCoordinate(numerator, origin, lengthValue, scale);
#else
    return static_cast<float>(static_cast<double>(numerator) / lengthValue * 8.0 * scale + origin);
#endif
}

__declspec(noinline) void BoneMarkerBounds(int x, int y, float scale, float* left, float* top,
                                           float* right, float* bottom) {
    float l = 0.0f;
    float t = 0.0f;
    float r = 0.0f;
    float b = 0.0f;
    // 0x49AFBD onward keeps the half extent on the x87 stack, stores the
    // leading edge as float, reloads it, then adds the full extent.
#if defined(_M_IX86)
    mikudancestudio::mmd_math::boneMarkerBounds(x, y, scale, &l, &t, &r, &b);
#else
    l = static_cast<float>(x - scale * 9.5);
    r = static_cast<float>(l + scale * 18.0);
    t = static_cast<float>(y - scale * 9.5);
    b = static_cast<float>(t + scale * 18.0);
#endif
    *left = l;
    *top = t;
    *right = r;
    *bottom = b;
}

void AppendQuad(SpriteVertex*& out, float left, float top, float right, float bottom, float u0,
                float v0, float u1, float v1) {
    constexpr D3DCOLOR white = 0xFFFFFFFFu;
    const SpriteVertex quad[6] = {
        {right, top, 0.0f, 1.0f, white, u1, v0},    {right, bottom, 0.0f, 1.0f, white, u1, v1},
        {left, top, 0.0f, 1.0f, white, u0, v0},     {left, top, 0.0f, 1.0f, white, u0, v0},
        {right, bottom, 0.0f, 1.0f, white, u1, v1}, {left, bottom, 0.0f, 1.0f, white, u0, v1},
    };
    for (const SpriteVertex& vertex : quad)
        *out++ = vertex;
}

bool AppendBoneV(LineVertex*& out, int startX, int startY, int endX, int endY, float scale,
                 D3DCOLOR color) {
    const int dx = endX - startX;
    const int dy = endY - startY;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
    if (length == 0.0f)
        return false;

    const LineVertex vertices[4] = {
        {BoneVCoordinate(-dy, startX, length, scale), BoneVCoordinate(dx, startY, length, scale),
         0.0f, 1.0f, color},
        {static_cast<float>(endX), static_cast<float>(endY), 0.0f, 1.0f, color},
        {static_cast<float>(endX), static_cast<float>(endY), 0.0f, 1.0f, color},
        {BoneVCoordinate(dy, startX, length, scale), BoneVCoordinate(-dx, startY, length, scale),
         0.0f, 1.0f, color},
    };
    for (const LineVertex& vertex : vertices)
        *out++ = vertex;
    return true;
}

void AppendLine(LineVertex*& out, int startX, int startY, int endX, int endY, D3DCOLOR color) {
    *out++ = {static_cast<float>(startX), static_cast<float>(startY), 0.0f, 1.0f, color};
    *out++ = {static_cast<float>(endX), static_cast<float>(endY), 0.0f, 1.0f, color};
}

} // namespace

void PrepareFrameSpriteOverlay(MMDApp* app) {
    if (app->RecordingWindow() != nullptr)
        return;

    app->ViewportRenderer().ClearOverlay(EditorOverlay::Sprites);
    auto* vb = app->SpriteOverlayVertices();
    if (vb == nullptr)
        return;

    SpriteVertex* out = nullptr;
    if (FAILED(vb->Lock(0, 0x445C0, reinterpret_cast<void**>(&out), 0)))
        return;
    SpriteVertex* const spriteBegin = out;
    app->SpriteOverlayPrimitiveCount() = 0;

    const auto append = [&](float left, float top, float right, float bottom, float u0, float v0,
                            float u1, float v1) {
        AppendQuad(out, left, top, right, bottom, u0, v0, u1, v1);
        app->SpriteOverlayPrimitiveCount() += 2;
    };

    D3DRenderer* sub = app->Renderer();
    const float scale = sub != nullptr ? sub->viewScale : 1.0f;
    const RECT view = app->ViewportRect();
    const float right = static_cast<float>(view.right);
    const float bottom = static_cast<float>(view.bottom);

    // 0x499BD0: bone projection plus the bone-line and bone-icon producers.
    // The original runs this after WORLD/VIEW/PROJECTION are installed and
    // appends to the line batch built by 0x4757C3 on the previous frame.
    if (app->CameraMode() == 0 && app->PlaybackActive() == 0 && sub != nullptr) {
        auto* model = app->SelectedModel();
        auto* device = sub->device;
        if (model != nullptr && device != nullptr) {
            const std::uint32_t spriteCountBeforeBone = app->SpriteOverlayPrimitiveCount();
            BoneOverlayResult boneResult{393939, 393939, 0.0f, 0, app->LineOverlayPrimitiveCount()};
            auto* bones = model->boneTable;
            const int boneCount = model->boneCount;
            const int selected = model->selectedBone;
            auto* active = model->boneSelection;
            auto* secondary = model->bonePhysicsState;
            const mdl::IkChain* relationships = model->ikChains;
            const int relationshipCount = static_cast<int>(model->ikChainCount);
            const int cursorKind = static_cast<int>(app->EditMode());
            // 658628 = playbackPhysicsMode (formerly misnamed boneFilter)
            const int physMode = app->PlaybackPhysicsMode();
            D3DMATRIX world{};
            D3DMATRIX viewMatrix{};
            D3DMATRIX projection{};
            device->GetTransform(D3DTS_WORLD, &world);
            device->GetTransform(D3DTS_VIEW, &viewMatrix);
            device->GetTransform(D3DTS_PROJECTION, &projection);

            for (int index = 0; bones != nullptr && index < boneCount; ++index) {
                auto* bone = &bones[index];
                float w = 0.0f;
                ProjectBonePoint(*bone, bone->position, world, viewMatrix, projection, view,
                                 &bone->screenX, &bone->screenY, &w);
                if (index == selected) {
                    boneResult.selectedX = bone->screenX;
                    boneResult.selectedY = bone->screenY;
                    boneResult.selectedClipW = w;
                }
                if (model->physicsMode == 2 && (bone->flags & mdl::kBoneFlagTailIsBone) == 0) {
                    float secondaryW = 0.0f;
                    ProjectBonePoint(*bone, bone->tailOffset, world, viewMatrix, projection, view,
                                     &bone->tailScreenX, &bone->tailScreenY, &secondaryW);
                }
            }

            // 0x49A1DC..0x49AE48: cursor modes 0/1 draw each bone as a
            // two-primitive V. Active relationship records may deliberately
            // emit the same V more than once with the relationship color.
            if (bones != nullptr) {
                // The renderer's line buffer is shared by bone indicators and
                // the final guide-line overlay pass.
                auto* lineVb = sub->lineVertexBuffer;
                const std::uint32_t incomingLineCount = boneResult.linePrimitiveCount;
                LineVertex* lines = nullptr;
                if (lineVb != nullptr && incomingLineCount < 5000 &&
                    SUCCEEDED(lineVb->Lock(40 * incomingLineCount, 40 * (5000 - incomingLineCount),
                                           reinterpret_cast<void**>(&lines), 0))) {
                    boneResult.linePrimitiveCount += cpu_bone::OverlayLines(
                        boneCount, [&](int i) -> const auto& { return bones[i]; },
                        relationships ? relationshipCount : 0,
                        [&](int i) -> const auto& { return relationships[i]; },
                        [&](int i) { return active && active[i]; },
                        [&](int i) { return secondary && secondary[i]; }, selected, cursorKind,
                        physMode, model->physicsMode == 2, scale,
                        [&](int x, int y, int ex, int ey, float size, std::uint32_t color) {
                            return AppendBoneV(lines, x, y, ex, ey, size, color);
                        },
                        [&](int x, int y, int ex, int ey, std::uint32_t color) {
                            AppendLine(lines, x, y, ex, ey, color);
                        });
                    LineVertex* const lineBegin = lines - 2 * boneResult.linePrimitiveCount;
                    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Lines, lineBegin,
                                                           boneResult.linePrimitiveCount * 2u,
                                                           sizeof(LineVertex));
                    DumpVertexBatch("line", lineBegin, boneResult.linePrimitiveCount, 40);
                    lineVb->Unlock();
                    // 0x46BF4D adds the returned absolute line counter to
                    // the global count; retain that original quirk exactly.
                    app->LineOverlayPrimitiveCount() += boneResult.linePrimitiveCount;
                }
            }

            if (bones != nullptr) {
                cpu_bone::OverlayMarkers(
                    boneCount, [&](int i) -> const auto& { return bones[i]; },
                    [&](int i) { return active && active[i]; },
                    [&](int i) { return secondary && secondary[i]; }, selected, cursorKind,
                    physMode,
                    [&](int index, float v0, float v1) {
                        float left, top, right, bottom;
                        BoneMarkerBounds(bones[index].screenX, bones[index].screenY, scale, &left,
                                         &top, &right, &bottom);
                        append(left, top, right, bottom, 0.46414399f, v0, 0.49874699f, v1);
                    });
            }

            boneResult.spritePrimitiveCount =
                app->SpriteOverlayPrimitiveCount() - spriteCountBeforeBone;
            app->ViewportToolCenterX() = boneResult.selectedX;
            app->ViewportToolCenterY() = boneResult.selectedY;
            app->state.selectedClipW = boneResult.selectedClipW;
        }
    }

    // 0x46C297..0x46C6C5: with no selected model, the camera look-at point
    // projects to the exact viewport centre. The original uses asymmetric
    // -9.5/+8.5 extents so texels land on the same pixels.
    if (app->CameraMode() != 0 && app->PlaybackActive() == 0 && app->CameraParentModel() < 0) {
        const float cx = static_cast<float>((view.left + view.right) / 2);
        const float cy = static_cast<float>((view.top + view.bottom) / 2);
        append(cx - scale * 9.5f, cy - scale * 9.5f, cx + scale * 8.5f, cy + scale * 8.5f,
               0.46414399f, 0.35086f, 0.49874699f, 0.385463f);
    }

    render::ViewportTools tools;
    tools.camera = app->CameraMode() != 0;
    tools.playing = app->PlaybackActive() != 0;
    tools.coordinateSystem = app->state.coordinateSystem;
    tools.boneMode = static_cast<std::int32_t>(app->state.v32c);
    tools.operation = static_cast<int>(app->ViewportToolOperation());
    tools.editMode = static_cast<int>(app->EditMode());
    tools.centerX = app->ViewportToolCenterX();
    tools.centerY = app->ViewportToolCenterY();
    render::AppendViewportTools(tools, right, bottom, scale, append);

    app->ViewportRenderer().CaptureOverlay(EditorOverlay::Sprites, spriteBegin,
                                           app->SpriteOverlayPrimitiveCount() * 3u,
                                           sizeof(SpriteVertex));
    DumpVertexBatch("sprite", spriteBegin, app->SpriteOverlayPrimitiveCount(), 84);
    vb->Unlock();
}

} // namespace mikudancestudio
