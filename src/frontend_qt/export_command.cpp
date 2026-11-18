#include "export_command.hpp"
#include "editor_window.hpp"
#include "video_encoder.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace mikudancestudio::qt {
namespace {
int Report(const std::string& reason, unsigned frames = 0) {
    const bool success = reason.empty();
    const char* status = success                        ? "complete"
                         : reason == "export-cancelled" ? "cancelled"
                                                        : "failed";
    const auto json = QJsonDocument(QJsonObject{{"version", 1},
                                                {"status", status},
                                                {"frames", int(frames)},
                                                {"reason", QString::fromStdString(reason)}})
                          .toJson(QJsonDocument::Compact);
    std::cout << json.constData() << '\n';
    return success ? 0 : reason == "export-cancelled" ? 3 : 1;
}

bool BackendFor(const QString& name, rhi::Backend& backend) {
    if (name == "default")
        backend = rhi::DefaultBackend();
    else if (name == "dx11")
        backend = rhi::Backend::D3D11;
    else if (name == "dx12")
        backend = rhi::Backend::D3D12;
    else if (name == "vulkan")
        backend = rhi::Backend::Vulkan;
    else if (name == "metal")
        backend = rhi::Backend::Metal;
    else
        return false;
    return rhi::BackendAvailable(backend);
}

int Export(const QString& jobPath, const std::filesystem::path& assets) {
    QFile input(jobPath);
    if (!input.open(QIODevice::ReadOnly))
        return Report("export-job-read-failed");
    constexpr qint64 maxBytes = 64 * 1024;
    const auto bytes = input.read(maxBytes + 1);
    if (input.error() != QFileDevice::NoError || bytes.size() > maxBytes)
        return Report("export-job-read-failed");
    const auto job = QJsonDocument::fromJson(bytes).object();
    if (job["version"].toInt(-1) != 1)
        return Report("export-job-version-unsupported");
    const QDir base(QFileInfo(input).absolutePath());
    const auto project = job["project"].toString(), destination = job["destination"].toString();
    if (project.isEmpty() || destination.isEmpty() || project.contains(QChar::Null) ||
        destination.contains(QChar::Null))
        return Report("export-job-path-invalid");
    const auto format = job["format"].toString();
    if (format != "png" && format != "mp4" && format != "mp4-pcm")
        return Report("export-job-format-invalid");
    if ((job.contains("audio") && !job["audio"].isBool()) ||
        (job.contains("alpha") && !job["alpha"].isBool()) ||
        (job.contains("cancelFile") && !job["cancelFile"].isString()))
        return Report("export-job-field-invalid");
    render::ExportSettings settings;
    const auto number = [&](const char* key, unsigned fallback) {
        return job.contains(key) ? unsigned(job[key].toInt(-1)) : fallback;
    };
    settings.first = number("first", settings.first);
    settings.last = number("last", settings.last);
    settings.width = number("width", settings.width);
    settings.height = number("height", settings.height);
    settings.rateNumerator = number("rateNumerator", settings.rateNumerator);
    settings.rateDenominator = number("rateDenominator", settings.rateDenominator);
    settings.alpha = job["alpha"].toBool(format == "png") ? render::ExportAlpha::Straight
                                                          : render::ExportAlpha::Opaque;
    std::string reason;
    render::ExportTimeline timeline;
    if (!render::MakeExportTimeline(settings, timeline, reason))
        return Report(reason);
    rhi::BackendOptions options;
    if (!BackendFor(job["backend"].toString(job.contains("backend") ? "" : "default"),
                    options.backend))
        return Report("export-job-backend-unavailable");
    const auto driver = job["driver"].toString(job.contains("driver") ? "" : "hardware");
    if (driver != "hardware" && driver != "software")
        return Report("export-job-driver-invalid");
    if (driver == "software")
        options.driver = rhi::BackendDriver::Software;
    const auto cancel = job["cancelFile"].toString();
    if (cancel.contains(QChar::Null))
        return Report("export-job-path-invalid");
    const auto cancelPath = cancel.isEmpty() ? QString{} : base.absoluteFilePath(cancel);
    const auto proceed = [&](unsigned, unsigned) {
        return cancelPath.isEmpty() || !QFileInfo::exists(cancelPath);
    };
    if (!proceed(0, timeline.frames))
        return Report("export-cancelled");
    editor::EditorSession session;
    if (!session.Open(Path(base.absoluteFilePath(project)), reason))
        return Report(reason);
    auto snapshot = render::CaptureExport(session);
    snapshot.sharedToons = Path(QCoreApplication::applicationDirPath()) / "data";
    const auto target = Path(base.absoluteFilePath(destination));
    const bool ok = format == "png"
                        ? ExportImages(snapshot, assets, options, target, settings, proceed, reason)
                        : ExportMovie(snapshot, assets, options, target, settings,
                                      format == "mp4" ? VideoProfile::Mp4 : VideoProfile::Mp4Pcm,
                                      job["audio"].toBool(true), proceed, reason);
    return ok ? Report({}, timeline.frames) : Report(reason.empty() ? "export-job-failed" : reason);
}
}

int RunExportCommand(const QString& jobPath, const std::filesystem::path& assets) {
    try {
        return Export(jobPath, assets);
    } catch (const std::exception&) {
        return Report("export-job-exception");
    }
}
}
