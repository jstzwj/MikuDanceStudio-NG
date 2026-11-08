#include "editor_window.hpp"
#include "export.hpp"
#include "numeric_dialog.hpp"
#include "english_name_dialog.hpp"
#include "model_physics_dialog.hpp"
#include "mikudancestudio/file_drop.hpp"
#include <QApplication>
#include <QAbstractButton>
#include <QCompleter>
#include <QLineEdit>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QScopeGuard>
#include <QLayout>
#include <QImage>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QSaveFile>
#include <cmath>
#include <fstream>
#ifdef __ANDROID__
#include "android_files.hpp"
#endif

namespace mikudancestudio::qt {
using namespace editor;
using namespace panel;
namespace {
bool AtomicSave(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes,
                std::string& error) {
    QSaveFile file(Path(path));
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) !=
            qint64(bytes.size()) ||
        !file.commit()) {
        error = file.errorString().toStdString();
        return false;
    }
    return true;
}
}
bool EditorWindow::Open(const std::filesystem::path& path) {
    std::string r;
    const bool ok = Result(session_.Open(path, r), r);
    if (ok)
        Refresh();
    return ok;
}
bool EditorWindow::Save(const std::filesystem::path& path) {
    std::string r;
    const bool ok = Result(session_.Save(path, AtomicSave, r), r);
    if (ok)
        Refresh();
    return ok;
}
bool EditorWindow::ConfirmUnsaved() {
    if (!session_.Dirty() || testing_)
        return true;
    const auto answer =
        QMessageBox::question(this, language_.Text("app.title"), language_.Text("dialog.unsaved"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;
    FileAction("file.save");
    return !session_.Dirty();
}
void EditorWindow::closeEvent(QCloseEvent* e) {
    bool confirmed;
    if (session_.EnhancedModelDirty() && !testing_) {
        QMessageBox dialog(QMessageBox::NoIcon, language_.Text("dialog.enhance.quit_title"),
                           language_.Text("dialog.enhance.quit"),
                           QMessageBox::Ok | QMessageBox::Cancel, this);
        dialog.setTextFormat(Qt::PlainText);
        dialog.setOption(QMessageBox::Option::DontUseNativeDialog);
        dialog.QDialog::setWindowTitle(language_.Text("dialog.enhance.quit_title"));
        dialog.setWindowFlag(Qt::WindowStaysOnTopHint);
        confirmed = dialog.exec() == QMessageBox::Ok;
    } else
        confirmed = ConfirmUnsaved();
    if (confirmed) {
        if (edgeDialog_)
            edgeDialog_->close();
        if (gravityDialog_)
            gravityDialog_->close();
        if (englishDialog_)
            englishDialog_->close();
        if (physicsDialog_)
            physicsDialog_->close();
        if (separate_)
            separate_->hide();
        e->accept();
    } else
        e->ignore();
}
QString EditorWindow::ChooseFile(FileKind kind, bool save, const QString& title,
                                 const QString& filter, const QString& suggestedName,
                                 bool confirmOverwrite) {
#ifdef __ANDROID__
    ++presentationPauses_;
    const auto resume = qScopeGuard([this] { --presentationPauses_; });
#endif
    const auto initial = fileHistory_.Initial(kind, QCoreApplication::applicationDirPath());
    QFileDialog dialog(separate_ ? separate_ : this, title, initial, filter);
    dialog.setObjectName("editor.fileDialog");
    dialog.setProperty("mmdInitialDirectory", initial);
    dialog.setProperty("mmdFileKind", int(kind));
    dialog.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(save ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, !confirmOverwrite);
    dialog.setOption(QFileDialog::DontUseNativeDialog, testing_);
#ifdef __ANDROID__
    // SAF folders are imported once with all dependencies. File operations then
    // use that local workspace, preserving the core's filesystem/path contract.
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setDirectory(AndroidWorkspaceRoot());
    // The desktop QFileDialog minimum can exceed a landscape phone's entire
    // available height, placing Open/Cancel below the navigation bar. Its file
    // views already scroll; let the outer layout fit the actual safe area.
    dialog.layout()->setSizeConstraint(QLayout::SetNoConstraint);
    dialog.setMinimumSize(280, 160);
    dialog.setWindowFlags(Qt::Window);
    dialog.setWindowState(Qt::WindowMaximized);
    // Touching Open/Cancel must not hide the keyboard on press, move the
    // button during adjustResize, and lose the release. Hardware Tab navigation
    // remains available; text focus survives until the dialog actually closes.
    for (auto* button : dialog.findChildren<QAbstractButton*>())
        button->setFocusPolicy(Qt::TabFocus);
    // QFileDialog's asynchronous filename completer opens a popup after an
    // IME commit. On Android it consumes the first outside touch on Open.
    // Keep completion inline so it cannot intercept the confirmation gesture.
    if (auto* edit = dialog.findChild<QLineEdit*>("fileNameEdit"))
        if (auto* completer = edit->completer())
            completer->setCompletionMode(QCompleter::InlineCompletion);
#endif
    if (save && filter.startsWith("Polygon Model files"))
        dialog.setDefaultSuffix("pmd");
    if (!suggestedName.isEmpty())
        dialog.selectFile(suggestedName);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return {};
    const auto path = dialog.selectedFiles().front();
    fileHistory_.Confirm(kind, path);
    return path;
}
void EditorWindow::FileAction(const QString& id, const QString& inputPath) {
    if (id == "model.enhanced_save") {
        const auto resource = session_.Runtime().Resource(session_.Selected().model);
        if (!resource || resource->SourceKind() != format::ModelKind::Pmd || session_.Playing())
            return;
        const auto path =
            inputPath.isEmpty()
                ? ChooseFile(FileKind::Model, true, language_.Text("enhance.save_title"),
                             "Polygon Model files(*.pmd);;All Files(*.*)")
                : inputPath;
        if (!path.isEmpty()) {
            std::string reason;
            Result(session_.SaveEnhancedModel(session_.Selected().model, Path(path), AtomicSave,
                                              reason),
                   reason);
            Refresh();
        }
        return;
    }
    if (id == "file.lipsync") {
        const auto path = inputPath.isEmpty()
                              ? ChooseFile(FileKind::Vsq, false, language_.Text("dialog.vsq.title"),
                                           "vsq files(*.vsq)")
                              : inputPath;
        if (!path.isEmpty()) {
            LoadLipSync(path);
            Refresh();
        }
        return;
    }
    if (id == "view.screen_size") {
        ScreenSize();
        return;
    }
    if (id == "background.load") {
        const auto path =
            ChooseFile(FileKind::Background, false, language_.Text("background.load_title"),
                       language_.Text("background.image_filter"));
        if (path.isEmpty())
            return;
        std::string r;
        Result(session_.ImportPicture(Path(path), unsigned(viewport_->width()),
                                      unsigned(viewport_->height()), r),
               r);
        Refresh();
        return;
    }
    if (id == "background.show") {
        std::string r;
        // Native toggle is a no-op if no image has ever been loaded.
        if (session_.Document().picture.enabled || session_.Assets().picture)
            Result(session_.ShowPicture(!session_.Document().picture.enabled, r), r);
        Refresh();
        return;
    }
    if (id == "background.black" || id.startsWith("background.capture.")) {
        std::string r;
        Result(session_.Edit(
                   "background",
                   [&](auto& d, auto&) {
                       if (id == "background.black")
                           d.appearance.blackBackground = !d.appearance.blackBackground;
                       else
                           d.view.captureMode = std::uint32_t(id.section('.', -1).toUInt());
                       return true;
                   },
                   r),
               r);
        Refresh();
        return;
    }
    if (id == "file.new") {
        if (ConfirmUnsaved()) {
            std::string r;
            Result(session_.New(r), r);
            Refresh();
        }
        return;
    }
    if (id == "file.exit") {
        close();
        return;
    }
    if (id == "edit.numeric") {
        Numeric();
        return;
    }
    if (id == "file.open") {
        if (!ConfirmUnsaved())
            return;
        auto p = ChooseFile(FileKind::Project, false, language_.Text(id), "MMD Project (*.pmm)");
        if (!p.isEmpty())
            Open(Path(p));
        return;
    }
    if (id == "file.save" || id == "file.save_as") {
        auto p = session_.Path();
        if (p.empty() || id == "file.save_as")
            p = Path(
                ChooseFile(FileKind::Project, true, language_.Text(id), "MMD Project (*.pmm)"));
        if (!p.empty())
            Save(p);
        return;
    }
    if (id == "file.wave") {
        const auto path = ChooseFile(FileKind::Wave, false, language_.Text(id), "WAV (*.wav)");
        if (path.isEmpty())
            return;
        std::string r;
        Result(session_.ImportWave(Path(path), r), r);
        Refresh();
        return;
    }
    if (id == "file.sequence") {
        const auto path =
            ChooseFile(FileKind::Project, true, language_.Text(id), {}, "frames", false);
        if (path.isEmpty())
            return;
        session_.Pause();
        audio_.Stop();
        timer_.stop();
        const auto& d = session_.Document();
        const auto start = unsigned(d.timeline.playStartFrame),
                   end = unsigned(d.timeline.playStopFrame);
        QProgressDialog progress(language_.Text(id), language_.Text("dialog.cancel"), 0,
                                 int(end - start + 1), this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        std::string r;
        const bool ok = ExportSequence(
            session_, assets_, viewport_->Options(), Path(path), start, end,
            d.envelope.settings.renderWidth, d.envelope.settings.renderHeight,
            [&](unsigned done, unsigned total) {
                progress.setMaximum(int(total));
                progress.setValue(int(done));
                QApplication::processEvents();
                return !progress.wasCanceled();
            },
            r);
        if (r != "export-cancelled")
            Result(ok, r);
        clock_.restart();
        if (!testing_)
            timer_.start();
        Refresh();
        return;
    }
    if (id == "file.image") {
        auto path = ChooseFile(FileKind::Project, true, language_.Text(id), "PNG (*.png)");
        if (path.isEmpty())
            return;
        QImage image;
        std::string r;
        const auto& settings = session_.Document().envelope.settings;
        if (Result(RenderImage(session_.Runtime(), session_.Assets(), assets_, viewport_->Options(),
                               settings.renderWidth, settings.renderHeight, image, r,
                               session_.DisplayOptions(), session_.Effects()),
                   r)) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit())
                Result(false, file.errorString().toStdString());
        }
        return;
    }
    const bool pose = id.startsWith("file.pose_"), motion = id.startsWith("file.motion_");
    if (pose || motion) {
        const bool save = id.endsWith("save");
        const auto filter = pose ? "MMD Pose (*.vpd)" : "MMD Motion (*.vmd)";
        const auto path = inputPath.isEmpty() ? ChooseFile(pose ? FileKind::Pose : FileKind::Motion,
                                                           save, language_.Text(id), filter)
                                              : inputPath;
        if (path.isEmpty())
            return;
        std::string r;
        // The portable disk parsers accept FILE*. QFile supplies a native open
        // descriptor, keeping Unicode filename policy in this frontend.
        QFile file(path);
        if (!save && !file.open(QIODevice::ReadOnly)) {
            Result(false, file.errorString().toStdString());
            return;
        }
        // Parse/serialize in a temporary anonymous stream: no locale path API.
        auto* stream = std::tmpfile();
        if (!stream) {
            Result(false, "temporary-stream-failed");
            return;
        }
        bool ok = false;
        if (!save) {
            const auto bytes = file.readAll();
            std::fwrite(bytes.data(), 1, std::size_t(bytes.size()), stream);
            std::rewind(stream);
        }
        if (pose) {
            format::VpdDocument d;
            ok = save ? (session_.ExportPose(session_.Selected().model, d, r) &&
                         format::WriteVpd(stream, d))
                      : (format::ReadVpd(stream, d) &&
                         session_.ImportPose(d, session_.Selected().model, r));
        } else {
            format::VmdDocument d;
            ok = save ? (session_.ExportMotion(session_.Selected().model, d, r) &&
                         format::WriteVmd(stream, d))
                      : (format::ReadVmd(stream, d) &&
                         session_.ImportMotion(d, session_.Selected().model, r));
        }
        if (save && ok) {
            std::rewind(stream);
            std::vector<std::uint8_t> bytes;
            char buffer[4096];
            std::size_t n;
            while ((n = std::fread(buffer, 1, sizeof(buffer), stream)))
                bytes.insert(bytes.end(), buffer, buffer + n);
            file.close();
            ok = AtomicSave(Path(path), bytes, r);
        }
        std::fclose(stream);
        Result(ok, r.empty() ? "motion-or-pose-file-invalid" : r);
        Refresh();
    }
}
void EditorWindow::DropFiles(const QList<QUrl>& urls) {
    for (const auto& url : urls) {
        if (!url.isLocalFile())
            continue;
        auto path = url.toLocalFile();
        // Preserve DragQueryFileW's UTF-16 truncation, including classification
        // of path components before the filename. QFile receives the same path.
        path.truncate(int(editor::DroppedFilePath(path.toStdU16String()).size()));
        const auto kind = editor::ClassifyDroppedFile(path.toStdU16String());
        if (kind == DroppedFileKind::Unknown)
            continue;
        if (kind == DroppedFileKind::Project && session_.Dirty()) {
            QMessageBox dialog(QMessageBox::NoIcon, language_.Text("drop.project_title"),
                               language_.Text("drop.unsaved"),
                               QMessageBox::Ok | QMessageBox::Cancel, separate_ ? separate_ : this);
            dialog.setObjectName("mmd.dropUnsaved");
            if (dialog.exec() != QMessageBox::Ok)
                break;
        }
        if ((kind == DroppedFileKind::Pose || kind == DroppedFileKind::Vocaloid ||
             kind == DroppedFileKind::Kinect) &&
            session_.Selected().model == 255) {
            Result(false, language_.Text("drop.select_model").toStdString());
            continue;
        }
        idleRenderState_ = 1;
        const auto remember = [&](FileKind type) { fileHistory_.Confirm(type, path); };
        std::string reason;
        switch (kind) {
        case DroppedFileKind::Project:
            remember(FileKind::Project);
            Open(Path(path));
            break;
        case DroppedFileKind::Model:
            remember(FileKind::Model);
            Result(session_.ImportModel(Path(path), reason), reason);
            break;
        case DroppedFileKind::Accessory:
            remember(FileKind::Accessory);
            Result(session_.ImportAccessory(Path(path), reason), reason);
            break;
        case DroppedFileKind::Pose:
        case DroppedFileKind::Motion:
            remember(kind == DroppedFileKind::Pose ? FileKind::Pose : FileKind::Motion);
            FileAction(kind == DroppedFileKind::Pose ? "file.pose_open" : "file.motion_open", path);
            break;
        case DroppedFileKind::Wave:
            remember(FileKind::Wave);
            Result(session_.ImportWave(Path(path), reason), reason);
            break;
        case DroppedFileKind::Vocaloid:
            LoadLipSync(path);
            break;
        case DroppedFileKind::Bitmap:
        case DroppedFileKind::Jpeg:
            if (kind == DroppedFileKind::Bitmap)
                remember(FileKind::Background);
            Result(session_.ImportPicture(Path(path), unsigned(viewport_->width()),
                                          unsigned(viewport_->height()), reason),
                   reason);
            break;
        default:
            // Classified native media/hardware commands retain their explicit
            // unsupported status until their corresponding stage is implemented.
            Result(false, language_.Text("menu.unavailable").toStdString());
            break;
        }
        Refresh();
    }
}
void EditorWindow::LoadLipSync(const QString& path) {
    std::string reason;
    unsigned failed = 0;
    if (session_.ImportVsq(Path(path), reason, &failed)) {
        lastError_.clear();
        KeyOverflow(failed, 20000, true);
        return;
    }
    QString message;
    if (reason.compare(0, 12, "vsq-missing-") == 0) {
        const auto vowel = reason.back();
        const auto kana = vowel == 'a'   ? 0x3042
                          : vowel == 'i' ? 0x3044
                          : vowel == 'u' ? 0x3046
                                         : 0x304a;
        message = language_.Text("dialog.vsq.missing")
                      .arg(language_.EnglishLayout() ? QChar(vowel) : QChar(ushort(kana)));
    } else
        message = language_.Text(reason == "vsq-sysex-unsupported" ? "dialog.vsq.sysex"
                                 : reason == "vsq-open-error"      ? "dialog.vsq.open"
                                                                   : "dialog.vsq.analysis");
    lastError_ = message;
    if (!testing_) {
        const auto title = language_.Text(reason.compare(0, 12, "vsq-missing-") == 0
                                              ? "dialog.vsq.missing_title"
                                              : "dialog.vsq.analysis_title");
        QMessageBox dialog(QMessageBox::NoIcon, title, message, QMessageBox::Ok, this);
        dialog.setOption(QMessageBox::Option::DontUseNativeDialog);
        dialog.QDialog::setWindowTitle(title);
        const bool running = timer_.isActive();
        timer_.stop();
        dialog.exec();
        clock_.restart();
        if (running)
            timer_.start();
    }
}

}
