#include "editor_settings.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace mikudancestudio::qt {
namespace {
constexpr int Version = 1;
constexpr qint64 MaxBytes = 512 * 1024;

void Validate(EditorSettings& settings) {
    if (!settings.separateSize.isValid() || settings.separateSize.isEmpty() ||
        settings.separateSize.width() > 32768 || settings.separateSize.height() > 32768)
        settings.separateSize = {800, 657};
    if (settings.separateGeometry.size() > 4096)
        settings.separateGeometry.clear();
    if (settings.language.isEmpty() || settings.language.size() > 64)
        settings.language = "system";
    if (settings.directories.size() > 8)
        settings.directories.clear();
    for (auto& path : settings.directories)
        if (path.size() > 32768 || path.contains(QChar::Null))
            path.clear();
}

enum class ReadState {
    Missing,
    Valid,
    Damaged,
    Future,
    Unreadable
};
ReadState Read(const QString& path, QJsonObject& object) {
    QFile file(path);
    if (!file.exists())
        return ReadState::Missing;
    if (!file.open(QIODevice::ReadOnly))
        return ReadState::Unreadable;
    const auto bytes = file.read(MaxBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return ReadState::Unreadable;
    if (bytes.size() > MaxBytes)
        return ReadState::Damaged;
    const auto document = QJsonDocument::fromJson(bytes);
    object = document.object();
    const auto version = object["version"].toInt(-1);
    if (version > Version)
        return ReadState::Future;
    return version == Version ? ReadState::Valid : ReadState::Damaged;
}

EditorSettings LegacySettings() {
    const QSettings old;
    EditorSettings s;
    s.language = old.value("language", s.language).toString();
    s.lowPower = old.value("editor/lowPower", s.lowPower).toBool();
    s.mipmaps = old.value("renderer/mipmaps", s.mipmaps).toBool();
    s.antialiasing = old.value("renderer/antialiasing", s.antialiasing).toBool();
    s.wireframe = old.value("renderer/wireframe", s.wireframe).toBool();
    s.rememberFolders = old.value("files/rememberFolders", s.rememberFolders).toBool();
    s.directories = old.value("files/directories").toStringList();
    s.separate = old.value("viewport/separate", s.separate).toBool();
    s.alwaysOnTop = old.value("viewport/alwaysOnTop", s.alwaysOnTop).toBool();
    s.separateGeometry = old.value("viewport/separateGeometry").toByteArray();
    s.separateSize = old.value("viewport/separateSize", s.separateSize).toSize();
    Validate(s);
    return s;
}

const char* ReadError(ReadState state) {
    switch (state) {
    case ReadState::Future:
        return "editor-settings-newer-version";
    case ReadState::Unreadable:
        return "editor-settings-read-failed";
    case ReadState::Damaged:
        return "editor-settings-damaged";
    default:
        return "";
    }
}
}

EditorSettingsStore::EditorSettingsStore(QString path, bool migrateLegacy)
    : path_(std::move(path)), migrateLegacy_(migrateLegacy) {
}

EditorSettingsStore EditorSettingsStore::ForApplication() {
#ifndef Q_OS_ANDROID
    QDir directory(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    if (directory.dirName() == "MacOS" && directory.cdUp() && directory.dirName() == "Contents" &&
        directory.cdUp() && directory.dirName().endsWith(".app") && directory.cdUp()) {
        // Locate the portable directory beside the bundle.
    } else
        directory.setPath(QCoreApplication::applicationDirPath());
#endif
    const auto portable = directory.filePath("portable-settings");
    if (QFileInfo(portable).isDir())
        return EditorSettingsStore(QDir(portable).filePath("editor.json"));
#endif
    const auto user = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return EditorSettingsStore(QDir(user).filePath("editor.json"), true);
}

EditorSettings EditorSettingsStore::Load(QString& diagnostic) const {
    QJsonObject object;
    const auto state = Read(path_, object);
    diagnostic = QString::fromLatin1(ReadError(state));
    if (state == ReadState::Missing && migrateLegacy_)
        return LegacySettings();
    EditorSettings s;
    if (state != ReadState::Valid)
        return s;
    s.language = object["language"].toString(s.language);
    s.lowPower = object["lowPower"].toBool(s.lowPower);
    s.mipmaps = object["mipmaps"].toBool(s.mipmaps);
    s.antialiasing = object["antialiasing"].toBool(s.antialiasing);
    s.wireframe = object["wireframe"].toBool(s.wireframe);
    s.rememberFolders = object["rememberFolders"].toBool(s.rememberFolders);
    for (const auto directory : object["directories"].toArray())
        s.directories.push_back(directory.toString());
    s.separate = object["separate"].toBool(s.separate);
    s.alwaysOnTop = object["alwaysOnTop"].toBool(s.alwaysOnTop);
    s.separateGeometry = QByteArray::fromBase64(object["geometry"].toString().toLatin1(),
                                                QByteArray::AbortOnBase64DecodingErrors);
    s.separateSize = {object["width"].toInt(s.separateSize.width()),
                      object["height"].toInt(s.separateSize.height())};
    Validate(s);
    return s;
}

bool EditorSettingsStore::Save(const EditorSettings& input, QString& diagnostic) const {
    diagnostic.clear();
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
        diagnostic = "editor-settings-directory-unavailable";
        return false;
    }
    QLockFile lock(path_ + ".lock");
    if (!lock.tryLock(0)) {
        diagnostic = "editor-settings-busy";
        return false;
    }
    QJsonObject previous;
    const auto state = Read(path_, previous);
    if (state == ReadState::Future || state == ReadState::Unreadable) {
        diagnostic = QString::fromLatin1(ReadError(state));
        return false;
    }
    if (state == ReadState::Damaged &&
        !QFile::copy(path_, path_ + ".corrupt-" + QUuid::createUuid().toString(QUuid::Id128))) {
        diagnostic = "editor-settings-backup-failed";
        return false;
    }
    auto s = input;
    Validate(s);
    const auto data =
        QJsonDocument(QJsonObject{{"version", Version},
                                  {"language", s.language},
                                  {"lowPower", s.lowPower},
                                  {"mipmaps", s.mipmaps},
                                  {"antialiasing", s.antialiasing},
                                  {"wireframe", s.wireframe},
                                  {"rememberFolders", s.rememberFolders},
                                  {"directories", QJsonArray::fromStringList(s.directories)},
                                  {"separate", s.separate},
                                  {"alwaysOnTop", s.alwaysOnTop},
                                  {"geometry", QString::fromLatin1(s.separateGeometry.toBase64())},
                                  {"width", s.separateSize.width()},
                                  {"height", s.separateSize.height()}})
            .toJson();
    QSaveFile file(path_);
    if (data.size() > MaxBytes || !file.open(QIODevice::WriteOnly) ||
        file.write(data) != data.size() || !file.commit()) {
        diagnostic = "editor-settings-write-failed";
        return false;
    }
    return true;
}
}
