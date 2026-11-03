#pragma once
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <array>
#include <algorithm>

namespace mikudancestudio::qt {
enum class FileKind {
    Project,
    Model,
    Accessory,
    Motion,
    Pose,
    Wave,
    Background,
    Vsq,
    Count
};
class FileDialogHistory {
    std::array<QString, unsigned(FileKind::Count)> directories_;

public:
    bool enabled = true; // Original startup checkMenu12D default.
    QString Initial(FileKind kind, const QString& applicationDirectory) const {
        if (kind == FileKind::Vsq)
            return QDir(applicationDirectory).filePath("UserFile/Vsq");
        const auto& remembered = directories_.at(unsigned(kind));
        if (enabled && !remembered.isEmpty())
            return remembered;
        static const char* defaults[]{"UserFile",           "UserFile/Model", "UserFile/Accessory",
                                      "UserFile/Motion",    "UserFile/Pose",  "UserFile/Wave",
                                      "UserFile/BackGround"};
        return QDir(applicationDirectory).filePath(defaults[unsigned(kind)]);
    }
    void Confirm(FileKind kind, const QString& path) {
        // Confirmation, even of a subsequently invalid file, updates the native
        // history. Cancellation and confirmation with the switch off do not.
        if (enabled && !path.isEmpty() && kind != FileKind::Vsq)
            directories_.at(unsigned(kind)) = QFileInfo(path).absolutePath();
    }
    QStringList Save() const { return QStringList(directories_.begin(), directories_.end()); }
    void Restore(const QStringList& paths) {
        if (paths.size() == int(directories_.size()) || paths.size() == int(FileKind::Vsq))
            std::copy(paths.begin(), paths.end(), directories_.begin());
    }
};
}
