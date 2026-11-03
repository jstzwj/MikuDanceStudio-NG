#include <Windows.h>
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/vac_format.hpp"
#include "vac_fixture.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
static void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
namespace {
struct Patch {
    void* address;
    std::vector<unsigned char> saved;
    void Write(const void* bytes) {
        DWORD old;
        Check(VirtualProtect(address, saved.size(), PAGE_EXECUTE_READWRITE, &old), "patch access");
        std::memcpy(address, bytes, saved.size());
        FlushInstructionCache(GetCurrentProcess(), address, saved.size());
        VirtualProtect(address, saved.size(), old, &old);
    }
    Patch(void* target, const void* bytes, std::size_t count) : address(target), saved(count) {
        std::memcpy(saved.data(), target, count);
        Write(bytes);
    }
    ~Patch() { Write(saved.data()); }
};
std::wstring mesh;
unsigned meshCalls;
void (*resolveUserFile)(void*, const wchar_t*);
// Explicit CP932 filesystem locator, separate from the original parsing logic.
void Locate(void*, const char* input, wchar_t* output, void* workspace) {
    wchar_t decoded[256]{};
    Check(MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, input, -1, decoded, 256) != 0,
          "test mesh CP932 path");
    resolveUserFile(workspace, decoded);
    wcscpy_s(output, 256, reinterpret_cast<wchar_t*>(static_cast<char*>(workspace) + 0xbd0));
}
HRESULT WINAPI LoadMesh(const wchar_t* filename, DWORD flags, void*, void* adjacency,
                        void** materials, void* instances, DWORD* count, void** output) {
    Check(flags == 0x220 && !adjacency && !instances, "native mesh request");
    mesh = filename;
    ++meshCalls;
    *materials = nullptr;
    *count = 0;
    *output = nullptr;
    return S_OK;
}
int WINAPI Message(HWND, LPCSTR, LPCSTR, UINT) {
    Check(false, "unexpected native modal message");
    return IDOK;
}
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 3, "usage: editor_vac_oracle_test original.exe fixture-directory");
    const auto originalPath = std::filesystem::absolute(argv[1]);
    const auto root = std::filesystem::absolute(argv[2]).make_preferred();
    std::filesystem::create_directories(root / "mesh");
    std::ofstream(root / "mesh/bound.x").close();
    const auto path = root / "fixture.vac";
    OriginalPhysicsModule original(originalPath);
    resolveUserFile = original.Function<decltype(resolveUserFile)>(0x9c00);
    auto loader = &LoadMesh;
    Patch meshLoader(original.Function<void*>(0x128888), &loader, sizeof loader);
    auto message = &Message;
    Patch messageBox(original.Function<void*>(0x128618), &message, sizeof message);
    std::array<unsigned char, 12> jump{0x48, 0xb8};
    auto locator = &Locate;
    std::memcpy(jump.data() + 2, &locator, sizeof locator);
    jump[10] = 0xff;
    jump[11] = 0xe0;
    Patch locate(original.Function<void*>(0x95c0), jump.data(), jump.size());
    // Stop after the original fclose, before materials/mesh post-processing.
    // This qualifies only the actual VAC parser and requested mesh, not D3DX.
    std::array<unsigned char, 5> done{0xe9};
    const std::int32_t delta = 0xdd0e1 - (0xdc9a8 + 5);
    std::memcpy(done.data() + 1, &delta, sizeof delta);
    Patch finish(original.Function<void*>(0xdc9a8), done.data(), done.size());
    std::vector<unsigned char> renderer(0x3ab00);
    PathResolutionWorkspace workspace{};
    const auto cases = VacCases();
    unsigned checked = 0;
    for (const auto& bytes : cases) {
        std::ofstream file(path, std::ios::binary);
        file.write(bytes.data(), std::streamsize(bytes.size()));
        file.close();
        mdl::AccessoryRecord native{};
        native.scale = native.opacity = 1;
        native.visible = 1;
        native.parentModel = -1;
        meshCalls = 0;
        original.Function<bool (*)(void*, HWND, void*, const wchar_t*, bool, void*)>(0xdc560)(
            &native, nullptr, renderer.data(), path.c_str(), true, &workspace);
        Check(meshCalls == 1 && std::filesystem::path(mesh) == "mesh/bound.x", "native mesh path");
        Check(!std::strcmp(native.name, "mesh\\bound.x"), "original VAC name retains mesh path");
        format::VacAccessory portable;
        std::string reason;
        Check(format::ReadVac(bytes, portable, reason), reason.c_str());
        Check(!std::memcmp(&native.scale, &portable.scale, sizeof(float)) &&
                  !std::memcmp(native.position, portable.position.data(), sizeof native.position) &&
                  !std::memcmp(native.rotation, portable.rotation.data(), sizeof native.rotation) &&
                  native.shadowEnabled == std::uint8_t(portable.shadowEnabled),
              "original VAC transform/radian/shadow bytes");
        Check(std::wstring(native.sourcePath) == path &&
                  std::filesystem::path(native.directory) == root / "",
              "original wrapper source and texture directory");
        ++checked;
    }
    const auto plain = (root / "mesh/bound.x").make_preferred();
    const std::int32_t plainDelta = 0xdd0e1 - (0xdcd14 + 5);
    std::memcpy(done.data() + 1, &plainDelta, sizeof plainDelta);
    Patch plainFinish(original.Function<void*>(0xdcd14), done.data(), done.size());
    mdl::AccessoryRecord native{};
    native.scale = native.opacity = 1;
    native.visible = 1;
    native.parentModel = -1;
    original.Function<void (*)(void*, HWND, void*, const wchar_t*, bool, void*)>(0xdc560)(
        &native, nullptr, renderer.data(), plain.c_str(), true, &workspace);
    Check(!std::strcmp(native.name, "bound.x") && !native.shadowEnabled &&
              native.parentModel == -1 && native.parentBone == 0,
          "original plain-X name and default state");
    std::printf(
        "%u exact original VAC parser cases passed; filesystem locator adapted, no GPU/UI\n",
        checked);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
