# Independent original composite regression

All inputs here are public generated fixtures, not external models. `scene.pmx`
comes from `WriteComplexModel(path, false, true, 1)` in `scene_fixture.hpp`.
The `.x` and BMP are the generated `OriginalComposite` inputs in
`editor_scene_original_gpu_test.cpp`. No original executable is redistributed.

`rest.png` and `posed.png` losslessly encode the native RGBA readbacks from
`pmx-opaque-rest-1-native.rgba` and `pmx-opaque-posed-1-native.rgba` in the explicit
D3D9Ex HAL hardware profile (NVIDIA TITAN Xp). The original executable SHA-256 is
`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`.
Camera, pose and scene scalars are specified independently by the portable test.
These references were not produced by an RHI backend or corrected after capture.

The frozen original output covers two accessory orders, coplanar projected
shadows, grid/plane, background image, PMX SDEF/bone/Morph pose, self-shadow and
512/257 pixel dimensions. RGB tolerance is one code value, Alpha and coverage
are exact, repeated submissions are byte exact. Live original comparison also
checks floating shadow output separately; the frozen PNGs qualify color only.

This strict reference currently passes the Windows DX11, DX12 and Vulkan
hardware profiles. WARP fails independently (Alpha blend rounding and coplanar
depth/line ties); Metal and software Vulkan have not been qualified against this
capture. The `editor_composite_render` CTest has the `native-reference` label and
must be invoked explicitly with the intended backend/driver. It does not form
part of the software-device `p4` suite and does not accept P4/C1 by itself.
