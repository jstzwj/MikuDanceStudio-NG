#pragma once
#include "editor_session.hpp"
#include "scene_effects.hpp"
#include <functional>
namespace mikudancestudio::render {
enum class ExportAlpha {
    Straight,
    Opaque
};
struct ExportSettings {
    // Inclusive MMD frame range; its time interval is [first/30, (last+1)/30).
    unsigned first = 0, last = 300, width = 640, height = 480;
    unsigned rateNumerator = 30, rateDenominator = 1;
    ExportAlpha alpha = ExportAlpha::Straight;
    std::array<std::uint8_t, 3> background{255, 255, 255};
};
struct ExportTimeline {
    ExportSettings settings;
    unsigned frames = 0;
    double SourceSeconds(unsigned index) const;
    std::int64_t TimestampUs(unsigned index) const;
    std::uint64_t AudioSample(unsigned index, unsigned rate) const;
    std::uint64_t SourceAudioSample(unsigned rate) const;
};
bool MakeExportTimeline(const ExportSettings&, ExportTimeline&, std::string&);
// Capture on the owner thread before starting a worker. Documents are values;
// immutable model/image resources are shared. Never borrows a live EditorSession.
struct ExportSnapshot {
    format::PmmSceneDocument document;
    editor::EditorAssets assets;
    editor::EffectDocument effects;
    SceneDisplayOptions display;
    std::uint64_t effectRevision = 0;
    std::filesystem::path sharedToons;
};
ExportSnapshot CaptureExport(const editor::EditorSession&);
struct ExportFrame {
    unsigned index = 0;
    std::int64_t startUs = 0, endUs = 0;
    std::vector<std::uint8_t> rgba;
};
using ExportProgress = std::function<bool(unsigned completed, unsigned total)>;
using ExportFrameSink = std::function<bool(const ExportFrame&, std::string&)>;
// No Qt, file encoding or visible surface. Synchronous bounded handoff: the next
// frame is not produced until the sink accepts this one. Callbacks run on caller.
bool RunExport(const ExportSnapshot&, const std::filesystem::path& builtins, rhi::BackendOptions,
               const ExportSettings&, const ExportFrameSink&, const ExportProgress&, std::string&);
}
