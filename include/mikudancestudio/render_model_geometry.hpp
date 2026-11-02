#pragma once
#include "rhi_model_replay.hpp"
#include "model_geometry_vertices.hpp"
#include <variant>

namespace mikudancestudio::render {
// CPU deformation output. The arithmetic producer owns these records; no
// native buffer, device identity or GPU lifetime is part of this contract.
struct ModelGeometrySnapshot {
    std::uint32_t vertexCount = 0, additionalUvCount = 0;
    rhi::ModelReplayResource main, edge, indices;
};

// Single producer thread. Staging is mutable; published snapshots own their
// bytes and can be copied to a render thread without borrowing staging memory.
// IDs are supplied by the scene owner and never reused while that scene lives.
class ModelGeometry {
public:
    ModelGeometry(std::uint64_t firstId, std::uint32_t count, unsigned additionalUv = 0);
    void* MainData();
    const void* MainData() const;
    std::size_t MainSize() const;
    std::vector<ModelEdgeVertex>& Edges() { return edges; }
    const std::vector<ModelEdgeVertex>& Edges() const { return edges; }
    unsigned AdditionalUvCount() const { return snapshot.additionalUvCount; }
    // Already-normalized contiguous indices, not loader-specific padding.
    bool SetIndices(rhi::IndexFormat, const void*, std::size_t);
    // A false flag promises that channel's staging has not changed. It must
    // already be published. This lets camera-only frames rebuild outlines
    // without scanning/copying immutable material vertices again.
    bool Publish(bool mainDirty = true, bool edgeDirty = true);
    const ModelGeometrySnapshot& Snapshot() const { return snapshot; }

private:
    using MainStorage =
        std::variant<std::vector<ExtendedModelVertex<0>>, std::vector<ExtendedModelVertex<1>>,
                     std::vector<ExtendedModelVertex<2>>, std::vector<ExtendedModelVertex<3>>,
                     std::vector<ExtendedModelVertex<4>>>;
    MainStorage main;
    std::vector<ModelEdgeVertex> edges;
    std::vector<std::uint8_t> outlineBytes;
    ModelGeometrySnapshot snapshot;
};

// Appends only the geometry used by this draw, preserving every additional UV.
rhi::ModelReplayResult AppendModelGeometry(rhi::ModelReplayStream&, const ModelGeometrySnapshot&,
                                           rhi::ModelReplayDraw, bool edge = false);
}
