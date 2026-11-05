#include "language.hpp"
#include "../../translations/messages.hpp"
#include <QCoreApplication>
#include <QHash>
#include <QLocale>
namespace mikudancestudio::qt {
const QVector<LanguageInfo>& Language::Available() {
    static const QVector<LanguageInfo> languages{
        {"en", QStringLiteral("English")},     {"ja", QStringLiteral("日本語")},
        {"zh_CN", QStringLiteral("简体中文")}, {"zh_TW", QStringLiteral("繁體中文")},
        {"ko", QStringLiteral("한국어")},      {"fr", QStringLiteral("Français")},
        {"de", QStringLiteral("Deutsch")},     {"es", QStringLiteral("Español")},
        {"it", QStringLiteral("Italiano")},    {"pt_BR", QStringLiteral("Português (Brasil)")},
        {"ru", QStringLiteral("Русский")},     {"nl", QStringLiteral("Nederlands")},
        {"pl", QStringLiteral("Polski")},      {"tr", QStringLiteral("Türkçe")},
        {"vi", QStringLiteral("Tiếng Việt")}};
    return languages;
}
QString Language::Resolve(QString code) {
    if (code == "system")
        code = QLocale::system().name();
    code.replace('-', '_');
    if (code.section('_', 0, 0).compare("zh", Qt::CaseInsensitive) == 0)
        return QLocale(code).script() == QLocale::TraditionalHanScript ? "zh_TW" : "zh_CN";
    code = code.section('_', 0, 0).toLower();
    for (const auto& language : Available())
        if (language.code.section('_', 0, 0) == code)
            return language.code;
    return "en";
}
Language::Language() {
    Set("system");
}
bool Language::Set(const QString& selection) {
    const auto code = Resolve(selection);
    // Reject invalid explicit settings atomically; unsupported system locales
    // resolve to English. Accept regional aliases from old preferences.
    if (selection != "system" && code == "en" &&
        selection.section('-', 0, 0).section('_', 0, 0).toLower() != "en")
        return false;
    auto translator = std::make_shared<QTranslator>();
    if (!translator->load(":/i18n/mikudancestudio_" + code + ".qm"))
        return false;
    translator_ = std::move(translator);
    code_ = code;
    selection_ = selection == "system" ? selection : code;
    return true;
}
QString Language::Text(const QString& id) const {
    const auto utf8 = id.toUtf8();
    if (translator_) {
        const auto translated = translator_->translate(nullptr, utf8.constData());
        if (!translated.isNull())
            return translated;
    }
    // Qt omits empty translations from QM. Original MMD intentionally leaves
    // some controls blank in Japanese; do not replace those with English.
    for (const auto& entry : kEmptyTranslations)
        if (id == QLatin1String(entry.id) && code_ == QLatin1String(entry.language))
            return {};
    static const auto source = [] {
        QHash<QString, QString> result;
        for (const auto& entry : kTranslationMessages)
            result.insert(QString::fromUtf8(entry.id), QString::fromUtf8(entry.source));
        return result;
    }();
    return source.value(id, id);
}
void Language::Activate() const {
    struct Active {
        std::shared_ptr<QTranslator> app;
        std::unique_ptr<QTranslator> base;
    };
    static Active active;
    auto base = std::make_unique<QTranslator>();
    const bool hasBase = code_ != "en" && base->load(":/i18n/qt/qtbase_" + code_ + ".qm");
    // Retain the previous objects until removed from Qt's translator list.
    if (active.app)
        QCoreApplication::removeTranslator(active.app.get());
    if (active.base)
        QCoreApplication::removeTranslator(active.base.get());
    active.app = translator_;
    active.base = std::move(base);
    if (hasBase)
        QCoreApplication::installTranslator(active.base.get());
    QCoreApplication::installTranslator(active.app.get());
}
}
