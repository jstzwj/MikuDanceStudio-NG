# Qt editor development distributions

These packages contain the current NG editor. P4/C1 acceptance is still in
progress; a successful package build does not imply complete MMD parity.

The deployment follows Muffin's separate `dist` target and relocated smoke-test
pattern, using Qt's deployment API for this project's Qt 6.8.3 baseline.
Windows includes Qt DLLs, MSVC runtime and platform/multimedia/image plugins.
macOS uses an `.app` with Qt frameworks and plugins; native Metal is its default
renderer. A build that also enables Vulkan includes MoltenVK. Linux includes Qt
and non-system dependencies; the host supplies glibc, display/audio services and
the Vulkan loader/driver. The Linux baseline is Ubuntu 22.04 x64; macOS is 13+
arm64. Packages are development artifacts, unsigned on Windows and ad-hoc signed
on macOS.

Build the portable editor, then deploy:

```sh
cmake -S cmake/portable-editor -B out/build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/platform
cmake --build out/build --config Release --target dist --parallel 3
python tests/test_editor_distribution.py --dist out/dist
```

For the locally installed Windows SDKs, configure with:

```powershell
cmake -S cmake/portable-editor -B out/build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH="$PWD/out/qt/6.8.3/msvc2022_64" `
  -DMDS_DXC="$PWD/out/dxc-dist/dxc.exe" `
  -DMDS_DXC_RUNTIME_DIR="$PWD/out/dxc-dist"
cmake --build out/build --config Release --target dist --parallel 3
```

Run `out/dist/MikuDanceStudioNG.exe` on Windows. Copy the entire distribution,
including Qt DLLs, plugins, fonts and assets, when moving it elsewhere. `out/qt`,
`out/dxc-dist` and `out/editor-fonts` are reusable SDK/input caches, not extra
application builds. Historical qualification captures/logs belong in
`out/evidence`; their former paths are retained in a local relocation manifest.

The target writes `out/dist`; platform archives and SHA-256 files go to
`out/releases`. Build caches and test executables stay in `out/build` (older
P4 qualification builds used `out/p4`). The deployment path is independent of
the chosen build directory. Override `MDS_DIST_DIR` and `MDS_DIST_ARCHIVE_DIR`
at configure time when keeping multiple distributions. The target
replaces only its own marked output after installation succeeds. It does not
copy the Qt SDK or depend on a Qt directory in the user's PATH. The smoke test
relocates the package into a temporary directory, removes Qt/developer search
paths, opens generated PMM data and renders offscreen. No desktop window or
audio output is used.

Configure with `Qt6_ROOT` or `CMAKE_PREFIX_PATH` pointing to the desired SDK.
Install `requirements-build.txt` with pip, then run `python tools/setup_qt.py`
to install the pinned SDK used by CI. Linux also requires
the X11/Wayland development packages listed in `portable-editor.yml`, plus
`patchelf` for deployment. Vulkan builds require DXC and Vulkan headers. Native
Metal builds use DXC and the pinned static SPIRV-Cross MSL compiler; they do not
require Vulkan headers or MoltenVK. Set `MIKUDANCESTUDIO_RHI_VULKAN=OFF` for a
Metal-only macOS editor/package; Metal defaults on in the portable editor profile.
Use `tools/fetch_dxc.py --output out/dxc` and configure `MDS_DXC` to its executable;
`MDS_DXC_RUNTIME_DIR` can override the runtime distribution location. On macOS,
use the pinned DXC source build in CI. When also enabling Vulkan, set
`MDS_MOLTENVK_LIBRARY` to the output of `tools/fetch_moltenvk_probe.py`.
Runtime compiler libraries and their licenses
are included; the build machine's SDK paths are unnecessary at runtime.

CI publishes archives as workflow artifacts after build, regression and package
smoke tests. A version tag creates a draft GitHub Release from the same tested
artifacts. Compiler caches are partitioned by OS, architecture, compiler image
and Qt version; dependency downloads use their pinned revisions. CMake build
trees and runner/compiler profiles are never restored as caches.

Windows CI checks DX11/DX12 WARP presentation and bundles Vulkan, but does not
claim Vulkan device execution on runners without a Vulkan driver. Linux and
macOS execute the Vulkan path. macOS additionally tests native Metal offscreen
and Qt presentation at normal/2x scale, actual drawable pixels, window recreation
and switching between Metal/Vulkan. Packages with either backend check their
shipped DXC. The macOS job also builds and relocates a Metal-only package with
no MoltenVK library. Hosted Apple paravirtual GPU evidence does not establish
performance or release qualification on physical Intel/Apple Silicon devices.

Qt's standard dialog translations are bundled in addition to the editor's
embedded English, Japanese and Simplified Chinese catalogs. Runtime licenses
and source references are under `licenses/`.

The unmodified, OFL-licensed Noto Sans CJK 2.004 Regular collection is included
for hosts without Japanese/Chinese system fonts. CMake fetches its pinned source
revision and verifies SHA-256; offline builds can set `MDS_EDITOR_FONT_FILE` to
that same TTC. Installed original fonts keep priority. This fallback supplies
glyphs and is not a claim of identical Windows font metrics on other systems.
