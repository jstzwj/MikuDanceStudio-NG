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
