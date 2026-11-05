#pragma once
#include <QString>
class QApplication;
namespace mikudancestudio::qt {
class EditorWindow;
int RunAndroidAcceptance(QApplication&, EditorWindow&, const QString& report);
}
