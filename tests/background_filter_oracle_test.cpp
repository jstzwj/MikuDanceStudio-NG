#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include "background_image_fixture.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rhi = mikudancestudio::rhi;
namespace {
void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(why);
}
void Hr(HRESULT value, const char* why) {
    Check(SUCCEEDED(value), why);
}
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
    T** Out() { return &p; }
    T* operator->() const { return p; }
};
struct Fixture {
    HWND window = nullptr;
    IDirect3D9* api = nullptr;
    IDirect3DDevice9* device = nullptr;
    bool ready = false;
    Fixture() {
        api = Direct3DCreate9(D3D_SDK_VERSION);
        window = CreateWindowW(L"STATIC", L"background oracle", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!api || !window)
            return;
        D3DPRESENT_PARAMETERS p{};
        p.Windowed = TRUE;
        p.hDeviceWindow = window;
        p.BackBufferWidth = p.BackBufferHeight = 64;
        p.BackBufferFormat = D3DFMT_A8R8G8B8;
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        ready = SUCCEEDED(api->CreateDevice(
            D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p, &device));
    }
    ~Fixture() {
        if (device)
            device->Release();
        if (api)
            api->Release();
        if (window)
            DestroyWindow(window);
    }
};
constexpr UINT kD3dxDefault = UINT(-1);
}

int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Fixture fixture;
    if (!fixture.ready)
        return 77;
    const std::filesystem::path output =
        argc == 3 && std::wcscmp(argv[1], L"--output") == 0 ? argv[2] : std::filesystem::path{};
    if (!output.empty())
        std::filesystem::create_directories(output);
    unsigned maximum = 0;
    for (const auto& test : kBackgroundImages) {
        const auto w = test.width, h = test.height;
        const auto source = BackgroundImage(test);
        const auto bmp = BackgroundTga(source);
        rhi::RgbaImage raw, prepared;
        Check(rhi::DecodeTextureRgba(bmp.data(), bmp.size(), raw), "decode fixture");
        Check(rhi::PrepareBackgroundImage(raw, prepared), "prepare background");
        Com<IDirect3DTexture9> texture;
        Hr(D3DXCreateTextureFromFileInMemoryEx(
               fixture.device, bmp.data(), unsigned(bmp.size()), 1024, 1024, 0, 0, D3DFMT_A8R8G8B8,
               D3DPOOL_MANAGED, kD3dxDefault, kD3dxDefault, 0, nullptr, nullptr, texture.Out()),
           "native resize");
        D3DLOCKED_RECT lock{};
        Hr(texture->LockRect(0, &lock, nullptr, D3DLOCK_READONLY), "native texture pixels");
        unsigned maxError = 0, differences = 0;
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned y = 0; y < 1024; ++y)
            for (unsigned x = 0; x < 1024; ++x) {
                const auto* native =
                    static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
                const auto* cpu = std::as_const(prepared.pixels).data() + (y * 1024 + x) * 4;
                for (unsigned c = 0; c < 4; ++c) {
                    hash = (hash ^ native[c < 3 ? 2 - c : c]) * 1099511628211ull;
                    const auto error =
                        unsigned(std::abs(int(cpu[c]) - int(native[c < 3 ? 2 - c : c])));
                    maxError = std::max(maxError, error);
                    differences += error != 0;
                }
            }
        Hr(texture->UnlockRect(0), "unlock native pixels");
        if (!output.empty()) {
            Com<IDirect3DTexture9> floating;
            Hr(D3DXCreateTextureFromFileInMemoryEx(fixture.device, bmp.data(), unsigned(bmp.size()),
                                                   1024, 1024, 1, 0, D3DFMT_A32B32G32R32F,
                                                   D3DPOOL_MANAGED, kD3dxDefault, kD3dxDefault, 0,
                                                   nullptr, nullptr, floating.Out()),
               "native floating resize");
            Hr(floating->LockRect(0, &lock, nullptr, D3DLOCK_READONLY), "native floating pixels");
            std::ofstream file(output / (std::to_string(w) + "x" + std::to_string(h) + "-" +
                                         std::to_string(test.pattern) + ".rgba32f"),
                               std::ios::binary);
            for (unsigned y = 0; y < 1024; ++y)
                file.write(static_cast<const char*>(lock.pBits) + y * lock.Pitch, 1024 * 16);
            Check(bool(file), "write native float diagnostic");
            Hr(floating->UnlockRect(0), "unlock native float");
        }
        std::printf("background %ux%u pattern=%u: max=%u different=%u hash=%016llx\n", w, h,
                    test.pattern, maxError, differences, static_cast<unsigned long long>(hash));
        if (test.nativeHash)
            Check(hash == test.nativeHash, "native output changed");
        maximum = std::max(maximum, maxError);
    }
    return maximum ? 2 : 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
