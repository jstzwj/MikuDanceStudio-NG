#pragma once
#include <QString>
class QWidget;
namespace mikudancestudio::qt {
QString PrepareAndroidAssets(QString& reason);
QString AndroidWorkspaceRoot();
bool ImportAndroidWorkspace(QWidget* parent, QString& reason);
}
