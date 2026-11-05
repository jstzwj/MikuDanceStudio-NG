#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace mikudancestudio::platform {
// Private workspace and bounded, shell-free compiler invocation. Child process
// cwd does not change the host process cwd. Windows children never open a console.
class ToolWorkspace {
public:
    std::filesystem::path path;
    ToolWorkspace();
    ~ToolWorkspace();
    ToolWorkspace(const ToolWorkspace&) = delete;
    ToolWorkspace& operator=(const ToolWorkspace&) = delete;
};
bool RunToolProcess(const std::filesystem::path& executable,
                    const std::vector<std::string>& arguments, const ToolWorkspace&,
                    unsigned timeoutMilliseconds, std::string& diagnostic);
}
