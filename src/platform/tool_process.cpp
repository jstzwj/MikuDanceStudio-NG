#include "mikudancestudio/tool_process.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace mikudancestudio::platform {
namespace fs = std::filesystem;
ToolWorkspace::ToolWorkspace() {
    static std::atomic<unsigned> serial{0};
    std::random_device random;
    for (unsigned n = 0; n < 32; ++n) {
        auto candidate = fs::temp_directory_path() / ("mds-shader-" + std::to_string(random()) +
                                                      "-" + std::to_string(++serial));
        if (fs::create_directory(candidate)) {
            path = std::move(candidate);
            fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace);
            return;
        }
    }
    throw std::runtime_error("shader_workspace_unavailable");
}
ToolWorkspace::~ToolWorkspace() {
    std::error_code ec;
    if (!path.empty())
        fs::remove_all(path, ec);
}

std::string Read(const fs::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    std::string value(limit, '\0');
    in.read(value.data(), value.size());
    value.resize(std::size_t(in.gcount()));
    return value;
}
#ifdef _WIN32
std::wstring Quote(const std::wstring& value) {
    std::wstring result = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
        if (c == L'"')
            result.push_back(L'\\');
        result.push_back(c);
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#endif
bool RunToolProcess(const fs::path& executable, const std::vector<std::string>& arguments,
                    const ToolWorkspace& workspace, unsigned timeout, std::string& diagnostic) {
    const auto log = workspace.path / "compiler.log";
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (output != INVALID_HANDLE_VALUE)
            CloseHandle(output);
        if (input != INVALID_HANDLE_VALUE)
            CloseHandle(input);
        diagnostic = "shader_process_io_failed";
        return false;
    }
    std::wstring command = Quote(executable.wstring());
    for (const auto& arg : arguments)
        command += L" " + Quote(fs::u8path(arg).wstring());
    STARTUPINFOW start{sizeof(STARTUPINFOW)};
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = input;
    start.hStdOutput = start.hStdError = output;
    PROCESS_INFORMATION process{};
    const bool created =
        CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                       nullptr, workspace.path.c_str(), &start, &process) != FALSE;
    CloseHandle(input);
    CloseHandle(output);
    if (!created) {
        diagnostic = "shader_process_creation_failed";
        return false;
    }
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, timeout);
    DWORD status = 1;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &status);
    else {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, INFINITE);
    }
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0) {
        diagnostic = "shader_compile_timeout";
        return false;
    }
    const bool success = status == 0;
#else
    std::vector<std::string> storage{executable.string()};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& item : storage)
        argv.push_back(item.data());
    argv.push_back(nullptr);
    const int output = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const int input = open("/dev/null", O_RDONLY);
    if (output < 0 || input < 0) {
        if (output >= 0)
            close(output);
        if (input >= 0)
            close(input);
        diagnostic = "shader_process_io_failed";
        return false;
    }
    // Only async-signal-safe calls in the child; argument storage is prepared
    // before fork. Never use a shell or interpret shader text as a command.
    const auto pid = fork();
    if (pid == 0) {
        if (chdir(workspace.path.c_str()) || dup2(input, 0) < 0 || dup2(output, 1) < 0 ||
            dup2(output, 2) < 0)
            _exit(126);
        close(input);
        close(output);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(input);
    close(output);
    if (pid < 0) {
        diagnostic = "shader_process_creation_failed";
        return false;
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    for (;;) {
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == pid)
            break;
        if (result < 0 || std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            diagnostic = "shader_compile_timeout";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
    diagnostic = Read(log, 1024 * 1024);
    if (!success && diagnostic.empty())
        diagnostic = "spirv_compile_failed";
    return success;
}
}
