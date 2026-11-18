#pragma once
#include <QByteArray>
#include <QSize>
#include <QStringList>

namespace mikudancestudio::qt {
struct EditorSettings {
    QString language = "system";
    bool lowPower = false;
    bool mipmaps = true, antialiasing = true, wireframe = false;
    bool rememberFolders = true;
    QStringList directories;
    bool separate = false, alwaysOnTop = false;
    QByteArray separateGeometry;
    QSize separateSize{800, 657};
};

class EditorSettingsStore {
public:
    explicit EditorSettingsStore(QString path, bool migrateLegacy = false);
    static EditorSettingsStore ForApplication();
    EditorSettings Load(QString& diagnostic) const;
    bool Save(const EditorSettings&, QString& diagnostic) const;

private:
    QString path_;
    bool migrateLegacy_;
};
}
