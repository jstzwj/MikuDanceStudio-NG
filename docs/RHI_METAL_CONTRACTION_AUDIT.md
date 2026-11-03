# Metal position arithmetic audit

This is a source/binary audit, not a new GPU qualification. The strict composite
failures in the P4 evidence remain failures. No production renderer was changed.

The Windows P4 build's generated SPIR-V was disassembled with SPIRV-Tools from
Vulkan SDK 1.4.341.1. Both built-in vertex variants mark every matrix/vector
multiply result with `NoContraction`:

| Module | Matrix/vector products | Marked NoContraction | Total NoContraction decorations | SHA-256 |
| --- | --- | --- | --- | --- |
| model_vertex | 11 | 11 | 31 | `a97247716c67becaaf50349d254ae1c9a5119daede0fded3e6ddb1dac87919d6` |
| color_vertex | 7 | 7 | 16 | `3181f5f3448aa32e8c7de69064a12ac120a0c6997ab4cd12b87bd22ffa2aa030` |

The production Metal adapter already sets `fastMathEnabled = NO`. In the pinned
SPIRV-Cross revision `a0fba56c34a6700f1724bf9b751da5b488a3775c`, the MSL
matrix/vector multiply implementation selects its explicit arithmetic helper
when **either** `invariant_float_math` is enabled **or**
`has_legacy_nocontract(result_type, result_id)` is true. Scalar add, subtract
and multiply use the same condition. The common GLSL implementation determines
that condition from the SPIR-V floating-point contraction/transformation flags.

Crucially, the MSL matrix helper is **not** a separately rounded multiply then
add for every component. `spvFMulMatrixVector` and `spvFMulVectorMatrix` start
from zero and accumulate in reverse component order using explicit `fma`.
The scalar multiply helper uses `fma(l, r, 0)`. The helpers use
`[[clang::optnone]]`. Therefore `NoContraction` must not be interpreted as proof
of identical scalar arithmetic to a particular DX11 dot-product instruction.
Actual generated MSL and observed vertex results are needed to compare those
paths; this static audit does not establish which operation produces the
remaining failed pixels.

Consequently, merely observing that `invariant_float_math` uses its default
false value is insufficient evidence that the existing position transform is
emitted as unrestricted native matrix multiplication. A proposed change must
first compare the actual generated MSL and its arithmetic, then rerun the
unchanged GPU gates. This audit does not prove identical addition trees,
division, interpolation, depth quantization or rasterization between GPUs, and
does not justify a depth bias or a relaxed image comparison.

Reproduce the binary inspection from the repository root:

```text
spirv-dis out/p4/production/generated/spirv/model_vertex.spv -o out/p4-model-vertex.spvasm
spirv-dis out/p4/production/generated/spirv/color_vertex.spv -o out/p4-color-vertex.spvasm
```

Relevant pinned upstream sources are `spirv_msl.cpp` (arithmetic instruction
emission and helper selection) and `spirv_glsl.cpp`
(`CompilerGLSL::has_legacy_nocontract`). The production options are in
`src/render/rhi_metal.mm`.
