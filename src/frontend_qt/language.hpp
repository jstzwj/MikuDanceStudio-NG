#pragma once
#include <QJsonObject>
#include <QString>
namespace mikudancestudio::qt {
class Language {
public:
    Language();
    bool Set(const QString& code);
    QString Text(const QString& id) const;
    QString Code() const { return code_; }

private:
    QString code_;
    QJsonObject catalog_;
};
}
