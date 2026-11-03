#pragma once
#include "model_physics_edit.hpp"
#include "scene_runtime.hpp"
#include "rhi_model_replay.hpp"

namespace mikudancestudio::render {
// Borrowed only for the synchronous preparation call; never retained by a frame.
struct PhysicsEditView {
    const editor::ModelPhysicsEdit* edit = nullptr;
    std::uint8_t slot = 255;
    int body = -1, joint = -1;
    bool jointPage = false;
};
enum class PhysicsGizmoMesh : unsigned {
    Sphere,
    Box,
    Hemisphere,
    Cylinder,
    Joint,
    Axis
};
struct PhysicsGizmoDraw {
    PhysicsGizmoMesh mesh;
    cpu_math::Matrix world;
    std::uint32_t argb;
};
struct PhysicsGizmoRange {
    std::uint32_t first, count;
};
using PhysicsGizmoRanges = std::array<PhysicsGizmoRange, 13>;
// Byte-exact original primitive vertices/indices; three body colors and the
// original two-color joint marker share one immutable vertex/index allocation.
void PhysicsGizmoGeometry(rhi::ModelReplayResource&, rhi::ModelReplayResource&,
                          PhysicsGizmoRanges&);
unsigned PhysicsGizmoRangeIndex(PhysicsGizmoMesh, std::uint32_t argb);
bool PhysicsGizmoDraws(const runtime::SceneRuntime&, const PhysicsEditView&,
                       std::uint32_t initialColor, std::vector<PhysicsGizmoDraw>&,
                       std::uint32_t& nextColor, std::string&);
}
