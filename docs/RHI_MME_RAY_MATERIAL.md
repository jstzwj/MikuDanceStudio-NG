# Ray material import and ordered material scripts

This batch imports the local, **unmodified** `Materials/material_2.0.fx` into
the shared shader contract. It does not enable the full Ray scene on RHI.
The first remaining root-source blocker is a computed non-const `static`
initializer (`mSSAOScale`, `ray.fx:61`); execution still requires additional shader, resource
and script semantics. Unsupported frames continue to use the current CPU
state through DX9 before any RHI scene submission.

## Source diagnostics and import scope

The v3 native requirements report distinguishes source import, wrapper export,
and actual VS/PS SM5 compilation. Source failures include the declaration,
technique/pass, token, expanded byte offset and file/line/column from `#line`.
Resource and execution support remain separate from successful compilation.

The source importer now supports literal constant arrays (1..256 elements),
internal structs without stage semantics, bare or bracketed sampler texture
references, explicit anisotropy requests and overloaded helpers. Stage entry
overloads are rejected. It emits a conservative reachable helper graph across
all declared pass entries and all overloads; unused sampler-parameter library
helpers need no invented texture/state binding. Anisotropic declarations must
provide an explicit `MaxAnisotropy` in 1..16; an inherited native value is not
guessed. A reachable untranslatable
sampler argument still fails. Immutable FX9 constants become `static const`
in modern HLSL so they do not create an implicit `$Globals` constant buffer.

The default portable sampling profile converts imported anisotropic filters
to trilinear while preserving explicit point filters, addressing and uploaded
mips. Native mode retains the requested anisotropy. The generic sampler
contract accepts anisotropic mip requests only with anisotropic minification
or magnification, matching the existing backend sampler representation.
The DX9 editor's filtering is unchanged.

## Material script contract

Selected object/object_ss techniques may execute up to eight shader passes.
Empty native techniques suppress their draw, including empty edge/shadow/zplot
techniques. Native SAS, rather than a second string parser, exports the selected
technique's command table. The initial material subset supports only:

- `RenderColorTarget0..3`, with no mip or cube-face selector;
- ordered `Pass` commands, with a maximum of 32 draws.

Clearing slot zero restores the calling draw's primary color attachment;
clearing another slot disables it. Outputs must be dense and unique. Shared
targets retain publisher identity, format and resolved viewport size. Feedback,
unknown targets, output-count mismatches and incompatible extents are rejected.

Frame assembly splits the pending host pass at each scripted object, preserving
object and pass order. It loads the caller's depth and each color attachment,
then resumes the host pass with Load semantics. It never invents a clear.
The surrounding scene script must initialize shared attachments before use.
A single-color scene capture remaps only the calling color attachment within
an existing material MRT group. Nested MRT captures remain unsupported.

Material states may inherit only the explicit supported CPU draw state.
Inherited alpha testing, stencil, partial color writes or unsupported blending
keep DX9 fallback. Explicit unsupported fixed state also fails. Scene Buffer
passes retain the stricter explicit-state requirement. This bounded inheritance
does not assert that an arbitrary Ray editor frame is supported.

## Public fixtures and gates

`LegacyMaterialMap.fx` and `LegacyMaterialMapScene.fx` are original CC0 fixtures
without alternate shader branches or portable annotations. The material uses
two ordered four-output geometry passes. The scene explicitly clears all seven
shared targets, captures the material, and combines the two groups. Signed and
greater-than-one values remain in RGBA16F targets. The public GPU tests require
both groups to cover pixels without overlapping; the hidden editor also checks
these masks on the native DX9 inputs before accepting its composite as an oracle.

`mme_material_script_test` uses generated real PMD/PMX inputs through the CPU
producer and actual texture preparation. It has independent paths:

- `null`: dependency/binding validation, missing initialization, feedback,
  stale handles, extent conflicts and atomic preparation failure.
- `<api>-compile`: compile and execute both material passes, compare all eight
  targets with DX11, verify strict final copy, repeated submission and device
  replacement. This path requires no DX9 device.
- `<api>`: run the native DX9 SAS technique against the same geometry and
  textures, then compare all eight raw targets. The oracle explicitly selects
  the portable trilinear profile for this comparison. It does not claim exact
  native-anisotropic parity.

Each GPU path covers opaque and translucent material values and widths
128 -> 163 -> 128. RGBA8 comparisons permit at most two code values; RGBA16F
comparisons permit at most one half ULP and reject nonfinite values. Copy,
repeat and replacement comparisons require byte equality.

The optional Ray gates load the original material and includes from
`MIKUDANCESTUDIO_RAY_TEST_EFFECT`'s directory. The public publisher initializes
its shared targets. This isolates the real material child; it does not execute
Ray's root, lighting, DefaultEffect chain or final composition. No Ray source,
model or texture bytes are copied into the repository.

The hidden editor gates additionally exercise public scripted materials through
production effect assignment, once-only updates, zero DX9 scene draws on RHI
frames, odd resize, switching, reconstruction and missing-publisher fallback.

## Validation on 2026-10-30

The x64 independent CPU/RHI scope passed all 51 gates, including ten public/Ray
material execution gates and DX12 debug/Vulkan validation variants. For every
one of the 120 material/viewport/opacity cases, all eight material attachments
and the combined FinalColor measured zero delta against DX11, including zero
half ULP on the two RGBA16F targets. Exact final copy, five repeat submissions
and device replacement retained byte equality. Limits were not widened.
Evidence: `out/mme-ray-material-x64-test.log` and `-details.log`.

Both architecture application builds and the diagnostics-off x64 product
build succeeded. All tests are offscreen or use never-shown HWNDs.

The corresponding x86 scope finished with 37 passes, 14 explicit Vulkan
runtime-HLSL/validation-capability skips and zero failures. Its 72 material
cases on DX11/DX12, including DX12 debug, also measured zero difference for
every intermediate and combined output. Evidence:
`out/mme-ray-material-x86-test.log` and `-details.log`.

The first recovered DX9 run passed x64 `mme_ray_material_dx11` against the
original Ray material child. It also exposed a public-oracle initialization
error: RHI used the attachment default Alpha of one while DX9 cleared to zero.
Shared MRT clears now use transparent black on both sides. The public UV clip
plane moves from .5 to .473 to avoid an exact pixel-center `clip(0)` decision
whose interpolated sign can differ between SM3 and SM5. Both ordered passes
and their coverage remain in the fixture; image limits are unchanged.

After these fixture changes, the focused independent x64 suite passed 11/11
gates. Its 120 material cases produced 1,080 attachment/final-color comparisons
with zero difference, including zero half ULP. The x86 rerun passed seven gates
with four explicit Vulkan runtime-HLSL/validation-capability skips and no failures.
Both architecture test builds succeeded. Evidence:
`out/mme-ray-material-resume-x64-test.log`, `-details.log`, and the corresponding
`out/mme-ray-material-resume-x86-test.log` / `-details.log`.

## RDP follow-up and native clip limitation

After the user restored the RDP session, both x64 public/Ray material DX11
native gates passed. They cover all eight raw attachments, generated PMD/PMX,
opaque/translucent material values, resize, repeat and replacement. RGBA8
differences were zero; RGBA16F stayed within one half ULP. Evidence:
`out/mme-ray-material-rdp-resume-material.log` and `-details.log`.

The editor's scripted-material composite initially still differed by 120 code
values. Temporary native readbacks localized this to the public transparent
shader's `clip(uv.x-.473)`: its target was empty before that draw, but afterward
covered the entire 1,803-pixel triangle, overlapping all 853 pixels in the
primary group. The UV attachment was interpolated normally, and the bound
native shader contained the subtract/add and `texkill` instructions. Target
restoration and composite texture bindings were correct. Disabling depth or
changing alpha test, filtering and shader-object recreation did not resolve it.
This is an observed native arithmetic-clip anomaly; its underlying cause is
not established. Evidence: `out/mme-ray-material-rdp-target-trace6.log`,
`out/mme-ray-material-rdp-target-trace7.log` and the state probe logs.

The public fixture now expresses the same inclusive half-plane as
`clip(uv.x>=.473 ? 1 : -1)`. It uses that expression on every backend, retaining
both material passes and the original image thresholds. The test additionally
requires both groups to have coverage with no overlap, and checks the native
editor inputs before accepting their composite. The original arithmetic form
is **not repaired or certified** by this fixture change. To reproduce it, replace
the transparent fixture's comparison with `clip(uv.x-.473)` and run
`ctest --test-dir build-x64/build -C Release -R '^editor_frame_dx11$' --output-on-failure`.
Temporary native tracing, forced states and readbacks were removed from
production code.

With the comparison fixture and production tracing removed, the x64
`editor_frame_dx11` and `editor_ray_fallback_dx11` gates passed for both PMD and
PMX. These exercise the earlier DefaultEffect/nested scenes, ordinary MRT/HDR,
the scripted material composite, odd resize, backend switching, reconstruction,
missing-publisher fallback, once-only updates and zero DX9 scene draws on RHI
frames. This confirms the tested fixture's composition; it does not execute a
complete Ray scene on RHI.

The attempted 29-gate x64 matrix finished with 15 passes and 14 failures:
all 11 independent gates, the two DX11 native gates and the two DX11 editor
gates passed. DX9 became unavailable again before the DX12/Vulkan native and
editor gates and both requirements gates could initialize. Errors were
`0x8876086C` / `0x8876086A`; the user identified RDP lock/disconnect as the
cause of earlier device interruptions. These failures remain failures, not
capability skips. Evidence: `out/mme-ray-material-rdp-x64-test.log` and
`-details.log`. Requirement files still dated 14:50 are old inventories;
they are not evidence of regeneration in this attempt.

After adding the explicit coverage assertions, the independent x64 suite
passed 11/11 gates: 120 cases, 1,080 attachment/final-color comparisons and
180 public coverage checks. Every comparison measured zero difference.
The x86 suite passed seven gates, with four explicit Vulkan capability skips
and no failures: 72 cases, 648 zero-difference comparisons and 108 public
coverage checks. Both architecture test builds succeeded. Evidence:
`out/mme-ray-material-rdp-coverage-x64-test.log`, `-details.log`, and the
corresponding `out/mme-ray-material-rdp-coverage-x86-test.log` / `-details.log`.

When DX9 became available again, the final x64 native matrix passed all 18
gates. All 120 public/Ray material cases and their 960 raw attachment
comparisons measured zero difference, including zero half ULP. All 180
hidden-editor image comparisons also measured zero RGBA difference. The six
editor gates cover all three backends with generated PMD/PMX, switching,
resize, device reconstruction, same-frame fallback and once-only updates.
The new native coverage assertions passed. DX12 debug and Vulkan validation
reported no errors. Both v3 requirement inventories were freshly regenerated;
each still reports 13 native pixel-sampler slots and the root import blocker
at `ray.fx:61`, `mSSAOScale`. Evidence:
`out/mme-ray-material-rdp-final-native-x64-test.log`, `-details.log`,
`-mme-ray-requirements.txt` and `-mme-ray-shared-requirements.txt`.

The final x86 native matrix finished with 14 passes, four explicit Vulkan
runtime-HLSL capability skips and zero failures. Its 72 native material cases
produced 576 zero-difference raw attachment comparisons. The 120 supported
editor image comparisons also measured zero difference; all six editor gates
passed, including safe fallback where Vulkan runtime shaders are unavailable.
That fallback is not Vulkan material execution. Both x86 v3 inventories were
freshly regenerated with the same 13-slot observation and root source blocker.
Evidence uses the corresponding `out/mme-ray-material-rdp-final-native-x86-*`
logs and inventory files. Together with the independent coverage suite, the
final focused scope is 29 passes on x64 and 21 passes/eight capability skips
on x86, with no remaining failures in these final runs.

These native results cover the bounded public scenes and isolated Ray material
child. They do not certify the original arithmetic clip expression or a complete
Ray scene on RHI. The earlier intermittent Vulkan/Ray crash observation also
remains open; see the preceding subeffect batch's stability note. This batch
makes no full-Ray or general FX9 compatibility claim.

## C3 root-effect baseline (2026-11-19)

The refreshed inventory uses the local `ray.fx` with SHA-256
`602f106f1a30cc3c77f27d27ffadd9a108228fc70a9a105c638f2176071408b4`.
Build `mme_ray_smoke_test` and `mme_offscreen_turn_test` with
`MIKUDANCESTUDIO_RAY_TEST_EFFECT` set to that file. Run
`ctest -C Release -R '^mme_ray_(shared_)?requirements$' --output-on-failure`
in the build directory. Both gates passed on a hidden Windows x64 DX9 HAL
device. They write `mme-ray-requirements.txt` and
`mme-ray-shared-requirements.txt` locally; Ray's sources and scene assets
remain external.

| Measurement | Result |
| --- | --- |
| Root script | 39 resource records, selected `DeferredLighting` technique, 31 successful `Draw=Buffer` calls; main target, depth and viewport restored |
| Shared resource turn | `MaterialMap` selected the expected child, with the native repeat-boundary and first-draw regression passing |
| Highest native pixel binding count | `DeferredLighting/ShadingTransparent` uses 13 bindings in slots 0–12, measured from the compiled FX9 shader |
| Strict importer, native-expanded root | `MME_legacy_static_initializer` at `ray.fx:61`, `mSSAOScale`. The initializer depends on live controller values |
| Cross-platform metadata import | `mds_effect_cli inspect` describes 170 parameters, 39 targets, 40 sampler declarations and 31 passes. Target names and pass declarations match the DX9 inventory in order |
| Portable instance initialization | `MME_runtime/mSSAOScale/default_reference_or_function` on the controller-derived static initializer |
| Shader specialization and sampler states | 23 passes carry FX9 compile arguments, beginning with `ShadowBlurX`. 18 sampler declarations have states outside the execution profile. Examples are `Gbuffer1Map` min/mag `NONE` and the black border on `BloomSamp1st` |

The public parser regressions cover nested default expressions, integer-vector
target dimensions, source sampler states and arguments to compiled pass entries.
The source inventory retains all declarations. Execution still checks sampler
states and the 16-slot layout limit. The current inventory distinguishes the
40 declarations across the effect from the maximum of 13 live bindings in a
single DX9 pass.

A generated DX9 fixture on NVIDIA TITAN Xp confirmed that changing an effect
parameter from 0 to 1 changes a static `lerp(0.25, 0.75, Control)` from
`0x404040` to `0xBFBFBF` in 8-bit RGB readback. Another generated fixture
applied min/mag `NONE` with either point or linear state inherited. The effect
set both states to 0 in each case. The sampled texel matched explicit point
in that fixture. Portable execution still requires qualification of `NONE`,
pass-local sampler allocation and compile arguments. The native root gate
covers an empty scene and the separate material turn. Root lighting and final
composition require a portable execution comparison with live controller inputs.

### Controller-derived shader initializers

Shader-private non-const statics now initialize in compiled HLSL from the
current draw's bound parameters. The existing host default and shared parameter
ABI keeps these values private. The public layout fixture changes its host
parameter after device replacement and checks alpha in every pixel. The
DX11 and DX12 WARP runs and hardware Vulkan on TITAN Xp passed this check
with the existing strict repeat, MRT and HDR comparisons. The local software
Vulkan gate could not start because the installed loader exposes no software
device.

The Ray instance now advances past `mSSAOScale`. Its next initializer
diagnostic is `MME_runtime/BloomScale/default_reference_or_function` on the
`static const` expression that reads the viewport aspect. The native-expanded
strict importer advances to the `FogMap` scene-target annotation. Both hidden
DX9 inventories still pass. Pass specialization, sampler allocation and
unsupported target/state semantics continue to block full root execution.
