// Optional local Windows x64 oracle. Executes the hash-verified v9.32 video
// refresh function, with VFW and a hidden real DX9 device. No reference binary
// or private media is distributed. These offsets belong solely to the oracle.
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <vfw.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
void Check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#include "original_physics_module.hpp"

namespace {
int blockedDialogs = 0;
LRESULT CALLBACK BlockDialogs(int code, WPARAM window, LPARAM data) {
    if (code == HCBT_CREATEWND) {
        const auto* creation = reinterpret_cast<CBT_CREATEWNDW*>(data);
        const auto name = creation->lpcs->lpszClass;
        if ((IS_INTRESOURCE(name) && reinterpret_cast<ULONG_PTR>(name) == 32770) ||
            (!IS_INTRESOURCE(name) && std::wcscmp(name, L"#32770") == 0)) {
            ++blockedDialogs;
            return 1; // prevent creation, never show an unexpected native dialog
        }
    }
    return CallNextHookEx(nullptr, code, window, data);
}
template <class T> T& Slot(std::vector<unsigned char>& bytes, std::size_t offset) {
    Check(offset + sizeof(T) <= bytes.size(), "oracle slot bounds");
    return *reinterpret_cast<T*>(bytes.data() + offset);
}
unsigned Number(IDirect3DDevice9* device, IDirect3DTexture9* texture) {
    IDirect3DSurface9 *surface = nullptr, *copy = nullptr;
    Check(texture && SUCCEEDED(texture->GetSurfaceLevel(0, &surface)), "native texture");
    Check(SUCCEEDED(device->CreateOffscreenPlainSurface(1024, 1024, D3DFMT_X8R8G8B8,
                                                        D3DPOOL_SYSTEMMEM, &copy, nullptr)),
          "native readback allocation");
    Check(SUCCEEDED(device->GetRenderTargetData(surface, copy)), "native readback");
    D3DLOCKED_RECT lock{};
    Check(SUCCEEDED(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY)), "native lock");
    unsigned value = 0;
    const unsigned y = unsigned(88.5 * 1024 / 96);
    for (unsigned bit = 0; bit < 12; ++bit) {
        const auto x = unsigned((bit * 12 + 6.5) * 1024 / 160);
        const auto* row = static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch;
        if (row[x * 4 + 2] > 128)
            value |= 1u << bit;
    }
    copy->UnlockRect();
    copy->Release();
    surface->Release();
    return value;
}
}
int wmain(int argc, wchar_t** argv) try {
    Check(argc == 4, "usage: oracle reference.exe native.avi report.json");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const auto hook = SetWindowsHookExW(WH_CBT, BlockDialogs, nullptr, GetCurrentThreadId());
    Check(hook, "install hidden-dialog guard");
    OriginalPhysicsModule original(argv[1]);
    const auto window = CreateWindowW(L"STATIC", L"Hidden video oracle", WS_OVERLAPPED, 0, 0, 64,
                                      64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(window && !IsWindowVisible(window), "hidden window");
    auto* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    Check(d3d, "DX9 runtime");
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    IDirect3DDevice9* device = nullptr;
    const auto created = d3d->CreateDevice(
        0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
        &pp, &device);
    if (FAILED(created)) {
        std::fprintf(stderr, "hidden DX9 device unavailable: %08lX\n", (unsigned long)created);
        return 77;
    }
    AVIFileInit();
    PAVIFILE file = nullptr;
    PAVISTREAM stream = nullptr;
    Check(AVIFileOpenW(&file, argv[2], OF_READ | OF_SHARE_DENY_NONE, nullptr) == 0, "AVI open");
    Check(AVIFileGetStream(file, &stream, streamtypeVIDEO, 0) == 0, "AVI stream");
    const auto reader = AVIStreamGetFrameOpen(stream, nullptr);
    Check(reader, "AVI frame reader");
    const auto dib = DrawDibOpen();
    Check(dib, "DrawDib");
    IDirect3DVertexBuffer9* vertices = nullptr;
    Check(SUCCEEDED(device->CreateVertexBuffer(168, 0, 0, D3DPOOL_MANAGED, &vertices, nullptr)),
          "native overlay buffer");
    std::vector<unsigned char> app(0xa4000), renderer(0x3b000);
    Slot<void*>(app, 0xa16e0) = renderer.data();
    Slot<HWND>(app, 0xa16c8) = window;
    Slot<unsigned char>(app, 0xa1b90) = 1; // English errors; none are expected
    Slot<IDirect3DDevice9*>(renderer, 0x3a9c0) = device;
    Slot<int>(renderer, 0x3a9c8) = 160;
    Slot<int>(renderer, 0x3a9cc) = 96;
    Slot<float>(renderer, 0x3a9d4) = 1;
    Slot<int>(app, 0xa1df4) = 160;
    Slot<int>(app, 0xa1df8) = 96;
    Slot<HDRAWDIB>(app, 0x9f2b8) = dib;
    Slot<IDirect3DVertexBuffer9*>(app, 0x9f2d0) = vertices;
    Slot<PAVISTREAM>(app, 0x9f2e0) = stream;
    Slot<PGETFRAME>(app, 0x9f2e8) = reader;
    Slot<LONG>(app, 0x9f2f0) = AVIStreamStart(stream);
    Slot<LONG>(app, 0x9f2f4) = AVIStreamStart(stream) + AVIStreamLength(stream);
    Slot<unsigned char>(app, 0x9f2f8) = 1; // numbered 30fps AVI
    Slot<float>(app, 0x9f304) = 1;
    Slot<int>(app, 0x9f308) = 160;
    Slot<int>(app, 0x9f30c) = 96;
    const auto refresh = original.Function<void (*)(void*)>(0x67fa0);
    auto& texture = Slot<IDirect3DTexture9*>(app, 0x9f2c0);
    auto& frame = Slot<int>(app, 0x1450);
    auto& visible = Slot<int>(app, 0x13ec);
    frame = 5;
    visible = 1;
    refresh(app.data());
    Check(Number(device, texture) == 5, "load/current sample 5");
    visible = 0; // native toggle only changes this flag
    frame = 20;  // hidden seek/step callers do not invoke refresh
    Check(Number(device, texture) == 5, "hidden mode 3 retains sample 5");
    // Cold load at the saved cursor, or the explicit mode-3 still-image event,
    // invokes this same function. Visibility is not a guard inside it.
    refresh(app.data());
    Check(visible == 0 && Number(device, texture) == 20, "hidden explicit refresh sample 20");
    visible = 1;
    frame = 30;
    refresh(app.data());
    Check(Number(device, texture) == 30, "resume sample 30");
    Check(blockedDialogs == 0, "unexpected native error dialog blocked");
    std::ofstream report(argv[3]);
    report << "{\"reference\":\"MikuMikuDanceE_v932x64\",\"refreshRva\":\"0x67fa0\","
              "\"hiddenWindow\":true,\"nullref\":false,\"samples\":[5,5,20,30],"
              "\"passed\":true}\n";
    Check(bool(report), "write native report");
    texture->Release();
    Slot<IDirect3DSurface9*>(app, 0x9f2c8)->Release();
    vertices->Release();
    DrawDibClose(dib);
    AVIStreamGetFrameClose(reader);
    AVIStreamRelease(stream);
    AVIFileRelease(file);
    AVIFileExit();
    device->Release();
    d3d->Release();
    DestroyWindow(window);
    UnhookWindowsHookEx(hook);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "native video oracle: %s\n", e.what());
    return 1;
}
