#pragma once
#include <QString>
#include <filesystem>

namespace mikudancestudio::qt {
int RunExportCommand(const QString& jobPath, const std::filesystem::path& assets);
}
