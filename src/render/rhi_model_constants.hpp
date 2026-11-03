#pragma once
#include "mikudancestudio/rhi_model_pipeline.hpp"
#include "mikudancestudio/rhi_model_shader_constants.hpp"

namespace mikudancestudio::rhi::detail {
struct PostProcessConstants {
    std::array<float, 4> multiply, add, uvRect;
    std::array<unsigned, 4> options;
    std::array<unsigned, 4> viewport;
};
inline PostProcessConstants BuildPostProcessConstants(const ModelPipeline& p) {
    const auto& s = p.postProcess;
    return {s.multiply,
            s.add,
            s.uvRect,
            {unsigned(s.mode), unsigned(s.linear), p.viewport ? p.viewport->width : 0,
             p.viewport ? p.viewport->height : 0},
            {p.viewport ? p.viewport->x : 0, p.viewport ? p.viewport->y : 0,
             p.viewport ? p.viewport->width : 0, p.viewport ? p.viewport->height : 0}};
}
inline BuiltinShadowConstants BuildBuiltinShadowConstants(const ModelPipeline& p,
                                                          const ModelViewport& viewport) {
    const auto& s = p.shadow;
    BuiltinShadowConstants c{s.lightViewProjection,
                             s.worldViewProjection,
                             s.rotation,
                             s.color,
                             s.toonColor,
                             s.specular,
                             s.lightDirection,
                             s.eye,
                             {unsigned(s.mode), unsigned(s.perspective), unsigned(s.transparent),
                              s.receive ? 65536u : 0},
                             {float(viewport.width), float(viewport.height), 0, 0},
                             s.textureMultiply,
                             s.textureAdd,
                             s.sphereMultiply,
                             s.sphereAdd,
                             s.lightingWorld,
                             s.diffuse};
    if (s.accessory)
        c.options[3] |= 262144u;
    if (s.textureMultiply != std::array<float, 4>{1, 1, 1, 1} ||
        s.sphereMultiply != std::array<float, 4>{1, 1, 1, 1} ||
        s.textureAdd != std::array<float, 4>{} || s.sphereAdd != std::array<float, 4>{})
        c.options[3] |= 131072u;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& t = p.shader.layout.stages[i];
        const unsigned role = t.role == MaterialTextureRole::Base     ? 1
                              : t.role != MaterialTextureRole::Sphere ? 0
                              : t.coordinates == MaterialCoordinates::MeshUv1
                                  ? (t.color == MaterialCombine::Add ? 5 : 4)
                              : t.color == MaterialCombine::Add ? 3
                                                                : 2;
        c.options[3] |= role << (i * 4);
    }
    return c;
}
struct ModelPixelConstants {
    std::array<std::array<unsigned, 4>, 3> stages{};
    std::array<unsigned, 4> alpha{};
};
struct ModelConstants {
    ModelVertexConstants vertex{};
    ModelPixelConstants pixel{};
    ModelCompatibilityConstants compatibility{};
};
// Measured D3DX9_43 x64 matrix product. Rows 0/3 use paired sums and rows
// 1/2 accumulate left to right; every product/sum rounds to float32.
// Native API state and resource ownership stay inside each device.
inline std::array<float, 16> Dx9MatrixMultiply(const std::array<float, 16>& a,
                                               const std::array<float, 16>& b) {
    std::array<float, 16> out{};
    for (unsigned r = 0; r < 4; ++r)
        for (unsigned c = 0; c < 4; ++c) {
            volatile float products[4];
            for (unsigned k = 0; k < 4; ++k) {
                products[k] = a[r * 4 + k] * b[k * 4 + c];
            }
            volatile float sum = products[0] + products[1];
            if (r == 0 || r == 3) {
                volatile float tail = products[2] + products[3];
                sum = sum + tail;
            } else {
                sum = sum + products[2];
                sum = sum + products[3];
            }
            out[r * 4 + c] = sum;
        }
    return out;
}
inline bool BuildModelConstants(const ModelPipeline& p, const ModelViewport& viewport,
                                bool measured, ModelConstants& out) {
    const auto& s = p.shader;
    auto& vc = out.vertex;
    auto& pc = out.pixel;
    vc.w = s.world;
    vc.v = s.view;
    vc.p = s.projection;
    vc.n = s.normalView;
    vc.transforms = s.textureTransforms;
    vc.diffuse = s.nativeDiffuse;
    vc.ambient = s.materialAmbient;
    vc.specular = s.materialSpecular;
    vc.emissive = s.emissive;
    vc.globalAmbient = s.globalAmbient;
    vc.flags = {s.lighting, s.normalizeNormals, s.specularEnabled, s.localViewer};
    vc.parameters = {s.shininess, float(viewport.width), float(viewport.height), 0};
    vc.combined = Dx9MatrixMultiply(Dx9MatrixMultiply(vc.w, vc.v), vc.p);
    if (measured) {
        auto& worldView = out.compatibility.worldView;
        worldView = Dx9MatrixMultiply(vc.w, vc.v);
        out.compatibility.combined = Dx9MatrixMultiply(worldView, vc.p);
        const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (p.vertexLayout != VertexLayout::PositionColor &&
            !BuildShaderNormalMatrix(worldView, identity, vc.n))
            return false;
    }
    for (unsigned i = 0; i < 8; ++i) {
        const auto& l = s.lights[i];
        vc.direction[i] = {l.direction[0], l.direction[1], l.direction[2], l.enabled ? 1.f : 0.f};
        vc.lightDiffuse[i] = l.diffuse;
        vc.lightAmbient[i] = l.ambient;
        vc.lightSpecular[i] = l.specular;
    }
    pc.alpha = {s.alphaTest ? (s.alphaTestFixed12 ? 2u : 1u) : 0u,
                static_cast<unsigned>(s.alphaCompare), s.alphaReference,
                s.lighting && s.specularEnabled};
    for (unsigned i = 0; i < 3; ++i) {
        vc.coordinates[i] = static_cast<unsigned>(s.layout.stages[i].coordinates);
        pc.stages[i] = {s.layout.stages[i].role != MaterialTextureRole::None,
                        static_cast<unsigned>(s.layout.stages[i].color), 0, 0};
    }
    vc.coordinates[3] = s.colorVertex ? 256u : 0u;
    for (unsigned i = 0; i < 4; ++i)
        vc.coordinates[3] |= unsigned(s.materialSources[i]) << (i * 2);
    return true;
}
}
