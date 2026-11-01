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

The editor's initial global document undo stack has been replaced by native
per-model bone history, with 30 entries per model. Undo does not undo loading,
camera/Morph edits or panel/selection changes. Bone key edits retain the previous
pose and edit markers; frame changes capture pending poses independently for
each model. Pose-with-frame undo restores its recorded cursor, while its redo
restores only the displaced pose. Repeated cycles retain the original undo
payload. Failed edits preserve both stacks, and unloading expires only that
model's history.

The explicit Windows x64 `editor_history_oracle_test <original.exe> <fixture>`
maps the hash-verified original without its entrypoint and calls its actual
undo/redo and pre-frame pose snapshot routines. It passed 123 exact pose, edit
marker, root-key and cursor checkpoints: ring wrap, 30-entry traversal, repeated
pose-with-frame cycles and repeated root registration undo/redo. Fixture input
snapshots are constructed independently; this is a kernel comparison, not an
original UI event-loop or all timeline-topology acceptance. No window is created.
The portable regression additionally checks model isolation, camera/Morph
preservation, selection, failed edits, gesture grouping and unload invalidation.
Touched-key identity, detached records and chained timeline operations remain
part of the broader timeline gate.

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

The hidden FX9 oracle exposed the original combined subtexture shader's
shadow-alpha multiplication by Toon alpha. The shared shader now preserves that
source behavior. After DX9 became available again, DX11/DX12/Vulkan each passed
39 independent FX9 cases: the existing accessory/material cases, UV1/UV4-only and
combined subtextures with UV/tint morphs, and three consecutive material-history
frames. RGB differences were at most one byte; alpha and coverage stayed exact.
Depth differences were zero in this run. The history oracle preserves the FX9
effect's native `spadd` value rather than supplying the prepared value.

The UV fixed-material regression also compares a nonidentity full float4
row-vector transform against independently transformed CPU UVs, including Z/W.
All three local backends passed exact image equality for this comparison.

[Run 41284928965](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41284928965)
at `31bab6d` passed all three platforms, including the additional UV/matrix gate.

Stock-effect material history now resolves `spadd` in final model/accessory
render order. Only successful submission advances it; repeated builds and failed
builds cannot. Asset reload retains it; explicit device/surface replacement resets
it. A changed inherited value invalidates the static frame once, then stable
frames reuse it. The local offscreen and hidden native tests passed all three
backends for first/next/stable frame behavior, reload, reset and display-order
changes. Image export warms up preceding material-history frames; exporting a
later range reproduces the same image as that range of a full sequence. This is
separate from MME history and does not close the remaining P4 exit gates.


[Run 41285421759](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41285421759)
at `0dafcea` passed stock-effect history, export range and viewport invalidation
checks on all three platforms.

Background-image placement now follows the original x64 single-precision
operation order. The previous Windows port had carried over the x86 double
formula. A hidden DX9 oracle invokes the original x64 quad writer and compares
all 168 vertex bytes across 108 offsets, zooms, viewport sizes and recording
branches. All cases passed exactly; a frozen digest of the original output
also gates the portable helper without loading the reference executable.
This validates placement only; image loading, filtering and complete background
interaction are still being integrated into the Qt editor.


The initial geometry CI exposed a leftover D3D9 include in the screen-vertex
transport header. That dependency is removed; the fixed numeric layout is shared
without Windows headers. [Run 41286817301](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41286817301)
at `49a1445` passed the native-derived geometry digest and all editor gates on
Windows, Linux and macOS.

Background texture preparation now reproduces x64 D3DX9_43 triangle/dither
arithmetic: float coverage coefficients per source texel, the native small-weight
cutoff, and joint XY accumulation in source order. Normalized gather weights or
two separable passes were mathematically similar but rounded differently. A
hidden DX9 test passed exact RGBA comparison for 22 generated cases: transparency,
alpha zero/one, deterministic noise, single rows/columns, up/downsampling,
unchanged axes and identity copies. Their native output digests gate the portable
implementation. The earlier public 106x106 constant/impulse counterexample also
now matches exactly at 128x128. These results cover RGBA8 preparation against the
x64 runtime, not all codecs, compressed textures, x86 arithmetic or complete
background scene rendering.


[Run 41287592528](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41287592528)
at `336a055` passed the 22 frozen native texture-preparation cases and existing
P4 gates on Windows, Linux and macOS.

The editor now loads BMP/PNG/true-color TGA background pictures, retains them
while hidden, saves relative references and reloads them independently of the
visibility flag. Shared frame production uses the native placement, 1024-square
texture and full box mip chain. The hidden D3DX test compares all mip levels of
all 22 fixtures exactly. Menus use the original English/Japanese labels and the
Simplified Chinese catalog; language changes preserve the document and picture.

The explicit `portable_background_dx9_test res/assets <backend>` compares the
final frame against native fixed-function DX9, with independently D3DX-created
textures and mip levels. DX11, DX12 and Vulkan each passed 36 cases exactly:
three generated alpha/noise textures, three viewport widths, offset/scaled
placement and initial/subsequent frames. Both sides use the approved portable
trilinear profile. The background alpha test explicitly uses the previously
measured 12-fractional-bit truncation before byte conversion; nearest rounding
alone failed transparent-edge coverage. This policy is scoped to background
quads, not silently applied to the existing material path. The result is measured
compatibility for this device/profile, not proof of all DX9 hardware behavior.

Successful submission advances initial blend state; failed/repeated preparation
does not. Resource reload retains it and device replacement resets it. Export
warms prior frames so later-range output matches a full sequence. Remaining image
codecs, background interaction, native undo semantics, and the broader P4 gates
above remain open.

Local command/language/save/reopen tests passed, and offscreen render/export
regressions passed separately on DX11, DX12 and Vulkan. The never-shown Qt native
viewport test passed all three backends, including hide/show, retained resource
versions and device replacement. The NG executable built successfully; it was
not launched on the desktop.


Pose copy/paste is now a shared editor command rather than a whole-model VPD
round trip. Copy retains only selected bones in model order; an empty selection
preserves the previous clipboard. Ordinary paste selects exact-name targets.
Mirroring resolves the exact source name, then searches CP932 side markers
anywhere and compares the opposite suffix, preserving original first-match and
fallback behavior. It negates position X and quaternion Y/Z and retains the
separate pose-edited/selected flags. The Windows adapter uses the same name
resolver. Qt now dispatches reverse paste and gates its availability.

The generated eight-bone regression passed selected-only copy, duplicate/empty
selection, prefix-independent suffix matching, unselected-bone preservation,
model switching and missing targets. Existing Windows clipboard/reset/undo
regression and the Qt offscreen command test also passed. This batch fixes
clipboard semantics; the broader history scope/depth and native reset semantics
in the portable editor remain separate pending work.


[Run 41289042076](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41289042076)
at `633e584` passed background integration on all three platforms.

Selected-bone reset now shares the native arithmetic with the Windows adapter.
Root X/Z and its edit marker survive; parented bones reset to identity; other
unparented bones transform their rest position by their own quaternion, subtract
rest and add the current root translation, then take the root quaternion.
Selection order is normalized to model order. The portable regression matches
the frozen original x64 float bits and checks root/child/reset/undo behavior;
the Windows regression independently supplies D3DX matrices to the same kernel.
Both passed locally, along with the Qt reset-button test and application build.
The x86 adapter retains its separate double evaluation order.

[Run 41290806720](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41290806720)
at `cae4dd6` passed the per-model history regressions on Windows, macOS and Linux.

Bone picking and box selection now share their algorithm with the Windows
adapter. Clicks use the strict eight-pixel radius, signed bone-type eligibility,
physics-mode restrictions, overlapping-marker cycling and Shift toggling. Box
selection retains its distinct eligibility and strict-edge rules; finishing a
box applies the native active-bone rules. Selection and edit-mode changes update
PMM metadata without evaluating physics, changing document revision or consuming
undo history. Switching models restores each model's selection. Unregistered
selection uses the pose-edited marker instead of selecting every bone.

The portable regression covers all 256 type bytes, physics modes, boundary and
overlap cases, active-bone finalization, model isolation and expired inputs. The
Qt test sends mouse events to a never-shown viewport and exercises click, Shift,
box and unregistered selection. All 14 local P4 tests passed. The Windows picking
regression and application build also passed. Projection currently follows the
shared producer's camera; independent native editor-camera comparison, visible
bone overlays and transform tools remain open P4 work.

[Run 41293720992](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41293720992)
at `6fb8d2a` passed the selection batch on Windows, macOS and Linux.

Viewport right/middle dragging and wheel input now use the Windows pump's shared
arithmetic. Model Shift-right drag pans the independent view offsets, Ctrl-right
drag changes distance, and ordinary right drag subtracts yaw/pitch. Camera-mode
middle drag follows either the rotated free camera basis or the parented
depth/FOV rule. Modifier precedence, signed wheel payloads and the followed-model
gate are explicit. Current camera changes preserve the runtime, published model
snapshots, physics world and active playback instead of rebuilding the scene.

Editor view production now distinguishes model reference pivots from the timeline
camera. Model-center/active-bone pivots, independent view offsets, transposed
parent rotation, suppression and the editor orthographic eye enter the shared
RHI input contract. Model/active-bone/playback changes also invalidate the
viewport cache. Export retains its existing explicit timeline convention.

The headless navigation regression checks modifier branches, parent depth
boundaries, follow gates, invalid values, history isolation and zero scene
updates. Qt offscreen tests send actual right-drag and wheel events. The explicit
Windows `editor_view_test <fixture> --d3dx` additionally passed 48 independent
D3DX rotation/free-pan cases within the fixed absolute `2e-5` matrix/position
tolerance; ordinary geometry assertions run on every platform. This is a math
comparison, not an original executable/event-loop or full scene-output oracle.
Saved attachment-basis transitions, camera application during model-mode
playback, view presets, bone overlays and transform tools remain open.

All 16 local P4 tests passed after this change. The never-shown native Qt viewport
also passed on DX11, DX12 and Vulkan, including cached navigation frames, model
switching, device replacement and preservation of the same CPU snapshot. The
application built; it was not launched on the desktop.

The rebuilt Windows `model_viewport_test`, `bone_pick_test` and
`input_sequence_test` also passed, as did the rebuilt portable `scene_runtime`
regression after sharing camera-center selection with the physics host.

[Run 41294898401](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41294898401)
at `ccfa66b` passed the navigation batch on Windows, macOS and Linux.

The next camera batch adds the original stored numpad angles, model/camera
presets, playback/repeat gates and camera-track reload at the current frame.
Model-mode NumPad 0 preserves unregistered model poses and the published CPU
snapshot. Attached-camera frame preparation publishes its eye offsets/distance;
free or suppressed tracking clears the saved attachment basis as the native
frame setup does. These changes do not evaluate the scene or consume history.

Qt pose readouts and numeric edits now address the active bone independently of
the multiselection order. No active bone displays the original `-----` values;
positions use two decimals and rotations one. The offscreen test exercises
these controls and sends keypad events to a never-shown viewport.

All 16 local P4 tests passed, as did hidden native viewport presentation on
DX11, DX12 and Vulkan and the 48-case D3DX camera math comparison. The application
built without being launched. Camera reference-mode compensation, follow-toggle
refresh semantics, event-dependent FOV rounding, original camera/output oracles,
bone overlays and transform tools remain open; this batch does not close P4.

[Run 41296145626](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41296145626)
at `433f8fa` passed the preset/readout batch on Windows, macOS and Linux.

Camera reference-center changes now compensate view offsets and distance without
rebuilding the scene or consuming bone history. Model-center and active-bone
references retain the native root fallback and stored parent-basis rules. The
Qt checkboxes invoke this shared command and retain their mutually exclusive
state. The opt-in Windows `editor_camera_oracle_test` loads a hash-verified
original x64 executable without entering its application or creating a window.
Its reference-switch function matched all 972 combinations of rotations,
parent/reference modes and active bones with maximum absolute error zero.
This is a command-kernel comparison using published skin matrices as inputs,
not an independent full-scene or event-loop oracle. The original free-camera
case with a stale attachment basis can change the picture; that behavior is
retained rather than hidden by a general picture-invariance assertion.

Document bone indices now resolve through the runtime's established name-bound
mapping before camera parenting, active-bone pivots, accessory parenting and
picking access resource bones. The reordered-name regression checks those
paths, invalid-reference atomicity and PMM save/reopen. All 16 local P4 tests,
the rebuilt scene-runtime regression and never-shown native Qt presentation
on DX11, DX12 and Vulkan passed. The application built without being launched.
Follow-toggle refresh, global-track application gates, event-dependent FOV
rounding and the broader original-output/UI gates remain open.

[Run 41297109061](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41297109061)
at `f365756` passed camera-reference switching and reordered document bindings
on Windows, macOS and Linux.

The follow-camera checkbox now invokes a dedicated shared command. Enabling it
reloads camera, light, self-shadow, gravity and accessory tracks at the current
frame, clears offsets and applies the native model-view reanchoring when in
model mode. Disabling it reanchors only when the selected model is the camera
parent. Both preserve unregistered bones, history, the physics world and the
published CPU snapshot. Gravity parameters reach the next simulation frame
without forcing a scene update. Validation completes before mutation.

The explicit original-executable camera oracle additionally passed 648 follow
on/off reanchoring cases with maximum absolute error zero, including stored
parent bases and missing active-bone fallbacks. It invokes only the original
matrix kernel, without a window or D3D device. The portable regression checks
global-track reload, history isolation, camera/model mode differences, unchanged
runtime/snapshot identity and invalid-basis rejection. Qt offscreen mouse clicks
on the actual checkbox passed with zero scene updates.
All 16 local P4 tests, the rebuilt scene-runtime test and native hidden Qt
presentation on DX11, DX12 and Vulkan passed. The application built without
being launched. This batch does not close the global playback gates or P4.

[Run 41297790963](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41297790963)
at `1cc021b` passed follow-camera refresh on Windows, macOS and Linux.

Global-track application is now explicit in the shared session/runtime API.
Editor frame seeking in model mode with following off retains camera, light,
self-shadow, gravity and accessories while seeking models. Camera mode or
following reloads those tracks; followed model seeks then reanchor the view.
Playback parks global tracks in plain model mode but always advances accessories.
Physics catch-up substeps apply models only; the final call applies the enabled
global/accessory groups. Standalone/export hosts retain the default full-track
policy. Global tracks now have terminal cursors: their last key is applied once,
so later manual camera adjustments survive subsequent playback frames.

The new `editor_playback` regression covers these gates, current-frame starts,
unregistered global state, gravity publication, strict terminal boundaries,
manual camera changes and substep cursor retention. The opt-in Windows
`editor_playback_oracle_test <original.exe>` invokes the hash-verified original
x64 playback function with independently constructed native key records and no
loaded models. Its only COM calls are intercepted by an in-memory recorder;
no D3D device, window or message loop is created. All 1200 value checkpoints
matched exactly, together with five track cursors and projection/light call
gates. This validates the global/accessory advance kernel, not playback-start
UI initialization, complete model scheduling or independent scene output.

All 17 local P4 regressions and the 99 scene-runtime checkpoints passed after
the complete rebuild. Never-shown native Qt presentation passed on DX11, DX12
and Vulkan, including resize, seek, device replacement, window recreation and
unload. The application built without being launched. Fractional playback-frame
arithmetic, mode-switch initialization and the broader UI/output gates remain
open; this batch does not close P4.

[Run 41299152285](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41299152285)
at `67b6b9a` passed the 17-test track-application batch on Windows, macOS and Linux.

The editor now explicitly selects native float playback-frame arithmetic. A
shared checked kernel preserves the original multiplication stores and rounded
quotient for both live models and global/accessory tracks. At `0.1f` seconds the
native product is exactly frame 3; a double intermediate could otherwise consume
the following key or exhaust a terminal cursor early. The editor's fixed-step
image-sequence export selects the same native rule. Standalone scene consumers
retain their previously accepted double default through a named arithmetic
policy. Invalid or out-of-range cursors leave output unchanged.

The playback regression checks frozen original instruction-stage bit patterns,
the frame-3 terminal boundary, and agreement between editor playback and model
snapshot frame metadata. The original executable oracle now covers terminal
keys at frames 3 and 30, 997-step fractional sampling, adjacent floats at one
second, application gates and physics substeps. All 403,200 value checkpoints
matched with maximum absolute error zero; cursors and recorded COM call gates
also matched. It still invokes only the original global/accessory kernel,
without creating a device or window.

All 17 local P4 regressions, the 99 scene-runtime checkpoints and never-shown
DX11/DX12/Vulkan presentation passed after the arithmetic change. The Windows
compatibility core rebuilt and its model playback/physics regression passed.
The export path then rebuilt and passed strict repeated image output and
cancellation. The application built without being launched. Mode-switch
initialization and the remaining UI/output gates are still open.
