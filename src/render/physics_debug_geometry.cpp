#include "mikudancestudio/physics_debug_geometry.hpp"
#include <cstring>
#include <algorithm>

namespace mikudancestudio::render {
namespace {
using namespace cpu_math;
#include "../physics/scene_gizmo_data.inc"
constexpr std::uint32_t red = 0xffff0000, green = 0xff9bff00, pink = 0xffff4b4b;
struct Vertex {
    float x, y, z;
    std::uint32_t rgba;
};
Matrix Scale(float x, float y, float z) {
    auto m = Identity();
    m[0] = x;
    m[5] = y;
    m[10] = z;
    return m;
}
Matrix Placement(const float* rotation, const float* position, bool joint = false) {
    auto m = Multiply(RotationZ(rotation[2]), RotationX(rotation[0]));
    if (joint)
        m = Multiply(Scale(1.5f, 1.5f, 1.5f), m);
    return Multiply(Multiply(m, RotationY(rotation[1])),
                    Translation({position[0], position[1], position[2]}));
}
void Shape(std::uint8_t shape, const std::array<float, 3>& size, const Matrix& transform,
           std::uint32_t color, std::vector<PhysicsGizmoDraw>& result) {
    const auto add = [&](PhysicsGizmoMesh mesh, Matrix local) {
        result.push_back({mesh, Multiply(local, transform), color});
    };
    if (shape == 0)
        add(PhysicsGizmoMesh::Sphere, Scale(size[0], size[0], size[0]));
    else if (shape == 1)
        add(PhysicsGizmoMesh::Box, Scale(size[0] * 2, size[1] * 2, size[2] * 2));
    else {
        const float r = size[0], h = size[1];
        add(PhysicsGizmoMesh::Hemisphere, Multiply(Scale(-r, -r, -r), Translation({0, -h, 0})));
        add(PhysicsGizmoMesh::Hemisphere, Multiply(Scale(r, r, r), Translation({0, h, 0})));
        add(PhysicsGizmoMesh::Cylinder, Scale(r, h * 2, r));
    }
}
}
unsigned PhysicsGizmoRangeIndex(PhysicsGizmoMesh mesh, std::uint32_t color) {
    return mesh == PhysicsGizmoMesh::Joint ? 12
                                           : unsigned(mesh) * 3 + (color == red     ? 0
                                                                   : color == green ? 1
                                                                                    : 2);
}
void PhysicsGizmoGeometry(rhi::ModelReplayResource& vertices, rhi::ModelReplayResource& indices,
                          PhysicsGizmoRanges& ranges) {
    std::vector<Vertex> vb;
    std::vector<std::uint16_t> ib;
    const auto append = [&](unsigned range, const unsigned* words, std::size_t count,
                            const unsigned short* elements, std::size_t elementCount,
                            std::uint32_t color, bool originalColors = false) {
        const auto base = std::uint16_t(vb.size());
        ranges[range] = {std::uint32_t(ib.size()), std::uint32_t(elementCount)};
        for (std::size_t i = 0; i < count; ++i) {
            Vertex v;
            std::memcpy(&v, words + 4 * i, sizeof v);
            const auto argb = originalColors ? words[4 * i + 3] : color;
            v.rgba = (argb & 0xff00ff00u) | ((argb & 255) << 16) | ((argb >> 16) & 255);
            vb.push_back(v);
        }
        for (std::size_t i = 0; i < elementCount; ++i)
            ib.push_back(base + elements[i]);
    };
    unsigned c = 0;
    for (auto color : {red, green, pink}) {
        append(c, kGizmoSphere58, 58, kGizmoSphereIdx, 240, color);
        append(3 + c, kGizmoCube8, 8, kGizmoCubeIdx, 24, color);
        append(6 + c, kGizmoSphere33, 33, kGizmoSphere33Idx, 128, color);
        append(9 + c, kGizmoArrow16, 16, kGizmoIdentityIdx, 16, color);
        ++c;
    }
    append(12, kGizmoBoxSel16, 16, kGizmoBoxSelIdx, 72, 0, true);
    vertices.vertexLayout = rhi::VertexLayout::PositionColor;
    vertices.bytes.resize(vb.size() * sizeof(Vertex));
    std::memcpy(vertices.bytes.data(), vb.data(), vertices.bytes.size());
    vertices.bytes.Seal();
    indices.kind = rhi::ReplayResourceKind::Indices;
    indices.indexFormat = rhi::IndexFormat::UInt16;
    indices.bytes.resize(ib.size() * sizeof(std::uint16_t));
    std::memcpy(indices.bytes.data(), ib.data(), indices.bytes.size());
    indices.bytes.Seal();
}
bool PhysicsGizmoDraws(const runtime::SceneRuntime& scene, const PhysicsEditView& view,
                       std::uint32_t initialColor, std::vector<PhysicsGizmoDraw>& result,
                       std::uint32_t& nextColor, std::string& reason) {
    result.clear();
    nextColor = initialColor;
    if (!view.edit) {
        const auto bodies = scene.PhysicsDebugBodies();
        for (bool dynamic : {true, false}) {
            nextColor = dynamic ? red : green;
            for (const auto& body : bodies)
                if (body.dynamic == dynamic)
                    Shape(body.shape, body.size, body.world, nextColor, result);
        }
    } else {
        const auto& edit = *view.edit;
        const auto* snapshot = scene.Snapshot(view.slot);
        if (!snapshot || snapshot->bones.empty()) {
            reason = "physics-preview-model-unavailable";
            return false;
        }
        for (std::size_t i = 0; i < std::max(edit.bodies.size(), edit.joints.size()); ++i) {
            if (edit.Body(int(i))) {
                const auto& body = edit.bodies[i];
                Matrix bone;
                if (!scene.BoneAttachment(view.slot, std::max(0, int(body.boneIndex)), bone,
                                          reason))
                    return false;
                const auto world = Multiply(Placement(body.rotation, body.position), bone);
                bool selected = int(i) == view.body;
                if (view.jointPage) {
                    selected = false;
                    if (view.joint >= 0 && std::size_t(view.joint) < edit.joints.size()) {
                        const auto& joint = edit.joints[view.joint];
                        selected = int(i) == joint.rigidA || int(i) == joint.rigidB;
                    }
                }
                if (selected)
                    nextColor = view.jointPage ? pink : red;
                Shape(body.shape,
                      {body.size[0], body.size[1] * (body.shape > 1 ? .5f : 1), body.size[2]},
                      world, nextColor, result);
                if (selected)
                    nextColor = green;
            }
            if (edit.Joint(int(i))) {
                const auto& joint = edit.joints[i];
                result.push_back({view.jointPage && int(i) == view.joint ? PhysicsGizmoMesh::Axis
                                                                         : PhysicsGizmoMesh::Joint,
                                  Placement(joint.rotation, joint.position, true), 0});
            }
        }
    }
    reason.clear();
    return true;
}
}
