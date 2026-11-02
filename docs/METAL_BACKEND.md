# Native Metal backend

The Apple-only `MIKUDANCESTUDIO_RHI_METAL` option builds an Objective-C++ adapter
against system Metal. It does not load Vulkan or MoltenVK. Both adapters can be
built together for comparisons. The minimum deployment target is macOS 13.

Material, shadow, postprocess and generic effect programs keep their existing
HLSL descriptions and constant ABI. DXC produces SPIR-V; the pinned SPIRV-Cross
MSL compiler translates it before Metal compiles a native library. Generic
programs pass the same binding and stage-interface reflection as Vulkan. MSL
compilation disables fast math. The first profile exposes VS/PS, four constant
slots, sixteen textures/samplers, and four ordered color attachments. Uploaded
textures support explicit mip levels and sRGB; generated portable mip chains
use the shared CPU preparation. Render-target mip generation is explicit between
passes. RGBA8, R32F and RGBA16F support native-value readback. Negotiated depth/
stencil attachments do not advertise sampling or packed readback; exact D24S8
is available only where the device reports it.

All passes execute on one Metal command queue with tracked resource hazards.
Three reusable uniform arenas are bounded to 2 MiB each. Reusing an arena waits
for its own submission. Dynamic vertex/index buffers use the shared scene pool;
CPU writes reject resources referenced by uncompleted or unsubmitted commands.
Destroy invalidates the public handle immediately, while retained command-buffer
references keep native storage alive until completion. Logical target resets
retain upload storage, invalidate attachment contents and advance the submission
epoch. Device replacement uses a new identity and rebuilds from CPU snapshots.
Readback is an explicit ordered staging blit followed by completion; normal
offscreen submission does not wait for every frame.

The native acceptance build deliberately disables Vulkan. It runs shared generic
binding/matrix/16-slot tests, exact-copy and alpha/orientation postprocessing,
MRT/HDR native float readback, explicit mip generation, generated self-shadow,
240 animated buffer updates and generated PMD/PMX/PMM scenes. A dedicated resource
test covers cross-device handles, writes before submission, retention across
logical reset and destruction while commands still reference resources. Tests
run with Metal API validation enabled. Native GPU availability is required;
the backend explicitly rejects a software-driver request.

Compute, tessellation, MSAA/history transfer, depth sampling and the diagnostic
DX9 double-precision arithmetic mode are outside this implemented profile.
Unsupported effects continue to be rejected by the shared capability checks.

## Qt presentation and deployment

Qt's generic window lease supplies a `CAMetalLayer`, a health callback and its
ownership lifetime. The adapter negotiates a single-sample BGRA8 drawable and
uses the shared HLSL copy program to present the common RGBA8 frame output.
Color values and alpha are copied without blending or an additional transfer
function; the layer declares the sRGB display color space. Physical-pixel extent
comes from Qt's device pixel ratio. Resize advances the device epoch and rebuilds
targets; suspended/unavailable drawables are retried, while a destroyed host
requires a new surface. The layer lease survives a device moved out of its
factory bundle. Ordinary frames do not read back images. Explicit presentation
acceptance reads the actual retained drawable and converts BGRA to portable RGBA;
the layer is not marked framebuffer-only so this optional operation is legal.

`DefaultBackend()` selects Metal when it is built. The Qt renderer menu retains
other compiled backends and replaces the native viewport/resources when switching.
The same `EditorSession` and CPU physics state remain attached to the new viewport.
`qt_metal_present_test` checks drawable bytes against the shared final target,
readback counters, physical extents, resize and destruction order. The normal Qt
presentation test covers navigation, model unload/reload, device replacement,
detached/docked/fullscreen windows and the Metal/Vulkan menu transaction. Native
tests run only on explicitly isolated CI displays, including `QT_SCALE_FACTOR=2`.

The portable editor enables Metal on Apple platforms. Runtime packaging includes
DXC and SPIRV-Cross notices, with MoltenVK required only when Vulkan is enabled.
CI also builds a Metal-only package and runs its relocated production executable
without SDK search paths. Native offscreen acceptance on revision `84c1098`:
[41421014257](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41421014257).
Qt and deployment acceptance on revision `93799c7`:
[41427342227](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41427342227).
Windows passed its 31 P4 tests and hidden DX11/DX12 lifecycle checks; Linux passed
35 P4 tests and isolated X11/Wayland presentation. macOS 15 passed 30 P4 tests,
native Metal lifecycle and exact drawable checks at normal/2x scale, cross-backend
menu switching, and both dual-backend and Metal-only relocated packages.
The runner reports an Apple paravirtual device; physical GPU performance, Intel
Mac coverage, signing/notarization and complete P4/C1 editor parity remain separate
acceptance work.

Qt 6.8.3's OpenGL wrapper links the obsolete AGL framework even when the selected
SDK no longer contains it. The editor's CMake integration removes that dependency,
following [Qt's upstream removal](https://github.com/qt/qtbase/commit/cdb33c3d5621ce035ad6950c8e2268fe94b73de5).
OpenGL and all other Qt dependencies remain linked. This permits the pinned Qt
package to build against the macOS 26 SDK without changing renderer behavior.

Package testing on macOS 26 also caught an intermittent exit-time invalid free
in Qt 6.8.3's `AppleUnifiedLogger`, called by plugin cleanup (native crash report
in [41429609406](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41429609406)).
That logger keeps a static `QString` bundle identifier which can be destroyed
before the last plugin log. The macOS application installs a stderr message sink
before constructing `QApplication`; it uses no static Qt objects, remains valid
through process exit, and retains Qt's category filtering and fatal behavior.
Relocated Apple package tests require three successful process exits, with plugin
diagnostics and Metal API validation enabled. A failed exit remains fatal even
if its diagnostic debugger rerun succeeds.

Known comparison limitation: enabling `MTL_DEBUG_LAYER=1` for MoltenVK 1.4.2
triggered `MTLDebugDevice::notifyExternalReferencesNonZeroOnDealloc` during the
existing Qt Vulkan lifecycle test in [41421819905](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41421819905).
Native Metal offscreen, Qt lifecycle, drawable-byte and scale tests retain Metal
API validation. Vulkan and the menu transaction between both APIs run in a
separate process with the previous Vulkan validation configuration. No image
tolerance is changed; the MoltenVK/Metal-debug interaction remains open.
