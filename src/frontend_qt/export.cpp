#include "export.hpp"
#include "editor_window.hpp"
#include <QTemporaryDir>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
namespace mikudancestudio::qt {
bool ExportSequence(const editor::EditorSession& editor, const std::filesystem::path& builtins,
                    const rhi::BackendOptions& options, const std::filesystem::path& destination,
                    unsigned first, unsigned last, unsigned width, unsigned height,
                    const ExportProgress& progress, std::string& reason) {
    if (first > last || last > 10000000 || !width || !height || width > 4096 || height > 4096) {
        reason = "invalid-export-range-or-size";
        return false;
    }
    const auto target = std::filesystem::absolute(destination).lexically_normal();
    if (std::filesystem::exists(target)) {
        reason = "export-destination-exists";
        return false;
    }
    if (!std::filesystem::is_directory(target.parent_path())) {
        reason = "export-parent-directory-missing";
        return false;
    }
    QTemporaryDir staging(Path(target.parent_path() / ".mds-export-XXXXXX"));
    if (!staging.isValid()) {
        reason = "export-staging-unavailable";
        return false;
    }
    const auto total = last - first + 1;
    if (progress && !progress(0, total)) {
        reason = "export-cancelled";
        return false;
    }
    format::SceneSession session;
    if (!format::SceneSession::Create(editor.Document(), session, reason))
        return false;
    runtime::SceneRuntime scene;
    if (!scene.LoadResources(std::move(session), editor.Assets().models, reason))
        return false;
    for (const auto& a : editor.Assets().accessories)
        if (!scene.AttachAccessory(a.first, a.second, reason))
            return false;
    if (!scene.Seek(0, reason) || !scene.StartPlayback(reason))
        return false;
    auto backend = rhi::CreateOffscreenBackendDevice(width, height, options);
    if (!backend) {
        reason = backend.reason;
        return false;
    }
    rhi::FileAssetSource files(builtins);
    render::SceneFrameProducer producer;
    render::SceneRenderSources sources;
    sources.models = editor.Assets().modelPaths;
    sources.accessories = editor.Assets().accessoryPaths;
    sources.picture = editor.Assets().picture;
    if (!producer.Load(scene, sources, files, reason))
        return false;
    render::SceneRenderSettings settings;
    settings.width = width;
    settings.height = height;
    rhi::SceneRenderer renderer(*backend.device);
    rhi::PreparedModelFrame prepared;
    rhi::CompletionToken token;
    for (unsigned index = 0; index <= last; ++index) {
        if (progress && !progress(index < first ? 0 : index - first, total)) {
            reason = "export-cancelled";
            return false;
        }
        if (index && !scene.Advance(float(double(index) / 30), 1.f / 30, reason))
            return false;
        if (index < first && !scene.Session()->Document().view.captureMode &&
            !producer.HasMaterialHistory())
            continue;
        rhi::RenderFrame frame;
        if (!producer.Build(scene, settings, frame, reason))
            return false;
        auto result = renderer.Prepare(1, frame, prepared);
        if (result)
            result = renderer.Submit(prepared, token);
        if (!result) {
            reason = result.reason;
            return false;
        }
        producer.Commit();
        if (index < first) {
            if (!renderer.Collect()) {
                reason = "export-resource-collection-failed";
                return false;
            }
            continue;
        }
        rhi::AttachmentReadback pixels;
        if (!renderer.ReadTarget(1, frame.finalColor, pixels)) {
            reason = "export-readback-failed";
            return false;
        }
        QImage image(pixels.bytes.data(), int(width), int(height), int(width * 4),
                     QImage::Format_RGBA8888);
        const auto name = QString("frame_%1.png").arg(index, 8, 10, QChar('0'));
        QSaveFile file(staging.path() + "/" + name);
        if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
            reason = "export-image-write-failed";
            return false;
        }
        if (!renderer.Collect()) {
            reason = "export-resource-collection-failed";
            return false;
        }
    }
    QJsonObject manifest{{"version", 1},       {"fps", 30},           {"first", int(first)},
                         {"last", int(last)},  {"width", int(width)}, {"height", int(height)},
                         {"alpha", "straight"}};
    QSaveFile metadata(staging.path() + "/sequence.json");
    const auto bytes = QJsonDocument(manifest).toJson();
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(bytes) != bytes.size() ||
        !metadata.commit()) {
        reason = "export-manifest-write-failed";
        return false;
    }
    if (progress && !progress(total, total)) {
        reason = "export-cancelled";
        return false;
    }
    if (!QDir().rename(staging.path(), Path(target))) {
        reason = "export-publish-failed";
        return false;
    }
    staging.setAutoRemove(false);
    reason.clear();
    return true;
}
}
