#pragma once
#include "background_image_fixture.hpp"
#include "mikudancestudio/background_placement.hpp"
#include "mikudancestudio/pmm_format.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include "../src/render/background_quad.hpp"
#include <d3dx9.h>
#include <wrl/client.h>
#include <stdexcept>

// Native fixed-function sampling/rasterization, with D3DX-created source and
// mips. Placement uses the helper independently checked against the original
// executable's 108 exact vertex snapshots. No RHI draw state is consumed.
class BackgroundDx9Reference {
    using MicrosoftDevice = Microsoft::WRL::ComPtr<IDirect3DDevice9>;
    HWND window_ = nullptr;
    HMODULE runtime_ = nullptr;
    Microsoft::WRL::ComPtr<IDirect3D9> api_;
    MicrosoftDevice device_;
    static void Check(HRESULT hr) {
        if (FAILED(hr))
            throw std::runtime_error("background DX9 HRESULT=" + std::to_string(unsigned(hr)));
    }

public:
    BackgroundDx9Reference() {
        window_ = CreateWindowExW(0, L"STATIC", L"MDS background oracle", 0, 0, 0, 1, 1,
                                  HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        api_.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!window_ || !api_)
            throw std::runtime_error("background DX9 unavailable");
        D3DPRESENT_PARAMETERS p{};
        p.Windowed = TRUE;
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        p.BackBufferWidth = p.BackBufferHeight = 1;
        p.BackBufferFormat = D3DFMT_A8R8G8B8;
        p.hDeviceWindow = window_;
        Check(api_->CreateDevice(0, D3DDEVTYPE_HAL, window_,
                                 D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p,
                                 &device_));
        runtime_ = LoadLibraryExW(L"d3dx9_43.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!runtime_)
            throw std::runtime_error("D3DX9_43 unavailable");
    }
    ~BackgroundDx9Reference() {
        device_.Reset();
        api_.Reset();
        if (window_)
            DestroyWindow(window_);
        if (runtime_)
            FreeLibrary(runtime_);
    }
    mikudancestudio::rhi::AttachmentReadback
    Render(const mikudancestudio::rhi::RgbaImage& source,
           const mikudancestudio::format::PmmBackgroundReference& picture, unsigned width,
           unsigned height, bool blend) {
        using namespace mikudancestudio;
        using Microsoft::WRL::ComPtr;
        auto load = reinterpret_cast<decltype(&D3DXCreateTextureFromFileInMemoryEx)>(
            GetProcAddress(runtime_, "D3DXCreateTextureFromFileInMemoryEx"));
        if (!load)
            throw std::runtime_error("D3DX texture export unavailable");
        const auto bytes = BackgroundTga(source);
        ComPtr<IDirect3DTexture9> texture, color;
        Check(load(device_.Get(), bytes.data(), UINT(bytes.size()), 1024, 1024, 0, 0,
                   D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, UINT(-1), UINT(-1), 0, nullptr, nullptr,
                   &texture));
        Check(device_->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                     D3DPOOL_DEFAULT, &color, nullptr));
        ComPtr<IDirect3DSurface9> target, read;
        Check(color->GetSurfaceLevel(0, &target));
        Check(device_->SetDepthStencilSurface(nullptr));
        Check(device_->SetRenderTarget(0, target.Get()));
        const D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
        Check(device_->SetViewport(&viewport));
        Check(device_->Clear(0, nullptr, D3DCLEAR_TARGET, 0x00ffffff, 1, 0));
        const auto rs = [&](D3DRENDERSTATETYPE state, DWORD value) {
            Check(device_->SetRenderState(state, value));
        };
        rs(D3DRS_LIGHTING, FALSE);
        rs(D3DRS_ZENABLE, FALSE);
        rs(D3DRS_STENCILENABLE, FALSE);
        rs(D3DRS_CULLMODE, D3DCULL_NONE);
        rs(D3DRS_ALPHABLENDENABLE, blend);
        rs(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        rs(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        rs(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        rs(D3DRS_BLENDOPALPHA, D3DBLENDOP_MAX);
        rs(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
        rs(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
        rs(D3DRS_ALPHATESTENABLE, TRUE);
        rs(D3DRS_ALPHAREF, 1);
        rs(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        Check(device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE));
        Check(device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE));
        Check(device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE));
        Check(device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE));
        Check(device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE));
        Check(device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT));
        Check(device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE));
        Check(device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP));
        Check(device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP));
        // The user-approved portable trilinear profile, with native D3DX mips.
        for (auto state : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER, D3DSAMP_MIPFILTER})
            Check(device_->SetSamplerState(0, state, D3DTEXF_LINEAR));
        Check(device_->SetTexture(0, texture.Get()));
        Check(device_->SetFVF(kTexturedScreenVertexFvf));
        render::BackgroundView view;
        view.width = int(width);
        view.height = int(height);
        view.scale = render::BackgroundViewScale(int(width));
        const auto rect =
            render::PlaceBackground({picture.offsetX, picture.offsetY, int(source.width),
                                     int(source.height), picture.scale},
                                    view);
        std::array<TexturedScreenVertex, 6> quad;
        WriteBackgroundQuad(quad.data(), rect.left, rect.top, rect.right, rect.bottom);
        Check(device_->BeginScene());
        Check(device_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad.data(),
                                       sizeof(TexturedScreenVertex)));
        Check(device_->EndScene());
        Check(device_->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8,
                                                   D3DPOOL_SYSTEMMEM, &read, nullptr));
        Check(device_->GetRenderTargetData(target.Get(), read.Get()));
        D3DLOCKED_RECT lock{};
        Check(read->LockRect(&lock, nullptr, D3DLOCK_READONLY));
        rhi::AttachmentReadback result;
        result.description = {width, height, rhi::TextureFormat::Rgba8, 0};
        result.bytes.resize(std::size_t(width) * height * 4);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
                for (unsigned c = 0; c < 4; ++c)
                    result.bytes[(std::size_t(y) * width + x) * 4 + c] =
                        static_cast<const std::uint8_t*>(
                            lock.pBits)[y * lock.Pitch + x * 4 + (c < 3 ? 2 - c : c)];
        Check(read->UnlockRect());
        return result;
    }
};
