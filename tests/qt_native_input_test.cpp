#include "editor_window.hpp"
#include "english_name_dialog.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include <QApplication>
#include <QLineEdit>
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __LINE__ << ": " #x ": " << reason << '\n';                               \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
namespace {
void Characters(HWND hwnd, const QString& text) {
    for (const auto c : text)
        SendMessageW(hwnd, WM_CHAR, c.unicode(), 1);
    QCoreApplication::processEvents();
}
void Key(HWND hwnd, WPARAM key) {
    SendMessageW(hwnd, WM_KEYDOWN, key, 1);
    SendMessageW(hwnd, WM_KEYUP, key, LPARAM(0xc0000001u));
    QCoreApplication::processEvents();
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    CHECK(argc == 3 && QGuiApplication::platformName() == "windows");
    const auto root = std::filesystem::absolute(argv[1]);
    qt::EditorWindow window(std::filesystem::absolute(argv[2]), true);
    // Native messages address only our never-shown HWNDs. No SendInput,
    // activation, focus changes, desktop coordinates or keyboard-layout changes.
    for (const char* code : {"en", "ja", "zh_CN"}) {
        CHECK(window.SetLanguage(code));
        auto* frame = static_cast<QLineEdit*>(window.Control(panel::kCurrentFrameEdit));
        const auto hwnd = reinterpret_cast<HWND>(frame->winId());
        CHECK(IsWindow(hwnd) && !IsWindowVisible(hwnd));
        frame->clear();
        const auto before = window.Session().Runtime().Counters().frames;
        Characters(hwnd, "17");
        CHECK(frame->text() == "17" && window.Session().Document().timeline.currentFrame == 0);
        CHECK(window.Session().Runtime().Counters().frames == before);
        Key(hwnd, VK_RETURN);
        CHECK(window.Session().Document().timeline.currentFrame == 17);
        CHECK(window.Session().Seek(0, reason));
        window.Refresh();
    }
    CHECK(window.Session().ImportModel(root / "english.pmd", reason));
    for (const char* code : {"en", "ja", "zh_CN"}) {
        qt::Language language;
        CHECK(language.Set(code));
        qt::EnglishNameDialog dialog(window.Session(), language, &window);
        CHECK(dialog.Active());
        QLineEdit* name = nullptr;
        for (auto* field : dialog.findChildren<QLineEdit*>())
            if (field->property("mmdControlId").toInt() == 667)
                name = field;
        CHECK(name);
        const auto hwnd = reinterpret_cast<HWND>(name->winId());
        CHECK(IsWindow(hwnd) && !IsWindowVisible(hwnd));
        name->clear();
        const auto text = QStringLiteral("\u65e5\u672c") + code;
        Characters(hwnd, text);
        CHECK(name->text() == text);
        Key(hwnd, VK_RETURN);
        format::PmmModelEnglishNames names;
        CHECK(dialog.Active() && window.Session().ModelEnglishNames(0, names, reason));
        CHECK(QString::fromUtf8(editor::EnglishNameDisplay(names.name).c_str()) == text);
        Key(hwnd, VK_ESCAPE);
        CHECK(!dialog.Active());
    }
    for (const auto* widget : QApplication::topLevelWidgets())
        CHECK(!widget->isVisible());
    CHECK(!window.isVisible() && window.RenderWindow()->Submissions() == 0);
    std::cout
        << "Native HWND text, Return/Escape and three-language editor dispatch passed without exposure\n";
}
