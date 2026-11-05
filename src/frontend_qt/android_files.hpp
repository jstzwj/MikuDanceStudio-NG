#pragma once
#include <QString>
class QWidget;
namespace mikudancestudio::qt {
QString PrepareAndroidAssets(QString& reason);
bool ImportAndroidWorkspace(QWidget* parent, QString& reason);
}
