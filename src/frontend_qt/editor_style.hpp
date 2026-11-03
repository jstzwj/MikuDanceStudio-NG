#pragma once
class QApplication;
namespace mikudancestudio::qt {
// Install before creating controls. Repeated calls preserve the selected style.
void InstallEditorStyle(QApplication&);
}
