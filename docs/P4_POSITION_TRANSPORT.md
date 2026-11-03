# P4 default vertex position isolation

P4/C1 is still **not accepted**. This offscreen diagnostic narrows the known
strict WARP composite failure; it does not change rendering or image thresholds.

`editor_composite_render_test` can export its actual draw-prefix images and, for
the default model vertex shader, its actual vertex bytes and 1,152-byte vertex
constants. Exported vertices follow the draw's indexed/nonindexed primitive
order, including its start/count; unused resource vertices are excluded.

The explicitly built `d3d11_position_transport_probe` compiles the unchanged
production default HLSL with the production `D3DCOMPILE_IEEE_STRICTNESS` flag.
It creates the vertex shader and a pass-through stream-output shader from that
same bytecode, capturing `SV_POSITION` before viewport transformation and
rasterization. It adds no shader outputs or altered arithmetic. The original
line/triangle topology is retained. Stream-output statistics must report every
primitive written with no overflow, every component must be finite, and two
captures on each device must match byte for byte. Adapter identities and raw
float outputs are retained.

On the local NVIDIA TITAN Xp and Microsoft Basic Render Driver (WARP), both rest
and posed frames cover draws 1, 2, 3, 5, 6 and 7: 6, 90, 6, 6, 6 and 3 assembled
vertices, respectively. All **468 float components per frame** have identical
bits between the hardware and WARP devices. The exported input vertices and
constants are also identical between the full hardware and WARP runs.

The full hardware images continue to pass the frozen original DX9 reference.
WARP still fails: rest has 141 components outside tolerance and five coverage
differences; posed has 24 components outside tolerance and no coverage
differences. The first hardware/WARP prefix difference is draw 2 (grid lines)
in rest, and draw 5 (projected shadow) in posed. No production depth bias,
matrix rounding change, tolerance change or reference replacement is justified
by these observations.

This excludes default-VS position arithmetic as the source of the measured
hardware/WARP difference for these draws. It does **not** compare other vertex
outputs, interpolation, pixel shading, clipping/viewport rounding, line coverage,
depth/stencil or blending. Built-in self-shadow/outline, generic programs,
screen-space shaders, Metal, Vulkan, other drivers and other scenes remain
outside this measurement. In particular, it is not evidence that every stage
before the pixel shader is identical.

Reproduce from an x64 portable-editor build (no window is created):

```powershell
cmake --build out/p4 --config Release --target editor_composite_render_test d3d11_position_transport_probe
$env:MDS_GPU_BACKEND = 'd3d11'
$env:MDS_GPU_DRIVER = 'hardware'
out/p4/Release/editor_composite_render_test.exe tests/data/p4_composite res/assets out/p4-position-transport-inputs
out/p4/Release/d3d11_position_transport_probe.exe out/p4-position-transport-inputs/rest-positions.tsv out/p4-position-transport-rest
out/p4/Release/d3d11_position_transport_probe.exe out/p4-position-transport-inputs/posed-positions.tsv out/p4-position-transport-posed
$env:MDS_GPU_DRIVER = 'software'
out/p4/Release/editor_composite_render_test.exe tests/data/p4_composite res/assets out/p4-position-transport-warp
# The final command is expected to fail the unchanged strict image comparison.
```

Counts, input/output SHA-256 hashes, driver identities and prefix differences
are recorded in [versioned evidence](evidence/p4-position-transport.json). Raw
artifacts remain under the ignored `out/p4-position-transport-*` directories.
The probe is excluded from ordinary builds and CTest: a diagnostic pass cannot
substitute for the strict image gate.
