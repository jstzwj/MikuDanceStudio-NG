#include "export.hpp"
#include "editor_window.hpp"
#include "background_video.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
namespace mikudancestudio::qt {
bool ExportImages(const render::ExportSnapshot& snapshot, const std::filesystem::path& builtins,
                  const rhi::BackendOptions& options, const std::filesystem::path& destination,
                  const render::ExportSettings& settings, const ExportProgress& progress,
                  std::string& reason) {
    render::ExportTimeline timeline;
    if (!render::MakeExportTimeline(settings, timeline, reason))
        return false;
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
    VideoDecoder video;
    if (!render::RunExport(
            snapshot, builtins, options, settings,
            [&](const render::ExportFrame& frame, std::string& error) {
                QImage image(frame.rgba.data(), int(settings.width), int(settings.height),
                             int(settings.width * 4), QImage::Format_RGBA8888);
                const auto name =
                    QString("frame_%1.png").arg(settings.first + frame.index, 8, 10, QChar('0'));
                QSaveFile file(staging.path() + "/" + name);
                if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") ||
                    !file.commit()) {
                    error = "export-image-write-failed";
                    return false;
                }
                return true;
            },
            progress, reason,
            ExportVideoSource(snapshot.assets.videoPath, video,
                              timeline.settings.rateNumerator == 30 &&
                                  timeline.settings.rateDenominator == 1)))
        return false;
    QJsonObject manifest{
        {"version", 2},
        {"fps", double(settings.rateNumerator) / settings.rateDenominator},
        {"rateNumerator", int(timeline.settings.rateNumerator)},
        {"rateDenominator", int(timeline.settings.rateDenominator)},
        {"first", int(settings.first)},
        {"last", int(settings.last)},
        {"frames", int(timeline.frames)},
        {"width", int(settings.width)},
        {"height", int(settings.height)},
        {"startUs", double(std::uint64_t(settings.first) * 1000000 / 30)},
        {"durationUs", double(timeline.TimestampUs(timeline.frames))},
        {"alpha", settings.alpha == render::ExportAlpha::Straight ? "straight" : "opaque"}};
    QSaveFile metadata(staging.path() + "/sequence.json");
    const auto bytes = QJsonDocument(manifest).toJson();
    if (!metadata.open(QIODevice::WriteOnly) || metadata.write(bytes) != bytes.size() ||
        !metadata.commit()) {
        reason = "export-manifest-write-failed";
        return false;
    }
    if (progress && !progress(timeline.frames, timeline.frames)) {
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
bool ExportSequence(const editor::EditorSession& editor, const std::filesystem::path& builtins,
                    const rhi::BackendOptions& options, const std::filesystem::path& destination,
                    unsigned first, unsigned last, unsigned width, unsigned height,
                    const ExportProgress& progress, std::string& reason) {
    auto snapshot = render::CaptureExport(editor);
    snapshot.sharedToons = Path(QCoreApplication::applicationDirPath()) / "data";
    render::ExportSettings settings;
    settings.first = first;
    settings.last = last;
    settings.width = width;
    settings.height = height;
    return ExportImages(snapshot, builtins, options, destination, settings, progress, reason);
}
}
