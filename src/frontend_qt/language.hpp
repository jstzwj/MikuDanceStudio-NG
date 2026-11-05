#pragma once
#include <QString>
#include <QTranslator>
#include <QVector>
#include <memory>
namespace mikudancestudio::qt {
struct LanguageInfo {
    QString code;
    QString nativeName;
};
class Language {
public:
    Language();
    bool Set(const QString& code);
    QString Text(const QString& id) const;
    QString Code() const { return code_; }
    QString Selection() const { return selection_; }
    // Local Language values used by reference tests never change the app.
    void Activate() const;
    static const QVector<LanguageInfo>& Available();
    static QString Resolve(QString code);
    bool EnglishLayout() const { return code_ != "ja" && !code_.startsWith("zh"); }
    bool AdaptiveCaptions() const { return code_ != "en" && code_ != "ja" && code_ != "zh_CN"; }

private:
    QString code_, selection_;
    std::shared_ptr<QTranslator> translator_;
};
}
