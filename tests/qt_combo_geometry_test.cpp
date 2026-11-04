#include "numeric_dialog.hpp"
#include "editor_style.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyleOptionComboBox>
#include <iostream>
using namespace mikudancestudio;
namespace {
class Dialog final : public qt::TemplateDialog {
public:
    Dialog(const ui::NumericTemplate& description, const qt::Language& language)
        : TemplateDialog(description, language, nullptr) {}
};
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2 || QGuiApplication::platformName() != "offscreen")
        return 2;
    qt::InstallEditorStyle(app);
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly))
        return 1;
    const auto rows = QJsonDocument::fromJson(input.readAll()).object()["rows"].toArray();
    if (rows.size() != 22)
        return 1;
    unsigned checked = 0;
    for (const auto& value : rows) {
        const auto row = value.toObject();
        for (const char* code : {"en", "ja", "zh_CN"}) {
            if ((row["language"] == "en") != (QString::fromLatin1(code) == "en"))
                continue;
            qt::Language language;
            if (!language.Set(code))
                return 1;
            const auto id = row["template"].toInt();
            const auto* description = id == 813   ? &ui::kNumericPhysicsKeysEn
                                      : id == 812 ? &ui::kNumericPhysicsKeysJa
                                      : id == 666 ? &ui::kNumericEnglishNameEn
                                      : id == 665 ? &ui::kNumericEnglishNameJa
                                      : id == 684 ? &ui::kNumericModelPhysicsEn
                                      : id == 683 ? &ui::kNumericModelPhysicsJa
                                                  : nullptr;
            if (!description)
                return 1;
            Dialog dialog(*description, language);
            QComboBox* combo = nullptr;
            for (auto* candidate : dialog.findChildren<QComboBox*>())
                if (candidate->property("mmdControlId").toInt() == row["control"].toInt())
                    combo = candidate;
            if (!combo)
                return 1;
            QStyleOptionComboBox option;
            option.initFrom(combo);
            const auto actual = combo->style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                               QStyle::SC_ComboBoxEditField, combo);
            const auto window = row["window"].toArray(), item = row["item"].toArray();
            const QRect expected(item[0].toInt(), item[1].toInt(),
                                 item[2].toInt() - item[0].toInt(),
                                 item[3].toInt() - item[1].toInt());
            if (combo->size() != QSize(window[0].toInt(), window[1].toInt()) ||
                actual != expected || dialog.isVisible()) {
                std::cerr << "Original combo rectangle mismatch: " << id << '/'
                          << row["control"].toInt() << '/' << code << '\n';
                return 1;
            }
            ++checked;
        }
    }
    std::cout << checked << " original combo rectangles across three languages; never shown\n";
    return checked == 33 ? 0 : 1;
}
