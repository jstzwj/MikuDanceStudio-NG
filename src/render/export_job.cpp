#include "mikudancestudio/export_job.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include <cmath>
#include <numeric>
namespace mikudancestudio::render {
double ExportTimeline::SourceSeconds(unsigned index) const {
    return double(settings.first) / 30 +
           double(index) * settings.rateDenominator / settings.rateNumerator;
}
std::int64_t ExportTimeline::TimestampUs(unsigned index) const {
    return std::int64_t(std::uint64_t(index) * settings.rateDenominator * 1000000 /
                        settings.rateNumerator);
}
std::uint64_t ExportTimeline::AudioSample(unsigned index, unsigned rate) const {
    return std::uint64_t(index) * settings.rateDenominator * rate / settings.rateNumerator;
}
std::uint64_t ExportTimeline::SourceAudioSample(unsigned rate) const {
    return std::uint64_t(settings.first) * rate / 30;
}
bool MakeExportTimeline(const ExportSettings& s, ExportTimeline& out, std::string& reason) {
    if (s.first > s.last || s.last >= 30u * 60 * 60 * 6 || !s.width || !s.height ||
        s.width > 8192 || s.height > 8192 || std::uint64_t(s.width) * s.height > 16777216 ||
        !s.rateNumerator || !s.rateDenominator || s.rateNumerator > 120000 ||
        s.rateDenominator > 10000 || double(s.rateNumerator) / s.rateDenominator < 1 ||
        double(s.rateNumerator) / s.rateDenominator > 120 ||
        (s.alpha != ExportAlpha::Straight && s.alpha != ExportAlpha::Opaque)) {
        reason = "invalid-export-range-size-rate-or-alpha";
        return false;
    }
    ExportTimeline next;
    next.settings = s;
    auto divisor = std::gcd(s.rateNumerator, s.rateDenominator);
    next.settings.rateNumerator /= divisor;
    next.settings.rateDenominator /= divisor;
    const auto denominator = std::uint64_t(30) * next.settings.rateDenominator;
    next.frames = unsigned(
        (std::uint64_t(s.last - s.first + 1) * next.settings.rateNumerator + denominator - 1) /
        denominator);
    out = next;
    reason.clear();
    return true;
}
ExportSnapshot CaptureExport(const editor::EditorSession& s) {
    return {s.Document(), s.Assets(), s.Effects(), s.DisplayOptions(), s.EffectRevision()};
}
bool RunExport(const ExportSnapshot& snapshot, const std::filesystem::path& builtins,
               rhi::BackendOptions options, const ExportSettings& requested,
               const ExportFrameSink& sink, const ExportProgress& progress, std::string& reason) {
    ExportTimeline timeline;
    if (!MakeExportTimeline(requested, timeline, reason) || !sink)
        return false;
    const auto& spec = timeline.settings;
    auto report = [&](unsigned done) {
        if (progress && !progress(done, timeline.frames)) {
            reason = "export-cancelled";
            return false;
        }
        return true;
    };
    if (!report(0))
        return false;
    format::SceneSession document;
    if (!format::SceneSession::Create(snapshot.document, document, reason))
        return false;
    runtime::SceneRuntime scene;
    if (!scene.LoadResources(std::move(document), snapshot.assets.models, reason))
        return false;
    for (const auto& a : snapshot.assets.accessories)
        if (!scene.AttachAccessory(a.first, a.second, reason))
            return false;
    const format::PmmSceneApplication application{true, true,
                                                  format::PmmPlaybackArithmetic::NativeFloat};
    if (!scene.Seek(0, reason) || !scene.StartPlayback(reason, application))
        return false;
    options.surface.reset();
    auto backend = rhi::CreateOffscreenBackendDevice(spec.width, spec.height, options);
    if (!backend) {
        reason = backend.reason;
        return false;
    }
    rhi::FileAssetSource files(builtins);
    SceneFrameProducer producer;
    SceneEffects effects;
    SceneRenderSources sources;
    sources.models = snapshot.assets.modelPaths;
    for (const auto& p : snapshot.assets.modelTexturePaths)
        sources.models[p.first] = p.second;
    sources.accessories = snapshot.assets.accessoryPaths;
    sources.modelToons = snapshot.assets.modelToons;
    sources.picture = snapshot.assets.picture;
    sources.sharedToons = snapshot.sharedToons;
    if (!producer.Load(scene, sources, files, reason))
        return false;
    SceneRenderSettings settings;
    settings.width = spec.width;
    settings.height = spec.height;
    settings.display = snapshot.display;
    unsigned samples = 1;
    if (snapshot.display.antialiasing) {
        const auto mask =
            backend.device->FormatFeatures(rhi::TextureFormat::Rgba8).sampleCounts &
            backend.device->FormatFeatures(rhi::TextureFormat::DepthStencil).sampleCounts;
        for (auto n : {8u, 4u, 2u})
            if (mask & n) {
                samples = n;
                break;
            }
    }
    rhi::SceneRenderer renderer(*backend.device);
    rhi::PreparedModelFrame prepared;
    rhi::CompletionToken token;
    auto draw = [&](bool output, unsigned index) {
        rhi::RenderFrame base, frame;
        if (!producer.Build(scene, settings, base, reason) ||
            !effects.Build(snapshot.effects, snapshot.effectRevision, scene, sources, settings,
                           options, base, frame, reason))
            return false;
        auto result = rhi::ApplyFrameMultisampling(frame, samples);
        if (result)
            result = renderer.Prepare(1, frame, prepared);
        if (result)
            result = renderer.Submit(prepared, token);
        if (!result) {
            effects.Abandon();
            reason = result.reason;
            return false;
        }
        producer.Commit();
        if (!effects.Commit(reason))
            return false;
        if (output) {
            rhi::AttachmentReadback pixels;
            if (!renderer.ReadTarget(1, frame.finalColor, pixels)) {
                reason = "export-readback-failed";
                return false;
            }
            ExportFrame exported{index, timeline.TimestampUs(index),
                                 timeline.TimestampUs(index + 1), std::move(pixels.bytes)};
            if (spec.alpha == ExportAlpha::Opaque)
                for (std::size_t n = 0; n < exported.rgba.size(); n += 4) {
                    const unsigned a = exported.rgba[n + 3];
                    for (unsigned c = 0; c < 3; ++c)
                        exported.rgba[n + c] = std::uint8_t(
                            (exported.rgba[n + c] * a + spec.background[c] * (255 - a) + 127) /
                            255);
                    exported.rgba[n + 3] = 255;
                }
            if (!sink(exported, reason))
                return false;
        }
        if (!renderer.Collect()) {
            reason = "export-resource-collection-failed";
            return false;
        }
        return true;
    };
    // Warm up at the original 30 Hz. Extra output samples use exact rational
    // times; merged 30 Hz checkpoints bound every native Advance interval.
    // Native physics retains its 1/60 catchup and remainder semantics. We never
    // derive simulation deltas from render/encode wall time.
    for (unsigned n = 0; n < spec.first; ++n) {
        if (!report(0))
            return false;
        if (n && !scene.Advance(float(double(n) / 30), 1.f / 30, reason, application))
            return false;
        if ((snapshot.document.view.captureMode || producer.HasMaterialHistory() ||
             !snapshot.effects.assignments.empty()) &&
            !draw(false, 0))
            return false;
    }
    double cursor = spec.first ? double(spec.first - 1) / 30 : 0;
    unsigned checkpoint = spec.first ? spec.first : 1;
    for (unsigned n = 0; n < timeline.frames; ++n) {
        if (!report(n))
            return false;
        const auto target = timeline.SourceSeconds(n);
        while (double(checkpoint) / 30 < target - 1e-10) {
            const double time = double(checkpoint++) / 30;
            if (!scene.Advance(float(time), float(time - cursor), reason, application))
                return false;
            cursor = time;
            if (!report(n))
                return false;
        }
        if (target > cursor) {
            if (!scene.Advance(float(target), float(target - cursor), reason, application))
                return false;
            cursor = target;
        }
        if (std::abs(double(checkpoint) / 30 - target) < 1e-10)
            ++checkpoint;
        if (!draw(true, n))
            return false;
    }
    if (!report(timeline.frames))
        return false;
    reason.clear();
    return true;
}
}
