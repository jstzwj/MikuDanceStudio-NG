# Portable packages

NG desktop distributions are self-contained application folders, without an
installer. Windows includes Qt DLLs/plugins, the MSVC runtime, translations,
fonts and DXC. macOS contains an `.app` with Qt frameworks/plugins, native Metal
and DXC; enabling Vulkan also bundles MoltenVK. Linux bundles Qt and compiler
libraries; the host supplies compatible glibc, X11/Wayland/audio services and a
Vulkan loader/driver. Runtime notices and licenses accompany every package.

Android uses a signed APK with Qt and built-in shaders; it has no external
runtime compiler. Import a complete workspace directory with the Android file
menu before opening PMM/models so relative textures, motions and model paths
remain intact. The initial Android UI targets landscape tablets and requires
Android 13+ / Vulkan 1.3. This is a separate deployment profile, not the Linux
desktop profile renamed to Android.

The [release guide](RELEASES.md) is authoritative for target platforms,
qualification requirements, signing and publication. P4 was accepted for
progression with physical mixed-DPI testing deferred; P5/MME remains incomplete.
A package build is not a claim of full MMD/MME/Ray compatibility.

## Build and deploy a desktop package

Install the pinned Qt 6.8.3 SDK and modules using `requirements-build.txt` and
`python tools/setup_qt.py`. Windows defaults to the native x64/ARM64 Qt SDK;
Linux uses gcc_64 and macOS clang_64. Configure `Qt6_ROOT` or `CMAKE_PREFIX_PATH`
when using another SDK installation. Qt Linguist tools and matching Qt base
translations are required. The application provides 15 language catalogs; see
[translation maintenance](../translations/README.md).

```sh
cmake -S cmake/portable-editor -B out/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/platform
cmake --build out/build --config Release --parallel 3
ctest --test-dir out/build -C Release -L 'p4|p5' --output-on-failure
cmake --build out/build --config Release --target dist --parallel 3
python tests/test_editor_distribution.py --dist out/dist
```

The local Windows x64 SDK example is:

```powershell
cmake -S cmake/portable-editor -B out/build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD/out/qt/6.8.3/msvc2022_64" `
  -DMDS_DXC="$PWD/out/dxc-dist/dxc.exe" `
  -DMDS_DXC_RUNTIME_DIR="$PWD/out/dxc-dist"
cmake --build out/build --config Release --target dist --parallel 3
python tests/test_editor_distribution.py --archive out/releases/MikuDanceStudio-NG-v0.0.1-windows-x64-portable.zip
```

Run `out/dist/MikuDanceStudioNG.exe`, `out/dist/MikuDanceStudioNG.app`, or
`out/dist/bin/MikuDanceStudioNG`. Copy the **entire folder** when relocating it.
The `dist` target replaces only its own marked output after a complete install.
It writes archives/checksums to `out/releases`; configure `MDS_DIST_DIR` and
`MDS_DIST_ARCHIVE_DIR` to keep multiple independent packages.

Local and branch packages are explicitly development builds. Release jobs
configure `MDS_RELEASE_TAG=vVERSION` and require HEAD at that exact tag with no
tracked source modifications. `build-info.json` records the product, version,
full source commit, actual binary architecture, Qt and compiled backends.
Archive names come from the executable header, not the packaging host CPU.

## Renderer/tool inputs

Vulkan requires Vulkan headers, host DXC and spirv-val at build time. Use
`tools/fetch_dxc.py --output out/dxc` for the checksum-pinned Windows/Linux
compiler; Windows supports native x64 and ARM64. macOS uses the pinned DXC
source build in CI. `MDS_DXC_RUNTIME_DIR` selects the matching runtime bundle.
Native Metal builds share HLSL through DXC and the pinned SPIRV-Cross MSL compiler.
Set `MIKUDANCESTUDIO_RHI_VULKAN=OFF` for a Metal-only macOS package. When enabling
Vulkan on macOS, set `MDS_MOLTENVK_LIBRARY` to the pinned MoltenVK distribution.

Linux build dependencies are listed in `portable-editor.yml`; deployment needs
`patchelf`. Windows CI exercises DX11/DX12 WARP; a Windows runner without a Vulkan
driver does not establish Vulkan device execution. Linux exercises software
Vulkan; macOS exercises native Metal and MoltenVK. Hosted evidence does not
establish performance on every physical GPU.

The unmodified OFL-licensed Noto Sans CJK 2.004 TTC is downloaded with a verified
SHA-256 and bundled with its license. Use `MDS_EDITOR_FONT_FILE` for offline
builds of the same qualified TTC. Original installed fonts retain priority;
fallback glyph differences follow the accepted P4 policy.

## Android reproduction

Use a Linux x64 build host, Java 17, Android SDK 35 / build-tools 35.0.0 and
NDK 26.1.10909125. `python tools/setup_qt.py --target android` installs matching
host tools and ARM64/x86_64 Qt SDKs. The workflow's `qt-cmake` invocation builds
one multi-ABI APK from the same sources, forwards host shader tools explicitly,
and embeds fonts, original assets, translations and licenses. The app's private
filesystem staging bridges Qt resources/document providers to the CPU core.

`android.yml` documents the exact configure/build, signing and emulator commands.
`tools/package_android.py` checks the manifest, signs with Android build-tools,
verifies signatures and writes the APK checksum. The emulator gate installs
that exact APK, checks PMM/15 languages/offscreen GPU output, then native
presentation, window/device rebuilding and background/resume. Desktop tests
stay offscreen/hidden; emulator interaction is restricted to its isolated CI
device and never touches the developer's desktop.
