#include <Windows.h>
#include <shellapi.h>
#include "mikudancestudio/file_drop.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
static void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio::editor;
namespace {
struct Patch {
    void* address;
    std::vector<unsigned char> saved;
    Patch(void* target, const void* bytes, std::size_t count) : address(target), saved(count) {
        std::memcpy(saved.data(), target, count);
        Write(bytes);
    }
    void Write(const void* bytes) {
        DWORD old;
        Check(VirtualProtect(address, saved.size(), PAGE_EXECUTE_READWRITE, &old), "patch access");
        std::memcpy(address, bytes, saved.size());
        FlushInstructionCache(GetCurrentProcess(), address, saved.size());
        VirtualProtect(address, saved.size(), old, &old);
    }
    ~Patch() { Write(saved.data()); }
};
std::vector<std::wstring> paths;
std::vector<std::pair<DroppedFileKind, std::wstring>> loads;
unsigned queries, finishes, messages;
bool remember, cancel;
unsigned char* app;
UINT WINAPI QueryA(HDROP, UINT index, LPSTR buffer, UINT count) {
    Check(index == UINT(-1) && !buffer && !count, "native drop count query");
    return UINT(paths.size());
}
UINT WINAPI QueryW(HDROP, UINT index, LPWSTR buffer, UINT count) {
    Check(index < paths.size() && buffer && count == 512, "native drop path query");
    const auto length = (std::min)(paths[index].size(), std::size_t(count - 1));
    std::copy_n(paths[index].data(), length, buffer);
    buffer[length] = 0;
    ++queries;
    return UINT(length);
}
void WINAPI Finish(HDROP) {
    ++finishes;
}
HMENU WINAPI Menu(HWND) {
    return reinterpret_cast<HMENU>(1);
}
UINT WINAPI MenuState(HMENU, UINT id, UINT flags) {
    Check(id == 301 && !flags, "drop remembers previous-folder policy");
    return remember ? MF_CHECKED : 0;
}
int WINAPI Message(HWND, LPCSTR, LPCSTR, UINT flags) {
    ++messages;
    Check(flags == MB_OK || flags == MB_OKCANCEL, "native drop message buttons");
    return cancel && flags == MB_OKCANCEL ? IDCANCEL : IDOK;
}
void Load(DroppedFileKind kind, const wchar_t* path) {
    loads.emplace_back(kind, path);
}
void Project(void*) {
    Load(DroppedFileKind::Project, reinterpret_cast<wchar_t*>(app + 0xa1924));
}
void Model(void*, const wchar_t* path) {
    Load(DroppedFileKind::Model, path);
}
void Pose(void*, const wchar_t* path) {
    Load(DroppedFileKind::Pose, path);
}
void Motion(void*, const wchar_t* path) {
    Load(DroppedFileKind::Motion, path);
}
void Accessory(void*, const wchar_t* path) {
    Load(DroppedFileKind::Accessory, path);
}
void Video(void*) {
    Load(DroppedFileKind::Video, reinterpret_cast<wchar_t*>(app + 0x9f0b8));
}
void Picture(void*) {
    const auto* path = reinterpret_cast<wchar_t*>(app + 0x9f33c);
    const auto kind =
        ClassifyDroppedFile(std::u16string_view(reinterpret_cast<const char16_t*>(path)));
    Load(kind, path);
}
void Wave(void*) {
    Load(DroppedFileKind::Wave, reinterpret_cast<wchar_t*>(app + 0xd8));
}
void Vocaloid(void*, const wchar_t* path) {
    Load(DroppedFileKind::Vocaloid, path);
}
void DisableKinect(void*) {
}
void OpenKinect(void*, const char*) {
    Load(DroppedFileKind::Kinect, paths[queries - 1].c_str());
}
void ConvertPath(void*, char* output, const wchar_t*, int) {
    output[0] = 0;
}
std::unique_ptr<Patch> Jump(OriginalPhysicsModule& original, unsigned rva, void* function) {
    std::array<unsigned char, 12> bytes{0x48, 0xb8};
    std::memcpy(bytes.data() + 2, &function, sizeof function);
    bytes[10] = 0xff;
    bytes[11] = 0xe0;
    return std::make_unique<Patch>(original.Function<void*>(rva), bytes.data(), bytes.size());
}
void Run(OriginalPhysicsModule& original, const std::vector<std::wstring>& input, bool camera,
         bool remembered, bool dirty = false, bool cancelled = false) {
    paths = input;
    loads.clear();
    queries = finishes = messages = 0;
    remember = remembered;
    cancel = cancelled;
    std::memset(app, 0, 0xa6000);
    app[0x328] = camera;
    app[0xa1b31] = dirty;
    original.Function<void (*)(HDROP)>(0x978b0)(reinterpret_cast<HDROP>(1));
}
unsigned Folder(DroppedFileKind kind) {
    switch (kind) {
    case DroppedFileKind::Project:
        return 0xa25ec;
    case DroppedFileKind::Model:
        return 0xa1e1c;
    case DroppedFileKind::Accessory:
        return 0xa2dbc;
    case DroppedFileKind::Pose:
        return 0xa3d5c;
    case DroppedFileKind::Motion:
        return 0xa358c;
    case DroppedFileKind::Wave:
        return 0xa452c;
    case DroppedFileKind::Video:
    case DroppedFileKind::Bitmap:
        return 0xa4cfc;
    default:
        return 0;
    }
}
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 2, "usage: editor_file_drop_oracle_test original.exe");
    OriginalPhysicsModule original(std::filesystem::absolute(argv[1]));
    std::vector<unsigned char> state(0xa6000);
    app = state.data();
    auto* pointer = app;
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    auto qa = &QueryA;
    auto qw = &QueryW;
    auto finish = &Finish;
    auto menu = &Menu;
    auto menuState = &MenuState;
    auto message = &Message;
    Patch a(original.Function<void*>(0x128568), &qa, sizeof qa);
    Patch b(original.Function<void*>(0x128570), &qw, sizeof qw);
    Patch c(original.Function<void*>(0x128578), &finish, sizeof finish);
    Patch d(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch e(original.Function<void*>(0x128660), &menuState, sizeof menuState);
    Patch f(original.Function<void*>(0x128618), &message, sizeof message);
    std::vector<std::unique_ptr<Patch>> loaders;
    for (const auto& [rva, function] :
         std::vector<std::pair<unsigned, void*>>{{0x82c10, reinterpret_cast<void*>(&Project)},
                                                 {0x966c0, reinterpret_cast<void*>(&Model)},
                                                 {0x6ba00, reinterpret_cast<void*>(&Pose)},
                                                 {0x6d170, reinterpret_cast<void*>(&Motion)},
                                                 {0x96f10, reinterpret_cast<void*>(&Accessory)},
                                                 {0x67a20, reinterpret_cast<void*>(&Video)},
                                                 {0x68970, reinterpret_cast<void*>(&Picture)},
                                                 {0x6b400, reinterpret_cast<void*>(&Wave)},
                                                 {0x6e880, reinterpret_cast<void*>(&Vocaloid)},
                                                 {0xa6f00, reinterpret_cast<void*>(&DisableKinect)},
                                                 {0xa6b30, reinterpret_cast<void*>(&OpenKinect)},
                                                 {0x9290, reinterpret_cast<void*>(&ConvertPath)}})
        loaders.push_back(Jump(original, rva, function));
    unsigned cases = 0;
    for (const auto* suffix :
         {L".pmm", L".PMM", L".Pmm", L".pMm", L".pmd", L".PMD", L".Pmd", L".pmx", L".PMX",
          L".Pmx", L".pMx", L".vpd", L".VPD", L".Vpd", L".vmd", L".VMD", L".Vmd", L".x",
          L".X",   L".vac", L".VAC", L"Vac",  L".vAc", L".avi", L".AVI", L".Avi", L".bmp",
          L".BMP", L".Bmp", L".jpg", L".JPG", L".Jpg", L".wav", L".WAV", L".Wav", L".vsq",
          L".VSQ", L".Vsq", L".oni", L".ONI", L".Oni", L".png"})
        for (bool remembered : {false, true})
            for (bool camera : {false, true}) {
                const auto path = std::wstring(L"C:\\fixture\\name") + suffix;
                Run(original, {path}, camera, remembered);
                const auto kind = ClassifyDroppedFile(
                    std::u16string_view(reinterpret_cast<const char16_t*>(path.c_str())));
                const bool blocked =
                    camera && (kind == DroppedFileKind::Pose || kind == DroppedFileKind::Vocaloid ||
                               kind == DroppedFileKind::Kinect);
                Check(queries == 1 && finishes == 1, "original drop query and cleanup");
                Check(messages == unsigned(blocked), "original model requirement");
                Check(loads.size() == unsigned(kind != DroppedFileKind::Unknown && !blocked),
                      "classifier and original routing agree");
                if (!loads.empty()) {
                    if (loads[0].first != kind || loads[0].second != path)
                        std::fwprintf(stderr, L"path=%ls kind=%u observed=%u loaded=%ls\n",
                                      path.c_str(), unsigned(kind), unsigned(loads[0].first),
                                      loads[0].second.c_str());
                    Check(loads[0].first == kind && loads[0].second == path,
                          "original loader path");
                }
                for (unsigned offset :
                     {0xa25ecu, 0xa1e1cu, 0xa2dbcu, 0xa3d5cu, 0xa358cu, 0xa452cu, 0xa4cfcu}) {
                    const auto saved = std::wstring(reinterpret_cast<wchar_t*>(app + offset));
                    Check(saved == (remembered && !blocked && offset == Folder(kind)
                                        ? L"C:\\fixture"
                                        : L""),
                          "original per-kind folder memory, including JPG omission");
                }
                ++cases;
            }
    Run(original,
        {L"C:\\fixture\\pose.vpd", L"C:\\fixture\\motion.vmd", L"C:\\fixture\\project.pmm",
         L"C:\\fixture\\after.pmd"},
        false, false, true, true);
    Check(queries == 3 && finishes == 0 && loads.size() == 2 && messages == 1,
          "native project cancellation aborts remaining files and skips DragFinish");
    for (const auto& path :
         {std::wstring(L"C:\\name.pmd\\ignored.wav"), std::wstring(L"C:\\name.vpd\\ignored.pmx"),
          std::wstring(L"C:\\name.pmm\\ignored.x"), std::wstring(L"C:\\Vacuum\\unknown.bin")}) {
        Run(original, {path}, false, false);
        const auto portable = ClassifyDroppedFile(
            std::u16string_view(reinterpret_cast<const char16_t*>(path.c_str())));
        Check(loads.size() == unsigned(portable != DroppedFileKind::Unknown),
              "original substring precedence");
        if (!loads.empty())
            Check(loads[0].first == portable, "original substring order");
        ++cases;
    }
    std::printf(
        "%u original drop classifications plus sequential cancellation passed; loaders observed, no UI or files opened\n",
        cases);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
