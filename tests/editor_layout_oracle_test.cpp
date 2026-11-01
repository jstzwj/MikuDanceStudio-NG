#include <Windows.h>
#include "mikudancestudio/mmd_editor_layout.hpp"
#include "editor_layout_reference.hpp"
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
static void Check(bool ok, const char* reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
namespace {
std::vector<ui::ControlRect> observed;
RECT client{};
BOOL WINAPI Rect(HWND, RECT* result) {
    *result = client;
    return TRUE;
}
BOOL WINAPI Position(HWND, HWND, int, int, int, int, UINT) {
    Check(false, "unexpected native window clamp");
    return FALSE;
}
HWND WINAPI Child(HWND, int id) {
    return reinterpret_cast<HWND>(std::uintptr_t(id));
}
BOOL WINAPI Move(HWND handle, int x, int y, int width, int height, BOOL repaint) {
    Check(repaint == TRUE, "native layout repaints every moved control");
    observed.push_back({int(reinterpret_cast<std::uintptr_t>(handle)), x, y, width, height});
    return TRUE;
}
BOOL WINAPI Visible(HWND) {
    return FALSE;
}
BOOL WINAPI Show(HWND, int) {
    Check(false, "unexpected native visibility change");
    return FALSE;
}
class Patch {
    unsigned char* address_;
    std::vector<unsigned char> saved_;
    void Write(const void* bytes) {
        DWORD old;
        Check(VirtualProtect(address_, saved_.size(), PAGE_EXECUTE_READWRITE, &old) != 0,
              "protect original layout patch");
        std::memcpy(address_, bytes, saved_.size());
        FlushInstructionCache(GetCurrentProcess(), address_, saved_.size());
        VirtualProtect(address_, saved_.size(), old, &old);
    }

public:
    Patch(const OriginalPhysicsModule& module, unsigned rva, const char* name, void* replacement)
        : address_(module.Function<unsigned char*>(rva)), saved_(sizeof(void*)) {
        std::memcpy(saved_.data(), address_, saved_.size());
        Check(*reinterpret_cast<void**>(address_) ==
                  reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), name)),
              "original layout import identity");
        Write(&replacement);
    }
    Patch(const OriginalPhysicsModule& module, unsigned rva, unsigned target)
        : address_(module.Function<unsigned char*>(rva)), saved_(5) {
        std::memcpy(saved_.data(), address_, 5);
        std::int32_t relative;
        std::memcpy(&relative, address_ + 1, 4);
        Check(address_[0] == 0xe8 && std::intptr_t(rva) + 5 + relative == target,
              "original layout UI refresh call identity");
        const std::array<unsigned char, 5> nop{0x90, 0x90, 0x90, 0x90, 0x90};
        Write(nop.data());
    }
    ~Patch() { Write(saved_.data()); }
};
template <class T> void Store(std::vector<unsigned char>& app, unsigned offset, T value) {
    std::memcpy(app.data() + offset, &value, sizeof value);
}
void Hash(std::uint64_t& hash, const ui::ControlRect& r) {
    for (auto value : {r.id, r.x, r.y, r.width, r.height})
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash = (hash ^ std::uint8_t(std::uint32_t(value) >> shift)) * 1099511628211ull;
}
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Check(argc == 2, "expected original executable path");
    OriginalPhysicsModule original(argv[1]);
    Patch a(original, 0x128740, "GetWindowRect", reinterpret_cast<void*>(&Rect));
    Patch b(original, 0x128608, "SetWindowPos", reinterpret_cast<void*>(&Position));
    Patch c(original, 0x1286d8, "GetClientRect", reinterpret_cast<void*>(&Rect));
    Patch d(original, 0x128658, "GetDlgItem", reinterpret_cast<void*>(&Child));
    Patch e(original, 0x1287b0, "MoveWindow", reinterpret_cast<void*>(&Move));
    Patch f(original, 0x1285a0, "IsWindowVisible", reinterpret_cast<void*>(&Visible));
    Patch g(original, 0x1285f0, "ShowWindow", reinterpret_cast<void*>(&Show));
    // Exclude refresh/painting and the separately tested viewport-control tail.
    // All 146 main control MoveWindow calls and coordinate instructions run.
    Patch h(original, 0x1d352, 0x5f7f0), i(original, 0x1d38b, 0x60ea0),
        j(original, 0x1fa48, 0x216a0), k(original, 0x1fa50, 0x1fa90);
    const auto layout = original.Function<void (*)(void*)>(0x1d250);
    std::vector<unsigned char> app(0xa6000);
    unsigned cases = 0, controls = 0;
    for (const auto& reference : kOriginalLayouts) {
        const std::array size{reference.sidebar, reference.bottom};
        std::uint64_t hash = 14695981039346656037ull;
        client = {0, 0, size[0] * 2, size[1]};
        for (unsigned flags = 0; flags < 128; ++flags) {
            std::fill(app.begin(), app.end(), 0);
            Store(app, 0xa54d4, .5f);
            Store(app, 0xa16e8, size[0]);
            for (unsigned n = 0; n < 7; ++n)
                app[0x328 + n] = std::uint8_t((flags >> n) & 1);
            observed.clear();
            layout(app.data());
            ui::EditorLayoutOptions options{bool(flags & 1), bool(flags & 2),  bool(flags & 4),
                                            bool(flags & 8), bool(flags & 16), bool(flags & 32),
                                            bool(flags & 64)};
            const auto expected = ui::MmdEditorControls(size[0], size[1], options);
            Check(observed.size() == expected.size(), "original main control count");
            for (unsigned n = 0; n < observed.size(); ++n) {
                const auto& a = observed[n];
                const auto& b = expected[n];
                if (a.id != b.id || a.x != b.x || a.y != b.y || a.width != b.width ||
                    a.height != b.height) {
                    std::fprintf(
                        stderr,
                        "layout side=%d bottom=%d flags=%u control=%u: original {%d,%d,%d,%d,%d}, shared {%d,%d,%d,%d,%d}\n",
                        size[0], size[1], flags, n, a.id, a.x, a.y, a.width, a.height, b.id, b.x,
                        b.y, b.width, b.height);
                    return 1;
                }
                Hash(hash, a);
                ++controls;
            }
            ++cases;
        }
        std::printf("{%d, %d, 0x%016llxull},\n", size[0], size[1],
                    static_cast<unsigned long long>(hash));
        Check(hash == reference.hash, "original layout reference hash");
    }
    std::printf("Original x64 main layout: %u cases, %u exact controls; no windows or GPU\n", cases,
                controls);
    const auto viewportLayout = original.Function<void (*)(void*)>(0x1fa90);
    cases = controls = 0;
    for (bool separate : {false, true}) {
        std::uint64_t hash = 14695981039346656037ull;
        for (int top : {0, 25, 83})
            for (int bottom : {480, 777})
                for (int boundary : kViewportFitBoundaries)
                    for (int delta : {-1, 0, 1}) {
                        const int sidebar = separate ? -9 : 250;
                        client = {0, 0, sidebar + 9 + boundary + delta, bottom + 100};
                        std::fill(app.begin(), app.end(), 0);
                        Store(app, 0xa16e8, 250);
                        Store(app, 0xa1de0, std::uintptr_t(separate ? 44 : 0));
                        Store(app, 0xa1df0, top);
                        Store(app, 0xa1df8, bottom);
                        observed.clear();
                        viewportLayout(app.data());
                        const auto expected = ui::MmdViewportControls(
                            sidebar, client.right, {0, 0, top, client.right, bottom - top});
                        Check(observed.size() == 22 && expected.size() == 22,
                              "original viewport control count");
                        for (unsigned n = 0; n < observed.size(); ++n) {
                            const auto& a = observed[n];
                            const auto& b = expected[n];
                            Check(a.id == b.id && a.x == b.x && a.y == b.y && a.width == b.width &&
                                      a.height == b.height,
                                  "original viewport control rectangle");
                            Hash(hash, a);
                            ++controls;
                        }
                        ++cases;
                    }
        std::printf("Viewport layout separate=%d: 0x%016llxull\n", int(separate),
                    static_cast<unsigned long long>(hash));
        Check(hash == kOriginalViewportLayouts[separate],
              "original viewport layout reference hash");
    }
    std::printf("Original x64 viewport layout: %u cases, %u exact controls; no windows or GPU\n",
                cases, controls);
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
