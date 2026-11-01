#include "scene_render_fixture.hpp"
#include "mikudancestudio/background_placement.hpp"
#include "../src/render/background_quad.hpp"

int wmain(int argc, wchar_t** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc != 3 || std::wcscmp(argv[1], L"--original"))
        return 2;
    OriginalPhysicsModule original(argv[2]);
    Fixture fixture;
    if (!fixture.ready)
        return 77;
    auto& app = *fixture.app;
    Com<IDirect3DVertexBuffer9> vertices;
    Hr(fixture.device->CreateVertexBuffer(kBackgroundQuadBytes, 0, kTexturedScreenVertexFvf,
                                          D3DPOOL_MANAGED, vertices.Out(), nullptr),
       "quad buffer");
#if defined(_M_X64)
    std::memcpy(&app.state.pad129, &vertices.p, sizeof vertices.p);
#else
    app.state.reservedPictureOverlayVertices = reinterpret_cast<std::uintptr_t>(vertices.p);
#endif
    unsigned count = 0;
    std::uint64_t hash = 14695981039346656037ull;
    for (int mode : {0, 1, 2})
        for (int width : {127, 511, 1280, 1920})
            for (float scale : {.125f, 1.f, 2.75f})
                for (int x : {-257, 0, 91}) {
                    render::BackgroundPlacement p{x, -x / 3, 79, 43, scale};
                    render::BackgroundView view;
                    view.left = 301;
                    view.top = 47;
                    view.width = width;
                    view.height = width * 3 / 4;
                    view.scale = render::BackgroundViewScale(width);
                    view.recording = mode != 0;
                    view.multisampled = mode == 1;
                    view.renderWidth = 1001;
                    view.surfaceWidth = 987;
                    view.surfaceHeight = 633;
                    app.ViewportRect() = {view.left, view.top, view.left + view.width,
                                          view.top + view.height};
                    app.Renderer()->viewScale = view.scale;
                    app.Renderer()->screenWidth = view.surfaceWidth;
                    app.Renderer()->screenHeight = view.surfaceHeight;
                    app.Renderer()->multisampleAvailable = view.multisampled;
                    app.state.renderW = view.renderWidth;
                    app.RecordingWindow() = view.recording ? reinterpret_cast<HWND>(1) : nullptr;
                    app.PictureWidth() = p.width;
                    app.PictureHeight() = p.height;
                    app.PictureScale() = p.scale;
                    app.PictureOffsetX() = p.x;
                    app.PictureOffsetY() = p.y;
#if defined(_M_X64)
                    // Original image loader calls this tail at RVA 68C78.
                    original.Function<void (*)(MMDApp*)>(0x68CB0)(&app);
#else
                    original.Function<void(__thiscall*)(MMDApp*)>(0x17130)(&app);
#endif
                    void* native = nullptr;
                    Hr(vertices->Lock(0, kBackgroundQuadBytes, &native, D3DLOCK_READONLY),
                       "read quad");
                    std::array<TexturedScreenVertex, 6> expected;
                    std::memcpy(expected.data(), native, sizeof expected);
                    for (std::size_t n = 0; n < sizeof expected; ++n)
                        hash =
                            (hash ^ static_cast<const std::uint8_t*>(native)[n]) * 1099511628211ull;
                    Hr(vertices->Unlock(), "unlock quad");
#if defined(_M_X64)
                    const auto rect = render::PlaceBackground(p, view);
#else
                    const auto rect = render::PlaceBackgroundX86(p, view);
#endif
                    std::array<TexturedScreenVertex, 6> actual;
                    WriteBackgroundQuad(actual.data(), rect.left, rect.top, rect.right,
                                        rect.bottom);
                    if (std::memcmp(actual.data(), expected.data(), sizeof actual) != 0) {
                        std::fprintf(stderr, "mode=%d width=%d scale=%g x=%d\n", mode, width, scale,
                                     x);
                        for (unsigned n = 0; n < 6; ++n)
                            std::fprintf(stderr, "%u actual=(%.9g,%.9g) native=(%.9g,%.9g)\n", n,
                                         actual[n].x, actual[n].y, expected[n].x, expected[n].y);
                    }
                    Check(std::memcmp(actual.data(), expected.data(), sizeof actual) == 0,
                          "shared background rectangle differs from original MMD");
                    ++count;
                }
    app.RecordingWindow() = nullptr;
    std::printf("Original MMD background geometry: %u exact cases, no displayed windows\n", count);
    std::printf("Native quad FNV1a64: %016llx\n", static_cast<unsigned long long>(hash));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
