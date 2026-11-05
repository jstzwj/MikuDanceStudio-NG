#include "android_files.hpp"
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace mikudancestudio::qt {
namespace {
bool CopyTree(const QString& source, const QString& destination, qint64& remaining, unsigned depth,
              QString& reason) {
    if (depth > 32 || !QDir().mkpath(destination)) {
        reason = QStringLiteral("Cannot create workspace directory");
        return false;
    }
    const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        const auto target = destination + '/' + entry.fileName();
        if (entry.isSymLink()) {
            reason = QStringLiteral("Workspace imports cannot contain symbolic links");
            return false;
        }
        if (entry.isDir()) {
            if (!CopyTree(entry.filePath(), target, remaining, depth + 1, reason))
                return false;
        } else {
            QFile input(entry.filePath());
            QSaveFile output(target);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
                reason = input.isOpen() ? output.errorString() : input.errorString();
                return false;
            }
            while (!input.atEnd()) {
                const auto chunk = input.read(1024 * 1024);
                remaining -= chunk.size();
                if (remaining < 0 || (chunk.isEmpty() && input.error() != QFile::NoError) ||
                    output.write(chunk) != chunk.size()) {
                    reason = QStringLiteral("Workspace read/write failed or import exceeds 2 GiB");
                    return false;
                }
            }
            if (!output.commit()) {
                reason = output.errorString();
                return false;
            }
        }
    }
    reason.clear();
    return true;
}
}
QString PrepareAndroidAssets(QString& reason) {
    const auto destination =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/assets";
    qint64 remaining = 64 * 1024 * 1024;
    return CopyTree(":/android/assets", destination, remaining, 0, reason) ? destination
                                                                           : QString{};
}
bool ImportAndroidWorkspace(QWidget* parent, QString& reason) {
    const auto source =
        QFileDialog::getExistingDirectory(parent, QObject::tr("Import workspace folder"));
    if (source.isEmpty()) {
        reason.clear();
        return true;
    }
    const auto root = AndroidWorkspaceRoot();
    if (!QDir().mkpath(root)) {
        reason = QStringLiteral("Cannot create workspace storage");
        return false;
    }
    // A separate destination preserves source projects and their relative model,
    // motion, effect and texture paths. Core filesystem APIs see only local files.
    QTemporaryDir temporary(root + "/workspace-XXXXXX");
    qint64 remaining = qint64(2) * 1024 * 1024 * 1024;
    if (!temporary.isValid() || !CopyTree(source, temporary.path(), remaining, 0, reason))
        return false;
    if (QDir(temporary.path()).isEmpty()) {
        reason = QStringLiteral("The selected document provider did not expose any files");
        return false;
    }
    temporary.setAutoRemove(false);
    QDir::setCurrent(temporary.path());
    return true;
}
QString AndroidWorkspaceRoot() {
    // Keep CPU-core paths inside the app's ordinary filesystem sandbox. Public
    // documents and external FUSE views remain provider inputs, never workspaces.
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/workspaces";
}
}
