#pragma once
#include <QString>
class QApplication;
namespace mikudancestudio::qt {
class EditorWindow;
int RunAndroidAcceptance(QApplication&, EditorWindow&, const QString& report,
                         const QString& mediaDirectory);
void StartAndroidUiReport(EditorWindow&, const QString& report);
}
