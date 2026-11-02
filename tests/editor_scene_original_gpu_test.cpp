#include <omp.h>
#include "scene_effect_fixture.hpp"
#include "mikudancestudio/editor_session.hpp"
#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "self_shadow_compare.hpp"
#include "d3d9_device_fixture.hpp"

namespace {
// Explicit test profile only: the ordinary D3D9 path remains the default.
// The original code still evaluates and draws independently. Only resource
// allocation is adapted for Ex; managed-pool eviction/reset is not qualified.
struct NativeApi {
    HMODULE module = nullptr;
    Com<IDirect3D9> api;
    NativeApi() {
        char profile[8]{};
        const auto size = GetEnvironmentVariableA("MDS_TEST_DX9EX", profile, sizeof profile);
        if (!size)
            return;
        Check(size < sizeof profile && !std::strcmp(profile, "1"), "MDS_TEST_DX9EX must be 1");
        module = LoadLibraryExW(L"d3d9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        Check(module != nullptr, "system D3D9 runtime");
        using Create = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
        const auto create = reinterpret_cast<Create>(GetProcAddress(module, "Direct3DCreate9Ex"));
        Check(create != nullptr, "Direct3DCreate9Ex entry point");
        IDirect3D9Ex* created = nullptr;
        Hr(create(D3D_SDK_VERSION, &created), "D3D9Ex API");
        api.p = created;
        std::puts("Original GPU oracle profile: D3D9Ex HAL (explicit, managed allocation adapter)");
    }
    ~NativeApi() {
        // Keep the DLL resident until all owned API/device interfaces are released.
        if (api.p) {
            api.p->Release();
            api.p = nullptr;
        }
        if (module)
            FreeLibrary(module);
    }
};
struct ExPoolDevice : SnapshotDeviceFixture {
    using SnapshotDeviceFixture::SnapshotDeviceFixture;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDirect3DDevice9)) {
            *output = this;
            AddRef();
            return S_OK;
        }
        if (iid == __uuidof(IDirect3DDevice9Ex)) {
            *output = nullptr;
            return E_NOINTERFACE;
        }
        return real->QueryInterface(iid, output);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage,
                                            D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DTexture9** texture, HANDLE* shared) override {
        if (pool == D3DPOOL_MANAGED) {
            pool = D3DPOOL_DEFAULT;
            usage |= D3DUSAGE_DYNAMIC;
        }
        return real->CreateTexture(width, height, levels, usage, format, pool, texture, shared);
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                 IDirect3DVertexBuffer9** buffer,
                                                 HANDLE* shared) override {
        return real->CreateVertexBuffer(
            length, usage, fvf, pool == D3DPOOL_MANAGED ? D3DPOOL_DEFAULT : pool, buffer, shared);
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format,
                                                D3DPOOL pool, IDirect3DIndexBuffer9** buffer,
                                                HANDLE* shared) override {
        return real->CreateIndexBuffer(length, usage, format,
                                       pool == D3DPOOL_MANAGED ? D3DPOOL_DEFAULT : pool, buffer,
                                       shared);
    }
};
rhi::AttachmentReadback NativeColor(const std::vector<DWORD>& pixels, unsigned dimension) {
    rhi::AttachmentReadback result;
    result.description = {dimension, dimension, rhi::TextureFormat::Rgba8};
    result.bytes.resize(pixels.size() * 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        result.bytes[4 * i] = std::uint8_t(pixels[i] >> 16);
        result.bytes[4 * i + 1] = std::uint8_t(pixels[i] >> 8);
        result.bytes[4 * i + 2] = std::uint8_t(pixels[i]);
        result.bytes[4 * i + 3] = std::uint8_t(pixels[i] >> 24);
    }
    return result;
}
// The oracle loads, poses, skins and draws through the original executable.
// No RHI packet, CPU snapshot, material constants or geometry feed this path.
std::array<rhi::AttachmentReadback, 2>
NativeFrame(Fixture& fixture, OriginalModel& model, StandardEffect& effect, const D3DXMATRIX& world,
            const D3DXMATRIX& view, const D3DXMATRIX& projection, unsigned dimension) {
    auto& app = *fixture.app;
    auto& renderer = effect.renderer;
    RenderTarget target(fixture.device, dimension);
    renderer.backbufferSurface = target.color.p;
    renderer.depthStencilSurface = target.depth.p;
    app.state.renderPassCount = 1;
    InitRenderStates(&app);
    Hr(fixture.device->SetVertexShader(nullptr), "native reset VS");
    Hr(fixture.device->SetPixelShader(nullptr), "native reset PS");
    Hr(fixture.device->SetTransform(D3DTS_WORLD, &world), "native world");
    Hr(fixture.device->SetTransform(D3DTS_VIEW, &view), "native view");
    Hr(fixture.device->SetTransform(D3DTS_PROJECTION, &projection), "native projection");
    Hr(fixture.device->SetLight(0, &app.SceneLight()), "native light");
    Hr(fixture.device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                             0x00ffffff, 1, 0),
       "native clear");
    Hr(fixture.device->BeginScene(), "native begin");
    OriginalRenderApp originalApp(app, model);
    originalApp.Shadow(model, view, world);
    // The original frame loop decrements the retired ABI workspace, rather
    // than the portable runtime field which happens to have the same purpose.
    *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(originalApp.State()) + 0xA11E0) = 0;
    originalApp.Frame(model, world);
    Hr(fixture.device->EndScene(), "native end");
    std::array<rhi::AttachmentReadback, 2> result;
    const auto pixels = target.Pixels();
    Check(std::count_if(pixels.begin(), pixels.end(), [](DWORD pixel) { return pixel >> 24; }) >
              100,
          "original frame must contain model pixels");
    result[0] = NativeColor(pixels, dimension);
    const auto shadow = ShadowPixels(fixture.device, renderer.shadowSurface, {});
    Check(std::count_if(shadow.begin(), shadow.end(),
                        [](DWORD bits) {
                            float depth;
                            std::memcpy(&depth, &bits, sizeof depth);
                            return depth < 1;
                        }) > 0,
          "original shadow map must contain model depth");
    result[1].description = {kSize, kSize, rhi::TextureFormat::R32Float};
    result[1].bytes.resize(shadow.size() * 4);
    std::memcpy(result[1].bytes.data(), shadow.data(), result[1].bytes.size());
    renderer.backbufferSurface = renderer.depthStencilSurface = nullptr;
    return result;
}
}
int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Check(argc == 3 || argc == 4,
          "usage: editor_scene_original_gpu_test original.exe asset-directory [dump-directory]");
    const auto output = argc == 4 ? std::filesystem::absolute(argv[3]) : std::filesystem::path{};
    omp_set_dynamic(0);
    omp_set_num_threads(2);
    OriginalPhysicsModule binary(std::filesystem::absolute(argv[1]));
    const auto assets = std::filesystem::absolute(argv[2]);
    const auto module = binary.Function<HMODULE>(0);
    const auto resource = FindResourceA(module, MAKEINTRESOURCEA(118), RT_RCDATA);
    Check(resource != nullptr, "original standard effect resource");
    const auto data = LockResource(LoadResource(module, resource));
    const auto size = SizeofResource(module, resource);
    std::ifstream input(assets / "skin_effect_sm3.fx", std::ios::binary);
    const std::vector<char> source((std::istreambuf_iterator<char>(input)), {});
    Check(size == source.size() && std::memcmp(data, source.data(), size) == 0,
          "standard effect source differs from original binary");
    rhi::FileAssetSource files(assets);
    const bool wireframe = std::getenv("MDS_TEST_WIREFRAME") != nullptr;
    NativeApi nativeApi;
    unsigned cases = 0;
    for (bool pmx : {false, true})
        for (bool translucent : {false, true}) {
            std::unique_ptr<ExPoolDevice> allocationAdapter;
            Fixture fixture(false, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                            nativeApi.api.p, [&](auto* device) -> IDirect3DDevice9* {
                                if (!nativeApi.api.p)
                                    return device;
                                allocationAdapter = std::make_unique<ExPoolDevice>(device);
                                return allocationAdapter.get();
                            });
            if (!fixture.ready)
                return 77;
            Check(!IsWindowVisible(fixture.window), "oracle window must never be shown");
            D3DCAPS9 caps{};
            Hr(fixture.device->GetDeviceCaps(&caps), "native device caps");
            Check(caps.DeviceType != D3DDEVTYPE_NULLREF, "native raster device required");
            Check(!nativeApi.api.p || caps.DeviceType == D3DDEVTYPE_HAL,
                  "explicit D3D9Ex profile must not fall back to another driver");
            D3DADAPTER_IDENTIFIER9 adapter{};
            Hr(fixture.d3d->GetAdapterIdentifier(0, 0, &adapter), "native adapter identity");
            std::printf("Original raster adapter: %s (type %u)\n", adapter.Description,
                        unsigned(caps.DeviceType));
            SharedToons toons(fixture);
            const auto path =
                fixture.directory / (pmx ? "original-frame.pmx" : "original-frame.pmd");
            if (pmx)
                WriteComplexModel(path, false, true, translucent ? .375f : 1.f);
            else
                WriteModel(path, translucent ? .375f : 1.f);
            if (!output.empty()) {
                std::filesystem::create_directories(output);
                const auto name = std::string(pmx ? "pmx" : "pmd") +
                                  (translucent ? "-alpha" : "-opaque") +
                                  (pmx ? ".pmx" : ".pmd");
                std::filesystem::copy_file(path, output / name,
                                           std::filesystem::copy_options::overwrite_existing);
            }
            fixture.LoadModelFile(path, 0, 1);
            OriginalModel original(binary, fixture, path, 2);
            original.Pose();
            original.model->toonFlag = 1;
            auto& app = *fixture.app;
            app.ResetSceneLight();
            app.CameraMode() = 1;
            app.PlaybackActive() = 0;
            app.state.editMode = 2;
            app.GroundGridEnabled() = app.GroundShadowEnabled() = 0;
            app.RenderWidth() = app.RenderHeight() = kSize;
            app.ViewOffsetX() = app.ViewOffsetY() = 0;
            app.CameraDistance() = pmx ? -45.f : -5.f;
            app.CameraFov() = 30;
            app.ShadowDistance() = .01125f;
            app.state.selfShadowEnabled = 1;
            const D3DXVECTOR3 focus(pmx ? 0.f : 1.f, pmx ? 10.f : .5f, 0);
            std::memcpy(app.CameraPosition(), &focus, sizeof focus);
            D3DXMATRIX world, view, projection, identity;
            D3DXMatrixIdentity(&identity);
            std::memcpy(&app.ViewRotationTransform(), &identity, sizeof identity);
            D3DXMatrixTranslation(&world, -focus.x, -focus.y, -focus.z);
            const D3DXVECTOR3 eye(0, 0, app.CameraDistance()), target(0, 0, 0), up(0, 1, 0);
            D3DXMatrixLookAtLH(&view, &eye, &target, &up);
            D3DXMatrixPerspectiveFovLH(&projection, app.CameraFov() * .01745329238474369f, 1, .1f,
                                       1000);
            original.Skin(app, world);
            editor::EditorSession session;
            std::string reason;
            Check(session.ImportModel(path, reason), reason.c_str());
            // Native mode switches reload the registered camera. Apply the pending
            // input only after switching, as an editor gesture would.
            Check(session.SelectModel(255, reason), reason.c_str());
            const auto applyInput = [&](auto& doc, auto&) {
                doc.physics.playbackMode = 0;
                doc.view.groundGrid = doc.view.groundShadow = 0;
                doc.timeline.editMode = 2;
                doc.cameraState[0] = focus.x;
                doc.cameraState[1] = focus.y;
                doc.cameraState[2] = focus.z;
                doc.cameraState[3] = doc.cameraState[4] = 0;
                doc.cameraState[5] = app.CameraDistance();
                doc.envelope.settings.cameraFov = 30;
                doc.cameraPerspective = 0;
                doc.lightState = {.6f, .6f, .6f, -.5f, -1.f, .5f};
                return true;
            };
            Check(session.Edit("native frame input", applyInput, reason), reason.c_str());
            render::SceneRenderSources sources;
            sources.models = session.Assets().modelPaths;
            render::SceneFrameProducer producer;
            Check(producer.Load(session.Runtime(), sources, files, reason), reason.c_str());
            render::SceneRenderSettings settings;
            app.state.wireframeRenderingEnabled = wireframe;
            settings.display.wireframe = wireframe;
            settings.width = settings.height = settings.shadow.mapSize = kSize;
            settings.editorModel = 255;
            settings.postProcessCopy = false;
            StandardEffect effect(*original.renderer, fixture.device, 3);
            for (bool posed : {false, true}) {
                const unsigned dimension = posed ? 257 : 512;
                app.RenderWidth() = app.RenderHeight() = dimension;
                settings.width = settings.height = dimension;
                if (posed) {
                    const float position[]{.125f, .25f, -.125f},
                        rotation[]{0, .1f, 0, std::sqrt(.99f)};
                    std::memcpy(original.model->boneKeys[0].position, position, sizeof position);
                    std::memcpy(original.model->boneKeys[0].rotation, rotation, sizeof rotation);
                    const unsigned morph = pmx ? 2 : 1;
                    original.model->morphKeys[morph].value = .625f;
                    Check(session.SetBone(0, 0, {.125f, .25f, -.125f}, {0, .1f, 0, std::sqrt(.99f)},
                                          reason),
                          reason.c_str());
                    Check(session.Register({{editor::Track::Bone, 0, 0, 0}}, reason),
                          reason.c_str());
                    Check(session.SetMorph(0, morph, .625f, reason), reason.c_str());
                    Check(session.RegisterMorph(0, morph, reason), reason.c_str());
                    Check(session.Seek(0, reason), reason.c_str());
                    Check(session.Edit("native pending view", applyInput, reason), reason.c_str());
                    original.Pose();
                    original.Skin(app, world);
                }
                for (int mode : {1, 2}) {
                    app.ShadowMode() = settings.shadow.mode = mode;
                    const auto expected =
                        NativeFrame(fixture, original, effect, world, view, projection, dimension);
                    const std::string prefix =
                        std::string(pmx ? "pmx" : "pmd") + (translucent ? "-alpha" : "-opaque") +
                        (posed ? "-posed" : "-rest") + "-" + std::to_string(mode);
                    for (unsigned i = 0; i < 2; ++i)
                        SaveRenderBytes(output, prefix + (i ? "-native.f32" : "-native.rgba"),
                                        expected[i].bytes.data(), expected[i].bytes.size());
                    rhi::RenderFrame frame;
                    Check(producer.Build(session.Runtime(), settings, frame, reason),
                          reason.c_str());
                    for (auto backend :
                         {rhi::Backend::D3D11, rhi::Backend::D3D12, rhi::Backend::Vulkan}) {
                        if (!rhi::BackendAvailable(backend))
                            continue;
                        rhi::BackendOptions options;
                        options.backend = backend;
                        auto device =
                            rhi::CreateOffscreenBackendDevice(dimension, dimension, options);
                        Check(bool(device), device.reason.c_str());
                        rhi::SceneRenderer renderer(*device.device);
                        rhi::PreparedModelFrame prepared;
                        auto preparedResult = renderer.Prepare(1, frame, prepared);
                        Check(bool(preparedResult), preparedResult.reason.c_str());
                        rhi::CompletionToken completion;
                        Check(bool(renderer.Submit(prepared, completion)), "portable submit");
                        bool accepted = true;
                        for (unsigned i = 0; i < 2; ++i) {
                            rhi::AttachmentReadback actual;
                            Check(renderer.ReadTarget(1, i ? 3 : frame.finalColor, actual),
                                  "portable readback");
                            SaveRenderBytes(output,
                                            prefix + "-" + std::to_string(unsigned(backend)) +
                                                (i ? ".f32" : ".rgba"),
                                            actual.bytes.data(), actual.bytes.size());
                            const auto diff = shadow_acceptance::Compare(actual, expected[i]);
                            std::printf(
                                "%s size=%u mode=%d backend=%u %s changed=%zu max=%.9g outside=%zu coverage=%zu\n",
                                prefix.c_str(), dimension, mode, unsigned(backend),
                                i ? "shadow" : "color", diff.elements, diff.maximum,
                                diff.outsideTolerance, diff.coverageDifferences);
                            accepted &= diff.Accepted();
                        }
                        Check(accepted, "independent original frame comparison");
                        // A second submission consumes the same frozen frame and
                        // resident resources. Its two outputs must match exactly.
                        std::array<rhi::AttachmentReadback, 2> first;
                        for (unsigned i = 0; i < 2; ++i)
                            Check(renderer.ReadTarget(1, i ? 3 : frame.finalColor, first[i]),
                                  "first output for repeat");
                        Check(bool(renderer.Submit(prepared, completion)), "repeat submit");
                        for (unsigned i = 0; i < 2; ++i) {
                            rhi::AttachmentReadback repeated;
                            Check(renderer.ReadTarget(1, i ? 3 : frame.finalColor, repeated),
                                  "repeat output");
                            Check(repeated.bytes == first[i].bytes,
                                  "repeat output must be byte exact");
                        }
                        ++cases;
                    }
                    effect.ReleaseTargets();
                }
            }
        }
    Check(cases > 0, "at least one production backend executed");
    std::printf("%u independent original scene comparisons passed\n", cases);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
