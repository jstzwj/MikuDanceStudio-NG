#include "mikudancestudio/shader_compiler.hpp"
#include "mikudancestudio/tool_process.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <regex>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace mikudancestudio::rhi {
namespace {
namespace fs = std::filesystem;
std::string Read(const fs::path& path, std::size_t limit) {
    std::ifstream in(path, std::ios::binary);
    std::string value(limit, '\0');
    in.read(value.data(), value.size());
    value.resize(std::size_t(in.gcount()));
    return value;
}
using Workspace = platform::ToolWorkspace;
bool Run(const fs::path& executable, const std::vector<std::string>& arguments,
         const Workspace& workspace, unsigned timeout, std::string& diagnostic) {
    return platform::RunToolProcess(executable, arguments, workspace, timeout, diagnostic);
}
class DxcCompiler final : public SpirvCompiler {
    ShaderCompilerOptions options;
    bool available = false;
    std::string identity;
    mutable std::mutex mutex;
    std::map<std::string, SnapshotBytes> cache;
    std::size_t cachedBytes = 0;
    ShaderCompilerStats stats;

public:
    explicit DxcCompiler(ShaderCompilerOptions selected) : options(std::move(selected)) {
        if (options.executable.empty() || !options.timeoutMilliseconds ||
            options.timeoutMilliseconds > 60000)
            return;
        std::error_code ec;
        options.executable = fs::absolute(options.executable, ec);
        if (ec || !fs::is_regular_file(options.executable, ec))
            return;
        try {
            Workspace workspace;
            available = Run(options.executable, {"--version"}, workspace,
                            options.timeoutMilliseconds, identity);
            if (available)
                identity = options.executable.u8string() + "\n" + identity;
        } catch (const std::exception& e) {
            identity = e.what();
        }
    }
    bool Available() const override { return available; }
    const std::string& Identity() const override { return identity; }
    ShaderCompilerStats Stats() const override {
        std::lock_guard<std::mutex> lock(mutex);
        return stats;
    }
    bool Compile(const ShaderModule& module, SnapshotBytes& output, std::string& error) override
        try {
        if (!module.spirv.empty()) {
            output = module.spirv;
            error.clear();
            return true;
        }
        if (!available) {
            error = "runtime_spirv_compiler_unavailable";
            return false;
        }
        if (module.hlsl.empty() || module.hlsl.size() > 8 * 1024 * 1024 ||
            module.hlsl.find('\0') != std::string::npos || module.entry.empty() ||
            module.entry.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                std::string::npos ||
            (module.stage != ShaderStage::Vertex && module.stage != ShaderStage::Pixel)) {
            error = "invalid_shader_source";
            return false;
        }
        static const std::regex includeDirective(R"((^|\n)[ \t]*#[ \t]*include\b)");
        if (std::regex_search(module.hlsl, includeDirective)) {
            error = "shader_includes_must_be_expanded";
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex);
        const std::string key =
            std::to_string(unsigned(module.stage)) + ":" + module.entry + ":" + module.hlsl;
        auto found = cache.find(key);
        if (found != cache.end()) {
            output = found->second;
            ++stats.cacheHits;
            error.clear();
            return true;
        }
        Workspace workspace;
        const auto source = workspace.path / "source.hlsl", object = workspace.path / "module.spv";
        {
            std::ofstream file(source, std::ios::binary);
            file.write(module.hlsl.data(), module.hlsl.size());
            if (!file) {
                error = "shader_source_write_failed";
                return false;
            }
        }
        std::vector<std::string> args{"-spirv",
                                      "-fspv-target-env=vulkan1.3",
                                      "-fvk-use-dx-layout",
                                      "-fvk-stage-io-order=decl",
                                      "-fvk-b-shift",
                                      "0",
                                      "0",
                                      "-fvk-t-shift",
                                      "4",
                                      "0",
                                      "-fvk-s-shift",
                                      "20",
                                      "0",
                                      "-Zpr",
                                      "-Ges",
                                      "-Gis",
                                      "-T",
                                      module.stage == ShaderStage::Vertex ? "vs_6_0" : "ps_6_0",
                                      "-E",
                                      module.entry,
                                      "-Fo",
                                      object.u8string(),
                                      source.u8string()};
        ++stats.compilations;
        if (!Run(options.executable, args, workspace, options.timeoutMilliseconds, error))
            return false;
        const auto data = Read(object, 16 * 1024 * 1024 + 1);
        if (data.size() < 20 || data.size() > 16 * 1024 * 1024 || data.size() % 4 ||
            static_cast<unsigned char>(data[0]) != 3 || static_cast<unsigned char>(data[1]) != 2 ||
            static_cast<unsigned char>(data[2]) != 35 || static_cast<unsigned char>(data[3]) != 7) {
            error = "missing_or_invalid_spirv_object";
            return false;
        }
        SnapshotBytes bytes;
        bytes.assign(data.begin(), data.end());
        bytes.Seal();
        const auto cost = bytes.size() + key.size();
        if (cost <= options.cacheBytes) {
            while (!cache.empty() && cachedBytes + cost > options.cacheBytes) {
                cachedBytes -= cache.begin()->first.size() + cache.begin()->second.size();
                cache.erase(cache.begin());
            }
            cache.emplace(key, bytes);
            cachedBytes += cost;
        }
        output = std::move(bytes);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
};
fs::path ExecutableDirectory() {
#ifdef _WIN32
    std::wstring path(32768, 0);
    const auto length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!length || length >= path.size())
        return {};
    path.resize(length);
    return fs::path(path).parent_path();
#elif defined(__APPLE__)
    std::uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::string path(length, 0);
    if (_NSGetExecutablePath(path.data(), &length))
        return {};
    return fs::path(path.c_str()).parent_path();
#else
    std::string path(32768, 0);
    const auto length = readlink("/proc/self/exe", path.data(), path.size());
    if (length <= 0 || std::size_t(length) == path.size())
        return {};
    path.resize(std::size_t(length));
    return fs::path(path).parent_path();
#endif
}
} // namespace
std::shared_ptr<SpirvCompiler> CreateDxcSpirvCompiler(ShaderCompilerOptions options) {
    return std::make_shared<DxcCompiler>(std::move(options));
}
std::shared_ptr<SpirvCompiler> DefaultSpirvCompiler() {
    static const auto compiler = [] {
#ifdef _WIN32
        const auto filename = "dxc.exe";
#else
        const auto filename = "dxc";
#endif
        auto candidate = ExecutableDirectory() / filename;
#ifdef MDS_DXC_EXECUTABLE
        if (!fs::is_regular_file(candidate))
            candidate = fs::u8path(MDS_DXC_EXECUTABLE);
#endif
        return CreateDxcSpirvCompiler({candidate});
    }();
    return compiler;
}
} // namespace mikudancestudio::rhi
