#include <Windows.h>
#include "mikudancestudio/editor_numeric.hpp"
#include "editor_numeric_fixture.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
static void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
#include "original_physics_module.hpp"
namespace {
std::vector<unsigned char> app(0xa3000);
std::array<float, 7> captured{}, edited{};
INT_PTR WINAPI Dialog(HINSTANCE, LPCSTR resource, HWND, DLGPROC, LPARAM) {
    Check(reinterpret_cast<std::uintptr_t>(resource) == 664, "original camera dialog resource");
    std::memcpy(captured.data(), app.data() + 0xa1b60, sizeof captured);
    std::memcpy(app.data() + 0xa1b60, edited.data(), sizeof edited);
    return IDOK;
}
class Patch {
    unsigned char* address_;
    std::vector<unsigned char> before_;
    void Write(const void* bytes) {
        DWORD old;
        Check(VirtualProtect(address_, before_.size(), PAGE_EXECUTE_READWRITE, &old),
              "patch protection");
        std::memcpy(address_, bytes, before_.size());
        FlushInstructionCache(GetCurrentProcess(), address_, before_.size());
        VirtualProtect(address_, before_.size(), old, &old);
    }

public:
    Patch(void* address, const void* replacement, std::size_t size)
        : address_(static_cast<unsigned char*>(address)), before_(size) {
        std::memcpy(before_.data(), address_, size);
        Write(replacement);
    }
    ~Patch() { Write(before_.data()); }
};
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 2, "expected original executable");
    OriginalPhysicsModule original(argv[1]);
    for (const auto& font :
         {std::pair{L"Arial", 15}, std::pair{L"\xff2d\xff33 \xff30\x30b4\x30b7\x30c3\x30af", 12}}) {
        HDC dc = CreateCompatibleDC(nullptr); // Memory DC, never a desktop DC.
        HFONT handle = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                   DEFAULT_PITCH, font.first);
        Check(dc && handle, "numeric dialog font allocation");
        const auto previous = SelectObject(dc, handle);
        const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        SIZE extent;
        Check(GetTextExtentPoint32W(dc, alphabet, 52, &extent),
              "numeric dialog base unit measurement");
        const bool equal = (extent.cx / 26 + 1) / 2 == 7 && extent.cy == font.second;
        SelectObject(dc, previous);
        DeleteObject(handle);
        DeleteDC(dc);
        Check(equal, "original font dialog base units");
    }
    auto** import = original.Function<void**>(0x128600);
    Check(*import == reinterpret_cast<void*>(
                         GetProcAddress(GetModuleHandleW(L"user32.dll"), "DialogBoxParamA")),
          "dialog import identity");
    void* stub = reinterpret_cast<void*>(&Dialog);
    Patch dialog(import, &stub, sizeof stub);
    auto* tail = original.Function<unsigned char*>(0x4ea0c);
    const unsigned char expected[]{0xe9, 0x4d, 0x04, 0x00, 0x00};
    Check(std::memcmp(tail, expected, sizeof expected) == 0, "camera command publication boundary");
    const unsigned char ret[]{0xc3, 0x90, 0x90, 0x90, 0x90};
    Patch stop(tail, ret, sizeof ret);
    // Call only the camera branch of the original command. Preserve its
    // nonvolatile registers; reserve its shadow/argument area separately.
    std::vector<unsigned char> wrapper{0x53, 0x41, 0x54, 0x48, 0x83, 0xec, 0x60, 0x0f, 0x11,
                                       0x74, 0x24, 0x40, 0x0f, 0x11, 0x7c, 0x24, 0x50, 0x48,
                                       0x8b, 0xd9, 0x45, 0x33, 0xe4, 0x48, 0xb8};
    const auto entry = reinterpret_cast<std::uintptr_t>(original.Function<void*>(0x4e896));
    for (unsigned shift = 0; shift < 64; shift += 8)
        wrapper.push_back(static_cast<unsigned char>(entry >> shift));
    const unsigned char finish[]{0xff, 0xd0, 0x0f, 0x10, 0x74, 0x24, 0x40, 0x0f, 0x10, 0x7c,
                                 0x24, 0x50, 0x48, 0x83, 0xc4, 0x60, 0x41, 0x5c, 0x5b, 0xc3};
    wrapper.insert(wrapper.end(), std::begin(finish), std::end(finish));
    void* memory = VirtualAlloc(nullptr, wrapper.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(memory != nullptr, "camera branch wrapper allocation");
    std::memcpy(memory, wrapper.data(), wrapper.size());
    DWORD old;
    Check(VirtualProtect(memory, wrapper.size(), PAGE_EXECUTE_READ, &old),
          "camera wrapper execute protection");
    FlushInstructionCache(GetCurrentProcess(), memory, wrapper.size());
    const auto execute = reinterpret_cast<void (*)(void*)>(memory);
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned i = 0; i < 256; ++i) {
        const auto input = NumericCameraInput(i);
        edited = NumericCameraEdited(i);
        std::fill(app.begin(), app.end(), 0);
        app[0x328] = app[0xa1b90] = 1;
        std::memcpy(app.data() + 0x36c, input.data(), 12);
        std::memcpy(app.data() + 0x348, input.data() + 3, 12);
        std::memcpy(app.data() + 0xa1900, input.data() + 6, 4);
        execute(app.data());
        const auto values = mikudancestudio::editor::CameraNumericValues(
            {input[0], input[1], input[2]}, {input[3], input[4], input[5]}, input[6]);
        Check(std::memcmp(captured.data(), values.data(), sizeof captured) == 0,
              "original camera input values");
        std::array<float, 7> output;
        std::memcpy(output.data(), app.data() + 0x36c, 12);
        std::memcpy(output.data() + 3, app.data() + 0x348, 12);
        std::memcpy(output.data() + 6, app.data() + 0xa1900, 4);
        const auto rotation = mikudancestudio::editor::CameraNumericRotation(edited);
        const std::array<float, 7> expectedOutput{edited[0],   edited[1],   edited[2], rotation[0],
                                                  rotation[1], rotation[2], -edited[6]};
        Check(std::memcmp(output.data(), expectedOutput.data(), sizeof output) == 0,
              "original camera accepted values");
        NumericHash(hash, captured);
        NumericHash(hash, output);
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    Check(hash == 0xa78d309a17788cb1ull, "original camera numeric corpus hash");
    std::printf(
        "Original camera numeric command: 256 exact input/output cases, hash %016llx; no windows or GPU\n",
        static_cast<unsigned long long>(hash));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
