#include "editor_window.hpp"
#include "model_labels.hpp"
#include "mikudancestudio/menu_commands.h"
#include <QApplication>
#include <QFileDialog>
#include <QTimer>
#include <iostream>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " << #x << ' ' << reason << '\n';                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3 && QGuiApplication::platformName() == "offscreen");
    const auto root = std::filesystem::absolute(argv[1]);
    for (const char* language : {"en", "ja", "zh_CN"}) {
        qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
        CHECK(window.SetLanguage(language));
        auto& session = window.Session();
        const auto action = [&]() -> QAction* {
            for (auto* a : window.findChildren<QAction*>())
                if (a->property("mmdCommandId").toInt() == MDS_CMD_SAVE_ENHANCED_MODEL)
                    return a;
            return nullptr;
        };
        CHECK(action() && !action()->isEnabled());
        CHECK(session.ImportModel(root / "english.pmx", reason));
        window.Refresh();
        CHECK(!action()->isEnabled());
        CHECK(session.UnloadModel(0, reason));
        CHECK(session.ImportModel(root / "english.pmd", reason));
        CHECK(session.SetEnglishName(0, editor::EnglishNameKind::Bone, 0, "saved via Qt", reason));
        CHECK(session.MarkEnhancedModel(0, reason));
        window.Refresh();
        CHECK(action()->isEnabled());
        const auto revision = session.Revision(), frames = session.Runtime().Counters().frames;
        const auto resource = session.Runtime().Resource(0);
        bool cancelled = false, matched = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = window.findChild<QFileDialog*>("editor.fileDialog");
            if (!dialog)
                return;
            matched = dialog->acceptMode() == QFileDialog::AcceptSave &&
                      dialog->fileMode() == QFileDialog::AnyFile &&
                      dialog->defaultSuffix() == "pmd" &&
                      !dialog->testOption(QFileDialog::DontConfirmOverwrite) &&
                      dialog->nameFilters() ==
                          QStringList{"Polygon Model files(*.pmd)", "All Files(*.*)"} &&
                      dialog->property("mmdFileKind").toInt() == int(qt::FileKind::Model);
            cancelled = true;
            dialog->reject();
        });
        action()->trigger();
        CHECK(cancelled && matched && session.EnhancedModelDirty());
        const auto output = root / (std::string(language) + "-qt-save.pmd");
        bool accepted = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = window.findChild<QFileDialog*>("editor.fileDialog");
            if (!dialog)
                return;
            dialog->selectFile(qt::Path(output));
            accepted = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        });
        action()->trigger();
        CHECK(accepted && std::filesystem::exists(output) && !session.EnhancedModelDirty());
        CHECK(session.Revision() != revision && session.Dirty() &&
              session.Runtime().Counters().frames == frames &&
              session.Runtime().Resource(0) == resource);
        editor::EditorSession loaded;
        CHECK(loaded.ImportModel(output, reason));
        format::PmmModelEnglishNames names;
        CHECK(loaded.ModelEnglishNames(0, names, reason) && names.bones[0] == "saved via Qt");
        CHECK(loaded.SetEnglishName(0, editor::EnglishNameKind::Bone, 0,
                                    std::string(18, 'a') + "\xe3\x81\x82", reason));
        CHECK(loaded.SaveEnhancedModel(
            0, output,
            [](const auto& path, const auto& bytes, auto&) {
                QFile file(qt::Path(path));
                return file.open(QIODevice::WriteOnly) &&
                       file.write(reinterpret_cast<const char*>(bytes.data()),
                                  qint64(bytes.size())) == qint64(bytes.size());
            },
            reason));
        CHECK(loaded.New(reason) && loaded.ImportModel(output, reason));
        CHECK(qt::ModelTrackLabel(loaded, 0, 0, false, true) == QString(18, 'a') + QChar(0xfffd));
        CHECK(session.Play(reason));
        window.Refresh();
        CHECK(!action()->isEnabled());
        session.Pause();
        CHECK(!window.isVisible());
    }
    std::cout << "Three-language enhanced-model file dialogs and reload passed\n";
}
