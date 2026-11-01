#include <Windows.h>
#include "mikudancestudio/editor_numeric.hpp"
#include "mikudancestudio/mmd_dialog_edit_metrics.hpp"
#include <commctrl.h>
#include "editor_numeric_fixture.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <memory>
#include <string>
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
// Record original edge-dialog control calls without creating a window. The
// seven-byte read, float arithmetic and current-model writes still execute in
// the hash-verified original; this intentionally does not emulate EDIT clipping.
std::string edgeText;
int edgeSlider = 0, forwarded = 0;
bool edgeDestroyed = false;
INT_PTR CALLBACK HiddenDialog(HWND, UINT, WPARAM, LPARAM) {
    return FALSE;
}
void EditWidthOracle(const wchar_t* path) {
    HMODULE controls = LoadLibraryW(L"comctl32.dll");
    Check(controls != nullptr, "load common controls");
    const auto init = reinterpret_cast<BOOL(WINAPI*)(const INITCOMMONCONTROLSEX*)>(
        GetProcAddress(controls, "InitCommonControlsEx"));
    INITCOMMONCONTROLSEX config{sizeof config, ICC_BAR_CLASSES};
    Check(init && init(&config), "initialize hidden trackbars");
    HMODULE resources =
        LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Check(resources != nullptr, "load original numeric resources");
    unsigned checkpoints = 0;
    for (int id : {662, 645, 663, 648, 664, 649, 653, 607}) {
        HRSRC resource = FindResourceW(resources, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(5));
        Check(resource != nullptr, "find original dialog resource");
        const auto* bytes =
            static_cast<const unsigned char*>(LockResource(LoadResource(resources, resource)));
        DWORD style;
        std::memcpy(&style, bytes + 12, sizeof style);
        Check((style & WS_VISIBLE) == 0, "original dialog must never be shown");
        HWND dialog = CreateDialogParamA(resources, MAKEINTRESOURCEA(id), nullptr, HiddenDialog, 0);
        Check(dialog && !IsWindowVisible(dialog), "create hidden original dialog");
        const bool edge = id == 662 || id == 645, screen = id == 653 || id == 607;
        const bool english = id == 662 || id == 663 || id == 664 || id == 653;
        HWND edit = GetDlgItem(dialog, edge ? 646 : screen ? 621 : 637);
        RECT rectangle;
        Check(edit && GetWindowRect(edit, &rectangle), "original edit geometry");
        for (const char* text :
             {"0.57", "1.37", "-0.25", "1.23456789", "111111", "......", "------", "  0.00",
              "-1.00", "12.34", "100.00", "1e+2", "-.125", " 12.346", "-45.679"}) {
            SendMessageA(edit, EM_SETSEL, 0, -1);
            SendMessageA(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
            char actual[128]{};
            GetWindowTextA(edit, actual, sizeof actual);
            const auto size = mikudancestudio::ui::NumericPrefillLength(
                text, english, rectangle.right - rectangle.left);
            Check(std::string(actual) == std::string(text).substr(0, size),
                  "original EDIT prefill clipping");
            ++checkpoints;
        }
        for (int position = 0; edge && position <= 200; ++position) {
            char text[64], actual[64]{};
            std::snprintf(text, sizeof text, "%3.2f", double(float(position) / 100.f));
            SendMessageA(edit, EM_SETSEL, 0, -1);
            SendMessageA(edit, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
            GetWindowTextA(edit, actual, sizeof actual);
            Check(std::string(actual) ==
                      std::string(text).substr(
                          0, mikudancestudio::ui::NumericPrefillLength(
                                 text, english, rectangle.right - rectangle.left)),
                  "original slider edit clipping");
            ++checkpoints;
        }
        DestroyWindow(dialog);
    }
    FreeLibrary(resources);
    FreeLibrary(controls);
    std::printf("Original hidden EDITs: %u prefill/slider width checkpoints; no shown windows\n",
                checkpoints);
}
HWND WINAPI EdgeControl(HWND, int id) {
    Check(id == 646 || id == 647, "original edge control identity");
    return reinterpret_cast<HWND>(std::uintptr_t(id));
}
LRESULT WINAPI EdgeMessage(HWND control, UINT message, WPARAM, LPARAM value) {
    if (control == EdgeControl(nullptr, 647)) {
        if (message == 0x400)
            return edgeSlider;
        if (message == 0x405)
            edgeSlider = std::clamp(int(value), 0, 200);
        else
            Check(message == 0x407 || message == 0x408 || message == 0x414,
                  "original edge trackbar message");
    } else if (message == 0xc2)
        edgeText = reinterpret_cast<const char*>(value);
    else
        Check(message == 0xb1, "original edge edit message");
    return 0;
}
int WINAPI EdgeRead(HWND, LPSTR out, int capacity) {
    Check(capacity == 8, "original edge seven-byte read limit");
    const auto count = std::min(edgeText.size(), std::size_t(capacity - 1));
    std::memcpy(out, edgeText.data(), count);
    out[count] = 0;
    return int(count);
}
int WINAPI EdgeLength(HWND) {
    return int(edgeText.size());
}
LONG_PTR WINAPI EdgeGetProc(HWND, int) {
    return 1;
}
LONG_PTR WINAPI EdgeSetProc(HWND, int, LONG_PTR) {
    return 1;
}
LRESULT WINAPI EdgeForward(WNDPROC, HWND, UINT, WPARAM, LPARAM) {
    ++forwarded;
    return 0;
}
BOOL WINAPI EdgeDestroy(HWND) {
    edgeDestroyed = true;
    return TRUE;
}
void EdgeHostBranch(OriginalPhysicsModule& original, unsigned entry, unsigned tail) {
    // RDI=app, RSI=0, 256-byte printf buffer at RSP+E0. Preserve touched
    // nonvolatile registers and give the original calls their own shadow area.
    std::vector<unsigned char> code{0x53, 0x56, 0x57, 0x48, 0x81, 0xec, 0,    2,   0,
                                    0,    0x48, 0x8b, 0xf9, 0x33, 0xf6, 0x48, 0xb8};
    const auto target = reinterpret_cast<std::uintptr_t>(original.Function<void*>(entry));
    for (unsigned shift = 0; shift < 64; shift += 8)
        code.push_back(std::uint8_t(target >> shift));
    code.insert(code.end(), {0xff, 0xe0});
    const auto resume = code.size();
    code.insert(code.end(), {0x48, 0x81, 0xc4, 0, 2, 0, 0, 0x5f, 0x5e, 0x5b, 0xc3});
    auto* memory = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    Check(memory != nullptr, "edge host branch allocation");
    std::memcpy(memory, code.data(), code.size());
    DWORD protection;
    Check(VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &protection),
          "edge host branch protection");
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    std::vector<unsigned char> jump{0xff, 0x25, 0, 0, 0, 0};
    const auto returnTo = reinterpret_cast<std::uintptr_t>(memory + resume);
    for (unsigned shift = 0; shift < 64; shift += 8)
        jump.push_back(std::uint8_t(returnTo >> shift));
    {
        Patch continuation(original.Function<void*>(tail), jump.data(), jump.size());
        reinterpret_cast<void (*)(void*)>(memory)(app.data());
    }
    VirtualFree(memory, 0, MEM_RELEASE);
}
void EdgeOracle(OriginalPhysicsModule& original) {
    std::vector<std::unique_ptr<Patch>> patches;
    const auto patch = [&](unsigned offset, auto replacement) {
        auto* pointer = reinterpret_cast<void*>(replacement);
        patches.push_back(
            std::make_unique<Patch>(original.Function<void*>(offset), &pointer, sizeof pointer));
    };
    patch(0x1445f8, app.data());
    patch(0x128658, &EdgeControl);
    patch(0x1287b8, &EdgeMessage);
    patch(0x128690, &EdgeRead);
    patch(0x1286c8, &EdgeLength);
    patch(0x128748, &EdgeGetProc);
    patch(0x128638, &EdgeSetProc);
    patch(0x128620, &EdgeForward);
    patch(0x128720, &EdgeDestroy);
    const auto dialog = original.Function<DLGPROC>(0x56e50);
    const auto edit = original.Function<WNDPROC>(0x57190);
    std::fill(app.begin(), app.end(), 0);
    std::array<std::vector<unsigned char>, 2> models{std::vector<unsigned char>(0x4000),
                                                     std::vector<unsigned char>(0x4000)};
    const auto get = [&](unsigned slot) {
        float value;
        std::memcpy(&value, models[slot].data() + 0x355c, 4);
        return value;
    };
    for (unsigned slot = 0; slot < 2; ++slot) {
        const auto pointer = models[slot].data();
        std::memcpy(app.data() + 0xbe8 + slot * 8, &pointer, sizeof pointer);
        const float initial = slot ? 1.25f : .57f;
        std::memcpy(models[slot].data() + 0x355c, &initial, 4);
    }
    const auto window = reinterpret_cast<HWND>(std::uintptr_t(100));
    std::memcpy(app.data() + 0xa1b80, &window, sizeof window);
    dialog(window, WM_INITDIALOG, 0, 0);
    Check(edgeText == "0.57" && edgeSlider == 57 && app[0xa1b31] == 0,
          "original edge initialization");
    edgeText = "1.75";
    edit(EdgeControl(window, 646), WM_KILLFOCUS, 0, 0);
    edit(EdgeControl(window, 646), WM_CHAR, VK_RETURN, 0);
    Check(forwarded == 2 && get(0) == .57f && app[0xa1b31] == 0,
          "original noncommitting edge messages");
    struct Input {
        const char* text;
        float value;
        int slider;
    };
    for (const auto& input :
         {Input{"1.23456789", 1.23456f, 123}, Input{"-0.25", -.25f, 0}, Input{"2.50", 2.5f, 200},
          Input{"nonsense", 0, 0}, Input{"0.57", .57f, 57}, Input{"2e30", 2e30f, 0},
          Input{"1,25", 1, 100}}) {
        edgeText = input.text;
        app[0xa1b31] = app[0xa1e18] = 0;
        edit(EdgeControl(window, 646), WM_KEYDOWN, VK_RETURN, 0);
        Check(get(0) == input.value && edgeSlider == input.slider && edgeText == input.text &&
                  app[0xa1b31] == 1 && app[0xa1e18] == 1,
              "original edge text publication");
    }
    app[0x13e0] = 1;
    for (int position = 0; position <= 200; ++position) {
        edgeSlider = position;
        dialog(window, WM_HSCROLL, 0, 0);
        char formatted[64];
        const float value = float(position) / 100.f;
        std::snprintf(formatted, sizeof formatted, "%3.2f", double(value));
        Check(get(0) == 1 && get(1) == value && edgeText == formatted,
              "original edge slider/current-model publication");
    }
    app[0x328] = 1;
    app[0xa1b31] = app[0xa1e18] = 0;
    edgeText = ".75";
    edit(EdgeControl(window, 646), WM_KEYDOWN, VK_RETURN, 0);
    Check(get(1) == 2 && edgeSlider == 75 && app[0xa1b31] == 0 && app[0xa1e18] == 0,
          "original camera mode ignores edge text");
    edgeSlider = 17;
    dialog(window, WM_HSCROLL, 0, 0);
    Check(get(1) == 2 && edgeText == "0.17" && app[0xa1b31] == 0,
          "original camera mode ignores edge slider");
    edgeText = "1.99";
    dialog(window, WM_COMMAND, IDCANCEL, 0);
    HWND stored;
    std::memcpy(&stored, app.data() + 0xa1b80, sizeof stored);
    Check(edgeDestroyed && !stored && get(1) == 2, "original edge close discards pending text");
    std::puts(
        "Original edge dialog: 213 initialization/message/text/slider/model/close checkpoints; no windows");
    std::memcpy(app.data() + 0xa1b80, &window, sizeof window);
    app[0xa1b31] = app[0xa1e18] = 0;
    for (unsigned slot = 0; slot < 2; ++slot) {
        app[0x13e0] = std::uint8_t(slot);
        edgeText = "uncommitted";
        EdgeHostBranch(original, 0x677b6, 0x678bb);
        Check(edgeText == (slot ? "2.00" : "1.00") && edgeSlider == (slot ? 200 : 100),
              "original model-combo refresh replaces pending edge text");
        Check(!app[0xa1b31] && !app[0xa1e18], "original edge host refresh does not publish edits");
    }
    edgeDestroyed = false;
    EdgeHostBranch(original, 0x671c3, 0x671dc);
    std::memcpy(&stored, app.data() + 0xa1b80, sizeof stored);
    Check(edgeDestroyed && !stored, "original camera-combo refresh destroys edge dialog");
    std::puts("Original edge host: two model refreshes and camera-mode destruction; no windows");
}
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
        std::array<int, 95> advances{};
        Check(GetCharWidth32W(dc, 32, 126, advances.data()), "original dialog ASCII glyph widths");
        Check(advances == (font.second == 15 ? mikudancestudio::ui::kDialogArialAdvances
                                             : mikudancestudio::ui::kDialogGothicAdvances),
              "original dialog ASCII advance table");
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
    EdgeOracle(original);
    EditWidthOracle(argv[1]);
    std::printf(
        "Original camera numeric command: 256 exact input/output cases, hash %016llx; no windows or GPU\n",
        static_cast<unsigned long long>(hash));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
