# Raw FX execution fixtures

These public fixtures are dedicated to the public domain under CC0-1.0.
They use ordinary FX9 declarations and SAS scripts, without an NG profile or alternate shader branch.

- `LayoutMaterial.fx` exercises constant packing, host-writable const values, matrices, bool/int,
  one-element arrays, all sixteen sampler slots and portable trilinear conversion.
- `Material.fx`, `Child.fx`, `ReaderMaterial.fx`, `OuterScene.fx`, `InnerScene.fx` extend the
  project's CPU scheduling fixtures with complete shaders and explicit render states.
  The child and reader deliberately sample nonzero mip levels.
- `MrtScene.fx` derives from `../portable/LegacyMrtScene.fx`. Its downsampling coordinates lie
  inside texels so point-filter ties do not obscure layout/numeric regressions. It uses the
  unchanged `../portable/LegacyMrtMaterial.fx` and exercises signed HDR, MRT and relative targets.

See [the execution contract](../../../docs/MME_SHADER_EXECUTION.md) for scope and tolerances.
