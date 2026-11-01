#include "language.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
namespace mikudancestudio::qt {
Language::Language() {
    auto locale = QLocale::system().name();
    Set(locale.startsWith("zh") ? "zh_CN" : locale.startsWith("ja") ? "ja" : "en");
}
bool Language::Set(const QString& code) {
    if (code != "en" && code != "ja" && code != "zh_CN")
        return false;
    QFile file(":/i18n/" + code + ".json");
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        return false;
    code_ = code;
    catalog_ = parsed.object();
    return true;
}
QString Language::Text(const QString& id) const {
    const auto value = catalog_.value(id);
    return value.isString() ? value.toString() : id;
}
}
