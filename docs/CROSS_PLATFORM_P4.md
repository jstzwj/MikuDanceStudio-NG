# P4 implementation and acceptance

Status: **in progress**. P4 and C1 have not been accepted. The current work also
includes strict MMD v9.32 layout/behavior parity and multilingual UI, as requested
on 2026-11-01. These are acceptance requirements, not claims about this revision.

## Shared document and commands

The editor has a new PMM v2 topology writer. It rebuilds links/offsets and reparses
the result before publishing it. The existing byte-preserving writer and runtime
bridge remain available. Editing invalid links, invalid numeric values or expired
selections must leave the session and history unchanged.

`EditorSession` owns the editable document, immutable model/accessory resources,
selection and undo history. It supports current poses, key registration/deletion,
copy/paste, VMD/VPD import/export, playback, model/accessory loading and PMM saving.
The frontend supplies atomic file replacement. Save As rewrites resource paths
relative to the new file, and rejects names/paths that PMM cannot represent.
Unregistered current poses are retained through editing, saving and reopening.
Model resources are shared across undo snapshots; GPU objects are never stored in
history. V1 input is converted to typed V2 output for topology edits.

Local Windows x64 checks executed during implementation:

- `pmm_edit_test`: generated linked tracks, overwrite/delete, interpolation,
  regenerated offsets, cyclic links and atomic rejection.
- `editor_session`: generated PMD editing, unregistered pose preservation,
  register/copy/paste/delete, undo/redo, invalid command, VMD/VPD exchange,
  failed write, save/reopen, unloading and playback update count.
- Existing `scene_runtime` regression: passed after the runtime changes.
- Existing public V1/V2/PMX/IK/physics/SDEF PMM fixtures opened, saved as V2,
  reopened and evaluated by the editor test.

These checks do not establish original-MMD behavior parity. In particular,
selection/group rules, history grouping and limits, interpolation editing, input
behavior and all frame-line operations require further implementation/comparison.

## Remaining exit gates

| Gate | Current state |
|---|---|
| Shared editing and serialization | First local regression passed; full command/compatibility coverage pending |
| Qt original layout and multilingual interface | Initial three-platform command/language regression passed; full original parity pending |
| C1 scene rendering | Model outlines, texture morphs and screen history passed on three platforms; accessory self-shadow passed local DX9 comparisons on DX11/DX12/Vulkan; full C1 comparison pending |
| WAV and synchronization | Deterministic consuming sink passed 120s clock/jitter, loop, seek, EOF and failure checks; native audio output acceptance pending |
| Fixed-step image sequence export and cancellation | Generated 30fps sequence, strict repeat and cancellation passed on three platforms |
| Three-platform open/edit/play/save/reopen/export | Initial generated workflow passed; complete C1 workflow coverage pending |
| MMD v9.32 independent geometry, behavior and output comparison | Pending |
| Input, IME, DPI, font metrics and lifecycle acceptance | Native surface lifecycle passed on all platforms; full input/IME/DPI/font acceptance pending |
| CPU frame-production performance | Pending |

All local UI testing uses Qt `offscreen` or windows that are never shown. Actual
window presentation/interaction uses isolated runners. Language changes must not
reload documents, alter selection, consume undo history or advance simulation.
The initial catalogs are English, Japanese and Simplified Chinese. Original
English/Japanese labels retain their original wording; translations use stable
message IDs rather than business logic branches.

The first Qt application and offscreen tests build on local Windows x64 with Qt
6.8.3. Five `p4` tests passed: `qt_editor`, `qt_editor_render`, `pmm_edit`,
`wave_format`, and `editor_session`. Export uses a separate CPU runtime and RHI
device, warms up physics from frame zero, reuses the renderer through the sequence,
and atomically publishes a private staging directory. Audio decoding/positioning
is tested without playing sound; physical output synchronization is not yet
accepted. The Qt surface adapter and many original UI interactions still require
further validation. The initial interface is not a parity-complete editor.

## Executed three-platform gates

[Portable editor run 41278764228](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41278764228)
at `8fe8ead` passed all three jobs. Each job ran the five P4 regressions and an
explicit native Qt viewport lifecycle test. Windows uses hidden DX11 WARP;
Linux uses isolated Xvfb/X11 and Weston/Wayland with software Vulkan; macOS uses
Cocoa and MoltenVK. The native test covers resize, device replacement, surface
destruction/recreation, unload/undo and backend submission without CPU re-evaluation.
It does not establish screenshot or interaction parity with MMD.

The subsequent local material-morph fixture verifies separate additive and
multiplicative texture/sphere/toon pools, changed RGB, unchanged alpha and exact
image restoration after resetting the morphs. This is the built-in effect path
used with self-shadow enabled; it does not alter fixed-function mode semantics.

Panel controls now exercise light color's 1/256 conversion, shadow-distance
conversion, accessory position/rotation/opacity and model attachment. Collapsing
a panel changes PMM UI metadata without consuming history or evaluating physics.
The Qt offscreen test explicitly loads installed Windows fonts, since that QPA
has no native font database. Test artifacts are drawn to a QImage; no local
window is shown. Full glyph/font metrics comparison remains an exit gate.

Screen accessories now consume the preceding successfully submitted capture.
Building or repeating a prepared frame does not advance history. Explicit device
replacement and surface destruction reset it; sequence export renders capture
warmup frames before the first requested output. The generated `screen.x` fixture
checks recursive output, repeated submission and exact restoration after reset.
All five local P4 regressions passed after these changes.

[Run 41280483143](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41280483143)
at `e40bffb` passed Windows, Linux and macOS, including native Qt surface lifecycle.
It includes panel controls, texture morph pools and screen capture history.

The next local batch adds accessory shadow casting and FX9 diffuse shadow
receiving, sphere multiplication/addition and combined texture inputs. The
explicit `portable_scene_dx9_test` compiles the shipped `skin_effect_sm3.fx` on a
hidden DX9 device. DX11, DX12 and Vulkan each passed 20 cases: normal/forced
shadow, both shadow projection modes, four accessory material variants and PMX
texture morphs. Every image and natural shadow map must contain written pixels.
RGB differences were at most one code value; alpha, coverage and repeated
submissions passed the existing strict contracts. This checks shader execution
against DX9; a full independent scene-input oracle remains required.

WAV output now has an injectable device boundary. A deterministic sink consumes
the real decoded PCM without opening an audio device. The Qt regression covers
120 seconds with timer jitter, exact PCM offsets, loop restart, seek, stop,
WAV EOF handoff and output-start failure. The editor follows the processed audio
clock while active, and restarts the stream when playback loops. These tests do
not measure physical output latency. Six local P4 tests passed after this batch.

[Run 41281340260](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41281340260)
at `cb9800c` passed all three platforms, including accessory shadow rendering,
the silent audio transport and native viewport lifecycle.

Timeline work now preserves PMD/PMX display groups, synthetic display/IK and
facial rows, collapsed child-key summaries and PMM group flags. Folding does not
evaluate physics or consume undo history. Local Qt tests cover fold/expand,
Ctrl selection, collapsed-key selection, undo and save/reopen. The explicit
Windows `qt_timeline_gdi` test draws the original GDI rectangle/grid/atlas command
sequence into a memory DIB, without any window or desktop DC. All pixels in the
key-grid region match Qt exactly across 18 size/scroll/selection cases. Text,
physics-key icons and the complete timeline interaction model are not covered
by that pixel oracle and remain open requirements.

[Run 41281826086](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41281826086)
at `c943bdd` passed all three platforms, including display-group interaction,
PMM persistence and the Windows timeline GDI oracle.

Interpolation editing now supports four bone/six camera channels, the all-channel
consistency rule, independent curve clipboard, linear reset and live point drag.
A continuous drag creates one history entry; unchanged drags create none. The
shared command regression covers channel isolation, model-to-camera clipboard,
clamping, mixed selections, undo/redo and PMM reload. Qt offscreen input exercises
the actual controls. The Windows memory-DIB oracle matches all 128x128 pixels for
266 curve/highlight cases, including signed control bytes; ten independent GDI
hashes also gate the Qt raster on other systems. This does not close the broader
per-model versus camera history, input, font or dialog parity requirements.

Range-selection scopes now cover global/accessory tracks, all model keys,
all/selected bone and morph tracks, and named display rows. Native bulk scopes
replace marks in the addressed tracks; selected/named model scopes add marks.
The column button extends every selected column across the active model or global
tracks, independently of the playhead. Qt retains each loaded model's scope while
switching models/languages and centers the playhead using the original sidebar
formula. Generated core/Qt tests cover these distinctions without scene evaluation.

Frame-zero deletion now resets retained camera, bone, morph, display and accessory
keys, preserving the native per-track fields and successors, and immediately
re-evaluates the affected pose. Regression covers undo/redo, interpolation/FOV
preservation, successor playback and PMM reload. Light/shadow/gravity roots reset
when no unselected successors remain. Their native head-cut behavior with retained
detached records is still unsupported: the command rejects the entire edit with
`native-root-reset-detached-keys-unsupported` rather than silently removing those
records. Detached-record identity/topology and automatic bone-curve rebuilding
remain requirements for complete deletion parity.

[Run 41282592148](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41282592148)
passed interpolation editing and the frozen GDI curve hashes on all platforms.
The range-scope and root-reset batches also passed all platforms in
[41283051387](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41283051387) and
[41283316500](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41283316500).

CPU evaluation now reuses private morph/skin/skeleton storage and exchanges two
owned publication snapshots. Rejection invalidates only scratch storage; the last
published snapshot remains valid. A generated 4,096-vertex test compares 120 frames
exactly with fresh evaluations and counts zero C++ heap allocations after warmup.
The same local test on Sour Miku (139,692 vertices) counted zero warm allocations,
versus 2,880 allocations / 4,522,563,480 bytes on the fresh path; this run averaged
18.52 versus 23.27 ms per evaluation. These are evaluation-only measurements, not
editor frame timing or a physical-GPU performance claim.

The Qt viewport reuses its immutable prepared CPU frame while revision, CPU
publication count and dimensions remain unchanged. Resize, edit, seek, resource
replacement, device reset and surface recreation invalidate it. Screen capture
history deliberately continues building every submitted frame. The hidden Windows
native test passed DX11/DX12/Vulkan, asserting repeated static submissions do not
rebuild the frame, and checking history/lifecycle invalidation. Eight local
core/Qt/render regressions passed; full-frame performance remains open.

[Run 41283882956](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41283882956)
passed CPU evaluation reuse and static viewport caching on all three platforms.
The legacy Windows x64 editor also built successfully; it was not launched.

The next material batch preserves all four additional PMX UV sets through CPU
publication and GPU buffers. Explicit layouts and generic input descriptions keep
the float4 components; fixed material and self-shadow programs sample UV1. Local
DX11/DX12/Vulkan each passed 24 generated cases covering UV1-4, subtexture-only
and combined base/subtexture materials, three shadow modes, UV morphs, byte-exact
repeat submission and restoration. The geometry and replay validators and existing
Qt render regression also passed.

The hidden FX9 oracle passed UV1-only cases, then exposed the original combined
subtexture shader's shadow-alpha multiplication by Toon alpha. The shared shader
now preserves that source behavior. DX9 became unavailable with 0x8876086C before
the corrected comparison could run; that comparison remains pending, not passed.
The fixture intentionally exercises separate sphere/texture tint pools and UV
morphs. Original inheritance of `spadd` between materials/frames remains a separate
state-parity requirement; these single-material fixtures explicitly initialize it.

The UV fixed-material regression also compares a nonidentity full float4
row-vector transform against independently transformed CPU UVs, including Z/W.
All three local backends passed exact image equality for this comparison.
