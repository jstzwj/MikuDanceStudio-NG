#include "editor_window.hpp"
#include "mikudancestudio/file_drop.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDataStream>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QMessageBox>
#include <QMimeData>
#include <QTimer>
#include <cstdio>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " " << reason << '\n';               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
bool Drop(QWidget* target, const QList<QUrl>& urls) {
    QMimeData mime;
    mime.setUrls(urls);
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    if (!enter.isAccepted())
        return false;
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    return drop.isAccepted() && drop.dropAction() == Qt::CopyAction;
}
QAction* Command(qt::EditorWindow& window, int id) {
    for (auto* action : window.findChildren<QAction*>())
        if (action->property("mmdCommandId").toInt() == id)
            return action;
    return nullptr;
}
template <class Document, class Writer>
bool Write(const std::filesystem::path& path, const Document& document, Writer writer) {
    auto* stream = std::tmpfile();
    if (!stream)
        return false;
    const bool ok = writer(stream, document);
    std::rewind(stream);
    QByteArray bytes;
    char buffer[4096];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, stream)))
        bytes.append(buffer, qsizetype(n));
    std::fclose(stream);
    QFile file(qt::Path(path));
    return ok && file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(QGuiApplication::platformName() == "offscreen" && argc == 3);
    const auto root = std::filesystem::absolute(argv[1]);
    const auto url = [&](const char* name) { return QUrl::fromLocalFile(qt::Path(root / name)); };
    qt::EditorWindow project(std::filesystem::absolute(argv[2]), true);
    CHECK(project.Save(root / "drop-empty.pmm"));
    QImage jpeg(2, 2, QImage::Format_RGBA8888);
    jpeg.fill(Qt::green);
    CHECK(jpeg.save(qt::Path(root / "drop.jpg")));
    QFile wave(qt::Path(root / "drop.wav"));
    CHECK(wave.open(QIODevice::WriteOnly));
    QDataStream pcm(&wave);
    pcm.setByteOrder(QDataStream::LittleEndian);
    pcm.writeRawData("RIFF", 4);
    pcm << quint32(36 + 1920);
    pcm.writeRawData("WAVEfmt ", 8);
    pcm << quint32(16) << quint16(3) << quint16(1) << quint32(48000) << quint32(192000)
        << quint16(4) << quint16(32);
    pcm.writeRawData("data", 4);
    pcm << quint32(1920);
    CHECK(wave.write(QByteArray(1920, '\0')) == 1920);
    wave.close();
    CHECK(editor::ClassifyDroppedFile(std::u16string(512, u'a') + u".pmd") ==
          editor::DroppedFileKind::Unknown);
    CHECK(editor::ClassifyDroppedFile(std::u16string(u"x\0file.pmd", 10)) ==
          editor::DroppedFileKind::Unknown);
    for (const char* language : {"en", "ja", "zh_CN"}) {
        qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
        CHECK(window.SetLanguage(language));
        auto& session = window.Session();
        CHECK(!Drop(&window, {QUrl("https://example.invalid/model.pmd")}));
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        CHECK(Drop(&window, {url("ignored.pMd"), url("unknown.png")}));
        CHECK(session.Revision() == revision && session.Runtime().Counters().frames == frames);
        CHECK(Drop(window.centralWidget(),
                   {url("triangle.pmx"), url("bound.x"), url("checker.bmp"), url("drop.wav")}));
        CHECK(session.Document().models.size() == 1 && session.Document().accessories.size() == 1 &&
              session.Assets().picture && session.Document().picture.enabled);
        CHECK(session.Assets().wave == root / "drop.wav" && session.Document().wave.enabled);
        CHECK(session.Selected().model == 0);
        // Pose/motion drops use the same disk parser and shared session command
        // as their menus, and retain the supplied file ordering.
        CHECK(session.SetBone(0, 0, {1, 2, 3}, {0, 0, 0, 1}, reason));
        format::VpdDocument pose;
        format::VmdDocument motion;
        CHECK(session.ExportPose(0, pose, reason) && session.ExportMotion(0, motion, reason));
        CHECK(Write(root / "drop.vpd", pose, format::WriteVpd));
        CHECK(Write(root / "drop.vmd", motion, format::WriteVmd));
        CHECK(session.SetBone(0, 0, {0, 0, 0}, {0, 0, 0, 1}, reason));
        CHECK(Drop(&window, {url("drop.vpd")}));
        CHECK(session.Document().models[0].bonePose[0].components[0] == 1);
        CHECK(Drop(&window, {url("drop.vmd")}));
        CHECK(Drop(&window, {url("missing.pmd"), url("pose.pmd")}));
        CHECK(session.Document().models.size() == 2);
        // BMP remembers its category; JPG deliberately does not replace it.
        const auto images = root / "jpeg-folder";
        std::filesystem::create_directory(images);
        std::filesystem::copy_file(root / "drop.jpg", images / "drop.jpg",
                                   std::filesystem::copy_options::overwrite_existing);
        CHECK(Drop(&window, {QUrl::fromLocalFile(qt::Path(images / "drop.jpg"))}));
        bool initialMatches = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = window.findChild<QFileDialog*>("editor.fileDialog");
            if (dialog) {
                std::error_code error;
                initialMatches = std::filesystem::equivalent(
                                     qt::Path(dialog->property("mmdInitialDirectory").toString()),
                                     root, error) &&
                                 !error;
                dialog->reject();
            }
        });
        Command(window, MDS_CMD_LOAD_BACKGROUND_IMAGE)->trigger();
        CHECK(initialMatches);
        // Original drop confirmation has only OK/Cancel. Cancel stops the
        // remainder; confirming replaces the scene before subsequent imports.
        CHECK(session.Dirty());
        bool sawCancel = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = window.findChild<QMessageBox*>("mmd.dropUnsaved");
            if (dialog) {
                sawCancel = dialog->standardButtons() == (QMessageBox::Ok | QMessageBox::Cancel);
                dialog->done(QMessageBox::Cancel);
            }
        });
        CHECK(Drop(&window, {url("drop-empty.pmm"), url("triangle.pmx")}));
        CHECK(sawCancel && session.Document().models.size() == 2);
        QTimer::singleShot(0, [&] {
            if (auto* dialog = window.findChild<QMessageBox*>("mmd.dropUnsaved"))
                dialog->done(QMessageBox::Ok);
        });
        CHECK(Drop(&window, {url("drop-empty.pmm"), url("triangle.pmx")}));
        CHECK(session.Document().models.size() == 1 && session.Document().accessories.empty());
        CHECK(session.SelectModel(255, reason));
        const auto before = session.Revision();
        CHECK(Drop(&window, {url("drop.vpd")}));
        CHECK(session.Revision() == before && !window.LastError().isEmpty());
        Command(window, MDS_CMD_TOGGLE_SEPARATE_WINDOW)->trigger();
        CHECK(window.SeparateWindow());
        CHECK(Drop(window.SeparateWindow(), {url("bound.x")}));
        CHECK(session.Document().accessories.size() == 1);
        CHECK(!session.Document().accessories[0].state.shadowEnabled);
        CHECK(Drop(window.SeparateWindow(), {url("placed.vac")}));
        CHECK(session.Document().accessories.size() == 2);
        const auto& vac = session.Document().accessories[1];
        CHECK(vac.state.scale == 1.25f && vac.state.shadowEnabled &&
              vac.state.rotation == (std::array<float, 3>{.1f, .2f, .3f}) &&
              vac.state.parentModel == -1 && vac.state.parentBone == 0);
        CHECK(!window.isVisible() && !window.SeparateWindow()->isVisible());
    }
    std::puts(
        "Qt offscreen drops: three languages, native routing, ordered load/failure, parser reuse, folder memory, cancellation and detached viewport passed");
    return 0;
}
