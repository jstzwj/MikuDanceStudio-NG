# Qt editor development distributions

These packages contain the current NG editor. P4/C1 acceptance is still in
progress; a successful package build does not imply complete MMD parity.

The deployment follows Muffin's separate `dist` target and relocated smoke-test
pattern, using Qt's deployment API for this project's Qt 6.8.3 baseline.
Windows includes Qt DLLs, MSVC runtime and platform/multimedia/image plugins.
macOS uses an `.app` with Qt frameworks, plugins and MoltenVK. Linux includes Qt
and non-system dependencies; the host supplies glibc, display/audio services and
the Vulkan loader/driver. The Linux baseline is Ubuntu 22.04 x64; macOS is 13+
arm64. Packages are development artifacts, unsigned on Windows and ad-hoc signed
on macOS.

Build the portable editor, then deploy:

```sh
cmake --build out/p4 --config Release --target dist --parallel 3
python tests/test_editor_distribution.py --dist out/p4/dist
```

The target writes `out/p4/dist`, a platform archive and its SHA-256 file. It
replaces only its own marked output after installation succeeds. It does not
copy the Qt SDK or depend on a Qt directory in the user's PATH. The smoke test
relocates the package into a temporary directory, removes Qt/developer search
paths, opens generated PMM data and renders offscreen. No desktop window or
audio output is used.

Configure with `Qt6_ROOT` or `CMAKE_PREFIX_PATH` pointing to the desired SDK.
Install `requirements-build.txt` with pip, then run `python tools/setup_qt.py`
to install the pinned SDK used by CI. Linux also requires
the X11/Wayland development packages listed in `portable-editor.yml`, plus
`patchelf` for deployment. Vulkan builds require DXC and Vulkan headers.
Use `tools/fetch_dxc.py --output out/dxc` and configure `MDS_DXC` to its executable;
`MDS_DXC_RUNTIME_DIR` can override the runtime distribution location. On macOS,
use the pinned DXC source build in CI and set `MDS_MOLTENVK_LIBRARY` to the output
of `tools/fetch_moltenvk_probe.py`. Runtime compiler libraries and their licenses
are included; the build machine's SDK paths are unnecessary at runtime.

CI publishes archives as workflow artifacts after build, regression and package
smoke tests. A version tag creates a draft GitHub Release from the same tested
artifacts. Compiler caches are partitioned by OS, architecture, compiler image
and Qt version; dependency downloads use their pinned revisions. CMake build
trees and runner/compiler profiles are never restored as caches.

Qt's standard dialog translations are bundled in addition to the editor's
embedded English, Japanese and Simplified Chinese catalogs. Runtime licenses
and source references are under `licenses/`.
