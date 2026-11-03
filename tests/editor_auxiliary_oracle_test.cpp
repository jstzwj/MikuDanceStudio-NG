#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/english_name_edit.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/app_layout.hpp"
#include "editor_physics_selection_fixture.hpp"
#include "editor_reduction_fixture.hpp"
#include "editor_morph_scale_fixture.hpp"
#include "editor_morph_purge_fixture.hpp"
#include "editor_blink_fixture.hpp"
#include "editor_key_capacity_fixture.hpp"
#include "editor_paste_capacity_fixture.hpp"
#include "editor_vsq_fixture.hpp"
#include "mikudancestudio/vsq_format.hpp"
#include "editor_camera_scale_fixture.hpp"
#include "gravity_edit_reference.hpp"
#include "mikudancestudio/gravity_edit.hpp"
#include "mikudancestudio/camera_key_math.hpp"
#include "mikudancestudio/cpu_model_state.hpp"
#include "mikudancestudio/idle_render_gate.hpp"
#include "mikudancestudio/audio_frame_preview.hpp"
#include "mikudancestudio/blink_registration.hpp"
#include "d3d9_device_fixture.hpp"
#include <cstdio>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
static void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"
using namespace mikudancestudio;
using namespace format;
using namespace editor;
namespace {
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
    Patch(void* address, const void* bytes, std::size_t count)
        : address_(static_cast<unsigned char*>(address)), before_(count) {
        std::memcpy(before_.data(), address_, count);
        Write(bytes);
    }
    ~Patch() { Write(before_.data()); }
};
std::map<int, std::string> resetTexts;
std::set<int> resetSliders;
HWND WINAPI ResetChild(HWND, int id) {
    return reinterpret_cast<HWND>(std::uintptr_t(id));
}
BOOL WINAPI ResetText(HWND window, LPCSTR text) {
    resetTexts.emplace(int(reinterpret_cast<std::uintptr_t>(window)), text);
    return TRUE;
}
LRESULT WINAPI ResetSlider(HWND window, UINT message, WPARAM redraw, LPARAM position) {
    Check(message == 0x405 && redraw == 1 && position == 0, "native reset slider arguments");
    resetSliders.insert(int(reinterpret_cast<std::uintptr_t>(window)));
    return 0;
}
struct NativeAuxiliary {
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::MorphRecord> poses;
    std::vector<mdl::MorphKey> morphs{mdl::kMorphKeyCapacity};
    std::vector<mdl::DisplayKey> displays{mdl::kDisplayKeyCapacity};
    std::vector<mdl::IkChain> chains;
    std::vector<std::vector<std::uint8_t>> ik{mdl::kDisplayKeyCapacity};
    explicit NativeAuxiliary(const PmmSceneModel& source, bool pmx)
        : poses(source.morphPose.size()), chains(source.ikPose.size()) {
        model->physicsMode = pmx ? 2 : 0;
        model->morphCount = unsigned(poses.size());
        model->morphs = poses.data();
        model->morphKeys = morphs.data();
        model->displayKeys = displays.data();
        model->ikChains = chains.data();
        model->ikChainCount = unsigned(chains.size());
        model->UndoCursor() = model->LatestUndoCursor() = 29;
        Check(source.displayKeys.dense[0].rigidStates.empty(), "fixture has no parent bindings");
        for (unsigned i = 0; i < displays.size(); ++i) {
            ik[i].resize(chains.size());
            displays[i].ikStates = ik[i].data();
        }
        for (unsigned i = 0; i < source.morphKeys.dense.size(); ++i) {
            morphs[i].frame = source.morphKeys.dense[i].frame;
            morphs[i].previous = source.morphKeys.dense[i].previous;
            morphs[i].next = source.morphKeys.dense[i].next;
            morphs[i].value = source.morphKeys.dense[i].value;
            morphs[i].allocated = source.morphKeys.dense[i].allocated;
        }
        const auto copyMorph = [&](unsigned index, const auto& key) {
            morphs[index].frame = key.frame;
            morphs[index].previous = key.previous;
            morphs[index].next = key.next;
            morphs[index].value = key.value;
            morphs[index].allocated = key.allocated;
        };
        for (const auto& key : source.morphKeys.sparse)
            copyMorph(unsigned(key.index), key.value);
        for (const auto& [index, key] : source.morphKeys.unused)
            copyMorph(index, key);
        const auto& display = source.displayKeys.dense[0];
        displays[0].visible = display.visible;
        displays[0].allocated = display.allocated;
        std::copy(display.ikEnabled.begin(), display.ikEnabled.end(), ik[0].begin());
    }
    void Register(OriginalPhysicsModule& original, const PmmSceneModel& source, Track kind,
                  unsigned frame, unsigned channel = 0) {
        model->loadComplete = source.selection.loadComplete;
        model->maxFrame = source.selection.maxFrame;
        for (unsigned i = 0; i < poses.size(); ++i)
            poses[i].value = source.morphPose[i];
        for (unsigned i = 0; i < chains.size(); ++i)
            chains[i].enabled = source.ikPose[i];
        // The command dispatcher clears key marks before calling these kernels.
        // This harness isolates registration; it does not execute the UI tail.
        for (auto& key : morphs)
            key.allocated = 0;
        for (auto& key : displays)
            key.allocated = 0;
        if (kind == Track::Morph)
            original.Function<void (*)(mdl::ModelRecord*, int, int)>(0xca9c0)(
                model.get(), int(channel), int(frame));
        else
            original.Function<void (*)(mdl::ModelRecord*, int)>(0xcaf80)(model.get(), int(frame));
    }
    void Compare(const PmmSceneModel& source) const {
        Check(model->maxFrame == source.selection.maxFrame, "native maximum frame differs");
        Check(model->UndoCursor() == 29 && model->LatestUndoCursor() == 29,
              "auxiliary registration altered native bone history");
        std::map<unsigned, PmmMorphKey> expectedMorphs;
        for (unsigned i = 0; i < source.morphKeys.dense.size(); ++i)
            expectedMorphs[i] = source.morphKeys.dense[i];
        for (const auto& key : source.morphKeys.sparse)
            expectedMorphs[unsigned(key.index)] = key.value;
        for (const auto& [index, key] : source.morphKeys.unused)
            expectedMorphs[index] = key;
        for (unsigned i = 0; i < morphs.size(); ++i) {
            const auto& actual = morphs[i];
            const auto found = expectedMorphs.find(i);
            Check((i < poses.size() || actual.frame != 0) ==
                      (found != expectedMorphs.end() && (i < poses.size() || found->second.frame)),
                  "native morph slot differs");
            if (found == expectedMorphs.end())
                continue;
            const auto& expected = found->second;
            Check(actual.frame == expected.frame && actual.previous == expected.previous &&
                      actual.next == expected.next && actual.allocated == expected.allocated &&
                      !std::memcmp(&actual.value, &expected.value, sizeof(float)),
                  "native morph registration differs");
        }
        std::map<unsigned, PmmDisplayKey> expectedDisplays{{0, source.displayKeys.dense[0]}};
        for (const auto& key : source.displayKeys.sparse)
            expectedDisplays[unsigned(key.index)] = key.value;
        for (unsigned i = 0; i < displays.size(); ++i) {
            const auto& actual = displays[i];
            const auto found = expectedDisplays.find(i);
            Check((i == 0 || actual.frame != 0) == (found != expectedDisplays.end()),
                  "native display slot differs");
            if (found == expectedDisplays.end())
                continue;
            const auto& expected = found->second;
            Check(actual.frame == expected.frame && actual.previous == expected.previous &&
                      actual.next == expected.next && actual.allocated == expected.allocated &&
                      actual.visible == expected.visible && ik[i] == expected.ikEnabled,
                  "native display registration differs");
        }
    }
};
unsigned ResetCommand(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    void* child = reinterpret_cast<void*>(&ResetChild);
    void* text = reinterpret_cast<void*>(&ResetText);
    void* slider = reinterpret_cast<void*>(&ResetSlider);
    for (const auto& item : {std::pair{0x128658u, "GetDlgItem"},
                             {0x128598u, "SetWindowTextA"},
                             {0x1287b8u, "SendMessageA"}})
        Check(*original.Function<void**>(item.first) ==
                  reinterpret_cast<void*>(
                      GetProcAddress(GetModuleHandleW(L"user32.dll"), item.second)),
              "reset command import identity");
    Patch a(original.Function<void*>(0x128658), &child, sizeof child);
    Patch b(original.Function<void*>(0x128598), &text, sizeof text);
    Patch c(original.Function<void*>(0x1287b8), &slider, sizeof slider);
    const unsigned char expected[]{0x48, 0x8b, 0x8d, 0xb0, 0x0b, 0, 0};
    Check(!std::memcmp(original.Function<void*>(0x53de6), expected, sizeof expected),
          "dispatcher epilogue identity");
    // The extracted branch calls Win32 with RSP aligned as in the dispatcher,
    // and changes only RBX/RDI/R12 among nonvolatile registers. Jump into it:
    // its outgoing shadow space must not contain a wrapper return address.
    std::vector<unsigned char> code{0x53, 0x57, 0x41, 0x54, 0x48, 0x83, 0xec, 0x20,
                                    0x48, 0x8b, 0xd9, 0x45, 0x33, 0xe4, 0x48, 0xb8};
    const auto entry = reinterpret_cast<std::uintptr_t>(original.Function<void*>(0x522f8));
    for (unsigned shift = 0; shift < 64; shift += 8)
        code.push_back(std::uint8_t(entry >> shift));
    code.push_back(0xff);
    code.push_back(0xe0);
    const auto returnOffset = code.size();
    const unsigned char done[]{0x48, 0x83, 0xc4, 0x20, 0x41, 0x5c, 0x5f, 0x5b, 0xc3};
    code.insert(code.end(), std::begin(done), std::end(done));
    void* memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(memory != nullptr, "reset wrapper allocation");
    std::memcpy(memory, code.data(), code.size());
    DWORD previous;
    Check(VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &previous),
          "reset wrapper executable");
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    const auto returnAddress = reinterpret_cast<std::uintptr_t>(memory) + returnOffset;
    std::vector<unsigned char> jump{0xff, 0x25, 0, 0, 0, 0};
    for (unsigned shift = 0; shift < 64; shift += 8)
        jump.push_back(std::uint8_t(returnAddress >> shift));
    Patch tail(original.Function<void*>(0x53de6), jump.data(), jump.size());
    const auto execute = reinterpret_cast<void (*)(void*)>(memory);
    unsigned count = 0;
    for (const char* file : {"facial.pmd", "tint-morph.pmx"})
        for (bool dirty : {false, true}) {
            EditorSession portable;
            std::string reason;
            Check(portable.ImportModel(root / file, reason), reason.c_str());
            const auto channels = portable.Document().models[0].morphPose.size();
            for (unsigned i = 0; i < channels; ++i)
                Check(portable.SetMorph(0, i, float(i + 1) / 8, reason), reason.c_str());
            if (!dirty)
                Check(portable.Save(
                          root / "reset.pmm",
                          [](const auto& path, const auto& bytes, auto&) {
                              std::ofstream out(path, std::ios::binary);
                              out.write(reinterpret_cast<const char*>(bytes.data()),
                                        std::streamsize(bytes.size()));
                              return bool(out);
                          },
                          reason),
                      reason.c_str());
            NativeAuxiliary native(portable.Document().models[0], true);
            for (unsigned i = 0; i < channels; ++i)
                native.poses[i].value = portable.Document().models[0].morphPose[i];
            std::vector<unsigned char> app(0xa3000, dirty ? 1 : 0);
            app[0x328] = 0;
            app[0x13e0] = 7;
            auto* model = native.model.get();
            std::memcpy(app.data() + 0xbe8 + 7 * 8, &model, sizeof model);
            const auto before = app;
            std::vector<unsigned char> modelBefore(sizeof *model);
            std::memcpy(modelBefore.data(), model, modelBefore.size());
            resetTexts.clear();
            resetSliders.clear();
            execute(app.data());
            Check(app == before && !std::memcmp(model, modelBefore.data(), modelBefore.size()),
                  "reset changed native document/history flags");
            Check(resetTexts ==
                      (std::map<int, std::string>{
                          {506, "0.00000"}, {511, "0.00000"}, {516, "0.00000"}, {521, "0.00000"}}),
                  "native reset edit texts");
            Check(resetSliders == (std::set<int>{505, 510, 515, 520}),
                  "native reset slider identities");
            Check(portable.ResetMorphs(0, reason), reason.c_str());
            Check(portable.Dirty() == dirty, "portable reset changed dirty state");
            for (unsigned i = 0; i < channels; ++i)
                Check(native.poses[i].value == 0 &&
                          portable.Document().models[0].morphPose[i] == native.poses[i].value,
                      "native reset weight differs");
            ++count;
        }
    VirtualFree(memory, 0, MEM_RELEASE);
    return count;
}
unsigned undoCalls = 0;
void CaptureUndo(void*) {
    ++undoCalls;
}
bool shadowMenuChecked;
unsigned shadowMenuWrites;
unsigned switchMenuCommand = MDS_CMD_TOGGLE_SELF_SHADOW;
HMENU WINAPI ShadowMenu(HWND) {
    return reinterpret_cast<HMENU>(1);
}
UINT WINAPI ShadowMenuState(HMENU, UINT command, UINT flags) {
    Check(command == switchMenuCommand && !flags, "native switch menu query");
    return shadowMenuChecked ? MF_CHECKED : MF_UNCHECKED;
}
DWORD WINAPI ShadowMenuCheck(HMENU, UINT command, UINT flags) {
    Check(command == switchMenuCommand && flags == (shadowMenuChecked ? MF_UNCHECKED : MF_CHECKED),
          "native shadow menu write");
    ++shadowMenuWrites;
    return 0;
}
unsigned ShadowSwitchCommands(OriginalPhysicsModule& original) {
    switchMenuCommand = MDS_CMD_TOGGLE_SELF_SHADOW;
    void* menu = reinterpret_cast<void*>(&ShadowMenu);
    void* state = reinterpret_cast<void*>(&ShadowMenuState);
    void* check = reinterpret_cast<void*>(&ShadowMenuCheck);
    Patch menuImport(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch stateImport(original.Function<void*>(0x128660), &state, sizeof state);
    Patch checkImport(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned count = 0;
    for (unsigned enabled : {0u, 1u, 7u})
        for (int mode : {0, 1, 2})
            for (bool checked : {false, true}) {
                std::memset(app.get(), 0, sizeof(*app));
                app->selfShadowEnabled = std::uint8_t(enabled);
                app->selfShadowMode = mode;
                app->sceneModified = 0x7d;
                app->physicsResetPending = 1;
                app->currentFrame = 123;
                auto expected = std::make_unique<MMDAppState>(*app);
                expected->selfShadowEnabled = !checked;
                expected->letterKeyState[13] = 1; // Native P edge, app+0x64.
                shadowMenuChecked = checked;
                shadowMenuWrites = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                   MDS_CMD_TOGGLE_SELF_SHADOW);
                Check(shadowMenuWrites == 1 &&
                          !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                      "native shadow switch changed mode, dirty, physics or other state");
                ++count;
            }
    return count;
}
std::wstring capturedDirectory;
unsigned fileDialogCalls;
BOOL WINAPI CancelFileDialog(OPENFILENAMEW* options) {
    ++fileDialogCalls;
    capturedDirectory = options->lpstrInitialDir ? options->lpstrInitialDir : L"";
    return FALSE;
}
BOOL WINAPI IgnoreCurrentDirectory(LPCWSTR) {
    return TRUE;
}
unsigned unexpectedFileUi;
int WINAPI RejectFileMessageA(HWND, LPCSTR text, LPCSTR, UINT) {
    ++unexpectedFileUi;
    std::fprintf(stderr, "unexpected file message: %s\n", text);
    return IDCANCEL;
}
int WINAPI RejectFileMessageW(HWND, LPCWSTR text, LPCWSTR, UINT) {
    ++unexpectedFileUi;
    std::fwprintf(stderr, L"unexpected file message: %ls\n", text);
    return IDCANCEL;
}
INT_PTR WINAPI RejectFileDialog(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM) {
    ++unexpectedFileUi;
    return IDCANCEL;
}
unsigned FileFolderCommands(OriginalPhysicsModule& original) {
    switchMenuCommand = MDS_CMD_TOGGLE_PREVIOUS_FOLDER;
    auto menu = &ShadowMenu;
    auto state = &ShadowMenuState;
    auto check = &ShadowMenuCheck;
    auto open = &CancelFileDialog;
    auto directory = &IgnoreCurrentDirectory;
    auto messageA = &RejectFileMessageA;
    auto messageW = &RejectFileMessageW;
    auto dialog = &RejectFileDialog;
    Patch a(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch b(original.Function<void*>(0x128660), &state, sizeof state);
    Patch c(original.Function<void*>(0x1287a0), &check, sizeof check);
    Patch d(original.Function<void*>(0x128090), &open, sizeof open);
    Patch e(original.Function<void*>(0x128098), &open, sizeof open);
    Patch f(original.Function<void*>(0x128278), &directory, sizeof directory);
    Patch g(original.Function<void*>(0x128618), &messageA, sizeof messageA);
    Patch h(original.Function<void*>(0x128708), &messageW, sizeof messageW);
    Patch i(original.Function<void*>(0x128600), &dialog, sizeof dialog);
    unexpectedFileUi = 0;
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (bool checked : {false, true})
        for (bool playing : {false, true}) {
            std::memset(app.get(), 0, sizeof(*app));
            app->playbackActive = playing;
            app->sceneModified = 0x7d;
            app->physicsResetPending = 1;
            auto before = std::make_unique<MMDAppState>(*app);
            shadowMenuChecked = checked;
            shadowMenuWrites = 0;
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, switchMenuCommand);
            Check(shadowMenuWrites == 1 && !std::memcmp(app.get(), before.get(), sizeof(*app)),
                  "original folder toggle changes only menu state");
            ++cases;
        }
    for (int command : {MDS_CMD_OPEN_PROJECT, MDS_CMD_LOAD_POSE, MDS_CMD_LOAD_MOTION,
                        MDS_CMD_LOAD_WAVE, MDS_CMD_LOAD_BACKGROUND_IMAGE})
        for (bool checked : {false, true}) {
            std::memset(app.get(), 0, sizeof(*app));
            app->englishUI = 1;
            // Pin the original x64 DirectSound availability byte independently
            // of the retired field layout; no audio device is created here.
            reinterpret_cast<unsigned char*>(app.get())[0x2d8] = 1;
            wcscpy_s(app->dirUser, L"C:\\history\\project");
            wcscpy_s(app->dirPose, L"C:\\history\\pose");
            wcscpy_s(app->dirMotion, L"C:\\history\\motion");
            wcscpy_s(app->dirWave, L"C:\\history\\wave");
            wcscpy_s(app->dirBg, L"C:\\history\\background");
            const auto category = command == MDS_CMD_OPEN_PROJECT  ? L"project"
                                  : command == MDS_CMD_LOAD_POSE   ? L"pose"
                                  : command == MDS_CMD_LOAD_MOTION ? L"motion"
                                  : command == MDS_CMD_LOAD_WAVE   ? L"wave"
                                                                   : L"background";
            const auto fallback = command == MDS_CMD_OPEN_PROJECT  ? L"UserFile"
                                  : command == MDS_CMD_LOAD_POSE   ? L"UserFile\\Pose"
                                  : command == MDS_CMD_LOAD_MOTION ? L"UserFile\\Motion"
                                  : command == MDS_CMD_LOAD_WAVE   ? L"UserFile\\Wave"
                                                                   : L"UserFile\\BackGround";
            shadowMenuChecked = checked;
            capturedDirectory.clear();
            fileDialogCalls = 0;
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, command);
            const auto expected = checked ? std::wstring(L"C:\\history\\") + category : fallback;
            if (capturedDirectory != expected)
                std::fwprintf(stderr, L"folder command=%d actual='%ls' expected='%ls'\n", command,
                              capturedDirectory.c_str(), expected.c_str());
            Check(!unexpectedFileUi && fileDialogCalls == 1 && capturedDirectory == expected,
                  "original file dialog initial directory");
            ++cases;
        }
    return cases;
}
struct NativeSamplerDevice : SnapshotDeviceFixture {
    std::map<std::pair<unsigned, D3DSAMPLERSTATETYPE>, DWORD> samplers;
    std::vector<std::pair<D3DRENDERSTATETYPE, DWORD>> states;
    NativeSamplerDevice() : SnapshotDeviceFixture(nullptr) {}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE state,
                                              DWORD value) override {
        samplers[{stage, state}] = value;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override {
        states.emplace_back(state, value);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE,
                                                   DWORD) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps) override {
        *caps = {};
        return S_OK;
    }
};
std::vector<std::pair<unsigned, void*>> reloadCalls;
bool ObserveToonReload(void* app) {
    reloadCalls.emplace_back(1, app);
    return true;
}
void ObserveTextureReload(void* renderer) {
    reloadCalls.emplace_back(2, renderer);
}
struct ReloadTextureFixture {
    void** table = nullptr;
    std::array<unsigned char, 16> pixels{};
    unsigned releases = 0, locks = 0, unlocks = 0;
};
ULONG STDMETHODCALLTYPE ReloadRelease(ReloadTextureFixture* self) {
    ++self->releases;
    return 0;
}
HRESULT STDMETHODCALLTYPE ReloadLock(ReloadTextureFixture* self, UINT level, D3DLOCKED_RECT* rect,
                                     const RECT* bounds, DWORD flags) {
    Check(level == 0 && !bounds && flags == D3DLOCK_READONLY, "native reload lock contract");
    ++self->locks;
    rect->Pitch = 8;
    rect->pBits = self->pixels.data();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE ReloadUnlock(ReloadTextureFixture* self, UINT level) {
    Check(level == 0, "native reload unlock level");
    ++self->unlocks;
    return S_OK;
}
struct ReloadAttempt {
    HRESULT result;
    UINT width, height;
};
struct ReloadImageInfo {
    UINT Width, Height, Depth, MipLevels;
    D3DFORMAT Format;
    UINT ResourceType, ImageFileFormat;
};
std::vector<ReloadAttempt> reloadAttempts;
std::vector<DWORD> reloadUsages;
std::array<ReloadTextureFixture, 3> reloadCreated;
HRESULT WINAPI ReloadFile(IDirect3DDevice9* device, LPCWSTR name, UINT width, UINT height,
                          UINT mips, DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD filter,
                          DWORD mipFilter, D3DCOLOR key, ReloadImageInfo* info,
                          PALETTEENTRY* palette, IDirect3DTexture9** texture) {
    Check(device == reinterpret_cast<IDirect3DDevice9*>(0x12345678) &&
              !std::wcscmp(name, L"fixture.bmp") && width == UINT(-1) && height == UINT(-1) &&
              mips == 1 && format == D3DFMT_A8R8G8B8 && pool == D3DPOOL_MANAGED &&
              filter == DWORD(-1) && mipFilter == DWORD(-1) && key == 0 && !palette,
          "native reload D3DX arguments");
    const auto index = reloadUsages.size();
    Check(index < reloadAttempts.size(), "native reload attempt bound");
    reloadUsages.push_back(usage);
    const auto attempt = reloadAttempts[index];
    info->Width = attempt.width;
    info->Height = attempt.height;
    *texture = attempt.result == S_OK ? reinterpret_cast<IDirect3DTexture9*>(&reloadCreated[index])
                                      : nullptr;
    return attempt.result;
}
unsigned TextureReloadCache(OriginalPhysicsModule& original) {
    auto file = &ReloadFile;
    Patch import(original.Function<void*>(0x1288e8), &file, sizeof file);
    std::array<void*, 22> table{};
    table[2] = reinterpret_cast<void*>(&ReloadRelease);
    table[19] = reinterpret_cast<void*>(&ReloadLock);
    table[20] = reinterpret_cast<void*>(&ReloadUnlock);
    const std::vector<std::vector<ReloadAttempt>> profiles{
        {{S_OK, 2, 2}},
        {{E_FAIL, 0, 0}, {S_OK, 2, 2}},
        {{E_FAIL, 0, 0}, {E_FAIL, 0, 0}},
        {{S_OK, 1, 1}, {S_OK, 2, 2}},
        {{S_OK, 1, 1}, {E_FAIL, 0, 0}, {S_OK, 2, 2}},
        {{S_OK, 1, 1}, {E_FAIL, 0, 0}, {E_FAIL, 0, 0}}};
    unsigned cases = 0;
    for (const auto& profile : profiles) {
        std::vector<unsigned char> renderer(0x3ab00);
        const wchar_t name[] = L"fixture.bmp";
        const auto* path = name;
        ReloadTextureFixture old;
        old.table = table.data();
        auto* prior = &old;
        void* device = reinterpret_cast<void*>(0x12345678);
        const DWORD tag = 0xa5123456;
        std::memcpy(renderer.data() + 8, &path, sizeof path);
        std::memcpy(renderer.data() + 16, &prior, sizeof prior);
        std::memcpy(renderer.data() + 24, &tag, sizeof tag);
        std::memcpy(renderer.data() + 0x3a9c0, &device, sizeof device);
        const auto before = renderer;
        reloadAttempts = profile;
        reloadUsages.clear();
        reloadCreated = {};
        for (auto& texture : reloadCreated) {
            texture.table = table.data();
            texture.pixels[8] = 20;
            texture.pixels[9] = 40;
            texture.pixels[10] = 60;
        }
        original.Function<void (*)(void*)>(0x8f30)(renderer.data());
        Check(old.releases == 1 && reloadUsages.size() == profile.size() && reloadUsages[0] == 1024,
              "native reload releases old entry and performs expected attempts");
        for (std::size_t n = 1; n < reloadUsages.size(); ++n)
            Check(reloadUsages[n] == 0, "native reload fallback usage");
        const bool ready = profile.back().result == S_OK;
        ReloadTextureFixture* actual = nullptr;
        DWORD actualTag = 0;
        std::memcpy(&actual, renderer.data() + 16, sizeof actual);
        std::memcpy(&actualTag, renderer.data() + 24, sizeof actualTag);
        Check(actual == (ready ? &reloadCreated[profile.size() - 1] : nullptr) &&
                  actualTag == (ready ? 0xa514283c : 0xa5000000),
              "native reload failure unbinds/zeros RGB, success samples bottom-left BGR");
        if (ready)
            Check(actual->locks == 1 && actual->unlocks == 1, "native reload samples once");
        if (profile[0].result == S_OK && profile[0].width == 1)
            Check(reloadCreated[0].releases == 1, "native reload releases 1x1 autogen retry");
        std::copy(before.begin() + 16, before.begin() + 28, renderer.begin() + 16);
        Check(renderer == before, "native reload changes only texture pointer and RGB tag");
        ++cases;
    }
    return cases;
}
unsigned TextureReloadCommands(OriginalPhysicsModule& original) {
    const auto jump = [](auto function) {
        std::array<unsigned char, 12> bytes{0x48, 0xb8};
        std::memcpy(bytes.data() + 2, &function, sizeof function);
        bytes[10] = 0xff;
        bytes[11] = 0xe0;
        return bytes;
    };
    const auto toon = jump(&ObserveToonReload), texture = jump(&ObserveTextureReload);
    Patch a(original.Function<void*>(0x9a130), toon.data(), toon.size());
    Patch b(original.Function<void*>(0x8f30), texture.data(), texture.size());
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (bool playing : {false, true})
        for (bool camera : {false, true}) {
            std::memset(app.get(), 0, sizeof(*app));
            auto* raw = reinterpret_cast<unsigned char*>(app.get());
            auto* renderer = reinterpret_cast<void*>(0x12345678);
            std::memcpy(raw + 0xa16e0, &renderer, sizeof renderer);
            app->playbackActive = playing;
            app->optflag[0] = camera;
            app->currentFrame = 123;
            app->sceneModified = 0x7d;
            app->physicsResetPending = 1;
            auto expected = std::make_unique<MMDAppState>(*app);
            reloadCalls.clear();
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, MDS_CMD_RESTORE_TEXTURES);
            Check(reloadCalls == (std::vector<std::pair<unsigned, void*>>{{1, app.get()},
                                                                          {2, renderer}}) &&
                      !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                  "original texture reload: toon then shared cache, no document/input changes");
            ++cases;
        }
    return cases;
}
struct AboutObservation {
    HWND owner = nullptr;
    std::string text, title;
    UINT flags = 0;
    unsigned calls = 0;
} aboutMessage;
int WINAPI AboutMessage(HWND owner, LPCSTR text, LPCSTR title, UINT flags) {
    aboutMessage = {owner, text, title, flags, aboutMessage.calls + 1};
    return IDOK;
}
unsigned AboutCommands(OriginalPhysicsModule& original) {
    auto message = &AboutMessage;
    Patch import(original.Function<void*>(0x128618), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (bool english : {false, true})
        for (bool floating : {false, true})
            for (bool playing : {false, true})
                for (int command : {MDS_CMD_ABOUT, MDS_CMD_ABOUT_PHYSICS}) {
                    std::memset(app.get(), 0, sizeof(*app));
                    auto* raw = reinterpret_cast<unsigned char*>(app.get());
                    raw[0xa1b90] = english; // Original locale byte, not the reconstructed wrapper.
                    app->floatingWindow = floating ? reinterpret_cast<HWND>(2) : nullptr;
                    app->playbackActive = playing;
                    app->sceneModified = 0x7d;
                    app->physicsResetPending = 1;
                    app->currentFrame = 123;
                    auto expected = std::make_unique<MMDAppState>(*app);
                    const bool physics = command == MDS_CMD_ABOUT_PHYSICS;
                    auto* expectedRaw = reinterpret_cast<unsigned char*>(expected.get());
                    *reinterpret_cast<int*>(expectedRaw + (physics ? 0x48 : 0x58)) = 1;
                    if (!physics)
                        *reinterpret_cast<int*>(expectedRaw + 0xc0) = 1;
                    aboutMessage = {};
                    original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, command);
                    Check(aboutMessage.calls == 1 &&
                              !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                          "original about dialog changes only native input latches");
                    Check(aboutMessage.owner ==
                                  (!physics && floating ? reinterpret_cast<HWND>(2) : nullptr) &&
                              aboutMessage.flags == (physics ? MB_TOPMOST : MB_OK),
                          "original about ownership and flags");
                    const std::string title =
                        physics
                            ? english
                                  ? "about physical engine"
                                  : "\x95\xa8\x97\x9d\x83\x47\x83\x93\x83\x57\x83\x93\x82\xc9\x82\xc2\x82\xa2\x82\xc4"
                            : "About";
                    const std::string body =
                        physics
                            ? "Bullet Physics Library Ver.2.75\n\nhttp://www.bulletphysics.com/"
                            : std::string(
                                  "MikuMikuDance Ver.9.32\n  (64bitOS Version)\n\nprogrammed by ") +
                                  (english ? "Yu Higuchi" : "\x94\xf3\x8c\xfb\x97\x44");
                    Check(aboutMessage.title == title && aboutMessage.text == body,
                          "original about text");
                    ++cases;
                }
    return cases;
}
unsigned DisplayFlagCommands(OriginalPhysicsModule& original, bool physicsBodies = false) {
    switchMenuCommand = physicsBodies ? MDS_CMD_TOGGLE_PHYSICS_BODIES : MDS_CMD_TOGGLE_WIREFRAME;
    auto menu = &ShadowMenu;
    auto check = &ShadowMenuCheck;
    Patch a(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch b(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (unsigned raw : {0u, 1u, 7u})
        for (bool playing : {false, true}) {
            std::memset(app.get(), 0, sizeof(*app));
            (physicsBodies ? app->rigidBodyDisplayEnabled : app->wireframeRenderingEnabled) =
                static_cast<unsigned char>(raw);
            app->playbackActive = playing;
            app->sceneModified = 0x7d;
            app->physicsResetPending = 1;
            app->currentFrame = 123;
            auto expected = std::make_unique<MMDAppState>(*app);
            (physicsBodies ? expected->rigidBodyDisplayEnabled
                           : expected->wireframeRenderingEnabled) = !raw;
            if (physicsBodies)
                expected->letterKeyState[5] = 1;
            shadowMenuChecked = raw != 0;
            shadowMenuWrites = 0;
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, switchMenuCommand);
            Check(shadowMenuWrites == 1 && !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                  "original display switch and D-key latch");
            ++cases;
        }
    return cases;
}
unsigned WireframeCommands(OriginalPhysicsModule& original) {
    return DisplayFlagCommands(original);
}
unsigned AntialiasCommands(OriginalPhysicsModule& original) {
    switchMenuCommand = MDS_CMD_TOGGLE_ANTIALIASING;
    auto menu = &ShadowMenu;
    auto state = &ShadowMenuState;
    auto check = &ShadowMenuCheck;
    Patch a(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch b(original.Function<void*>(0x128660), &state, sizeof state);
    Patch c(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    NativeSamplerDevice device;
    std::vector<unsigned char> renderer(0x3ab00);
    auto* rendererPointer = renderer.data();
    auto* devicePointer = static_cast<IDirect3DDevice9*>(&device);
    std::memcpy(renderer.data() + 0x3a9c0, &devicePointer, sizeof devicePointer);
    const auto beforeRenderer = renderer;
    unsigned cases = 0;
    for (bool checked : {false, true})
        for (bool playing : {false, true})
            for (unsigned latch : {0u, 7u}) {
                std::memset(app.get(), 0, sizeof(*app));
                std::memcpy(reinterpret_cast<unsigned char*>(app.get()) + 0xa16e0, &rendererPointer,
                            sizeof rendererPointer);
                app->playbackActive = playing;
                app->sceneModified = 0x7d;
                app->physicsResetPending = 1;
                app->currentFrame = 123;
                app->letterKeyState[11] = latch;
                auto expected = std::make_unique<MMDAppState>(*app);
                expected->letterKeyState[11] = 1; // Original command RVA 0x4fa4e: [app+0x5c].
                shadowMenuChecked = checked;
                shadowMenuWrites = 0;
                device.states.clear();
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, switchMenuCommand);
                Check(shadowMenuWrites == 1 &&
                          !std::memcmp(app.get(), expected.get(), sizeof(*app)) &&
                          renderer == beforeRenderer,
                      "native antialias menu, H latch, unchanged scene/renderer");
                Check(device.states.size() == 1 &&
                          device.states[0].first == D3DRS_MULTISAMPLEANTIALIAS &&
                          device.states[0].second == DWORD(!checked),
                      "native antialias render state");
                EditorSession portable;
                portable.SetAntialiasing(checked);
                const auto revision = portable.Revision(),
                           appearance = portable.AppearanceRevision();
                portable.SetAntialiasing(!checked);
                Check(portable.DisplayOptions().antialiasing == !checked &&
                          portable.Revision() == revision &&
                          portable.AppearanceRevision() == appearance + 1 && !portable.CanUndo(),
                      "portable antialias display state");
                ++cases;
            }
    return cases;
}
unsigned MipmapCommands(OriginalPhysicsModule& original) {
    switchMenuCommand = MDS_CMD_TOGGLE_MIPMAP;
    auto menu = &ShadowMenu;
    auto state = &ShadowMenuState;
    auto check = &ShadowMenuCheck;
    Patch a(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch b(original.Function<void*>(0x128660), &state, sizeof state);
    Patch c(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    NativeSamplerDevice device;
    std::vector<unsigned char> renderer(0x3ab00);
    auto* rendererPointer = renderer.data();
    auto* devicePointer = static_cast<IDirect3DDevice9*>(&device);
    unsigned cases = 0;
    for (bool checked : {false, true})
        for (unsigned raw : {0u, 1u, 7u})
            for (bool playing : {false, true})
                for (DWORD anisotropy : {1u, 16u}) {
                    std::memset(app.get(), 0, sizeof(*app));
                    std::fill(renderer.begin(), renderer.end(), 0);
                    std::memcpy(renderer.data() + 0x3a9c0, &devicePointer, sizeof devicePointer);
                    std::memcpy(renderer.data() + 0x3a9b0, &anisotropy, sizeof anisotropy);
                    renderer[0x3aa84] = static_cast<unsigned char>(raw);
                    std::memcpy(reinterpret_cast<unsigned char*>(app.get()) + 0xa16e0,
                                &rendererPointer, sizeof rendererPointer);
                    app->playbackActive = playing;
                    app->sceneModified = 0x7d;
                    const auto before = std::make_unique<MMDAppState>(*app);
                    shadowMenuChecked = checked;
                    shadowMenuWrites = 0;
                    device.samplers.clear();
                    original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                       MDS_CMD_TOGGLE_MIPMAP);
                    Check(shadowMenuWrites == 1 && renderer[0x3aa84] == !checked &&
                              !std::memcmp(app.get(), before.get(), sizeof(*app)),
                          "native mip switch changes only menu and renderer policy");
                    const auto sampler = [&](unsigned stage, D3DSAMPLERSTATETYPE kind) {
                        return device.samplers.at({stage, kind});
                    };
                    Check(sampler(0, D3DSAMP_MINFILTER) ==
                                  (checked ? D3DTEXF_LINEAR : D3DTEXF_ANISOTROPIC) &&
                              sampler(0, D3DSAMP_MAGFILTER) ==
                                  (checked ? D3DTEXF_LINEAR : D3DTEXF_ANISOTROPIC),
                          "original base sampler min/mag policy");
                    for (unsigned stage = 0; stage < 3; ++stage)
                        Check(sampler(stage, D3DSAMP_MIPFILTER) ==
                                      (checked ? D3DTEXF_NONE : D3DTEXF_ANISOTROPIC) &&
                                  sampler(stage, D3DSAMP_MAXANISOTROPY) ==
                                      (checked ? 1u : anisotropy),
                              "original mip and anisotropy policy");
                    ++cases;
                }
    return cases;
}
unsigned AudioPreviewCommands(OriginalPhysicsModule& original) {
    void* menu = reinterpret_cast<void*>(&ShadowMenu);
    void* check = reinterpret_cast<void*>(&ShadowMenuCheck);
    Patch menuImport(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch checkImport(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    switchMenuCommand = MDS_CMD_TOGGLE_WAVE_ON_FRAME;
    unsigned count = 0;
    for (unsigned enabled : {0u, 1u, 7u})
        for (bool playing : {false, true})
            for (bool wave : {false, true}) {
                std::memset(app.get(), 0, sizeof(*app));
                app->wavPlaysOnFrameMove = std::uint8_t(enabled);
                app->playbackActive = playing;
                app->waveEnabled = wave;
                app->sceneModified = 0x7d;
                app->currentFrame = 123;
                auto expected = std::make_unique<MMDAppState>(*app);
                expected->wavPlaysOnFrameMove = !enabled;
                expected->letterKeyState[16] = 1;
                shadowMenuChecked = enabled != 0;
                shadowMenuWrites = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, switchMenuCommand);
                Check(shadowMenuWrites == 1 &&
                          !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                      "native frame-WAV toggle changes only preference, menu and F latch");
                ++count;
            }
    return count;
}
double observedPreviewSeconds = -1;
void* observedPreviewAudio = nullptr;
void ObservePreview(void* audio, double seconds) {
    observedPreviewAudio = audio;
    observedPreviewSeconds = seconds;
}
unsigned AudioPreviewMath(OriginalPhysicsModule& original) {
    const auto observe = &ObservePreview;
    std::array<unsigned char, 12> jump{0x48, 0xb8};
    std::memcpy(jump.data() + 2, &observe, sizeof observe);
    jump[10] = 0xff;
    jump[11] = 0xe0;
    Patch restart(original.Function<void*>(0xdac20), jump.data(), jump.size());
    const unsigned char expected[]{0x8b, 0x87, 0x50, 0x14, 0, 0};
    Check(!std::memcmp(original.Function<void*>(0x3d485), expected, sizeof expected),
          "native frame-WAV time block identity");
    // Preserve RDI and the original call alignment/shadow space. Execute the
    // actual CVTSI2SS/DIVSS/CVTPS2PD block, observing its WaveRestartAt argument.
    const unsigned char done[]{0x48, 0x83, 0xc4, 0x20, 0x5f, 0xc3};
    Patch tail(original.Function<void*>(0x3d4be), done, sizeof done);
    std::vector<unsigned char> code{0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xf9, 0x48, 0xb8};
    const auto entry = reinterpret_cast<std::uintptr_t>(original.Function<void*>(0x3d485));
    for (unsigned shift = 0; shift < 64; shift += 8)
        code.push_back(std::uint8_t(entry >> shift));
    code.insert(code.end(), {0xff, 0xe0});
    void* memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(memory != nullptr, "preview wrapper allocation");
    std::memcpy(memory, code.data(), code.size());
    DWORD protection;
    Check(VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &protection),
          "preview wrapper executable");
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    auto app = std::make_unique<MMDAppState>();
    std::memset(app.get(), 0, sizeof(*app));
    app->audioContext = reinterpret_cast<void*>(0x12345678);
    std::vector<std::uint32_t> frames{0,         1,         2,          29,        30,   31,
                                      59,        60,        61,         999,       1000, 1001,
                                      0x1000000, 0x1000001, 0x7fffffff, 0xffffffff};
    for (unsigned n = 0; n < 64; ++n)
        frames.push_back(n * 327671u + 7);
    for (const auto frame : frames) {
        app->currentFrame = std::int32_t(frame);
        observedPreviewSeconds = -1;
        observedPreviewAudio = nullptr;
        reinterpret_cast<void (*)(MMDAppState*)>(memory)(app.get());
        Check(observedPreviewSeconds == AudioFramePreviewSeconds(frame) &&
                  observedPreviewAudio == app->audioContext,
              "native frame-WAV start offset uses unsigned wrap and float division");
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    return unsigned(frames.size());
}
std::vector<unsigned> previewOrder;
void* previewContext = nullptr;
DWORD previewFrequency = 0;
BOOL WINAPI PreviewKill(HWND window, UINT_PTR id) {
    Check(window == reinterpret_cast<HWND>(2) && id == 100, "native preview KillTimer");
    previewOrder.push_back(1);
    return TRUE;
}
void PreviewClose(void* audio) {
    Check(audio == previewContext, "native preview close context");
    previewOrder.push_back(2);
}
bool PreviewStart(void* audio) {
    Check(audio == previewContext, "native preview start context");
    previewOrder.push_back(3);
    return true;
}
HRESULT STDMETHODCALLTYPE PreviewFrequency(void*, DWORD frequency) {
    previewFrequency = frequency;
    previewOrder.push_back(4);
    return E_INVALIDARG;
}
void PreviewFeed(void* audio, double seconds) {
    Check(audio == previewContext, "native preview feed context");
    observedPreviewSeconds = seconds;
    previewOrder.push_back(5);
}
UINT_PTR WINAPI PreviewTimer(HWND window, UINT_PTR id, UINT interval, TIMERPROC proc) {
    Check(window == reinterpret_cast<HWND>(2) && id == 100 &&
              interval == AudioFramePreviewMilliseconds && !proc,
          "native preview SetTimer");
    previewOrder.push_back(6);
    return 100;
}
unsigned AudioPreviewRestart(OriginalPhysicsModule& original) {
    const auto jump = [](auto function) {
        std::array<unsigned char, 12> code{0x48, 0xb8};
        std::memcpy(code.data() + 2, &function, sizeof function);
        code[10] = 0xff;
        code[11] = 0xe0;
        return code;
    };
    const auto close = jump(&PreviewClose), start = jump(&PreviewStart), feed = jump(&PreviewFeed);
    Patch a(original.Function<void*>(0xda250), close.data(), close.size());
    Patch b(original.Function<void*>(0xda930), start.data(), start.size());
    Patch c(original.Function<void*>(0xdab80), feed.data(), feed.size());
    auto kill = &PreviewKill;
    auto timer = &PreviewTimer;
    Patch d(original.Function<void*>(0x128760), &kill, sizeof kill);
    Patch e(original.Function<void*>(0x128738), &timer, sizeof timer);
    std::array<void*, 16> table{};
    table[15] = reinterpret_cast<void*>(&PreviewFrequency);
    auto* vtable = table.data();
    auto* buffer = &vtable;
    unsigned count = 0;
    for (LONG volume : {-10000l, -600l, 0l})
        for (double seconds : {0., .25, 1., 143165584.}) {
            std::array<unsigned char, 0x300> audio{};
            HWND window = reinterpret_cast<HWND>(2);
            std::memcpy(audio.data() + 0x18, &window, sizeof window);
            std::memcpy(audio.data() + 0x28, &buffer, sizeof buffer);
            std::memcpy(audio.data() + 0x288, &volume, sizeof volume);
            const auto before = audio;
            previewContext = audio.data();
            previewOrder.clear();
            observedPreviewSeconds = -1;
            original.Function<void (*)(void*, double)>(0xdac20)(audio.data(), seconds);
            Check(
                previewOrder == (std::vector<unsigned>{1, 2, 3, 4, 5, 6}) &&
                    observedPreviewSeconds == seconds && previewFrequency == DWORD(volume) &&
                    audio == before,
                "native preview restart sequence, 33ms stop and invalid volume-as-frequency call");
            ++count;
        }
    return count;
}
std::uintptr_t __cdecl PreviewThread(void(__cdecl* entry)(void*), unsigned stackSize,
                                     void* context) {
    Check(entry && stackSize == 0 && context == previewContext, "native preview starts feeder");
    return 1234; // Observe the request without starting any thread or audio device.
}
unsigned AudioPreviewFileOffsets(OriginalPhysicsModule& original,
                                 const std::filesystem::path& root) {
    auto thread = &PreviewThread;
    Patch spawn(original.Function<void*>(0x1283c0), &thread, sizeof thread);
    const auto path = root / "preview-offset.tmp";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "preview offset file");
    std::array<unsigned char, 64> bytes{};
    DWORD written = 0;
    Check(WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) &&
              written == bytes.size(),
          "preview offset file bytes");
    unsigned count = 0;
    for (DWORD rate : {8000u, 44100u, 48000u, 192000u})
        for (WORD align : {1u, 2u, 4u, 6u, 8u})
            for (DWORD frame :
                 {0u, 1u, 2u, 30u, 31u, 60u, 61u, 5400u, 6000u, 0x1000000u, 0x7fffffffu}) {
                Check(SetFilePointer(file, 44, nullptr, FILE_BEGIN) == 44,
                      "preview initial data offset");
                std::array<unsigned char, 0x300> audio{};
                void* buffer = reinterpret_cast<void*>(1);
                const DWORD avg = rate * align, data = 44;
                std::memcpy(audio.data() + 0x28, &buffer, sizeof buffer);
                std::memcpy(audio.data() + 0x38, &avg, sizeof avg);
                std::memcpy(audio.data() + 0x3c, &align, sizeof align);
                std::memcpy(audio.data() + 0x268, &data, sizeof data);
                std::memcpy(audio.data() + 0x270, &file, sizeof file);
                previewContext = audio.data();
                Check(original.Function<bool (*)(void*, double)>(0xdab80)(
                          audio.data(), AudioFramePreviewSeconds(frame)),
                      "native feeder start");
                const auto actual = SetFilePointer(file, 0, nullptr, FILE_CURRENT);
                const auto expected =
                    44 + AudioFramePreviewSampleOffset(frame, rate, align) * align;
                Check(actual == expected,
                      "native signed-byte seek, alignment and negative-seek fallback");
                std::uintptr_t worker;
                std::memcpy(&worker, audio.data() + 0x258, sizeof worker);
                Check(worker == 1234, "native preview worker token");
                ++count;
            }
    CloseHandle(file);
    return count;
}
unsigned AudioDisableCommands(OriginalPhysicsModule& original) {
    void* menu = reinterpret_cast<void*>(&ShadowMenu);
    void* check = reinterpret_cast<void*>(&ShadowMenuCheck);
    Patch menuImport(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch checkImport(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    switchMenuCommand = MDS_CMD_TOGGLE_WAVE_DISABLED;
    unsigned count = 0;
    for (unsigned disabled : {0u, 1u, 7u})
        for (unsigned playing : {0u, 1u})
            for (unsigned wave : {0u, 1u}) {
                std::memset(app.get(), 0, sizeof(*app));
                app->automaticFrameAdvanceEnabled = std::uint8_t(disabled);
                app->playbackActive = std::uint8_t(playing);
                app->waveEnabled = std::uint8_t(wave);
                app->sceneModified = 0x7d;
                app->physicsResetPending = 1;
                app->currentFrame = 123;
                auto expected = std::make_unique<MMDAppState>(*app);
                expected->automaticFrameAdvanceEnabled = !disabled;
                expected->letterKeyState[5] = 1; // Native x64 D latch, app+0x44.
                shadowMenuChecked = disabled != 0;
                shadowMenuWrites = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                   MDS_CMD_TOGGLE_WAVE_DISABLED);
                if (std::memcmp(app.get(), expected.get(), sizeof(*app))) {
                    const auto* actual = reinterpret_cast<const unsigned char*>(app.get());
                    const auto* wanted = reinterpret_cast<const unsigned char*>(expected.get());
                    for (std::size_t i = 0; i < sizeof(*app); ++i)
                        if (actual[i] != wanted[i])
                            std::fprintf(stderr, "audio switch byte %zx: actual=%u expected=%u\n", i,
                                         unsigned(actual[i]), unsigned(wanted[i]));
                }
                Check(shadowMenuWrites == 1 &&
                          !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                      "native disable-WAV switch changed playback, dirty or other state");
                ++count;
            }
    return count;
}
unsigned PlaybackPhysicsSwitchCommands(OriginalPhysicsModule& original) {
    switchMenuCommand = MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF;
    void* menu = reinterpret_cast<void*>(&ShadowMenu);
    void* state = reinterpret_cast<void*>(&ShadowMenuState);
    void* check = reinterpret_cast<void*>(&ShadowMenuCheck);
    Patch menuImport(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch stateImport(original.Function<void*>(0x128660), &state, sizeof state);
    Patch checkImport(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned count = 0;
    for (unsigned raw : {0u, 1u, 7u})
        for (bool checked : {false, true})
            for (bool playing : {false, true})
                for (bool camera : {false, true}) {
                    std::memset(app.get(), 0, sizeof(*app));
                    app->playbackAlwaysOnOffMode = std::uint8_t(raw);
                    app->playbackActive = playing;
                    app->optflag[0] = camera;
                    app->playbackPhysicsMode = 3;
                    app->savedPlaybackPhysicsMode = 1;
                    app->sceneModified = 0x7d;
                    app->physicsResetPending = 1;
                    auto expected = std::make_unique<MMDAppState>(*app);
                    expected->playbackAlwaysOnOffMode = !checked;
                    expected->letterKeyState[13] = 1;
                    shadowMenuChecked = checked;
                    shadowMenuWrites = 0;
                    original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, switchMenuCommand);
                    Check(shadowMenuWrites == 1 &&
                              !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                          "native playback physics switch changes only next-start policy");
                    ++count;
                }
    return count;
}
HWND lowPowerOwner;
unsigned lowPowerMessages;
HWND WINAPI LowPowerChild(HWND owner, int id) {
    Check(owner == lowPowerOwner && id == panel::kFrameVolumeCheckbox,
          "native low-power checkbox owner");
    return reinterpret_cast<HWND>(552);
}
LRESULT WINAPI LowPowerMessage(HWND window, UINT message, WPARAM value, LPARAM extra) {
    Check(window == reinterpret_cast<HWND>(552) && message == BM_SETCHECK &&
              value == !shadowMenuChecked && !extra,
          "native low-power checkbox state");
    ++lowPowerMessages;
    return 0;
}
unsigned LowPowerCommands(OriginalPhysicsModule& original) {
    switchMenuCommand = MDS_CMD_TOGGLE_LOW_POWER;
    void* menu = reinterpret_cast<void*>(&ShadowMenu);
    void* state = reinterpret_cast<void*>(&ShadowMenuState);
    void* check = reinterpret_cast<void*>(&ShadowMenuCheck);
    void* child = reinterpret_cast<void*>(&LowPowerChild);
    void* message = reinterpret_cast<void*>(&LowPowerMessage);
    Patch a(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch b(original.Function<void*>(0x128660), &state, sizeof state);
    Patch c(original.Function<void*>(0x1287a0), &check, sizeof check);
    Patch d(original.Function<void*>(0x128658), &child, sizeof child);
    Patch e(original.Function<void*>(0x1287b8), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned count = 0;
    for (unsigned raw : {0u, 1u, 7u})
        for (bool checked : {false, true})
            for (bool playing : {false, true})
                for (bool floating : {false, true})
                    for (bool button : {false, true}) {
                        std::memset(app.get(), 0, sizeof(*app));
                        app->frameVolumeControlEnabled = std::uint8_t(raw);
                        app->playbackActive = playing;
                        app->floatingWindow = floating ? reinterpret_cast<HWND>(42) : nullptr;
                        app->sceneModified = 0x7d;
                        app->physicsResetPending = 1;
                        app->messageSeen = 7;
                        auto expected = std::make_unique<MMDAppState>(*app);
                        expected->frameVolumeControlEnabled = !checked;
                        lowPowerOwner = app->floatingWindow;
                        shadowMenuChecked = checked;
                        shadowMenuWrites = lowPowerMessages = 0;
                        original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                            nullptr, button ? panel::kFrameVolumeCheckbox : switchMenuCommand);
                        Check(shadowMenuWrites == 1 && lowPowerMessages == unsigned(!button) &&
                                  !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                              "native low-power menu authority and complete application state");
                        ++count;
                    }
    return count;
}
unsigned LowPowerFrameGate(OriginalPhysicsModule& original) {
    // Execute the original gate instructions, preserving its three nonvolatile
    // registers. Only the unrelated frame-driver stack spill is omitted.
    const unsigned char spill[]{0x48, 0x89, 0x8d, 0xe0, 0, 0, 0};
    Check(!std::memcmp(original.Function<void*>(0x27568), spill, sizeof spill),
          "native render-gate spill identity");
    const unsigned char nops[]{0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    const unsigned char done[]{0x41, 0x5f, 0x41, 0x5e, 0x41, 0x5c, 0xc3};
    Patch skip(original.Function<void*>(0x27568), nops, sizeof nops);
    Patch tail(original.Function<void*>(0x275e5), done, sizeof done);
    std::vector<unsigned char> code{0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0xb8};
    const auto entry = reinterpret_cast<std::uintptr_t>(original.Function<void*>(0x2755e));
    for (unsigned shift = 0; shift < 64; shift += 8)
        code.push_back(std::uint8_t(entry >> shift));
    code.insert(code.end(), {0xff, 0xe0});
    void* memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(memory != nullptr, "render-gate wrapper allocation");
    std::memcpy(memory, code.data(), code.size());
    DWORD protection;
    Check(VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &protection),
          "render-gate wrapper executable");
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    const auto execute = reinterpret_cast<void (*)(MMDAppState*)>(memory);
    auto app = std::make_unique<MMDAppState>();
    unsigned count = 0;
    for (unsigned state : {0u, 1u, 2u, 3u, 7u, UINT_MAX})
        for (unsigned playing : {0u, 1u, 7u})
            for (unsigned low : {0u, 1u, 7u})
                for (bool dialog : {false, true}) {
                    std::memset(app.get(), 0, sizeof(*app));
                    app->messageSeen = state;
                    app->playbackActive = std::uint8_t(playing);
                    app->frameVolumeControlEnabled = std::uint8_t(low);
                    auto* bytes = reinterpret_cast<unsigned char*>(app.get());
                    bytes[0xa1b98] = dialog;
                    app->sceneModified = 0x7d;
                    app->physicsResetPending = 1;
                    auto expected = std::make_unique<MMDAppState>(*app);
                    for (int step = 0; step < 4; ++step) {
                        AdvanceIdleRenderGate(expected->messageSeen, playing != 0, low != 0,
                                              dialog);
                        execute(app.get());
                        Check(!std::memcmp(app.get(), expected.get(), sizeof(*app)),
                              "native render gate state and complete application bytes");
                        ++count;
                    }
                }
    VirtualFree(memory, 0, MEM_RELEASE);
    return count;
}
unsigned ModelRenderFlags(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    auto app = std::make_unique<MMDAppState>();
    auto native = std::make_unique<mdl::ModelRecord>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned count = 0;
    for (const auto* file : {"articulated.pmd", "articulated.pmx"})
        for (bool playing : {false, true})
            for (unsigned raw : {0u, 1u, 7u})
                for (bool shadow : {false, true}) {
                    EditorSession portable;
                    std::string reason;
                    Check(portable.ImportModel(root / file, reason), reason.c_str());
                    Check(portable.Document().models[0].toonFlag == 1 &&
                              portable.Document().models[0].postLoadFlag == 0,
                          "native model import rendering defaults");
                    Check(portable.Edit(
                              "raw-flag",
                              [&](auto& d, auto&) {
                                  d.models[0].toonFlag = d.models[0].postLoadFlag =
                                      std::uint8_t(raw);
                                  return true;
                              },
                              reason),
                          reason.c_str());
                    if (playing)
                        Check(portable.Play(reason), reason.c_str());
                    std::memset(app.get(), 0, sizeof(*app));
                    std::memset(native.get(), 0, sizeof(*native));
                    app->modelSlots[0] = native.get();
                    app->playbackActive = playing;
                    app->sceneModified = 0x7d;
                    app->currentFrame = 123;
                    native->toonFlag = native->postLoadFlag2 = std::uint8_t(raw);
                    const auto beforeApp = std::make_unique<MMDAppState>(*app);
                    auto expected = std::make_unique<mdl::ModelRecord>(*native);
                    (shadow ? expected->toonFlag : expected->postLoadFlag2) = !raw;
                    const auto counters = portable.Runtime().Counters();
                    const auto revision = portable.Revision();
                    const bool undo = portable.CanUndo(), redo = portable.CanRedo();
                    const auto* snapshot = portable.Runtime().Snapshot(0);
                    original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                        nullptr, shadow ? panel::kShadowCheckbox : panel::kAddBlendCheckbox);
                    Check(!std::memcmp(app.get(), beforeApp.get(), sizeof(*app)) &&
                              !std::memcmp(native.get(), expected.get(), sizeof(*native)),
                          "native model rendering toggle side effects");
                    Check(portable.ToggleModelRenderFlag(shadow), "portable rendering toggle");
                    const auto& model = portable.Document().models[0];
                    Check(model.toonFlag == native->toonFlag &&
                              model.postLoadFlag == native->postLoadFlag2 &&
                              portable.Playing() == playing && portable.Revision() == revision &&
                              portable.CanUndo() == undo && portable.CanRedo() == redo &&
                              portable.Runtime().Counters().frames == counters.frames &&
                              portable.Runtime().Snapshot(0) == snapshot,
                          "model render toggle changed pose, playback or history");
                    ++count;
                }
    return count;
}
unsigned ModelDisplayCommands(OriginalPhysicsModule& original) {
    void* menu = reinterpret_cast<void*>(&ShadowMenu);
    void* check = reinterpret_cast<void*>(&ShadowMenuCheck);
    Patch menuImport(original.Function<void*>(0x1286a0), &menu, sizeof menu);
    Patch checkImport(original.Function<void*>(0x1287a0), &check, sizeof check);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned count = 0;
    for (bool transparency : {false, true})
        for (unsigned raw : {0u, 1u, 7u})
            for (bool playing : {false, true})
                for (bool loaded : {false, true}) {
                    std::memset(app.get(), 0, sizeof(*app));
                    app->characterTransparentMode = app->modelNonDisplayMode = std::uint8_t(raw);
                    app->playbackActive = playing;
                    app->sceneModified = 0x7d;
                    app->physicsResetPending = 1;
                    app->currentFrame = 123;
                    std::vector<std::unique_ptr<mdl::ModelRecord>> models, expectedModels;
                    if (loaded)
                        for (unsigned slot : {0u, 4u, 254u}) {
                            models.push_back(std::make_unique<mdl::ModelRecord>());
                            auto& model = *models.back();
                            model.loadComplete = slot != 4;
                            model.displayState = std::uint8_t(slot);
                            app->modelSlots[slot] = &model;
                            expectedModels.push_back(std::make_unique<mdl::ModelRecord>(model));
                            if (transparency)
                                expectedModels.back()->displayState = !raw;
                        }
                    auto expected = std::make_unique<MMDAppState>(*app);
                    (transparency ? expected->characterTransparentMode
                                  : expected->modelNonDisplayMode) = !raw;
                    expected->letterKeyState[transparency ? 4 : 6] = 1;
                    switchMenuCommand = transparency ? MDS_CMD_TOGGLE_MODEL_TRANSPARENCY
                                                     : MDS_CMD_TOGGLE_MODEL_NON_DISPLAY;
                    shadowMenuChecked = raw != 0;
                    shadowMenuWrites = 0;
                    original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, switchMenuCommand);
                    Check(shadowMenuWrites == 1 &&
                              !std::memcmp(app.get(), expected.get(), sizeof(*app)),
                          "native model display command application state");
                    for (unsigned i = 0; i < models.size(); ++i)
                        Check(!std::memcmp(models[i].get(), expectedModels[i].get(),
                                           sizeof(mdl::ModelRecord)),
                              "native model display command model state");
                    ++count;
                }
    return count;
}
unsigned FrameRateCommands(OriginalPhysicsModule& original) {
    auto state = std::make_unique<MMDAppState>();
    auto* pointer = state.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned count = 0;
    for (float previous : {30.f, 60.f, 1000.f})
        for (int command : {MDS_CMD_FPS_UNLIMITED, MDS_CMD_FPS_30, MDS_CMD_FPS_60}) {
            std::memset(state.get(), 0, sizeof(*state));
            state->fpsLimit = previous;
            state->sceneModified = 0x7d;
            state->physicsResetPending = 1;
            state->currentFrame = 123;
            std::vector<unsigned char> expected(sizeof(*state));
            std::memcpy(expected.data(), state.get(), expected.size());
            const float target = command == MDS_CMD_FPS_30   ? 30.f
                                 : command == MDS_CMD_FPS_60 ? 60.f
                                                             : 1000.f;
            std::memcpy(expected.data() + offsetof(MMDAppState, fpsLimit), &target, sizeof target);
            // No original entrypoint, window, device or simulation is involved.
            execute(nullptr, command);
            Check(!std::memcmp(state.get(), expected.data(), expected.size()),
                  "native FPS command changed fields other than the display rate");
            ++count;
        }
    return count;
}
unsigned SceneCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    // Execute the full original dispatcher with a private application record.
    // Stub only window refresh and history capture; the actual quaternion
    // conversion, selection loop and deferred-reset writes execute unchanged.
    const unsigned char ret = 0xc3;
    Patch refresh(original.Function<void*>(0x20dd0), &ret, 1);
    Patch readouts(original.Function<void*>(0x20ce0), &ret, 1);
    Patch timeline(original.Function<void*>(0x5f7f0), &ret, 1);
    std::vector<unsigned char> jump{0xff, 0x25, 0, 0, 0, 0};
    const auto capture = reinterpret_cast<std::uintptr_t>(&CaptureUndo);
    for (unsigned shift = 0; shift < 64; shift += 8)
        jump.push_back(std::uint8_t(capture >> shift));
    Patch undo(original.Function<void*>(0x26fd0), jump.data(), jump.size());
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned count = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "triangle.pmd"})
        for (int choice : {-1, 0, 1}) {
            EditorSession portable;
            std::string reason;
            Check(portable.ImportModel(root / file, reason), reason.c_str());
            const auto resource = portable.Runtime().Resource(0);
            const auto bonesCount = unsigned(resource->BoneNames().size());
            const int active = choice == 1 ? int(bonesCount - 1) : choice;
            Check(portable.SelectBones({0}, active, reason), reason.c_str());
            if (active >= 0)
                Check(portable.SetBone(0, unsigned(active), {2, 3, 4}, {0, .6f, 0, .8f}, reason),
                      reason.c_str());
            NativeAuxiliary native(portable.Document().models[0],
                                   resource->SourceKind() == ModelKind::Pmx);
            auto* model = native.model.get();
            std::vector<mdl::BoneRecord> bones(bonesCount);
            std::vector<unsigned char> selected(bonesCount), edited(bonesCount);
            selected[0] = 1;
            model->boneTable = bones.data();
            model->boneCount = bonesCount;
            model->boneSelection = selected.data();
            model->bonePhysicsState = edited.data();
            model->selectedBone = active;
            for (unsigned i = 0; i < bonesCount; ++i) {
                const auto& pose = portable.Document().models[0].bonePose[i];
                std::copy_n(pose.components.begin(), 3, bones[i].trans);
                std::copy_n(pose.components.begin() + 3, 4, bones[i].rotQuat);
                edited[i] = pose.flags[1];
                bones[i].hasRigidBody = resource->EditingBones()[i].hasRigidBody;
            }
            std::fill(app.begin(), app.end(), 0);
            std::memcpy(app.data() + 0xbe8, &model, sizeof model);
            undoCalls = 0;
            const auto frame = portable.Runtime().Counters().frames;
            execute(nullptr, MDS_CMD_RESET_BONE_CAMERA_ROTATION);
            Check(portable.ResetRotation(reason), reason.c_str());
            Check(undoCalls == unsigned(active >= 0), "native rotation undo capture count");
            Check(app[0xa1b31] == unsigned(active >= 0), "native rotation dirty gate");
            for (unsigned i = 0; i < bonesCount; ++i) {
                const auto& pose = portable.Document().models[0].bonePose[i];
                Check(!std::memcmp(bones[i].trans, pose.components.data(), 12) &&
                          !std::memcmp(bones[i].rotQuat, pose.components.data() + 3, 16) &&
                          edited[i] == pose.flags[1],
                      "native rotation reset pose");
            }
            ++count;
            execute(nullptr, MDS_CMD_SELECT_PHYSICS_BONES);
            Check(portable.SelectPhysicsBones(reason), reason.c_str());
            for (unsigned i = 0; i < bonesCount; ++i)
                Check(bool(selected[i]) == (std::find(portable.Selected().bones.begin(),
                                                      portable.Selected().bones.end(),
                                                      i) != portable.Selected().bones.end()),
                      "native physics bone selection");
            Check(model->selectedBone == portable.ActiveBone(), "native selected physics bone");
            ++count;
            execute(nullptr, MDS_CMD_RESET_PHYSICS_BODIES);
            portable.ResetPhysicsBodies();
            Check(app[0x9fcc1] == portable.Runtime().Simulation().resetPending && app[0x9fcc1] == 1,
                  "native deferred physical reset");
            Check(frame == portable.Runtime().Counters().frames, "commands advanced physics");
            ++count;
            Check(portable.SelectModel(255, reason), reason.c_str());
            auto camera = portable.Runtime().Session()->Pose().camera;
            camera.position = {1, 2, 3};
            camera.rotation = {.2f, -.4f, .6f};
            camera.distance = -17;
            Check(portable.SetCamera(camera, reason), reason.c_str());
            app[0x328] = 1;
            app[0xa1b31] = 0;
            std::memcpy(app.data() + 0x348, camera.rotation.data(), 12);
            std::memcpy(app.data() + 0x36c, camera.position.data(), 12);
            std::memcpy(app.data() + 0xa1900, &camera.distance, 4);
            execute(nullptr, MDS_CMD_RESET_BONE_CAMERA_ROTATION);
            Check(portable.ResetRotation(reason), reason.c_str());
            const auto actual = portable.Runtime().Session()->Pose().camera;
            Check(!std::memcmp(app.data() + 0x348, actual.rotation.data(), 12) &&
                      !std::memcmp(app.data() + 0x36c, actual.position.data(), 12) &&
                      !std::memcmp(app.data() + 0xa1900, &actual.distance, 4) && app[0xa1b31] == 1,
                  "native camera rotation reset");
            ++count;
        }
    return count;
}
unsigned PhysicsKeyCommand(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned cases = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "triangle.pmd"})
        for (bool reverse : {false, true})
            for (unsigned mode = 0; mode < 3; ++mode) {
                EditorSession portable;
                std::string reason;
                Check(PhysicsSelectionFixture(portable, root, file, reverse, mode, reason),
                      reason.c_str());
                const auto& source = portable.Document().models[0];
                const auto resource = portable.Runtime().Resource(0);
                NativeAuxiliary native(source, resource->SourceKind() == ModelKind::Pmx);
                auto* model = native.model.get();
                std::vector<mdl::BoneKey> keys(mdl::kBoneKeyCapacity);
                std::vector<mdl::BoneRecord> bones(source.bonePose.size());
                model->boneCount = unsigned(bones.size());
                model->boneTable = bones.data();
                model->boneKeys = keys.data();
                for (unsigned i = 0; i < bones.size(); ++i) {
                    int index;
                    Check(portable.Runtime().DocumentBoneIndex(0, int(i), index, reason),
                          reason.c_str());
                    bones[i].hasRigidBody =
                        resource->EditingBones()[std::size_t(index)].hasRigidBody;
                }
                const auto copy = [&](unsigned index, const auto& key) {
                    keys[index].frame = key.frame;
                    keys[index].previous = key.previous;
                    keys[index].next = key.next;
                    keys[index].physicsDisabled = key.physicsDisabled;
                    std::copy(key.position.begin(), key.position.end(), keys[index].position);
                    std::copy(key.rotation.begin(), key.rotation.end(), keys[index].rotation);
                };
                for (unsigned i = 0; i < source.boneKeys.dense.size(); ++i)
                    copy(i, source.boneKeys.dense[i]);
                for (const auto& key : source.boneKeys.sparse)
                    copy(unsigned(key.index), key.value);
                for (const auto& key : source.boneKeys.unused)
                    copy(key.first, key.second);
                for (auto& key : keys)
                    key.allocated = 1;
                for (auto& key : native.morphs)
                    key.allocated = 1;
                for (auto& key : native.displays)
                    key.allocated = 1;
                std::fill(app.begin(), app.end(), 0);
                std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                execute(nullptr, MDS_CMD_SELECT_PHYSICS_ON_FRAMES);
                Check(portable.SelectPhysicsOnKeys(reason), reason.c_str());
                std::set<unsigned> marked;
                const auto& result = portable.Document().models[0].boneKeys;
                for (unsigned i = 0; i < result.dense.size(); ++i)
                    if (result.dense[i].allocated)
                        marked.insert(i);
                for (const auto& key : result.sparse)
                    if (key.value.allocated)
                        marked.insert(unsigned(key.index));
                for (unsigned i = 0; i < keys.size(); ++i) {
                    if (bool(keys[i].allocated) != (marked.count(i) != 0))
                        std::fprintf(stderr,
                                     "%s reverse=%u mode=%u record=%u native=%u portable=%u\n",
                                     file, unsigned(reverse), mode, i, unsigned(keys[i].allocated),
                                     unsigned(marked.count(i)));
                    Check(bool(keys[i].allocated) == (marked.count(i) != 0),
                          "native physics-key pool marks");
                }
                for (const auto& key : native.morphs)
                    Check(!key.allocated, "native physics-key morph sweep");
                for (const auto& key : native.displays)
                    Check(!key.allocated, "native physics-key display sweep");
                Check(!app[0xa1b31], "native physics-key selection does not dirty scene");
                ++cases;
            }
    return cases;
}
std::map<int, std::string> rangeFields;
int WINAPI RangeText(HWND window, LPSTR text, int capacity) {
    const auto& value = rangeFields.at(int(reinterpret_cast<std::uintptr_t>(window)));
    const auto count = std::min(value.size(), std::size_t(capacity - 1));
    std::memcpy(text, value.data(), count);
    text[count] = 0;
    return int(count);
}
unsigned RangeCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    auto child = &ResetChild;
    auto text = &RangeText;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child);
    Patch getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const auto execute = original.Function<void (*)(HWND, WPARAM)>(0x3f550);
    unsigned cases = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx"}) {
        EditorSession portable;
        std::string reason;
        Check(PhysicsSelectionFixture(portable, root, file, false, 1, reason), reason.c_str());
        const bool edited = portable.Edit(
            "detached-range-record",
            [](auto& document, auto&) {
                auto& track = document.models[0].boneKeys;
                auto key = track.dense[0];
                key.frame = 3;
                key.previous = key.next = 0;
                key.allocated = 0;
                track.sparse.push_back({30, 0, key});
                key.frame = 5; // overlaps the forward chain but owns its own mark
                track.sparse.push_back({31, 0, key});
                return true;
            },
            reason);
        Check(edited, reason.c_str());
        const auto& source = portable.Document().models[0];
        NativeAuxiliary native(source,
                               portable.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx);
        auto* model = native.model.get();
        std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
        std::vector<mdl::BoneRecord> poses(source.bonePose.size());
        model->boneCount = unsigned(poses.size());
        model->boneTable = poses.data();
        model->boneKeys = bones.data();
        std::strcpy(poses[0].name, "selection-root");
        const auto copy = [](const auto& track, auto& pool) {
            const auto one = [&](unsigned index, const auto& key) {
                pool[index].frame = key.frame;
                pool[index].previous = key.previous;
                pool[index].next = key.next;
                pool[index].allocated = key.allocated;
            };
            for (unsigned i = 0; i < track.dense.size(); ++i)
                one(i, track.dense[i]);
            for (const auto& key : track.sparse)
                one(unsigned(key.index), key.value);
            for (const auto& key : track.unused)
                one(key.first, key.second);
        };
        copy(source.boneKeys, bones);
        copy(source.morphKeys, native.morphs);
        copy(source.displayKeys, native.displays);
        std::fill(app.begin(), app.end(), 0);
        std::memcpy(app.data() + 0xbe8, &model, sizeof model);
        struct Step {
            const char* name;
            unsigned first, last;
            KeyScope scope;
        };
        const Step steps[] = {
            {"selection-root", 5, 5, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"All frame", 2, 2, {}},
            {"All bone", 0, 0, {KeyScopeKind::Bones}},
            {"All facial", 5, 5, {KeyScopeKind::Morphs}},
            {"disp/IK/OP", 0, 0, {KeyScopeKind::Track, {Track::Display, 0, 0, 0}}},
            {"selection-root", 5, 5, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"All frame", 99, 99, {}},
            {"selection-root", 5, 5, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"selection-root", 3, 3, {KeyScopeKind::Track, {Track::Bone, 0, 0, 0}}},
            {"All frame", 3, 3, {}},
            {"All frame", 5, 2, {}}};
        const auto compare = [&](const auto& track, const auto& pool) {
            std::vector<unsigned char> expected(pool.size());
            for (unsigned i = 0; i < track.dense.size(); ++i)
                expected[i] = track.dense[i].allocated;
            for (const auto& key : track.sparse)
                expected[unsigned(key.index)] = key.value.allocated;
            for (const auto& key : track.unused)
                expected[key.first] = key.second.allocated;
            for (unsigned i = 0; i < pool.size(); ++i) {
                if (pool[i].allocated != expected[i])
                    std::fprintf(stderr, "%s step=%u record=%u original=%u portable=%u\n", file,
                                 cases, i, unsigned(pool[i].allocated), unsigned(expected[i]));
                Check(pool[i].allocated == expected[i], "native range pool marks");
            }
        };
        const auto revision = portable.Revision(), frames = portable.Runtime().Counters().frames;
        for (const auto& step : steps) {
            rangeFields = {{425, std::to_string(step.first)},
                           {426, std::to_string(step.last)},
                           {434, step.name}};
            execute(nullptr, 415);
            portable.SelectKeys(step.first, step.last, step.scope);
            const auto& result = portable.Document().models[0];
            compare(result.boneKeys, bones);
            compare(result.morphKeys, native.morphs);
            compare(result.displayKeys, native.displays);
            Check(!app[0xa1b31] && portable.Revision() == revision &&
                      portable.Runtime().Counters().frames == frames,
                  "range selection is metadata only");
            ++cases;
        }
    }
    return cases;
}
std::array<float, 3> nativeGravityVector;
unsigned nativeGravityWrites = 0;
void CaptureGravity(void*, const float* value) {
    std::copy_n(value, 3, nativeGravityVector.begin());
    ++nativeGravityWrites;
}
void WriteGravity(std::vector<unsigned char>& app, const PmmEvaluatedGravity& value) {
    std::memcpy(app.data() + 0x9fcc4, value.direction.data(), 12);
    std::memcpy(app.data() + 0x9fcd0, &value.acceleration, 4);
    std::memcpy(app.data() + 0x9fcd4, &value.noise, 4);
    app[0xa1d68] = value.noiseEnabled;
}
unsigned GravityChannels(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x5f7f0), &ret, 1);
    std::vector<unsigned char> app(0xa3000), physics(0x88);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::array<void*, 14> vtable{};
    vtable[13] = reinterpret_cast<void*>(&CaptureGravity);
    auto* table = vtable.data();
    auto* world = &table;
    auto* scene = physics.data();
    std::memcpy(physics.data() + 0x80, &world, sizeof world);
    std::memcpy(app.data() + 0x9fcb8, &scene, sizeof scene);
    const auto execute = original.Function<void (*)(int, float)>(0x95b40);
    EditorSession portable;
    std::string reason;
    Check(portable.ImportModel(root / "articulated.pmx", reason), "gravity fixture import");
    unsigned count = 0;
    const auto revision = portable.Revision(), frames = portable.Runtime().Counters().frames;
    const bool undo = portable.CanUndo();
    for (unsigned channel : {0, 1, 2, 3, 4, 2, 0})
        for (float value : {0.f, -.75f, -2.f, .57f, 2.f, 10.125f, 1000000.f}) {
            WriteGravity(app, portable.Runtime().Session()->Pose().gravity);
            app[0xa137b] = 0;
            nativeGravityWrites = 0;
            execute(int(channel), value);
            Check(portable.SetGravityChannel(channel, value, reason), "portable gravity edit");
            const auto& result = portable.Runtime().Session()->Pose().gravity;
            Check(!std::memcmp(app.data() + 0x9fcc4, result.direction.data(), 12) &&
                      !std::memcmp(app.data() + 0x9fcd0, &result.acceleration, 4) &&
                      !std::memcmp(app.data() + 0x9fcd4, &result.noise, 4),
                  "native gravity channel state");
            Check(nativeGravityWrites == unsigned(channel < 4),
                  "native immediate gravity update gate");
            if (channel < 4) {
                std::array<float, 3> vector;
                cpu_model::Math{}.normalize(vector.data(), result.direction.data());
                for (unsigned i = 0; i < 3; ++i) {
                    vector[i] = (vector[i] * result.acceleration) * 10.f;
                    Check(std::abs(vector[i] - nativeGravityVector[i]) <=
                              2e-5f * std::max(1.f, std::abs(nativeGravityVector[i])),
                          "native gravity vector within portable normalization budget");
                }
            }
            Check(app[0xa137b] == 1 && !app[0xa1b31] && portable.Revision() == revision &&
                      portable.Runtime().Counters().frames == frames &&
                      portable.CanUndo() == undo &&
                      portable.Selected().tracks.count({Track::Gravity, 255, 0, 0}),
                  "gravity preview preserves history/physics and selects row");
            ++count;
        }
    return count;
}
unsigned GravityRegistration(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    EditorSession portable;
    std::string reason;
    Check(portable.ImportModel(root / "articulated.pmx", reason), "gravity registration fixture");
    Check(portable.SelectModel(255, reason), "gravity camera context");
    std::vector<unsigned char> app(0xa3000);
    std::vector<mdl::GravityKey> native(10000);
    auto* pool = native.data();
    std::memcpy(app.data() + 0x3e8, &pool, sizeof pool);
    const auto execute = original.Function<void (*)(void*, unsigned)>(0x5da60);
    unsigned count = 0;
    const auto copy = [](const auto& key, auto& result) {
        result.frame = key.frame;
        result.previous = key.previous;
        result.next = key.next;
        result.acceleration = key.acceleration;
        std::copy(key.direction.begin(), key.direction.end(), result.direction);
        result.noise = key.noise;
        result.noiseEnabled = key.noiseEnabled;
        result.selected = key.selected;
    };
    for (unsigned cycle = 0; cycle < 3; ++cycle)
        for (unsigned frame : {0, 10, 4, 20, 10, 0, 60, 30}) {
            Check(portable.Seek(frame, reason), "gravity registration seek");
            Check(portable.SetGravityChannel(0, float(count) * .375f - 3.f, reason) &&
                      portable.SetGravityChannel(1, -.5f, reason) &&
                      portable.SetGravityChannel(2, float(cycle) * .25f, reason) &&
                      portable.SetGravityChannel(3, .75f, reason) &&
                      portable.SetGravityChannel(4, float(count) - 10.5f, reason) &&
                      portable.SetGravityNoise(count % 2 != 0, reason),
                  "gravity registration values");
            std::fill(native.begin(), native.end(), mdl::GravityKey{});
            const auto& source = portable.Document().gravityKeys;
            copy(source.dense[0], native[0]);
            for (const auto& key : source.sparse)
                copy(key.value, native[unsigned(key.index)]);
            for (const auto& key : source.unused)
                copy(key.second, native[key.first]);
            for (auto& key : native)
                key.selected = 0; // original dialog's caller sweep
            WriteGravity(app, portable.Runtime().Session()->Pose().gravity);
            auto maximum = portable.Document().timeline.lastRegisteredFrame;
            std::memcpy(app.data() + 0x9f038, &maximum, 4);
            execute(app.data(), frame);
            const auto updates = portable.Runtime().Counters().frames;
            const auto physics = portable.Runtime().Simulation().cursorSeconds;
            Check(portable.RegisterGravity(reason), "portable gravity registration");
            const auto& result = portable.Document().gravityKeys;
            std::vector<mdl::GravityKey> actual(10000);
            copy(result.dense[0], actual[0]);
            for (const auto& key : result.sparse)
                copy(key.value, actual[unsigned(key.index)]);
            for (const auto& key : result.unused)
                copy(key.second, actual[key.first]);
            Check(!std::memcmp(actual.data(), native.data(), actual.size() * sizeof(actual[0])),
                  "native gravity registration pool bytes");
            Check(!std::memcmp(app.data() + 0x9f038,
                               &portable.Document().timeline.lastRegisteredFrame, 4),
                  "native gravity registration watermark");
            Check(portable.Runtime().Counters().frames == updates &&
                      portable.Runtime().Simulation().cursorSeconds == physics,
                  "gravity registration never evaluates or steps physics");
            ++count;
        }
    return count;
}
void GravityHiddenControls(OriginalPhysicsModule& original, const wchar_t* path) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x5f7f0), &ret, 1);
    std::vector<unsigned char> app(0xa3000), physics(0x88);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::array<void*, 14> vtable{};
    vtable[13] = reinterpret_cast<void*>(&CaptureGravity);
    auto* table = vtable.data();
    auto* world = &table;
    auto* scene = physics.data();
    std::memcpy(physics.data() + 0x80, &world, sizeof world);
    std::memcpy(app.data() + 0x9fcb8, &scene, sizeof scene);
    HMODULE controls = LoadLibraryW(L"comctl32.dll");
    const auto init = reinterpret_cast<BOOL(WINAPI*)(const INITCOMMONCONTROLSEX*)>(
        GetProcAddress(controls, "InitCommonControlsEx"));
    INITCOMMONCONTROLSEX config{sizeof config, ICC_BAR_CLASSES};
    Check(init && init(&config), "hidden gravity common controls");
    HMODULE resources =
        LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    Check(resources != nullptr, "hidden gravity resource module");
    const auto resource = FindResourceW(resources, MAKEINTRESOURCEW(803), MAKEINTRESOURCEW(5));
    const auto* bytes =
        static_cast<const unsigned char*>(LockResource(LoadResource(resources, resource)));
    DWORD style;
    Check(bytes && SizeofResource(resources, resource) >= 26, "gravity dialog resource");
    std::memcpy(&style, bytes + 12, 4);
    Check(!(style & WS_VISIBLE), "gravity resource must never show automatically");
    WriteGravity(app, {0, 3, 9.8f, {0, -1, 0}});
    HWND dialog = CreateDialogParamA(resources, MAKEINTRESOURCEA(803), nullptr,
                                     original.Function<DLGPROC>(0x94e80), 0);
    Check(dialog && !IsWindowVisible(dialog), "original gravity dialog stays hidden");
    std::memcpy(app.data() + 0xa1d58, &dialog, sizeof dialog);
    const auto mirror = original.Function<void (*)(HWND)>(0x95f00);
    for (const auto& sample : kGravityMirrorReference) {
        WriteGravity(app, {0, 3, sample.input, {0, -1, 0}});
        mirror(dialog);
        char text[256];
        GetWindowTextA(GetDlgItem(dialog, 709), text, 256);
        float live;
        std::memcpy(&live, app.data() + 0x9fcd0, 4);
        Check(std::strcmp(text, sample.text) == 0 && live == sample.output,
              "original gravity formatting and EDIT feedback");
        Check(!IsWindowVisible(dialog), "original gravity mirror must stay hidden");
    }
    const auto slider = GetDlgItem(dialog, 637);
    Check(SendMessageW(slider, TBM_GETPAGESIZE, 0, 0) == 40 &&
              SendMessageW(slider, TBM_GETLINESIZE, 0, 0) == 1,
          "native dialog trackbar steps");
    for (const auto& key : std::vector<std::pair<int, int>>{{VK_LEFT, -1},
                                                            {VK_UP, -1},
                                                            {VK_RIGHT, 1},
                                                            {VK_DOWN, 1},
                                                            {VK_PRIOR, -40},
                                                            {VK_NEXT, 40},
                                                            {VK_HOME, -100},
                                                            {VK_END, 100}}) {
        SendMessageW(slider, TBM_SETPOS, 0, 0);
        SendMessageW(slider, WM_KEYDOWN, key.first, 0);
        Check(SendMessageW(slider, TBM_GETPOS, 0, 0) == key.second,
              "native dialog trackbar keyboard");
    }
    SendMessageW(slider, TBM_SETPOS, 0, 0);
    for (unsigned i = 0; i < kDialogWheelDeltas.size(); ++i) {
        const auto delta = kDialogWheelDeltas[i];
        SendMessageW(slider, WM_MOUSEWHEEL, WPARAM(WORD(delta)) << 16, 0);
        Check(SendMessageW(slider, TBM_GETPOS, 0, 0) == kDialogWheelPositions[i],
              "native dialog trackbar wheel accumulation");
    }
    DestroyWindow(dialog);
    FreeLibrary(resources);
    FreeLibrary(controls);
    std::printf(
        "8 original hidden gravity mirrors: real EDIT notifications and formatted values\n");
}
unsigned MorphScaleCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch curve(original.Function<void*>(0x62bb0), &ret, 1);
    Patch seek(original.Function<void*>(0xcbd90), &ret, 1);
    Patch controls(original.Function<void*>(0xcd750), &ret, 1);
    auto child = &ResetChild;
    auto text = &RangeText;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child);
    Patch getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    struct Input {
        const char* scaleText;
        const char* addText;
        float scale, add;
    };
    const Input inputs[]{{"1.0", "0.0", 1, 0},
                         {"0", "0", 0, 0},
                         {"-2suffix", ".5suffix", -2, .5f},
                         {"1.25", "-.125", 1.25f, -.125f},
                         {"nonnumeric", "1.5", 0, 1.5f},
                         {"1.00000000000000000e9", "                  9.5", 1, 9}};
    unsigned cases = 0;
    for (const char* file : {"facial.pmd", "articulated.pmx"})
        for (const auto& input : inputs) {
            EditorSession portable;
            std::string reason;
            Check(MorphScaleFixture(portable, root, file, reason), reason.c_str());
            const auto before = portable.Document().models[0];
            NativeAuxiliary native(before,
                                   portable.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx);
            const auto put = [&](unsigned i, const auto& key) {
                auto& out = native.morphs.at(i);
                out.frame = key.frame;
                out.previous = key.previous;
                out.next = key.next;
                out.value = key.value;
                out.allocated = key.allocated;
            };
            for (unsigned i = 0; i < before.morphKeys.dense.size(); ++i)
                put(i, before.morphKeys.dense[i]);
            for (const auto& key : before.morphKeys.sparse)
                put(unsigned(key.index), key.value);
            for (const auto& [i, key] : before.morphKeys.unused)
                put(i, key);
            auto* model = native.model.get();
            std::fill(app.begin(), app.end(), 0);
            std::memcpy(app.data() + 0xbe8, &model, sizeof model);
            rangeFields = {{686, input.scaleText}, {687, input.addText}};
            original.Function<void (*)(HWND)>(0x9d120)(nullptr);
            Check(portable.ScaleMorphKeys(input.scale, input.add, reason), reason.c_str());
            const auto& track = portable.Document().models[0].morphKeys;
            std::map<unsigned, PmmMorphKey> expected(track.unused.begin(), track.unused.end());
            for (unsigned i = 0; i < track.dense.size(); ++i)
                expected[i] = track.dense[i];
            for (const auto& key : track.sparse)
                expected[unsigned(key.index)] = key.value;
            for (unsigned i = 0; i < native.morphs.size(); ++i) {
                const auto& a = native.morphs[i];
                const auto b = expected.count(i) ? expected.at(i) : PmmMorphKey{};
                Check(a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
                          a.allocated == b.allocated &&
                          !std::memcmp(&a.value, &b.value, sizeof(float)),
                      "native morph-scale full pool differs");
            }
            Check(app[0xa1b31] && model->UndoCursor() == 29 && model->LatestUndoCursor() == 29,
                  "native morph-scale dirty/history contract");
            ++cases;
        }
    return cases;
}
bool purgeConfirm;
unsigned purgePanel;
int WINAPI PurgeMessage(HWND, LPCSTR text, LPCSTR title, UINT flags) {
    const auto wanted = std::string("Trying to delete all ") +
                        (purgePanel == 3   ? "lip"
                         : purgePanel == 2 ? "eyes"
                                           : "eyebrow") +
                        " frame.\nYou cannot undo this oparation.\n\nAre you OK?";
    Check(text == wanted && std::strcmp(title, "delete lip frame") == 0 && flags == 0x40001,
          "original purge confirmation text and flags");
    return purgeConfirm ? IDOK : IDCANCEL;
}
INT_PTR WINAPI ShiftDialog(HINSTANCE, LPCSTR resource, HWND, DLGPROC, LPARAM parameter) {
    Check(reinterpret_cast<std::uintptr_t>(resource) == 655 && parameter == 0,
          "original mouth-shift dialog resource");
    return purgeConfirm ? IDOK : IDCANCEL;
}
unsigned MorphPurgeCommands(OriginalPhysicsModule& original, const std::filesystem::path& root,
                            bool shifting = false) {
    if (shifting) {
        const auto parse = *original.Function<int (**)(const char*)>(0x128478);
        for (const auto& [text, value] : std::vector<std::pair<const char*, int>>{
                 {" -15tail", -15}, {"+7.5", 7}, {"nonsense", 0}, {"--1", 0},
                 {"2147483648", INT_MAX}, {"-2147483649", INT_MIN}})
            Check(parse(text) == value, "original frame-shift integer prefix/overflow");
    }
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch curve(original.Function<void*>(0x62bb0), &ret, 1);
    const auto seekByte = *original.Function<unsigned char*>(0xcbd90);
    Patch seek(original.Function<void*>(0xcbd90), &ret, 1);
    auto message = &PurgeMessage;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    auto dialog = &ShiftDialog;
    Patch modal(original.Function<void*>(0x128600), &dialog, sizeof dialog);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (const auto* file : {"morph-panels.pmd", "morph-panels.pmx"})
        for (bool reverse : {false, true})
            for (unsigned length : {0u, 1u, 2u})
                for (unsigned panel :
                     shifting ? std::vector<unsigned>{3} : std::vector<unsigned>{1, 2, 3})
                    for (bool accept : {false, true})
                        for (auto shift :
                             shifting ? std::vector<std::int32_t>{INT_MIN, -21, -20, -15, -10, -1,
                                                                  0, 1, INT_MAX - 15, INT_MAX}
                                      : std::vector<std::int32_t>{0}) {
                            EditorSession portable;
                            std::string reason;
                            Check(MorphPurgeFixture(portable, root, file, reverse, length, reason),
                                  reason.c_str());
                            const auto before = portable.Document().models[0];
                            NativeAuxiliary native(before,
                                                   portable.Runtime().Resource(0)->SourceKind() ==
                                                       ModelKind::Pmx);
                            const auto put = [&](unsigned i, const auto& key) {
                                auto& out = native.morphs.at(i);
                                out.frame = key.frame;
                                out.previous = key.previous;
                                out.next = key.next;
                                out.value = key.value;
                                out.allocated = key.allocated;
                            };
                            for (unsigned i = 0; i < before.morphKeys.dense.size(); ++i) {
                                put(i, before.morphKeys.dense[i]);
                                int mapped;
                                Check(portable.Runtime().DocumentMorphIndex(0, int(i), mapped,
                                                                            reason),
                                      reason.c_str());
                                native.poses[i].panel =
                                    static_cast<mdl::MorphPanel>(portable.Runtime()
                                                                     .Resource(0)
                                                                     ->Document()
                                                                     .morphs[mapped]
                                                                     .header.panel);
                            }
                            for (const auto& key : before.morphKeys.sparse)
                                put(unsigned(key.index), key.value);
                            for (const auto& [i, key] : before.morphKeys.unused)
                                put(i, key);
                            auto* model = native.model.get();
                            std::fill(app.begin(), app.end(), 0);
                            std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                            app[0xa1b90] = 1;
                            if (shifting)
                                std::memcpy(app.data() + 0xa1918, &shift, sizeof shift);
                            auto expectedApp = app;
                            expectedApp[0xc0] = 1;
                            if (accept)
                                expectedApp[0xa1b31] = 1;
                            purgePanel = panel;
                            purgeConfirm = accept;
                            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                                nullptr, shifting     ? MDS_CMD_SHIFT_MOUTH_FRAMES
                                         : panel == 3 ? MDS_CMD_DELETE_MOUTH_FRAMES
                                         : panel == 2 ? MDS_CMD_DELETE_EYE_FRAMES
                                                      : MDS_CMD_DELETE_EYEBROW_FRAMES);
                            if (accept) {
                                const bool ok = shifting ? portable.ShiftMouthKeys(shift, reason)
                                                         : portable.PurgeMorphKeys(panel, reason);
                                if (!ok)
                                    std::fprintf(
                                        stderr,
                                        "morph edit failed %s reverse=%u length=%u shift=%d: %s\n",
                                        file, unsigned(reverse), length, shift, reason.c_str());
                                Check(ok, reason.c_str());
                            }
                            const auto& track = portable.Document().models[0].morphKeys;
                            std::map<unsigned, PmmMorphKey> expected(track.unused.begin(),
                                                                     track.unused.end());
                            for (unsigned i = 0; i < track.dense.size(); ++i)
                                expected[i] = track.dense[i];
                            for (const auto& key : track.sparse)
                                expected[unsigned(key.index)] = key.value;
                            for (unsigned i = 0; i < native.morphs.size(); ++i) {
                                const auto& a = native.morphs[i];
                                const auto b = expected.count(i) ? expected.at(i) : PmmMorphKey{};
                                if (a.frame != b.frame || a.previous != b.previous ||
                                    a.next != b.next || a.allocated != b.allocated ||
                                    std::memcmp(&a.value, &b.value, sizeof(float)))
                                    std::fprintf(
                                        stderr,
                                        "morph edit %s reverse=%u length=%u panel=%u accept=%u shift=%d slot=%u native=%u/%u/%u/%u/%g portable=%u/%u/%u/%u/%g\n",
                                        file, unsigned(reverse), length, panel, unsigned(accept),
                                        shift, i, a.frame, a.previous, a.next, a.allocated, a.value,
                                        b.frame, b.previous, b.next, b.allocated, b.value);
                                Check(a.frame == b.frame && a.previous == b.previous &&
                                          a.next == b.next && a.allocated == b.allocated &&
                                          !std::memcmp(&a.value, &b.value, sizeof(float)),
                                      "original morph purge full pool differs");
                            }
                            Check(app == expectedApp && model->UndoCursor() == 29 &&
                                      model->LatestUndoCursor() == 29,
                                  "original purge application/history side effects");
                            if (shifting && accept) {
                                Patch nativeSeek(original.Function<void*>(0xcbd90), &seekByte, 1);
                                std::vector<std::uint32_t> cursors(native.poses.size());
                                std::vector<unsigned char> active(native.poses.size());
                                model->morphKeyCursors = cursors.data();
                                model->morphTrackActive = active.data();
                                const auto playback = [&] {
                                    original.Function<void (*)(mdl::ModelRecord*, float, int)>(0xd0320)(model, 0, 0);
                                    original.Function<void (*)(mdl::ModelRecord*, float, int)>(0xd08c0)(model, .1f, 0);
                                    PmmScenePlaybackState state;
                                    Check(StartPmmScenePlayback(portable.Document(), state), "shifted playback start");
                                    Check(AdvancePmmScenePlayback(portable.Document(), .1f, state,
                                                                 {true, true, PmmPlaybackArithmetic::NativeFloat}),
                                          "shifted playback advance");
                                    for (unsigned i = 0; i < native.poses.size(); ++i)
                                        Check(!std::memcmp(&state.pose.models[0].morphValues[i], &native.poses[i].value, sizeof(float)),
                                              "original playback after mouth shift/reuse differs");
                                };
                                playback();
                                original.Function<int (*)(mdl::ModelRecord*, int, int)>(0xcbd90)(
                                    model, 2, 0);
                                const bool ok = portable.Seek(2, reason);
                                Check(ok, reason.c_str());
                                const auto& values = portable.Document().models[0].morphPose;
                                for (unsigned i = 0; i < values.size(); ++i)
                                    Check(!std::memcmp(&values[i], &native.poses[i].value,
                                                       sizeof(float)),
                                          "original seek after mouth shift differs");
                                unsigned channel = 0;
                                while (channel < native.poses.size() &&
                                       native.poses[channel].panel != mdl::MorphPanel::mouth)
                                    ++channel;
                                Check(channel < native.poses.size(), "mouth fixture channel");
                                Check(portable.Seek(30, reason), reason.c_str());
                                Check(portable.SetMorph(0, channel, .375f, reason), reason.c_str());
                                native.Register(original, portable.Document().models[0], Track::Morph, 30, channel);
                                const bool registered = portable.RegisterMorph(0, channel, reason);
                                Check(registered, reason.c_str());
                                native.Compare(portable.Document().models[0]);
                                original.Function<int (*)(mdl::ModelRecord*, int, int)>(0xcbd90)(model, 2, 0);
                                const bool sought = portable.Seek(2, reason);
                                Check(sought, reason.c_str());
                                const auto& reused = portable.Document().models[0].morphPose;
                                for (unsigned i = 0; i < reused.size(); ++i) {
                                    if (std::memcmp(&reused[i], &native.poses[i].value, sizeof(float)))
                                        std::fprintf(stderr, "reuse %s reverse=%u length=%u shift=%d channel=%u portable=%.9g native=%.9g\n",
                                                     file, unsigned(reverse), length, shift, i, reused[i], native.poses[i].value);
                                    Check(!std::memcmp(&reused[i], &native.poses[i].value, sizeof(float)),
                                          "original seek after freed-slot reuse differs");
                                }
                                playback();
                            }
                            ++cases;
                        }
    return cases;
}
const unsigned blinkSamples[]{32767, 0, 0, 16384, 1, 32766, 12000};
unsigned nativeBlinkCalls, blinkReported, blinkOverflow;
unsigned blinkDialogs, blinkMissing;
bool blinkAccept = true;
int NativeBlinkRandom() {
    return int(blinkSamples[nativeBlinkCalls++ % std::size(blinkSamples)]);
}
INT_PTR WINAPI BlinkDialog(HINSTANCE, LPCSTR resource, HWND, DLGPROC, LPARAM parameter) {
    Check(reinterpret_cast<std::uintptr_t>(resource) == 656 && !parameter,
          "native blink dialog resource");
    ++blinkDialogs;
    return blinkAccept ? IDOK : IDCANCEL;
}
int WINAPI BlinkReport(HWND, LPCSTR text, LPCSTR title, UINT flags) {
    if (!std::strcmp(title, "randomly register blinking")) {
        Check(
            flags == MB_TOPMOST &&
                !std::strcmp(
                    text,
                    "You cannot register blinking because this model has not facial motion of 'blink'."),
            "native missing-blink notification");
        ++blinkMissing;
        return IDOK;
    }
    if (!std::strcmp(title, "register frame")) {
        Check(!flags && std::strstr(text, "20000"), "native blink pool overflow");
        ++blinkOverflow;
        return IDOK;
    }
    Check(std::strcmp(title, "register blinking") == 0 && flags == MB_TOPMOST,
          "native blink report title/flags");
    Check(std::sscanf(text, "%u blinking is registerd", &blinkReported) == 1,
          "native blink report count");
    char expected[100];
    std::snprintf(expected, sizeof expected, "%u blinking is registerd", blinkReported);
    Check(!std::strcmp(text, expected), "native complete blink report text");
    return IDOK;
}
unsigned BlinkCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch curve(original.Function<void*>(0x62bb0), &ret, 1);
    Patch seek(original.Function<void*>(0xcbd90), &ret, 1);
    auto message = &BlinkReport;
    auto dialog = &BlinkDialog;
    auto random = &NativeBlinkRandom;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    Patch modal(original.Function<void*>(0x128600), &dialog, sizeof dialog);
    Patch rng(original.Function<void*>(0x128440), &random, sizeof random);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    blinkAccept = true;
    for (const auto* file : {"blink.pmd", "blink.pmx"})
        for (const auto& range :
             std::vector<std::pair<int, int>>{{0, 0}, {0, 1}, {10, 500}, {500, 10}, {-1, 50}})
            for (int free : {-1, 0, 2}) {
                if (free >= 0 && range != std::pair{0, 1})
                    continue;
                EditorSession portable;
                std::string reason;
                Check(portable.ImportModel(root / file, reason), reason.c_str());
                Check(portable.HasBlinkMorph(), "public fixture must have a blink morph");
                if (free >= 0)
                    Check(portable.Edit(
                              "blink-capacity",
                              [&](auto& d, auto&) {
                                  FillBlinkCapacity(d, unsigned(free));
                                  return true;
                              },
                              reason),
                          reason.c_str());
                const auto before = portable.Document().models[0];
                NativeAuxiliary native(before, portable.Runtime().Resource(0)->SourceKind() ==
                                                   ModelKind::Pmx);
                auto* model = native.model.get();
                model->physicsFlags = 1;
                model->maxFrame = before.selection.maxFrame;
                for (const auto& key : before.morphKeys.sparse) {
                    auto& out = native.morphs[unsigned(key.index)];
                    out.frame = key.value.frame;
                    out.previous = key.value.previous;
                    out.next = key.value.next;
                    out.value = key.value.value;
                    out.allocated = key.value.allocated;
                }
                for (unsigned i = 0; i < before.morphKeys.dense.size(); ++i)
                    native.morphs[i].next = before.morphKeys.dense[i].next;
                for (unsigned i = 0; i < native.poses.size(); ++i)
                    std::memcpy(native.poses[i].name, before.morphNames[i].data(),
                                before.morphNames[i].size());
                std::memset(app.get(), 0, sizeof(*app));
                app->modelSlots[0] = model;
                app->englishUI = 1;
                auto* bytes = reinterpret_cast<unsigned char*>(app.get());
                const int start = range.first, end = range.second;
                std::memcpy(bytes + 0xa191c, &start, sizeof start);
                std::memcpy(bytes + 0xa1920, &end, sizeof end);
                // Repeat without resetting native/portable phase or the key pools.
                for (int repeat = 0; repeat < 2; ++repeat) {
                    nativeBlinkCalls = 0;
                    blinkOverflow = 0;
                    blinkReported = UINT_MAX;
                    original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                       MDS_CMD_REGISTER_BLINKING);
                    unsigned portableCalls = 0, count = UINT_MAX, failed = UINT_MAX;
                    const bool ok = portable.RegisterBlinks(
                        start, end, count, reason,
                        [&] { return blinkSamples[portableCalls++ % std::size(blinkSamples)]; },
                        &failed);
                    Check(ok, reason.c_str());
                    Check(count == blinkReported && nativeBlinkCalls == portableCalls &&
                              failed == blinkOverflow,
                          "native blink count/random consumption differs");
                    native.Compare(portable.Document().models[0]);
                    Check(portable.Document().timeline.lastRegisteredFrame ==
                                  app->lastRegisteredFrame &&
                              app->sceneModified,
                          "native blink dirty/watermark differs");
                    ++cases;
                }
            }
    blinkAccept = false;
    for (bool pmx : {false, true})
        for (bool missing : {false, true}) {
            EditorSession portable;
            std::string reason;
            const auto file = missing ? (pmx ? "tint-morph.pmx" : "facial.pmd")
                                      : (pmx ? "blink.pmx" : "blink.pmd");
            Check(portable.ImportModel(root / file, reason), reason.c_str());
            const auto before = portable.Document().models[0];
            NativeAuxiliary native(before, pmx);
            auto* model = native.model.get();
            for (unsigned i = 0; i < native.poses.size(); ++i)
                std::memcpy(native.poses[i].name, before.morphNames[i].data(),
                            before.morphNames[i].size());
            const auto modelBefore = std::make_unique<mdl::ModelRecord>(*model);
            std::memset(app.get(), 0, sizeof(*app));
            app->modelSlots[0] = model;
            app->englishUI = 1;
            app->sceneModified = 0x7d;
            app->physicsResetPending = 1;
            auto expected = std::make_unique<MMDAppState>(*app);
            const std::uint32_t enter = 1;
            std::memcpy(reinterpret_cast<unsigned char*>(expected.get()) + 0xc0, &enter, sizeof enter);
            blinkDialogs = blinkMissing = nativeBlinkCalls = 0;
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, MDS_CMD_REGISTER_BLINKING);
            if (blinkDialogs != unsigned(!missing) || blinkMissing != unsigned(missing) ||
                nativeBlinkCalls || std::memcmp(app.get(), expected.get(), sizeof(*app)) ||
                std::memcmp(model, modelBefore.get(), sizeof(*model))) {
                std::fprintf(stderr, "blink cancel %s missing=%u dialogs=%u messages=%u random=%u\n",
                             file, unsigned(missing), blinkDialogs, blinkMissing, nativeBlinkCalls);
                const auto differences = [](const void* a, const void* b, std::size_t size) {
                    const auto* x = static_cast<const unsigned char*>(a);
                    const auto* y = static_cast<const unsigned char*>(b);
                    unsigned count = 0;
                    for (std::size_t i = 0; i < size && count < 20; ++i)
                        if (x[i] != y[i]) {
                            std::fprintf(stderr, "byte %zx actual=%u expected=%u\n", i,
                                         unsigned(x[i]), unsigned(y[i]));
                            ++count;
                        }
                };
                differences(app.get(), expected.get(), sizeof(*app));
                differences(model, modelBefore.get(), sizeof(*model));
            }
            Check(blinkDialogs == unsigned(!missing) && blinkMissing == unsigned(missing) &&
                      !nativeBlinkCalls && !std::memcmp(app.get(), expected.get(), sizeof(*app)) &&
                      !std::memcmp(model, modelBefore.get(), sizeof(*model)),
                  "native cancelled/missing blink command state");
            native.Compare(before);
            ++cases;
        }
    blinkAccept = true;
    return cases;
}
unsigned MorphCapacity(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    auto message = &BlinkReport;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    unsigned cases = 0;
    for (const char* file : {"facial.pmd", "tint-morph.pmx"})
        for (unsigned free : {0u, 1u, 2u})
            for (unsigned frame : {0u, 10u, 1005u, 22000u})
                for (bool all : {false, true}) {
                    EditorSession portable;
                    std::string reason;
                    Check(portable.ImportModel(root / file, reason), reason.c_str());
                    Check(portable.Edit(
                              "morph capacity",
                              [&](auto& d, auto&) {
                                  FillBlinkCapacity(d, free);
                                  d.timeline.currentFrame = int(frame);
                                  for (auto& value : d.models[0].morphPose)
                                      value = .625f;
                                  return true;
                              },
                              reason),
                          reason.c_str());
                    const auto before = portable.Document().models[0];
                    const bool pmx = portable.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx;
                    NativeAuxiliary native(before, pmx);
                    native.model->physicsFlags = 1;
                    native.model->maxFrame = before.selection.maxFrame;
                    for (auto& key : native.morphs)
                        key.allocated = 0;
                    for (auto& key : native.displays)
                        key.allocated = 0;
                    for (unsigned channel = 0; channel < native.poses.size(); ++channel)
                        native.poses[channel].value = before.morphPose[channel];
                    blinkOverflow = 0;
                    const unsigned first = pmx ? 0 : 1;
                    const unsigned end = all ? unsigned(native.poses.size()) : first + 1;
                    for (unsigned channel = first; channel < end; ++channel)
                        original.Function<void (*)(mdl::ModelRecord*, int, int)>(0xca9c0)(
                            native.model.get(), int(channel), int(frame));
                    unsigned failed = UINT_MAX;
                    Check(all ? portable.RegisterAllMorphs(0, reason, &failed)
                              : portable.RegisterMorph(0, first, reason, &failed),
                          reason.c_str());
                    Check(failed == blinkOverflow, "native regular Morph overflow count");
                    native.Compare(portable.Document().models[0]);
                    Check(portable.Dirty(), "full Morph registration still marks modified");
                    ++cases;
                }
    return cases;
}
unsigned capacityMessages, advertisedCapacity;
bool modelCapacity;
int WINAPI CapacityMessage(HWND, LPCSTR text, LPCSTR title, UINT flags) {
    char expected[200];
    std::snprintf(expected, sizeof expected,
                  modelCapacity
                      ? "You cannot regist over %u point\nPlease execute 'delete unused frame'"
                      : "You cannot regist over %upoint.\nPlease execute 'delete unused frame'",
                  advertisedCapacity);
    Check(!flags && !std::strcmp(title, "register frame") && !std::strcmp(text, expected),
          "native capacity message bytes");
    ++capacityMessages;
    return IDOK;
}
unsigned DisplayCapacity(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    auto message = &CapacityMessage;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
    unsigned cases = 0;
    advertisedCapacity = 1000;
    modelCapacity = true;
    for (const char* file : {"articulated.pmd", "articulated.pmx"})
        for (unsigned free : {0u, 1u})
            for (unsigned frame : {0u, 2u, 3u, 21000u}) {
                EditorSession portable;
                std::string reason;
                Check(portable.ImportModel(root / file, reason), reason.c_str());
                Check(portable.Edit(
                          "display capacity",
                          [&](auto& d, auto&) {
                              FillKeyCapacity(d.models[0].displayKeys, free);
                              d.timeline.currentFrame = int(frame);
                              d.models[0].selection.loadComplete = 0;
                              for (auto& enabled : d.models[0].ikPose)
                                  enabled = 0;
                              return true;
                          },
                          reason),
                      reason.c_str());
                const auto before = portable.Document();
                const auto& source = before.models[0];
                NativeAuxiliary native(source, portable.Runtime().Resource(0)->SourceKind() ==
                                                   ModelKind::Pmx);
                const auto copy = [&](unsigned index, const auto& key) {
                    auto& out = native.displays[index];
                    out.frame = key.frame;
                    out.previous = key.previous;
                    out.next = key.next;
                    out.visible = key.visible;
                    out.allocated = key.allocated;
                    native.ik[index] = key.ikEnabled;
                    out.ikStates = native.ik[index].data();
                };
                copy(0, source.displayKeys.dense[0]);
                for (const auto& key : source.displayKeys.sparse)
                    copy(unsigned(key.index), key.value);
                native.model->maxFrame = source.selection.maxFrame;
                native.model->loadComplete = source.selection.loadComplete;
                native.model->physicsFlags = 1;
                native.model->boneKeys = bones.data();
                for (auto& key : bones)
                    key.allocated = 1;
                for (unsigned i = 0; i < native.chains.size(); ++i)
                    native.chains[i].enabled = source.ikPose[i];
                std::memset(app.get(), 0, sizeof(*app));
                app->modelSlots[0] = native.model.get();
                app->currentFrame = int(frame);
                app->lastRegisteredFrame = before.timeline.lastRegisteredFrame;
                capacityMessages = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                    nullptr, panel::kRegisterModelDisplayButton);
                unsigned failed = UINT_MAX;
                const auto frames = portable.Runtime().Counters().frames;
                const bool undo = portable.CanUndo(), redo = portable.CanRedo();
                Check(portable.Register({{Track::Display, 0, 0, frame}}, reason, &failed),
                      reason.c_str());
                Check(failed == capacityMessages && app->sceneModified == 1 && portable.Dirty() &&
                          app->lastRegisteredFrame ==
                              portable.Document().timeline.lastRegisteredFrame &&
                          portable.Runtime().Counters().frames == frames &&
                          portable.CanUndo() == undo && portable.CanRedo() == redo,
                      "native full display command side effects");
                Check(std::all_of(bones.begin(), bones.end(),
                                  [](const auto& k) { return !k.allocated; }),
                      "native full display clears complete bone pool");
                native.Compare(portable.Document().models[0]);
                ++cases;
            }
    return cases;
}
unsigned GlobalCapacity(OriginalPhysicsModule& original) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    Patch timeline(original.Function<void*>(0x5f7f0), &ret, 1);
    auto message = &CapacityMessage;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::vector<mdl::CameraKey> camera(10000);
    std::vector<mdl::LightKey> light(10000);
    std::vector<mdl::SelfShadowKey> shadow(10000);
    std::vector<mdl::GravityKey> gravity(10000);
    std::vector<mdl::AccessoryKey> accessories(10000);
    unsigned cases = 0;
    modelCapacity = false;
    for (auto kind : {Track::Camera, Track::Light})
        for (unsigned free : {0u, 1u})
            for (unsigned frame : {0u, 2u, 3u, 21000u}) {
                EditorSession portable;
                std::string reason;
                Check(portable.Edit(
                          "global capacity",
                          [&](auto& d, auto&) {
                              if (kind == Track::Camera)
                                  FillKeyCapacity(d.cameraKeys, free);
                              else
                                  FillKeyCapacity(d.lightKeys, free);
                              d.timeline.currentFrame = int(frame);
                              return true;
                          },
                          reason),
                      reason.c_str());
                const auto before = portable.Document();
                std::memset(app.get(), 0, sizeof(*app));
                app->cameraKeyTrack = camera.data();
                app->lightKeyTrack = light.data();
                app->selfShadowKeyTrack = shadow.data();
                app->gravityKeyTrack = gravity.data();
                for (auto& keys : app->accKeyTracks)
                    keys = accessories.data();
                const auto marks = [](auto& pool) {
                    for (auto& key : pool) {
                        key = {};
                        key.selected = 1;
                    }
                };
                marks(camera);
                marks(light);
                marks(shadow);
                marks(gravity);
                marks(accessories);
                const auto cameraPut = [&](unsigned i, const auto& key) {
                    auto& out = camera[i];
                    out.frame = key.frame;
                    out.previous = key.previous;
                    out.next = key.next;
                    out.distance = key.distance;
                    out.fov = int(key.fov);
                    out.parentModel = key.parentModel;
                    out.parentBone = key.parentBone;
                    out.perspective = key.perspective;
                    out.selected = key.selected;
                    std::copy(key.eye.begin(), key.eye.end(), out.eye);
                    std::copy(key.target.begin(), key.target.end(), out.target);
                    std::memcpy(out.interpolation, key.interpolation.data(), 24);
                };
                const auto lightPut = [&](unsigned i, const auto& key) {
                    auto& out = light[i];
                    out.frame = key.frame;
                    out.previous = key.previous;
                    out.next = key.next;
                    out.selected = key.selected;
                    std::copy(key.color.begin(), key.color.end(), out.color);
                    std::copy(key.direction.begin(), key.direction.end(), out.direction);
                };
                cameraPut(0, before.cameraKeys.dense[0]);
                lightPut(0, before.lightKeys.dense[0]);
                for (const auto& key : before.cameraKeys.sparse)
                    cameraPut(unsigned(key.index), key.value);
                for (const auto& key : before.lightKeys.sparse)
                    lightPut(unsigned(key.index), key.value);
                std::copy_n(before.cameraState.begin(), 3, app->cameraPosition);
                app->cameraPitch = before.cameraState[6];
                app->cameraYaw = before.cameraState[7];
                app->cameraRoll = before.cameraState[8];
                app->cameraDistance = before.cameraState[5];
                app->cameraFov = float(before.envelope.settings.cameraFov);
                app->cameraPerspective = before.cameraPerspective;
                app->cameraParentModel = before.appearance.cameraParentModel;
                app->cameraParentBone = before.appearance.cameraParentBone;
                std::copy_n(before.lightState.begin(), 3, app->reservedLightColor);
                std::copy_n(before.lightState.begin() + 3, 3, app->reservedLightDirection);
                app->currentFrame = int(frame);
                app->lastRegisteredFrame = before.timeline.lastRegisteredFrame;
                reinterpret_cast<unsigned char*>(app.get())[0xa1b90] = 1;
                capacityMessages = 0;
                advertisedCapacity = kind == Track::Camera ? 600000 : 10000;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(
                    nullptr, kind == Track::Camera ? panel::kRegisterCameraButton
                                                   : panel::kRegisterLightButton);
                unsigned failed = UINT_MAX;
                Check(portable.Register({{kind, 255, 0, frame}}, reason, &failed), reason.c_str());
                Check(failed == capacityMessages && app->sceneModified == 1 && portable.Dirty() &&
                          app->lastRegisteredFrame ==
                              portable.Document().timeline.lastRegisteredFrame,
                      "native full global command side effects");
                const auto check = [&](const auto& pool, const auto& track, const auto& convert) {
                    using T = typename std::decay_t<decltype(track.dense)>::value_type;
                    std::map<unsigned, const T*> keys{{0, &track.dense[0]}};
                    for (const auto& key : track.sparse)
                        keys.emplace(unsigned(key.index), &key.value);
                    // Reuse the field converter to require exact native bytes,
                    // including the reserved fields that registration preserves.
                    for (unsigned i = 0; i < pool.size(); ++i) {
                        const bool alive = i == 0 || pool[i].frame != 0;
                        const auto found = keys.find(i);
                        Check(alive == (found != keys.end()), "native global occupied slots");
                        if (alive) {
                            const auto saved = pool[i];
                            convert(i, *found->second);
                            Check(!std::memcmp(&saved, &pool[i], sizeof saved),
                                  "native global key payload");
                        } else
                            Check(!pool[i].selected, "native unoccupied global marks cleared");
                    }
                };
                if (kind == Track::Camera)
                    check(camera, portable.Document().cameraKeys, cameraPut);
                else
                    check(light, portable.Document().lightKeys, lightPut);
                Check(std::all_of(accessories.begin(), accessories.end(),
                                  [](const auto& k) { return !k.selected; }),
                      "native global command clears every accessory pool");
                ++cases;
            }
    return cases;
}
void BoneCapacity(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    auto message = &CapacityMessage;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    EditorSession portable;
    std::string reason;
    Check(portable.ImportModel(root / "articulated.pmx", reason), reason.c_str());
    Check(portable.Edit(
              "bone capacity",
              [&](auto& d, auto&) {
                  FillKeyCapacity(d.models[0].boneKeys, 0);
                  d.timeline.currentFrame = 3;
                  for (auto& pose : d.models[0].bonePose)
                      pose.flags[1] = 1;
                  return true;
              },
              reason),
          reason.c_str());
    const auto& source = portable.Document().models[0];
    const auto roots = unsigned(source.bonePose.size());
    auto native = std::make_unique<mdl::ModelRecord>();
    std::vector<mdl::BoneRecord> bones(roots);
    std::vector<mdl::BoneKey> records(mdl::kBoneKeyCapacity);
    std::vector<mdl::MorphKey> morphs(mdl::kMorphKeyCapacity);
    std::vector<mdl::DisplayKey> displays(mdl::kDisplayKeyCapacity);
    std::vector<unsigned char> edited(roots, 1), selected(roots, 1);
    native->boneCount = roots;
    native->boneTable = bones.data();
    native->boneKeys = records.data();
    native->morphKeys = morphs.data();
    native->displayKeys = displays.data();
    native->bonePhysicsState = edited.data();
    native->boneSelection = selected.data();
    native->UndoCursor() = native->LatestUndoCursor() = 29;
    native->maxFrame = source.selection.maxFrame;
    native->physicsFlags = 1;
    const auto put = [&](unsigned index, const auto& key) {
        auto& out = records[index];
        out.frame = key.frame;
        out.previous = key.previous;
        out.next = key.next;
        std::copy(key.position.begin(), key.position.end(), out.position);
        std::copy(key.rotation.begin(), key.rotation.end(), out.rotation);
        std::memcpy(out.interpolation, key.interpolation.data(), 16);
        out.allocated = key.allocated;
        out.physicsDisabled = key.physicsDisabled;
    };
    std::vector<KeyAddress> keys;
    for (unsigned i = 0; i < roots; ++i) {
        put(i, source.boneKeys.dense[i]);
        std::copy_n(source.bonePose[i].components.begin(), 3, bones[i].trans);
        std::copy_n(source.bonePose[i].components.begin() + 3, 4, bones[i].rotQuat);
        keys.push_back({Track::Bone, 0, i, 3});
    }
    for (const auto& key : source.boneKeys.sparse)
        put(unsigned(key.index), key.value);
    advertisedCapacity = unsigned(mdl::kBoneKeyCapacity);
    modelCapacity = true;
    capacityMessages = 0;
    original.Function<void (*)(mdl::ModelRecord*, int, int)>(0xc7ea0)(native.get(), 3, 0);
    unsigned failed = UINT_MAX;
    Check(portable.Register(keys, reason, &failed), reason.c_str());
    Check(failed == roots && capacityMessages == roots && portable.CanUndo() &&
              native->UndoCursor() == 0 && native->undoRings[0].slots[0].recordCount == 0,
          "native full bone command retains empty-capture undo entry");
    const auto& after = portable.Document().models[0];
    Check(native->maxFrame == after.selection.maxFrame &&
              std::all_of(edited.begin(), edited.end(), [](auto b) { return !b; }) &&
              std::all_of(after.bonePose.begin(), after.bonePose.end(),
                          [](const auto& b) { return !b.flags[1]; }),
          "native full bone edit markers and watermark");
    for (unsigned i = 0; i < records.size(); ++i) {
        const auto saved = records[i];
        put(i, i < roots ? after.boneKeys.dense[i] : after.boneKeys.sparse[i - roots].value);
        Check(!std::memcmp(&saved, &records[i], sizeof saved),
              "native full bone pool unchanged apart from marks");
    }
    Check(portable.Undo(reason) && portable.Redo(reason), "full bone empty-capture undo/redo");
    auto free = *original.Function<void (**)(void*)>(0x128450);
    for (auto& ring : native->undoRings)
        for (auto& slot : ring.slots) {
            if (slot.bonePose)
                free(slot.bonePose);
            if (slot.keySnapshots)
                free(slot.keySnapshots);
        }
    std::puts(
        "Original full 600000-slot bone command: failures, complete pool, markers and empty-capture history passed");
}
unsigned PasteCapacity(OriginalPhysicsModule& original) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    Patch reload(original.Function<void*>(0x59c30), &ret, 1);
    Patch seekCamera(original.Function<void*>(0x5ba90), &ret, 1);
    Patch seekLight(original.Function<void*>(0x5caf0), &ret, 1);
    Patch seekShadow(original.Function<void*>(0x5d320), &ret, 1);
    Patch syncAccessory(original.Function<void*>(0x5e510), &ret, 1);
    auto message = &CapacityMessage;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::vector<mdl::CameraKey> camera(10000);
    std::vector<mdl::LightKey> light(10000);
    std::vector<mdl::SelfShadowKey> shadow(10000);
    std::vector<mdl::GravityKey> gravity(10000);
    std::vector<mdl::AccessoryKey> accessories(10000);
    std::array<mdl::CameraClipboardRecord, 3> cameraClipboard{};
    std::array<mdl::LightClipboardRecord, 3> lightClipboard{};
    const auto defaults = NewPmmScene();
    for (unsigned i = 0; i < 3; ++i) {
        auto& c = cameraClipboard[i];
        const auto& key = defaults.cameraKeys.dense[0];
        c.frame = i;
        c.distance = key.distance;
        c.fov = key.fov;
        c.perspective = key.perspective;
        c.parentModel = key.parentModel;
        c.parentBone = key.parentBone;
        std::copy(key.eye.begin(), key.eye.end(), c.eye);
        std::copy(key.target.begin(), key.target.end(), c.target);
        std::memcpy(c.interpolation, key.interpolation.data(), 24);
        c.eye[0] = float(31 + i);
        auto& l = lightClipboard[i];
        const auto& value = defaults.lightKeys.dense[0];
        l.frame = i;
        std::copy(value.color.begin(), value.color.end(), l.color);
        std::copy(value.direction.begin(), value.direction.end(), l.direction);
        l.color[0] = float(i + 1) / 8;
    }
    const auto cameraPut = [&](unsigned i, const PmmCameraKey& key) {
        auto& out = camera[i];
        out.frame = key.frame;
        out.previous = key.previous;
        out.next = key.next;
        out.distance = key.distance;
        out.fov = key.fov;
        out.parentModel = key.parentModel;
        out.parentBone = key.parentBone;
        out.perspective = key.perspective;
        out.selected = key.selected;
        std::copy(key.eye.begin(), key.eye.end(), out.eye);
        std::copy(key.target.begin(), key.target.end(), out.target);
        std::memcpy(out.interpolation, key.interpolation.data(), 24);
    };
    const auto lightPut = [&](unsigned i, const PmmLightKey& key) {
        auto& out = light[i];
        out.frame = key.frame;
        out.previous = key.previous;
        out.next = key.next;
        out.selected = key.selected;
        std::copy(key.color.begin(), key.color.end(), out.color);
        std::copy(key.direction.begin(), key.direction.end(), out.direction);
    };
    unsigned cases = 0;
    std::uint64_t hash = 14695981039346656037ull;
    modelCapacity = false;
    advertisedCapacity = 10000;
    for (unsigned cameraFree : {0u, 1u, 2u})
        for (unsigned lightFree : {0u, 1u, 2u})
            for (unsigned frame : {0u, 3u, 21000u}) {
                EditorSession portable;
                std::string reason;
                Check(GlobalPasteCapacityFixture(portable, cameraFree, lightFree, frame, reason),
                      reason.c_str());
                const auto before = portable.Document();
                std::memset(app.get(), 0, sizeof(*app));
                const auto clear = [](auto& pool) {
                    for (auto& key : pool) {
                        key = {};
                        key.selected = 1;
                    }
                };
                clear(camera);
                clear(light);
                clear(shadow);
                clear(gravity);
                clear(accessories);
                app->cameraKeyTrack = camera.data();
                app->lightKeyTrack = light.data();
                app->selfShadowKeyTrack = shadow.data();
                app->gravityKeyTrack = gravity.data();
                for (auto& keys : app->accKeyTracks)
                    keys = accessories.data();
                cameraPut(0, before.cameraKeys.dense[0]);
                lightPut(0, before.lightKeys.dense[0]);
                for (const auto& key : before.cameraKeys.sparse)
                    cameraPut(unsigned(key.index), key.value);
                for (const auto& key : before.lightKeys.sparse)
                    lightPut(unsigned(key.index), key.value);
                auto* raw = reinterpret_cast<unsigned char*>(app.get());
                raw[0x328] = raw[0xa1b90] = 1;
                auto* c = cameraClipboard.data();
                auto* l = lightClipboard.data();
                std::memcpy(raw + 0x3a8, &c, sizeof c);
                std::memcpy(raw + 0x3b0, &l, sizeof l);
                const unsigned count = 3;
                std::memcpy(raw + 0x9e504, &count, 4);
                std::memcpy(raw + 0x9e508, &count, 4);
                app->currentFrame = int(frame);
                app->lastRegisteredFrame = before.timeline.lastRegisteredFrame;
                capacityMessages = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, panel::kPasteButton);
                std::vector<Track> rejected;
                Check(portable.Paste(frame, false, reason, &rejected), reason.c_str());
                Check(rejected.size() == capacityMessages && app->sceneModified == 1 &&
                          portable.Dirty() &&
                          app->lastRegisteredFrame ==
                              portable.Document().timeline.lastRegisteredFrame,
                      "native partial paste reports and watermarks");
                const auto compare = [&](const auto& pool, const auto& track, const auto& convert) {
                    using T = typename std::decay_t<decltype(track.dense)>::value_type;
                    std::map<unsigned, const T*> keys{{0, &track.dense[0]}};
                    for (const auto& key : track.sparse)
                        keys.emplace(unsigned(key.index), &key.value);
                    for (unsigned i = 0; i < pool.size(); ++i) {
                        const bool alive = i == 0 || pool[i].frame != 0;
                        const auto found = keys.find(i);
                        Check(alive == (found != keys.end()),
                              "native partial paste occupied slots");
                        if (alive) {
                            const auto saved = pool[i];
                            convert(i, *found->second);
                            Check(!std::memcmp(&saved, &pool[i], sizeof saved),
                                  "native partial paste complete record bytes");
                        } else
                            Check(!pool[i].selected, "native partial paste unused marks");
                    }
                };
                compare(camera, portable.Document().cameraKeys, cameraPut);
                compare(light, portable.Document().lightKeys, lightPut);
                Check(std::all_of(accessories.begin(), accessories.end(),
                                  [](const auto& k) { return !k.selected; }),
                      "native partial paste clears accessory marks");
                HashPasteCapacity(hash, portable.Document());
                ++cases;
            }
    std::printf("Native partial-paste frozen hash: %016llx\n",
                static_cast<unsigned long long>(hash));
    return cases;
}
std::wstring vsqInputPath;
unsigned vsqSeekCount;
int vsqSoughtFrame;
BOOL WINAPI SelectVsqFile(OPENFILENAMEW* options) {
    Check(vsqInputPath.size() < options->nMaxFile, "native VSQ file buffer");
    Check(std::wstring(options->lpstrInitialDir) == L"UserFile\\Vsq",
          "native VSQ initial directory");
    std::copy(vsqInputPath.begin(), vsqInputPath.end(), options->lpstrFile);
    options->lpstrFile[vsqInputPath.size()] = 0;
    return TRUE;
}
void ObserveVsqSeek(mdl::ModelRecord*, int frame, int) {
    ++vsqSeekCount;
    vsqSoughtFrame = frame;
}
unsigned VsqCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    auto open = &SelectVsqFile;
    auto directory = &IgnoreCurrentDirectory;
    auto child = &ResetChild;
    auto length = +[](HWND) -> int { return 0; };
    auto message = +[](HWND, UINT, WPARAM, LPARAM) -> LRESULT { return 0; };
    auto reject = &CapacityMessage;
    Patch a(original.Function<void*>(0x128090), &open, sizeof open);
    Patch b(original.Function<void*>(0x128278), &directory, sizeof directory);
    Patch c(original.Function<void*>(0x128658), &child, sizeof child);
    Patch d(original.Function<void*>(0x1286c8), &length, sizeof length);
    Patch e(original.Function<void*>(0x1287b8), &message, sizeof message);
    Patch f(original.Function<void*>(0x128618), &reject, sizeof reject);
    unsigned char jump[]{0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    auto seek = &ObserveVsqSeek;
    std::memcpy(jump + 2, &seek, sizeof seek);
    Patch g(original.Function<void*>(0xcbd90), jump, sizeof jump);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
    std::uint64_t hash = 14695981039346656037ull;
    unsigned cases = 0;
    for (const char* file : {"lips.pmd", "lips.pmx"})
        for (unsigned profile = 0; profile < VsqProfiles().size() + 2; ++profile) {
            const auto bytes = VsqProfiles()[profile < VsqProfiles().size() ? profile : 0];
            Check(WriteVsqFixture(root / "native-vsq.vsq", bytes), "write native VSQ fixture");
            Check(WriteVsqFixture(root / "portable-vsq.vsq", bytes), "write portable VSQ fixture");
            EditorSession portable;
            std::string reason;
            Check(portable.ImportModel(root / file, reason), reason.c_str());
            if (profile >= VsqProfiles().size())
                Check(portable.Edit(
                          "vsq-capacity",
                          [&](auto& document, auto&) {
                              FillKeyCapacity(document.models[0].morphKeys,
                                              (profile - unsigned(VsqProfiles().size())) * 2);
                              return true;
                          },
                          reason),
                      reason.c_str());
            const auto& source = portable.Document().models[0];
            NativeAuxiliary native(source, std::string(file) == "lips.pmx");
            native.model->physicsFlags = 1;
            native.model->boneKeys = bones.data();
            native.model->maxFrame = source.selection.maxFrame;
            for (auto& key : bones)
                key.allocated = 1;
            for (unsigned i = 0; i < native.poses.size(); ++i)
                std::copy(source.morphNames[i].begin(), source.morphNames[i].end(),
                          native.poses[i].name);
            std::memset(app.get(), 0, sizeof(*app));
            app->modelSlots[0] = native.model.get();
            reinterpret_cast<unsigned char*>(app.get())[0xa1b90] = 1;
            vsqInputPath = (root / "native-vsq.vsq").wstring();
            unexpectedFileUi = vsqSeekCount = 0;
            capacityMessages = 0;
            advertisedCapacity = 20000;
            modelCapacity = true;
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, MDS_CMD_LOAD_LIP_SYNC);
            Check(!unexpectedFileUi && vsqSeekCount == 1 && app->sceneModified == 1,
                  "native complete VSQ command callbacks");
            unsigned failed = 0;
            Check(portable.ImportVsq(root / "portable-vsq.vsq", reason, &failed), reason.c_str());
            Check(failed == capacityMessages, "native VSQ partial-capacity notices");
            const auto& actual = portable.Document();
            Check(app->currentFrame == actual.timeline.currentFrame &&
                      app->timelineStartFrame == actual.timeline.startFrame &&
                      app->lastRegisteredFrame == actual.timeline.lastRegisteredFrame &&
                      vsqSoughtFrame == actual.timeline.currentFrame,
                  "native VSQ command tail");
            native.Compare(actual.models[0]);
            for (const auto& key : bones)
                Check(!key.allocated, "native VSQ clears entire bone marks");
            const auto read = [](const auto& path) {
                std::ifstream stream(path, std::ios::binary);
                Check(bool(stream), "VSQ report exists");
                return std::string(std::istreambuf_iterator<char>(stream), {});
            };
            const auto nativeReport = read(root / "native-vsq.txt");
            const auto portableReport = read(root / "portable-vsq.txt");
            if (nativeReport != portableReport)
                std::fprintf(stderr, "native report:\n%sportable report:\n%s", nativeReport.c_str(),
                             portableReport.c_str());
            Check(nativeReport == portableReport, "native VSQ report bytes");
            frame_line_fixture::Hash(hash, actual.models[0].morphKeys);
            ++cases;
        }
    std::printf("Native VSQ frozen model hash: %016llx\n", static_cast<unsigned long long>(hash));
    for (const char* file : {"triangle.pmd", "triangle.pmx"}) {
        EditorSession portable;
        std::string reason;
        Check(portable.ImportModel(root / file, reason), reason.c_str());
        NativeAuxiliary native(portable.Document().models[0], std::string(file) == "triangle.pmx");
        const auto missing = +[](HWND, LPCSTR text, LPCSTR title, UINT flags) -> int {
            Check(std::string(text) ==
                          "Cannot Lipsync because this model has not facial data of 'a'!!" &&
                      std::string(title) == "lipsync" && flags == 0,
                  "native VSQ missing-mouth notice");
            ++unexpectedFileUi;
            return IDOK;
        };
        Patch missingPrompt(original.Function<void*>(0x128618), &missing, sizeof missing);
        std::memset(app.get(), 0, sizeof(*app));
        app->modelSlots[0] = native.model.get();
        reinterpret_cast<unsigned char*>(app.get())[0xa1b90] = 1;
        unexpectedFileUi = vsqSeekCount = 0;
        original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, MDS_CMD_LOAD_LIP_SYNC);
        Check(unexpectedFileUi == 1 && !vsqSeekCount && app->sceneModified == 1,
              "native missing-mouth tail");
        const auto revision = portable.Revision();
        Check(!portable.ImportVsq(root / "portable-vsq.vsq", reason) && reason == "vsq-missing-a" &&
                  portable.Revision() != revision && portable.Dirty(),
              "portable missing-mouth command");
        native.Compare(portable.Document().models[0]);
        ++cases;
    }
    return cases;
}
unsigned ModelPasteCapacity(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    Patch controls(original.Function<void*>(0x62bb0), &ret, 1);
    Patch seek(original.Function<void*>(0xcbd90), &ret, 1);
    auto message = &CapacityMessage;
    Patch prompt(original.Function<void*>(0x128618), &message, sizeof message);
    auto app = std::make_unique<MMDAppState>();
    auto* pointer = app.get();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    std::vector<mdl::BoneKey> bones(mdl::kBoneKeyCapacity);
    unsigned cases = 0;
    std::uint64_t hash = 14695981039346656037ull;
    // Start with at least one free slot: the original Reset*Cursor points one
    // past the table when initially full, and the registrar then reads it.
    // Mid-loop exhaustion exercises the defined native path without that OOB.
    for (unsigned morphFree : {1u, 2u})
        for (unsigned displayFree : {1u, 2u}) {
            EditorSession portable;
            std::string reason;
            Check(ModelPasteCapacityFixture(portable, root, morphFree, displayFree, reason),
                  reason.c_str());
            const auto& source = portable.Document().models[0];
            NativeAuxiliary native(source, true);
            Check(native.chains.empty(), "model partial-paste fixture needs no IK");
            native.model->physicsFlags = 1;
            native.model->boneKeys = bones.data();
            native.model->maxFrame = source.selection.maxFrame;
            for (auto& key : bones)
                key.allocated = 1;
            for (unsigned i = 0; i < native.poses.size(); ++i) {
                Check(source.morphNames[i].size() < sizeof(native.poses[i].name),
                      "native Morph name capacity");
                std::copy(source.morphNames[i].begin(), source.morphNames[i].end(),
                          native.poses[i].name);
            }
            const auto copy = [&](unsigned i, const auto& key) {
                auto& out = native.displays[i];
                out.frame = key.frame;
                out.previous = key.previous;
                out.next = key.next;
                out.visible = key.visible;
                out.allocated = key.allocated;
            };
            copy(0, source.displayKeys.dense[0]);
            for (const auto& key : source.displayKeys.sparse)
                copy(unsigned(key.index), key.value);
            std::array<mdl::MorphClipboardRecord, 3> morphs{};
            std::array<mdl::DisplayClipboardRecord, 3> displays{};
            for (unsigned i = 0; i < 3; ++i) {
                std::copy(source.morphNames[0].begin(), source.morphNames[0].end(), morphs[i].name);
                morphs[i].frame = displays[i].frame = i;
                morphs[i].value = float(i + 1) / 8;
                displays[i].visible = std::uint8_t(i % 2);
            }
            std::memset(app.get(), 0, sizeof(*app));
            app->modelSlots[0] = native.model.get();
            app->currentFrame = 3;
            app->lastRegisteredFrame = portable.Document().timeline.lastRegisteredFrame;
            auto* raw = reinterpret_cast<unsigned char*>(app.get());
            auto* m = morphs.data();
            auto* d = displays.data();
            std::memcpy(raw + 0x398, &m, sizeof m);
            std::memcpy(raw + 0x3a0, &d, sizeof d);
            const unsigned count = 3;
            std::memcpy(raw + 0x9e4fc, &count, 4);
            std::memcpy(raw + 0x9e500, &count, 4);
            capacityMessages = 0;
            // Both notices use model wording but different record limits.
            // Run once with a collector which recognizes each exact text.
            const auto observer = +[](HWND owner, LPCSTR text, LPCSTR title, UINT flags) -> int {
                advertisedCapacity = std::strstr(text, "20000") ? 20000 : 1000;
                modelCapacity = true;
                return CapacityMessage(owner, text, title, flags);
            };
            Patch modelPrompt(original.Function<void*>(0x128618), &observer, sizeof observer);
            original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr, panel::kPasteButton);
            std::vector<Track> rejected;
            const auto frames = portable.Runtime().Counters().frames;
            const bool undo = portable.CanUndo(), redo = portable.CanRedo();
            Check(portable.Paste(3, false, reason, &rejected), reason.c_str());
            if (!(rejected.size() == capacityMessages && app->sceneModified == 1 &&
                  app->lastRegisteredFrame == portable.Document().timeline.lastRegisteredFrame &&
                  portable.Runtime().Counters().frames == frames && portable.CanUndo() == undo &&
                  portable.CanRedo() == redo))
                std::fprintf(
                    stderr,
                    "partial model free=%u/%u reports=%zu/%u dirty=%u max=%d/%d frames=%llu/%llu undo=%d/%d redo=%d/%d\n",
                    morphFree, displayFree, rejected.size(), capacityMessages,
                    unsigned(app->sceneModified), app->lastRegisteredFrame,
                    portable.Document().timeline.lastRegisteredFrame,
                    static_cast<unsigned long long>(frames),
                    static_cast<unsigned long long>(portable.Runtime().Counters().frames), undo,
                    portable.CanUndo(), redo, portable.CanRedo());
            Check(rejected.size() == capacityMessages && app->sceneModified == 1 &&
                      app->lastRegisteredFrame ==
                          portable.Document().timeline.lastRegisteredFrame &&
                      portable.Runtime().Counters().frames == frames &&
                      portable.CanUndo() == undo && portable.CanRedo() == redo,
                  "native partial model paste side effects");
            native.Compare(portable.Document().models[0]);
            frame_line_fixture::Hash(hash, portable.Document().models[0].morphKeys);
            frame_line_fixture::Hash(hash, portable.Document().models[0].displayKeys);
            ++cases;
        }
    std::printf("Native model partial-paste frozen hash: %016llx\n",
                static_cast<unsigned long long>(hash));
    return cases;
}
unsigned CameraScaleCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    const unsigned char ret = 0xc3;
    Patch seek(original.Function<void*>(0x59c30), &ret, 1);
    Patch refresh(original.Function<void*>(0x20dd0), &ret, 1);
    auto child = &ResetChild;
    auto text = &RangeText;
    Patch getChild(original.Function<void*>(0x128658), &child, sizeof child);
    Patch getText(original.Function<void*>(0x128690), &text, sizeof text);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (const char* file : {"facial.pmd", "articulated.pmx"})
        for (unsigned variant = 0; variant < 11; ++variant) {
            EditorSession portable;
            std::string reason;
            Check(CameraScaleFixture(portable, root, file, reason), reason.c_str());
            const auto before = portable.Document().cameraKeys;
            std::vector<mdl::CameraKey> native(10000);
            const auto put = [&](unsigned i, const auto& key) {
                auto& out = native.at(i);
                out.frame = key.frame;
                out.previous = key.previous;
                out.next = key.next;
                out.fov = keyframe_edit::SignedCameraFov(key.fov);
                out.distance = key.distance;
                out.selected = key.selected;
                out.perspective = key.perspective;
                out.parentModel = key.parentModel;
                out.parentBone = key.parentBone;
                std::copy(key.eye.begin(), key.eye.end(), out.eye);
                std::copy(key.target.begin(), key.target.end(), out.target);
                std::memcpy(out.interpolation, key.interpolation.data(), 24);
            };
            for (unsigned i = 0; i < before.dense.size(); ++i)
                put(i, before.dense[i]);
            for (const auto& key : before.sparse)
                put(unsigned(key.index), key.value);
            for (const auto& [i, key] : before.unused)
                put(i, key);
            const auto nativeBefore = native;
            std::fill(app.begin(), app.end(), 0);
            auto* keys = native.data();
            std::memcpy(app.data() + 0x3d0, &keys, sizeof keys);
            auto values = CameraScaleIdentity;
            if (variant >= 1 && variant <= 8) {
                values[2 * (variant - 1)] = -1.25f;
                values[2 * (variant - 1) + 1] = 35.7f;
            } else if (variant == 9) {
                values.fill(0);
            } else if (variant == 10) {
                values[14] = 1e30f;
                values[15] = -1e30f;
            }
            rangeFields.clear();
            for (unsigned i = 0; i < values.size(); ++i) {
                char field[24];
                std::snprintf(field, sizeof field, "%.9g", double(values[i]));
                rangeFields[686 + int(i)] = field;
            }
            original.Function<void (*)(HWND)>(0x9c330)(nullptr);
            Check(portable.ScaleCameraKeys(values, reason), reason.c_str());
            const auto& track = portable.Document().cameraKeys;
            std::map<unsigned, PmmCameraKey> expected(track.unused.begin(), track.unused.end());
            for (unsigned i = 0; i < track.dense.size(); ++i)
                expected[i] = track.dense[i];
            for (const auto& key : track.sparse)
                expected[unsigned(key.index)] = key.value;
            for (unsigned i = 0; i < native.size(); ++i) {
                const auto& a = native[i];
                if (!expected.count(i)) {
                    Check(!std::memcmp(&a, &nativeBefore[i], sizeof a),
                          "native free camera changed");
                    continue;
                }
                const auto& b = expected.at(i);
                Check(a.frame == b.frame && a.previous == b.previous && a.next == b.next &&
                          a.selected == b.selected && std::uint32_t(a.fov) == b.fov &&
                          a.parentModel == b.parentModel && a.parentBone == b.parentBone &&
                          a.perspective == b.perspective &&
                          !std::memcmp(&a.distance, &b.distance, 4) &&
                          !std::memcmp(a.eye, b.eye.data(), 12) &&
                          !std::memcmp(a.target, b.target.data(), 12) &&
                          !std::memcmp(a.interpolation, b.interpolation.data(), 24),
                      "native camera-scale full pool differs");
            }
            Check(app[0xa1b31], "native camera-scale dirty flag");
            ++cases;
        }
    return cases;
}
unsigned reductionCount = 0, reductionMessages = 0;
int WINAPI ReductionMessage(HWND, LPCSTR text, LPCSTR title, UINT flags) {
    Check(std::strcmp(title, "delete unused frame") == 0 && flags == MB_TOPMOST,
          "native reduction report identity");
    reductionCount = unsigned(std::atoi(text));
    ++reductionMessages;
    return IDOK;
}
unsigned ReductionCommands(OriginalPhysicsModule& original, const std::filesystem::path& root) {
    // Execute the original marking and reporting command. Deletion's independent
    // slot/undo comparison lives in editor_frame_line_oracle_test.
    const unsigned char ret = 0xc3;
    Patch deletion(original.Function<void*>(0x64b30), &ret, 1);
    auto message = &ReductionMessage;
    Patch report(original.Function<void*>(0x128618), &message, sizeof message);
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    unsigned cases = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "facial.pmd"})
        for (bool reverse : {false, true})
            for (unsigned variant = 0; variant < 8; ++variant) {
                EditorSession portable;
                std::string reason;
                Check(ReductionFixture(portable, root, file, reverse, variant, reason),
                      reason.c_str());
                const auto before = portable.Document().models[0];
                const auto resource = portable.Runtime().Resource(0);
                NativeAuxiliary native(before, resource->SourceKind() == ModelKind::Pmx);
                auto* model = native.model.get();
                std::vector<mdl::BoneRecord> bones(before.bonePose.size());
                std::vector<mdl::BoneKey> keys(mdl::kBoneKeyCapacity);
                model->boneCount = unsigned(bones.size());
                model->boneTable = bones.data();
                model->boneKeys = keys.data();
                for (unsigned i = 0; i < bones.size(); ++i) {
                    int mapped;
                    Check(portable.Runtime().DocumentBoneIndex(0, int(i), mapped, reason),
                          reason.c_str());
                    bones[i].type =
                        static_cast<mdl::BoneType>(resource->EditingBones()[mapped].type);
                }
                const auto copy = [](const auto& track, auto&& put) {
                    for (unsigned i = 0; i < track.dense.size(); ++i)
                        put(i, track.dense[i]);
                    for (const auto& key : track.sparse)
                        put(unsigned(key.index), key.value);
                    for (const auto& [i, key] : track.unused)
                        put(i, key);
                };
                const auto links = [](auto& out, const auto& in) {
                    out.frame = in.frame;
                    out.previous = in.previous;
                    out.next = in.next;
                    out.allocated = in.allocated;
                };
                copy(before.boneKeys, [&](unsigned i, const auto& key) {
                    auto& out = keys[i];
                    links(out, key);
                    std::copy(key.position.begin(), key.position.end(), out.position);
                    std::copy(key.rotation.begin(), key.rotation.end(), out.rotation);
                    std::copy(key.interpolation.begin(), key.interpolation.end(),
                              out.interpolation);
                    out.physicsDisabled = key.physicsDisabled;
                });
                copy(before.morphKeys, [&](unsigned i, const auto& key) {
                    links(native.morphs[i], key);
                    native.morphs[i].value = key.value;
                });
                copy(before.displayKeys, [&](unsigned i, const auto& key) {
                    links(native.displays[i], key);
                    native.displays[i].visible = key.visible;
                    native.ik[i] = key.ikEnabled;
                    native.displays[i].ikStates = native.ik[i].data();
                });
                for (auto& key : keys)
                    key.allocated = 1;
                for (auto& key : native.morphs)
                    key.allocated = 1;
                for (auto& key : native.displays)
                    key.allocated = 1;
                std::fill(app.begin(), app.end(), 0);
                std::memcpy(app.data() + 0xbe8, &model, sizeof model);
                app[0xa1b90] = 1;
                reductionMessages = 0;
                original.Function<void (*)(HWND, WPARAM)>(0x3f550)(nullptr,
                                                                   MDS_CMD_DELETE_UNUSED_FRAMES);
                unsigned count;
                Check(portable.DeleteUnusedFrames(count, reason), reason.c_str());
                Check(count == reductionCount && reductionMessages == 1 && app[0xa1b31] == 1,
                      "native reduction count/dirty/report");
                const auto& after = portable.Document().models[0];
                const auto compare = [](const auto& pool, const auto& old, const auto& changed) {
                    std::set<unsigned> removed;
                    for (const auto& key : old.sparse)
                        if (std::none_of(changed.sparse.begin(), changed.sparse.end(),
                                         [&](const auto& k) { return k.index == key.index; }))
                            removed.insert(unsigned(key.index));
                    for (unsigned i = 0; i < pool.size(); ++i)
                        Check(bool(pool[i].allocated) == (removed.count(i) != 0),
                              "native full-pool reduction marks");
                };
                compare(keys, before.boneKeys, after.boneKeys);
                compare(native.morphs, before.morphKeys, after.morphKeys);
                compare(native.displays, before.displayKeys, after.displayKeys);
                ++cases;
            }
    return cases;
}
unsigned gravityCapacityMessages = 0;
int WINAPI GravityCapacityMessage(HWND, LPCSTR text, LPCSTR, UINT) {
    Check(std::strstr(text, "10000") != nullptr, "native gravity capacity message");
    ++gravityCapacityMessages;
    return IDOK;
}
void GravityCapacity(OriginalPhysicsModule& original) {
    EditorSession portable;
    std::string reason;
    const bool edited = portable.Edit(
        "gravity-capacity",
        [](auto& d, auto&) {
            FillGravityCapacity(d);
            return true;
        },
        reason);
    Check(edited, reason.c_str());
    std::vector<unsigned char> app(0xa3000);
    auto* pointer = app.data();
    Patch context(original.Function<void*>(0x1445f8), &pointer, sizeof pointer);
    const unsigned char ret = 0xc3;
    Patch rebuild(original.Function<void*>(0x5f7f0), &ret, 1);
    Patch paint(original.Function<void*>(0x60ea0), &ret, 1);
    auto message = &GravityCapacityMessage;
    Patch report(original.Function<void*>(0x128618), &message, sizeof message);
    std::vector<mdl::CameraKey> camera(10000);
    std::vector<mdl::LightKey> light(10000);
    std::vector<mdl::SelfShadowKey> shadow(10000);
    std::vector<mdl::GravityKey> gravity(10000);
    std::vector<mdl::AccessoryKey> accessories(10000), lastAccessory(10000);
    const auto bind = [&](unsigned offset, auto& pool) {
        auto* data = pool.data();
        std::memcpy(app.data() + offset, &data, sizeof data);
        for (auto& key : pool)
            key.selected = 1;
    };
    bind(0x3d0, camera);
    bind(0x3d8, light);
    bind(0x3e0, shadow);
    bind(0x3e8, gravity);
    for (unsigned slot = 0; slot < 255; ++slot)
        bind(0x3f0 + slot * 8, slot == 254 ? lastAccessory : accessories);
    for (unsigned i = 0; i < gravity.size(); ++i) {
        gravity[i].frame = i;
        gravity[i].previous = i ? i - 1 : 0;
        gravity[i].next = i == 9999 ? 0 : i + 1;
        gravity[i].acceleration = 9.8f;
        gravity[i].direction[1] = -1;
    }
    WriteGravity(app, portable.Runtime().Session()->Pose().gravity);
    const unsigned frame = 10000, maximum = 9999;
    std::memcpy(app.data() + 0x1450, &frame, 4);
    std::memcpy(app.data() + 0x9f038, &maximum, 4);
    app[0xa1b90] = 1;
    original.Function<void (*)()>(0x95dd0)();
    Check(gravityCapacityMessages == 1 && app[0xa1b31] == 1,
          "native full-pool command reports and dirties");
    const auto cleared = [](const auto& pool) {
        return std::all_of(pool.begin(), pool.end(), [](const auto& key) { return !key.selected; });
    };
    Check(cleared(camera) && cleared(light) && cleared(shadow) && cleared(gravity) &&
              cleared(accessories) && cleared(lastAccessory),
          "native full-pool command clears all marks");
    const auto revision = portable.Revision(), frames = portable.Runtime().Counters().frames;
    Check(!portable.RegisterGravity(reason) && reason == "gravity-key-capacity",
          "portable capacity report");
    Check(portable.Revision() != revision && portable.Runtime().Counters().frames == frames &&
              portable.Document().gravityKeys.sparse.size() == 9999 &&
              portable.Document().timeline.lastRegisteredFrame == 9999 &&
              !portable.Document().gravityKeys.dense[0].selected,
          "portable native capacity side effects");
    for (const auto& key : portable.Document().gravityKeys.sparse)
        Check(!key.value.selected, "portable full-pool marks");
    std::puts(
        "Original full gravity dialog registration: capacity, all track sweeps and dirty flag passed");
}
}
#include "editor_english_names_oracle.hpp"
#include "editor_model_save_oracle.hpp"
#include "editor_toon_oracle.hpp"
#include "editor_model_physics_oracle.hpp"
#include "editor_clipboard_chain_oracle.hpp"
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Check(argc == 3 || argc == 4,
          "usage: editor_auxiliary_oracle_test original.exe generated-fixture [--mouth-shift]");
    OriginalPhysicsModule original(argv[1]);
    if (argc == 4) {
        if (std::wcscmp(argv[3], L"--clipboard-chain") == 0) {
            std::printf("%u original complete clipboard chain checkpoints passed\n",
                        GlobalClipboardChains(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--model-physics") == 0) {
            std::printf("%u original physics dialog snapshots passed\n",
                        ModelPhysicsCommands(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--toon") == 0) {
            std::printf("%u original complete toon commands passed\n",
                        ToonCommands(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--enhanced-save") == 0) {
            std::printf("%u original enhanced-model save commands passed\n",
                        EnhancedSaveCommands(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--english-names") == 0) {
            std::printf("%u original English-name apply commands passed\n",
                        EnglishNameCommands(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--vsq") == 0) {
            std::printf("%u original complete VSQ commands passed\n",
                        VsqCommands(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--paste-capacity") == 0) {
            std::printf("%u original partial paste capacity commands passed\n",
                        PasteCapacity(original));
            std::printf("%u original model partial paste capacity commands passed\n",
                        ModelPasteCapacity(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--key-capacity") == 0) {
            std::printf(
                "%u original full display and %u original full global capacity commands passed\n",
                DisplayCapacity(original, argv[2]), GlobalCapacity(original));
            BoneCapacity(original, argv[2]);
            return 0;
        }
        if (std::wcscmp(argv[3], L"--frame-audio") == 0) {
            std::printf(
                "%u original frame-WAV commands, %u time blocks and %u restart sequences passed\n",
                AudioPreviewCommands(original), AudioPreviewMath(original),
                AudioPreviewRestart(original));
            std::printf("%u original frame-WAV actual file seeks passed\n",
                        AudioPreviewFileOffsets(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--texture-reload") == 0) {
            std::printf("%u original texture reload commands passed\n",
                        TextureReloadCommands(original));
            std::printf("%u original texture reload cache profiles passed\n",
                        TextureReloadCache(original));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--about") == 0) {
            std::printf("%u original about command comparisons passed\n", AboutCommands(original));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--mipmap") == 0) {
            std::printf("%u original mipmap command comparisons passed\n",
                        MipmapCommands(original));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--wireframe") == 0) {
            std::printf("%u original wireframe command comparisons passed\n",
                        WireframeCommands(original));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--antialias") == 0) {
            std::printf("%u original antialias command comparisons passed\n",
                        AntialiasCommands(original));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--debug-bodies") == 0) {
            std::printf("%u original body-display command comparisons passed\n",
                        DisplayFlagCommands(original, true));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--file-folders") == 0) {
            std::printf("%u original file folder comparisons passed\n",
                        FileFolderCommands(original));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--morph-capacity") == 0) {
            std::printf("%u original Morph capacity comparisons passed\n",
                        MorphCapacity(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--blink") == 0) {
            std::printf("%u original blink command comparisons passed\n",
                        BlinkCommands(original, argv[2]));
            return 0;
        }
        if (std::wcscmp(argv[3], L"--low-power") == 0) {
            std::printf("%u original low-power commands and %u render-gate checkpoints passed\n",
                        LowPowerCommands(original), LowPowerFrameGate(original));
            return 0;
        }
        Check(std::wcscmp(argv[3], L"--mouth-shift") == 0, "unknown oracle filter");
        std::printf("%u original mouth shifts passed\n",
                    MorphPurgeCommands(original, argv[2], true));
        return 0;
    }
    std::printf("%u original low-power commands and %u render-gate checkpoints passed\n",
                LowPowerCommands(original), LowPowerFrameGate(original));
    std::printf("%u original blink command comparisons passed\n", BlinkCommands(original, argv[2]));
    std::printf("%u original Morph capacity comparisons passed\n",
                MorphCapacity(original, argv[2]));
    std::printf("%u original model rendering toggles: complete model/application bytes\n",
                ModelRenderFlags(original, argv[2]));
    std::printf(
        "%u original self-shadow switches: independent mode and unchanged application state\n",
        ShadowSwitchCommands(original));
    std::printf(
        "%u original disable-WAV switches: unchanged playback and complete application state\n",
        AudioDisableCommands(original));
    std::printf(
        "%u original playback physics switches: menu authority, pending policy and complete state\n",
        PlaybackPhysicsSwitchCommands(original));
    std::printf(
        "%u original model display commands: sparse slots, playback, raw flags and complete state\n",
        ModelDisplayCommands(original));
    std::printf("%u original FPS commands: exact rate and unchanged application state\n",
                FrameRateCommands(original));
    std::printf("%u original file folder comparisons passed\n", FileFolderCommands(original));
    std::printf("%u original mipmap command comparisons passed\n", MipmapCommands(original));
    std::printf("%u original antialias command comparisons passed\n", AntialiasCommands(original));
    std::printf("%u original wireframe command comparisons passed\n", WireframeCommands(original));
    std::printf("%u original about command comparisons passed\n", AboutCommands(original));
    std::printf("%u original texture reload commands passed\n", TextureReloadCommands(original));
    std::printf("%u original texture reload cache profiles passed\n", TextureReloadCache(original));
    std::printf("%u original frame-WAV commands, %u time blocks and %u restart sequences passed\n",
                AudioPreviewCommands(original), AudioPreviewMath(original),
                AudioPreviewRestart(original));
    std::printf("%u original frame-WAV actual file seeks passed\n",
                AudioPreviewFileOffsets(original, argv[2]));
    unsigned count = 0;
    for (const char* file : {"articulated.pmd", "articulated.pmx", "facial.pmd"}) {
        EditorSession session;
        std::string reason;
        Check(session.ImportModel(std::filesystem::path(argv[2]) / file, reason), reason.c_str());
        const bool pmx = session.Runtime().Resource(0)->SourceKind() == ModelKind::Pmx;
        NativeAuxiliary native(session.Document().models[0], pmx);
        const unsigned frames[]{0, 20, 5, 30, 20, 1, 0, 60};
        for (unsigned cycle = 0; cycle < 3; ++cycle)
            for (unsigned i = 0; i < std::size(frames); ++i)
                for (auto kind : {Track::Morph, Track::Display}) {
                    const unsigned channel = pmx ? 0 : 1;
                    if (kind == Track::Morph &&
                        channel >= session.Document().models[0].morphPose.size())
                        continue; // PMD fixture contains only the non-editable base morph.
                    Check(session.Seek(frames[i], reason), reason.c_str());
                    Check(session.Edit(
                              "auxiliary-input",
                              [&](auto& d, auto&) {
                                  auto& model = d.models[0];
                                  model.selection.loadComplete = (i + cycle) % 2;
                                  for (auto& weight : model.morphPose)
                                      weight = float((i * 3 + cycle) % 9) / 8;
                                  for (auto& enabled : model.ikPose)
                                      enabled = (i + cycle / 2) % 2;
                                  return true;
                              },
                              reason),
                          reason.c_str());
                    native.Register(original, session.Document().models[0], kind, frames[i],
                                    channel);
                    const auto* runtime = &session.Runtime();
                    const auto updates = runtime->Counters().frames;
                    const bool undo = session.CanUndo(), redo = session.CanRedo();
                    Check(kind == Track::Morph
                              ? session.RegisterMorph(0, channel, reason)
                              : session.Register({{kind, 0, 0, frames[i]}}, reason),
                          reason.c_str());
                    native.Compare(session.Document().models[0]);
                    Check(runtime == &session.Runtime() && updates == runtime->Counters().frames,
                          "registration advanced or replaced the CPU runtime");
                    Check(undo == session.CanUndo() && redo == session.CanRedo(),
                          "registration altered portable bone history");
                    ++count;
                }
    }
    std::printf("%u original morph/display registrations: exact slots, values, IK and visibility\n",
                count);
    std::printf(
        "%u original reset-command branches: weights, controls, document and history flags\n",
        ResetCommand(original, argv[2]));
    std::printf(
        "%u original full-dispatch checks: rotation reset, physics selection and deferred reset\n",
        SceneCommands(original, argv[2]));
    std::printf(
        "%u original physics-key commands: full pool sweeps, reversed bindings and empty chains\n",
        PhysicsKeyCommand(original, argv[2]));
    std::printf(
        "%u original chained range commands: full pools, additive rows and detached records\n",
        RangeCommands(original, argv[2]));
    std::printf(
        "%u original gravity channel edits: values, row selection and immediate world vectors\n",
        GravityChannels(original, argv[2]));
    std::printf("%u original gravity registrations: complete pool bytes, payloads and watermarks\n",
                GravityRegistration(original, argv[2]));
    GravityHiddenControls(original, argv[1]);
    GravityCapacity(original);
    std::printf(
        "%u original reduction commands: complete pool marks, counts and detached records\n",
        ReductionCommands(original, argv[2]));
    std::printf(
        "%u original morph-scale applies: full pool bytes, prefix parsing and unused slots\n",
        MorphScaleCommands(original, argv[2]));
    std::printf(
        "%u original morph purges: complete pool, tail, categories, reordered channels and cancellation\n",
        MorphPurgeCommands(original, argv[2]));
    std::printf(
        "%u original mouth shifts: full pool, freed predecessors, overflow, reordered channels and cancellation\n",
        MorphPurgeCommands(original, argv[2], true));
    std::printf(
        "%u original camera-scale applies: full pool bytes, angle conversion and signed FOV\n",
        CameraScaleCommands(original, argv[2]));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
