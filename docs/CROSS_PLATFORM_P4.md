# P4 implementation and acceptance

Status: **in progress**. P4 and C1 have not been accepted. The current work also
includes strict MMD v9.32 layout/behavior parity and multilingual UI, as requested
on 2026-11-01. These are acceptance requirements, not claims about this revision.

The macOS Qt viewport now defaults to native Metal, with Vulkan available for
comparisons. Window generations, Retina, actual drawable pixels, backend switching
and relocated packages have separate [Metal acceptance evidence](METAL_BACKEND.md).
This backend addition does not close the remaining MMD layout/behavior gates.

## Shared document and commands

The editor has a new PMM v2 topology writer. It rebuilds offsets and reparses
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
Frame-line/marked-key deletion and bone registration now have touched-record
evidence below; physics registration is connected to the Qt control and CPU pose
source. Reused slot payloads now have the independent coverage recorded below;
clipboard and chained range operations remain part of the broader timeline gate.

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

The selected-target clipboard command (250, F) is now connected to the shared
session and all three Qt languages. It pastes only copied bone/accessory keys
into the active bone/accessory, retaining time offsets relative to the complete
copied selection. Bone paste uses partial per-model history; accessory paste
preserves all models' pending poses and the physics reset flag, as in the original
dispatcher. Missing targets and frame overflow leave the document/history intact.
The optional original-executable clipboard oracle passed **168 additional exact
selected-target cases**, including ignored source names, movable/rotation-only
and fixed-axis targets, overwrite/insertion/append and storage orders. The earlier
168 ordinary tables, 168 fixed-axis cases and seven mirrored-name comparisons
still pass. Public regressions cover mixed-track offsets, cross-model targeting,
undo/redo, save/reopen, unloaded targets, retained clipboard and playback gating;
Qt menu/F dispatch passed in English, Japanese and Chinese under `offscreen`.
This closes that command's integration gap, not the complete clipboard/input gate.

| Gate | Current state |
|---|---|
| Shared editing and serialization | First local regression passed; full command/compatibility coverage pending |
| Qt original layout and multilingual interface | Initial three-platform command/language regression passed; full original parity pending |
| C1 scene rendering | Model outlines, texture morphs and screen history passed on three platforms; accessory self-shadow passed local DX9 comparisons on DX11/DX12/Vulkan; full C1 comparison pending |
| WAV and synchronization | Qualified Windows silent profile and new CoreAudio/PulseAudio 120-second runs passed; latest Windows hosted runner has no audio device and reports unavailable; Linux uses an explicit 50 ms virtual queue; physical speaker latency and the default two-second cold null-sink profile are unqualified |
| Fixed-step image sequence export and cancellation | Generated 30fps sequence, strict repeat and cancellation passed on three platforms |
| Three-platform open/edit/play/save/reopen/export | Initial generated workflow passed; complete C1 workflow coverage pending |
| MMD v9.32 independent geometry, behavior and output comparison | Pending |
| Input, IME, DPI, font metrics and lifecycle acceptance | Native surface lifecycle passed on all platforms; full input/IME/DPI/font acceptance pending |
| CPU frame-production performance | Local Sour Miku complete offscreen CPU/GPU budget passed on DX11/DX12/Vulkan; generated three-platform CPU/GPU qualification passed at 6dbc9cf; physical presentation latency is separate |

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
with their native head-cut behavior, retaining unselected detached records. The
later marked-key deletion batch below adds record-order processing and automatic
bone-curve rebuilding with original executable comparisons. Clipboard and chained
range-edit compatibility remain separate timeline requirements.

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

[Run 41300134678](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41300134678)
at `0301404` passed native-float editor timing on Windows, macOS and Linux.

Model/camera selection is now an editor command with explicit refresh semantics.
Entering camera mode reloads globals and accessories, clears view offsets and
restores the identity attachment basis. Entering model mode reanchors against
the selected model; following reloads globals, while following off restores the
native default light. Switching between models reloads only the camera when
leaving or entering its followed parent. Other model switches preserve the view.
The command restores each model's bone selection and resets the native edit mode
without rebuilding models, physics or undo history. Playback rejects mode
switches and disables both Qt mode controls.

The shared view-refresh transaction validates all global/accessory evaluation,
camera parent references and basis values before publishing. Follow-toggle
refresh uses the same operation. PMM saves retain the selected model even while
in camera mode, and opening respects the saved camera-mode flag without invoking
an interactive mode change or discarding an unregistered camera pose. Qt's
viewport toggle uses that document selection instead of a stale window-local
slot.

The new public three-model regression covers these gates, non-parent and parent
switches, default lighting, current poses, history/runtime identity, atomic
rejection and save/reopen of both modes. Qt offscreen tests activate the real
model combo, invoke the mode button and switch all three languages while checking
state preservation. Existing navigation assertions now isolate reference-change
commands from the newly implemented mode-change effects. The opt-in original
camera math oracle still passed 972 reference and 648 follow cases exactly;
this is kernel evidence, not an independent full original UI-event oracle.

All 18 local P4 regressions and the 99 scene-runtime checkpoints passed after
the mode-change rebuild. Never-shown Qt presentation passed on DX11, DX12 and
Vulkan, and the application built without being launched. Bone overlays,
manipulation tools and the remaining complete UI/output gates are still open;
this batch does not close P4.

[Run 41301962498](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41301962498)
at `bf56cbe` passed editor mode switching on Windows, macOS and Linux.

The Qt viewport now composes editor bone lines and atlas markers onto the final
color target after scene history capture. Both Windows compatibility rendering
and the portable producer use the same storage-independent marker/filter/IK
rules. The portable producer consumes published skin matrices and immutable
rest/tail metadata; selection changes never reevaluate animation or physics.
It maps PMM selection, edit markers and IK selectors to resource bone indices.
It preserves native per-type atlas cells, draw order, repeated IK-link Vs,
physics filtering, the selected-marker exception and the unattached camera
look-at marker. Coordinate-system strips, transform gizmos and drag behavior
remain separate pending work.

Overlay resource IDs stay stable when hidden, playing or resized. Pure clear-only
frames release their leases and revival publishes higher versions. The viewport
cache now also observes multi-selection and edit-mode changes, which may leave
both the active bone and document revision unchanged. Export and `screen.bmp`
capture precede editor composition and receive scene color only.

The opt-in Windows x64 `editor_overlay_oracle_test <original.exe> <fixture> <assets>`
calls the hash-verified original at RVA C4980. Its renderer and line-buffer COM
objects are in-memory records; no original entrypoint, window, message loop or
GPU device is used. All 1800 exact normalized vertex/count cases passed across
PMD/PMX, ten PMD bone types, offset tails, single/multiple selection, pending
edits, physics modes, disabled physics, camera rotations, odd/even viewport
sizes and all five cursor modes. This independently checks overlay production
with supplied model poses/camera matrices, not their full native production,
GPU rasterization or the complete UI event loop.

The public regression passed 30 mode cases on each of DX11, DX12 and Vulkan,
with strict repeated image equality, playback hide/revival, resize, camera
mode and pure-clear resource retirement. All 19 P4 regressions passed, the
compatibility core rebuilt and its model playback/physics test passed, and
the application built without being launched. The earlier rebuilt scene-runtime
regression passed all 99 checkpoints. One combined local run exceeded the
presentation wrapper's 60-second limit; the subsequent standalone never-shown
presentation run passed all three backends, including selection/mode cache
invalidation, replacement, recreation and unload. No comparison tolerance or
wrapper limit was relaxed. Complete overlay raster/output comparison and the
remaining P4 gates stay open.

[Run 41304124144](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41304124144)
at `d7798c6` passed bone overlays and the public mode/lifecycle regression on
Windows, macOS and Linux.

The native operation-panel strip, six axis controls, hover highlights and
rotate/move cursor pictures now share one atlas-layout helper between the
Windows host and Qt. Drawable-viewport hit rectangles are also shared, including
strict edges, the rotation ring and the coordinate-cycle control. Qt maps
device-pixel hit coordinates explicitly; the full DPI gate is still open.
The L shortcut and strip control cycle transient coordinates (three camera
states, two model states). Entering model mode resets camera-only state 2;
coordinate changes do not dirty the document, create history or evaluate the
scene. Window-background keyboard events now use the same filter as controls.

Frozen atlas and hit-boundary checks, coordinate-state checks and actual Qt L
key events passed. Never-shown presentation passed DX11/DX12/Vulkan with these
controls, and the compatibility host rebuilt with its playback/physics check
passing. The previous 1800-case original bone oracle still passed. This adds
the operation controls' presentation and coordinate selection; bone/axis drag
execution, cursor capture/restoration and 3D axis geometry remain pending.


The operation-control batch at `ceb6199` passed on Windows, macOS and Linux
([run 41304858919](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41304858919)).

Bone dragging now shares the native algorithm kernel with the Qt host. The
portable model snapshot publishes the solved `matWorld` basis as well as skin
matrices; screen movement converts through each selected root's parent-space
rows. Selected children inherit a selected parent's change once. The twelve
rotation/movement modes retain native full-angle quaternion construction,
local/world axes, PMX fixed/local-axis handling and Shift/Ctrl precision rules.
Portable arithmetic follows the x64 binary32 boundary; the compatibility x86
adapter retains its existing wider intermediates and math functions.

Qt operation controls and cursor handles execute these commands. Press saves
selected poses, including a click with no movement; subsequent movement merges
into one undo entry. Direct handles enter temporary edit mode 5 and return to
Rotate/Move on release. Focus loss, surface teardown, backend replacement and
interrupted editor gestures release the active gesture. Axis controls do not
require positive clip depth; screen-plane handles require a valid projection.
Export remains independent from editor overlays.

Local validation passed all 21 P4 regressions, the 99 scene-runtime checkpoints,
Qt synthetic gestures at scale factors 1, 1.5 and 2, and never-shown presentation
on DX11/DX12/Vulkan. The compatibility core built and its model playback/physics
regression passed. The application was built without being launched.

The opt-in Windows x64 `editor_bone_drag_oracle_test <original.exe> <fixture>`
uses the hash-verified original frame-driver block `[32F41,35EEB)`. A small ABI
adapter supplies its register/scratch context and returns at the continuation;
the block's own instructions are preserved. No original executable entrypoint,
window, message pump or GPU device is used. The oracle supplies raw original
application offsets independently from this port's compatibility state layout.
All 1296 cases passed across PMD/PMX, independent selected roots, selected
parent/child pairs, native Japanese arm/finger axes, rotated parent poses,
camera rotations, perspective modes, both coordinate modes, three precision
settings and all twelve drag modes. Maximum absolute translation error was
`9.53674316e-7`; rotation error was `8.94069672e-8`, within the preselected
`2e-5` translation / `2e-6` quaternion limits. Pending-edit flags matched exactly.
This checks drag operations with supplied published matrices, not independent
pose production or the complete native UI loop.

Centered cursor warping/hiding/restoration, complete 3D axis drawing, box
selection feedback, physical input and the complete original UI/DPI/font/audio
acceptance gates remain open. P4/C1 is not yet accepted.


Qt picking and box selection now use the same physical-pixel projection as bone
dragging. The white selection rectangle shares its exact four-segment order and
color with the Windows host. Press/move/release invalidate the viewport cache
without adding pose history or evaluating the scene; focus loss and teardown
finish the selection and remove feedback. Offscreen Qt gestures passed at
scale factors 1, 1.5 and 2, including reverse-direction boxes and picking.
This covers coordinate mapping for these operations, not the complete DPI or
physical input acceptance gate.


The shared bone drag batch at `500f9bb` passed the 21-test editor workflow on
Windows, macOS and Linux
([run 41307364939](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41307364939)).

Centered bone-axis dragging now hides the Qt window cursor and restores its
previous shape when the drag ends or loses focus. On exposed Windows/Cocoa/X11
viewports it centers the pointer on press and restores the native operation-cell
center on release. Restoration coordinates share one helper with the Windows
host. Focus loss, teardown and backend replacement release the cursor without
warping into an inactive window. Offscreen tests exercise cursor state only and
never move the global pointer; the local 1/1.5/2 scale-factor gesture tests and
frozen restoration-point checks passed. Actual exposed pointer interaction is
still an isolated-runner acceptance item. Wayland cursor hiding works through
Qt, but its platform plugin supports explicit mouse capture only for popups;
ordinary viewport capture requires the protocol adapter described below.

The Windows Win32 foundation run exposed an evaluation-model leak: the public
scene-runtime dependency propagated Bullet's `/arch:IA32` into portable editor
commands and their tests. The scene API only forward-declares its physics world;
the physics dependency is now private. Physics/runtime compilation retains the
legacy setting, while editor arithmetic retains its specified binary32 boundary.
The original frozen reset-pose bit pattern and exact camera-wheel assertion pass
without changing expectations or tolerances.
The full local Win32 foundation suite then passed (75 registered tests, with
only the explicitly external PMM asset case skipped), including all P4 commands,
scene/physics comparisons and the dependency audit.
The same boundary explicitly exports the floating-point policy for public
inline arithmetic; ARM64 consumers also need `-ffp-contract=off` to retain
separate multiply/add rounding instead of inheriting it accidentally from Bullet.

The Qt viewport now draws the bundled `axis.x` operation mesh before 2D bone
feedback. Native and portable hosts share the selected-bone transform and local
axis construction. The mesh has its own depth clear; it does not occlude the
subsequent screen markers and is excluded from exports and screen history.
Upload identities survive hiding/playback, resize and device replacement, with
fresh versions after a pure-clear retirement. The special importer accepts only
the operation mesh's convex quads, generated normals and white vertex colors;
ordinary accessory imports keep their existing strict rules.

The opt-in Windows x64 `editor_axis_oracle_test <original.exe> <fixture>` calls
the hash-verified original function at RVA `37490` with raw application offsets
and fake COM transform/clear callbacks. It passed **10,488 cases**, including PMD
Japanese local axes, PMX axes, constrained bones, local/world coordinates,
three frame/eye/FOV variants and operation/drag eligibility. Maximum absolute
matrix error was `9.53674316e-7`, under the existing `2e-5` matrix limit. This
checks supplied bone/frame matrices, not independent pose generation or native
GPU rasterization. No original entrypoint, window or GPU device runs.

Local DX11/DX12/Vulkan image tests passed strict repeated submission and exact
restoration after device replacement, alongside playback hiding and odd-size
resize. Removing only the 3D pass changes the rendered image. The native viewport
regression passed; the 1296-case original drag comparison and 1800-case exact
bone-overlay comparison also passed after extracting local axes. Offscreen Qt
gestures passed at scale factors 1, 1.5 and 2. Complete exposed input, UI/font
and C1 rasterization acceptance remain open.

The first operation-axis CI run passed Windows/Linux but exposed a fixed D24S8
attachment on macOS. It now uses the same capability-selected `DepthStencil`
format as the main scene, preserving stencil on devices with D32S8. The parallel
P3 macOS performance run also exposed a late query-availability sample: explicit
completion drains now collect completed profiling queries before frame-slot
reuse. Ordinary polling stays nonblocking and profiling remains opt-in. A local
never-shown Vulkan presentation test retained all 90 timestamp samples with no
new image readbacks. The Paravirtual driver's zero durations remain recorded as
unavailable; budgets and image comparison criteria are unchanged.

The depth-format and completed-query fixes passed all three editor/rendering
platform jobs at `8d6daec`, including macOS operation-axis rendering and the
unchanged 90-sample performance gate
([editor](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41311149790),
[rendering](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41311149708)).
The Windows x64/x86, Linux and macOS foundation matrix also passed.

Camera/accessory operation rows now execute native drag modes 13..18 in Qt.
The compatibility and portable hosts share position/angle arithmetic, retaining
the native x86 intermediate policy. Camera-relative translation, parent/world
translation, accessory targeting and Shift-before-Control precision keep their
distinct rules. Camera-only edits retain published model snapshots without CPU
scene evaluation or bone history. Model switches preserve the independent
accessory selection; clearing it had prevented accessory-axis editing after
returning to camera mode. Focus loss, surface teardown and backend replacement
finish the gesture and restore the cursor shape.

The opt-in Windows x64 `editor_scene_axis_oracle_test <original.exe> <fixture>`
executes the hash-verified original frame block `[35EEB,36B66)` with independently
constructed application/accessory offsets. Only edit-control lookup/echo imports
are intercepted; no native window, entrypoint or GPU runs. All **2160 cases**
passed with maximum absolute error **zero**, across three coordinate modes,
parented/free cameras, rotated views, all modifier combinations, positive/zero/
negative motion and six drag modes. This compares supplied camera/accessory
states, not the complete original input loop or attachment evaluation.

Local validation passed all 21 P4 regressions, the full 75-test Win32 foundation
suite (one explicitly external PMM asset case skipped), 99 scene-runtime
checkpoints and never-shown native Qt presentation on DX11/DX12/Vulkan. Qt
synthetic gestures passed at scale factors 1, 1.5 and 2. The compatibility drag
precision, accessory drag and model viewport regressions also passed. This
batch does not close the remaining exposed input, full UI or C1 output gates.

The viewport header now uses the original two tool hit boxes, bundled indicator
and release positions. Header zoom/pan has its own native arithmetic, distinct
from middle-button navigation; Ctrl manipulates the loaded background picture,
including when hidden. Picture drags update only placement and vertex versions,
retaining decoded images, texture identities and published model snapshots.
They survive PMM save/reopen without adding bone history. The Qt container also
synchronizes its child viewport size before exposure, so hidden and real input
use the same physical-pixel geometry. Right/middle view drags now use that same
coordinate contract at fractional scale factors.

The opt-in Windows x64 `editor_header_oracle_test <original.exe> <fixture>`
executes the hash-verified original frame block `[32207,32BEA)`, including its
actual picture-vertex refresh through a memory-only fake COM buffer. All **1760
cases** passed with maximum absolute error **zero**. Cases cover camera/model
views, parented/free cameras, rotation, FOV, depth, all precision modifiers,
positive/zero/negative motion, picture visibility and four view scales. Native
global cursor movement is suppressed; execution stops before the UI refresh
call. This establishes supplied-state arithmetic, not original event-loop or
full window behavior. No original window or GPU device runs.

Local validation passed all 21 P4 tests, the 75-test Win32 foundation suite
(one explicitly external asset test skipped), and offscreen Qt gestures at
scale factors 1, 1.5 and 2. DX11/DX12/Vulkan background tests preserve exact
repeated output and restore the original image after resetting placement.
Background-video manipulation belongs to P6; exposed input, Wayland pointer
lock and the remaining C1/UI gates are still open.

Wayland centered tools now request the compositor's relative-pointer and
one-shot pointer-lock protocols on Qt's existing connection/event queue. A header
press locks the canvas surface; an axis press locks the native child viewport.
Accelerated surface motion accumulates fractional physical pixels before entering
the same tool commands as ordinary Qt input. Matching absolute motion is ignored
while a request is pending/active. Release supplies a surface-local cursor hint;
the compositor retains authority over its final position. Focus loss, backend
replacement, surface teardown, protocol removal, compositor revocation and an
ungranted lock cancel the gesture and restore cursor state. Missing protocols
produce an explicit diagnostic rather than a silently bounded hidden drag.

The new Linux `qt_wayland_pointer_*` gate runs a separate Qt client against a
private, memory-only Qt Wayland compositor, with real registry/seat/xdg-surface
transport and scripted optional pointer protocols. It covers fractional motion,
scales 1/1.5/2, duplicate absolute events, release hints, revocation, cancellation,
missing protocols and an ungranted request. All five Linux protocol cases and
the three-platform editor workflow passed at `1edf61f`
([run 41316100403](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41316100403));
local Windows synthetic gestures passed at all three scale factors.
No test connects to the user's Wayland socket or moves the desktop pointer.

The opt-in Windows x64 `editor_layout_oracle_test <original.exe>` now calls the
actual main layout routine at RVA `1D250`. Its seven window imports are replaced
by memory-only recorders; four painting/refresh calls are excluded. All **768
states / 112128 ordered control rectangles** matched exactly: six sidebar/client
sizes and all 128 camera/panel option combinations, with 146 moves per state.
The independent native record streams also supply six frozen hashes for the
portable `editor_layout` regression on every platform. This checks main-control
placement expressions and order. It does not compare native font metrics,
collapsed combo geometry, visibility, viewport-tail controls or window chrome.

The same oracle additionally executes RVA `1FA90` for all 22 viewport-strip
controls. It passed **756 states / 16632 exact rectangles**, including the pixel
before, on and after every fit boundary, three header heights, two strip heights,
and both main/separate-window insets. Two additional frozen hashes cover those
native streams in the portable test. Main and viewport strips together now have
**1524 states / 128760 exact rectangles** of independent arithmetic coverage.
This still does not establish actual native widget/font rasterization, full
visibility/menu behavior or a separate-window Qt implementation.

Qt now supports the native sidebar divider hit interval, drag clamps and
proportional window resize. The compatibility host and Qt share the arithmetic;
x64 retains its binary32 resize multiply, while the x86 compatibility policy
retains its double intermediate. The original layout oracle passed another
**42 exact width/ratio cases**, including the one-pixel difference at `0.7f *
1000`. A minimum-width clamp preserves the ratio for subsequent enlargement.
Divider motion updates PMM UI metadata without evaluating the scene, replacing
published snapshots, changing document revision or adding bone undo history.
Focus loss ends the gesture. Save/reopen retains the final sidebar width.

Local validation passed all **22 P4 regressions** and the three touched Win32
editor/layout tests. Offscreen Qt input passed at scale factors 1, 1.5 and 2,
including drag limits, shrink/enlarge, cancellation and language changes. The
previous main/viewport layout references passed all three platform editor jobs
at `0360a5c`
([run 41316951102](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41316951102)).
These tests cover supplied Qt canvas dimensions, not exposed window-manager
resize events, full native widget metrics or the remaining P4 exit gates.

The sidebar batch at `98b0e9d` passed the Windows/Linux/macOS editor matrix
([run 41318415150](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41318415150)).

## Editor CPU frame preparation

`Viewport::PrepareFrame` is the production CPU path used by presentation, and
can also run without a native surface or GPU device. Optional timings separate
view setup, asset loading, scene construction and editor overlays. A new
`qt_editor_performance` gate drives `EditorWindow::Tick`/`Refresh` and this same
path, measuring a public 8190-vertex PMX through static, navigation and animation
cases (10 warmup and 90 measured frames each). It asserts exactly one scene
update per animation tick, none on navigation/static frames, cached static frames
with zero measured C++ allocations, no GPU submissions, retained immutable frame
bytes and unload/resize validity. Shared-runner CPU budgets are fixed at P95
100 ms / maximum 500 ms; the local profile uses 33.333334 ms / 100 ms. Reports
separate UI/update, scene, overlay, aggregate CPU time and C++ allocations; Qt's
own allocator, driver allocations, painting, GPU time and presentation are not
included. This is not a complete displayed-frame or physical GPU qualification.

The initial local Sour Miku run (139692 vertices, Intel Core i7-10700, Windows
x64 MSVC Release / Qt 6.8.3) failed the unchanged local budget: navigation P95
83.21 ms. Profiling identified two avoidable costs. Qt rebuilt unchanged combo
models every refresh; it now synchronizes changed items and retains unchanged
timeline rows. Independent decodes of shared material textures repeatedly
compared full pixel arrays; sealed byte storage now memoizes successful **exact**
comparisons using bounded weak references. Mutation/resealing invalidates the
entry, and mutable byte buffers retain ordinary exact comparison. Hash equality
does not replace byte equality. Geometry publication reuses outline staging,
avoids allocation for unchanged bytes and copies changed bytes once into owned
immutable storage. These changes preserve resource IDs and version rules.

Local validation passed all **23 P4 tests**, exact image/snapshot, material,
geometry and scene-renderer regressions, the touched Win32 RHI tests, offscreen
Qt scales 1/1.5/2, and never-shown DX11/DX12/Vulkan presentation and recovery.
The performance/geometry batch subsequently passed Windows, Linux and macOS in
[run 41320419772](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41320419772)
at `9438cba`, including the CPU budget and native surface jobs. The full
menu/dialog, font/IME and independent C1 output gates remain open.

After the final geometry change, the same local Sour Miku workload passed the
original CPU budget: navigation P95 **12.15 ms**, animation P95 **31.12 ms**;
static preparation had zero measured allocations. Navigation allocated bytes
fell from 18.31 MB to 11.66 MB per frame. The generated workload measured 2.48 ms
navigation / 3.32 ms animation P95. Values and precise scope are retained in
[the CPU evidence record](evidence/cross-platform-p4-cpu.json). The compatibility
Windows executable also rebuilt successfully. These CPU-only values are not FPS
claims; cold texture loading and combined GPU/presentation budgets remain
separate measurements.

## Original menu structure and command routing

Qt now consumes a generated menu tree from `res/templates/menu/menus.rc`:
eight top-level menus, the nested enhance-model menu, **103 command IDs and
48 separators**. English and Japanese retain the original strings (including
original spelling), mnemonics and order. All commands have Chinese translations
under the same stable IDs. Backend/language settings, undo/redo shortcuts and
image-sequence export occupy a separate NG menu following the original menus.

`scripts/editor_menu.py --check` compares the source tree with both independent
binary RT_MENU records in `res/gen_resources.res`, then checks the generated
header and three catalogs. The offscreen `qt_menu` regression independently
flattens the actual QMenu/QAction tree and compares its labels, IDs and nesting
with those binary records. Repeated language replacement destroys the previous
actions, preserves document/selection state and does not evaluate the scene.
Windows `dialog_rc.py verify` also confirmed all 48 dialogs and both menus still
compile byte-identically to their original resource anchors.

**37 commands are connected** in this batch, including the previous file,
background and physics-mode operations, eight select-all track commands,
information/axis/tracking toggles and ground-shadow settings. Selection sweeps
preserve marks in unrelated tracks/models; the accessory sweep includes every
loaded accessory. The test covers mode-dependent availability, menu/panel
checkmark synchronization, mutually exclusive physics modes, unloading and the
save shortcut with focus in a panel edit. Unsupported commands stay disabled
with a translated explanation. The binary-tree check is structural acceptance,
**not acceptance of all 103 command behaviors or native menu font metrics**.

While tracing the floor command, the audit found that the shared runtime had
not applied PMM `floorVisible` to Bullet's ground collision flags. That command
was left disabled pending the actual physics path and trajectory fix below;
changing a menu checkmark alone does not establish support. Full timeline/menu
behavior, dialogs, native audio, input/font metrics and independent C1 frame
output remain open P4 gates.

Local Windows validation for the menu batch passed all **24 P4 tests** and the
Qt application build. These tests remained offscreen; no desktop window or
audio output was opened. The new menu oracle is included in all three editor
CI jobs. The batch passed Windows, Linux and macOS in
[run 41321824124](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41321824124)
at `0f5a7f0`, including the native surface jobs.

## Physics floor command and PMM restoration

The next batch connects the floor command (38 connected menu commands in total).
It replaces the existing ground body's collision flags with exactly `1` or `-1`,
without rebuilding the world, changing body transforms/velocities, advancing
simulation, stopping playback or creating an undo entry. Like the native menu,
the toggle does not mark the document dirty, but an explicit save includes it.
New scenes enable the floor. Older V1 files lacking the optional floor section
retain the editor's current setting; standalone V1 decoding starts enabled.

The shared runtime now applies an explicit saved floor value before its initial
simulation and when replacing settings. The audit also found that both legacy
PMM loaders incorrectly wrote Bullet's deactivation timer. Both loaders and the
legacy menu now call the same ground-flag helper as the portable runtime.

The optional local `physics_floor_oracle_test <original-x64.exe>` executes only
the actual ground-body store instructions extracted from the hash-verified
original: menu RVAs `500BC/500F7`, V2 loader `81C1A/81C52`, V1 loader
`884B6/884EE`. All **24 complete body-byte comparisons** passed. It maps no UI,
opens no renderer and does not invoke the original application entrypoint. This
is an instruction-level oracle; it is not a full original PMM-loading workflow.

The portable Bullet test exercises on/off/on contact in the same world. New
public PMD/PMX fixtures explicitly include collision group 16 (the floor);
existing articulated fixtures deliberately disable all collision masks and
remain byte-identical. Editor playback tests verify actual landing/falling both
directly and after PMM save/reopen. Qt's offscreen menu test checks playback,
snapshot/body identity, unchanged update/revision counters, menu state and
persistence. P4's remaining full behavior and output gates are unchanged.

Local Windows validation passed all **24 P4 tests**, four additional x64
physics/runtime/PMM tests and six Win32 tests, including V1 setting inheritance.
The Qt application and legacy Windows application both built successfully;
the legacy hidden-window floor test and its three PMM regressions passed.
No local desktop interaction or audio output was used.

The floor batch passed all three editor jobs at `9e6fe74` in
[run 41323624845](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41323624845),
including each platform's native surface checks.

## Inserting timeline frame lines

The bone/camera and Morph/light insertion commands now share a portable editing
operation. It shifts only non-root keys at or after the current frame, retaining
record indices, links, payloads and selection bytes. Frame-zero roots stay in
place. Validation rejects broken links and frame overflow before mutation.
Only the edited model or global track is evaluated; other models' unregistered
poses and unrelated camera/light state survive. Camera insertion resets view
offsets, as the original camera seek does.

Bone insertion captures per-model key/pose history. Morph, camera and light
insertion do not add bone history. Selection follows the moved records through
insertion and repeated undo/redo. The original no-op distinction is retained:
the bone keyboard command with no affected keys leaves revision and history
alone; its menu dispatcher marks the scene dirty. Neither case rebuilds the
runtime. Affected insertion retains native maximum-frame high-water values.

The optional `editor_frame_line_oracle_test <original-x64.exe>` calls the
hash-verified original insertion routines at RVAs `89A90` and `8B230`. All
**56 complete key-table comparisons** passed, including flags, padding, native
history record count, dirty state and maximum-frame stores. It uses fake UI
imports and omits pose evaluation/painting calls; no original entrypoint,
window or GPU runs. This establishes the insertion kernel, not the whole native
event loop or deletion/automatic-interpolation behavior.

The public portable test covers four track types, stable nonconsecutive indices,
cursor boundaries, atomic overflow rejection, evaluated poses, model isolation,
selection, repeated history traversal, PMM reopening and rejection during
playback. Qt additionally exercises both menu commands and `I`/`U`, text-input
focus, modifier handling, key-repeat suppression, model/camera mode and playback
availability. There are now 40 connected native menu commands. Deleting frame
lines, detached records and the remaining P4 exit gates still need acceptance.

An additional **24 original undo/redo table checkpoints** change selection
after insertion. Undo restores the captured flags of touched keys while leaving
untouched records alone; redo restores the flags displaced by undo. The portable
history now retains that distinction, including repeated cycles.

Local validation passed all **25 P4 tests**, the Qt application build, and
Win32 frame-line/history/PMM editing tests. The new public test is included in
each of the three editor CI build lists. All local runs remained offscreen or
headless, with no audio device output.

The insertion batch passed all three editor jobs at `37461af` in
[run 41325408397](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41325408397),
including native surface checks.

## Automatic bone-key interpolation

The Qt automatic-interpolation checkbox (native control 530) now starts enabled
and controls actual bone-key registration. Its setting survives new/open and
language changes without altering document history or rebuilding the runtime.
Overwriting a key with automatic interpolation disabled preserves its hand-edited
curve; a new key receives the original default curve instead of copying a
possibly modified frame-zero curve. Automatic registration updates neighboring
handles and includes those changes in the same bone-key undo entry.

The Windows host and portable editor use the same `bone_key_interpolation.hpp`
kernel. The optional `bone_key_interpolation_oracle_test <original-x64.exe>`
calls original RVA `C9070` directly, without an entrypoint, UI, GPU or patched
instructions. All **200 complete key-table comparisons** passed. The public
fixture freezes its output-curve checksum `ef40b2f667094778` and exercises roots,
all four lanes, first/interior/last keys, increasing/decreasing/turning/equal
positions and finite extreme/tiny values. The finite-overflow case also fixes
the old host's unordered-product branch to match the original `COMISS/JBE`.

The new session test checks default smoothing, neighbor changes, repeated
undo/redo, manual overwrites, new linear keys, atomic rejection and PMM
save/reopen. Existing linear playback/insertion fixtures explicitly disable
automatic interpolation and retain their previous strict numeric assertions.
The Qt test checks the real checkbox, language changes and unchanged runtime
identity/revision. This covers registration smoothing; frame-line deletion and
its native interpolation/history topology remain the next timeline work.

Local validation passed all **26 P4 tests**, the Qt and legacy Windows
application builds, four Win32 editor tests, and the legacy hidden-window
`undo_frame_test` plus PMM editing regression. No comparison tolerance changed.

All three editor CI jobs at `76fdf08` passed in
[run 41326507308](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41326507308);
the formats, rendering and Bullet workflows also passed.

## Stable key-record identities

Portable registration and deletion now splice existing records instead of
rebuilding and renumbering every chain. Registration reuses the first free slot
after the roots, overwriting a payload retains its links, and unrelated records
keep their indices. The root-zero `previous` field accepts the native terminal
unlink sentinel store; it is not traversed as a predecessor edge. Other roots
and all live chain links retain their validation.

The PMM editing test covers all eight key types, interleaved channels, out-of-order
registration, hole reuse, payload overwrite, terminal deletion, invalid atomic
rejection and persistence of record indices. This is a prerequisite for native
frame-line deletion/history, not acceptance of detached-key history or the
remaining P4 gates.

Validation passed all 26 local P4 tests, the Qt application build, and Win32
PMM editing/frame-line regressions. Tests remained offscreen or headless.

The stable-identity batch passed the three editor jobs at `17f6e0f` in
[run 41327655587](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41327655587),
along with the formats and rendering workflows.

## Frame-line deletion and partial key history

The Qt delete-frame-line menus and `K`/`J` now dispatch bone/camera and morph/light
commands through EditorSession. There are 42 connected native menu commands.
Text editing, key repeats, playback availability and modifier handling follow
the insertion commands. Only the bone operation creates undo history; a bone
keyboard no-op leaves the runtime/revision alone while its menu marks dirty.
Frame limits retain the native high-water values.

Deletion scans stable record indices, unlinks keys at the cursor and shifts later
keys down. Frame-one keys merge into the roots at cursor zero, preserving each
track's native field subset: bone physics, camera parents/selection and two
interpolation columns, and light selection survive. Bone automatic interpolation
rebuilds neighboring curves at the same point as the original operation.

History captures each touched slot on its first actual visit, rather than taking
the entire pre-command key table. In the original, frame-zero merging can leave
the root modified after undo and retain a detached frame-one record. Allocation
order can also make a forward link bypass that restored predecessor. The PMM
contract preserves these cases, validates all references/time order, and allows
that bone-only bypass only through a detached predecessor of the same channel.
Key enumeration includes detached records; seeking uses the actual predecessor
selected by the original routine. The later marked-key deletion batch extends
this contract; registration/clipboard and chained edits remain broader gates.

The extended `editor_frame_line_oracle_test` calls original deletion RVAs
`8A110`/`8B3A0`, undo/redo and actual model seeking at `CBD90`. It passed **384
persisted-record table checkpoints** and **6,192 bit-exact bone seek poses**,
covering four track types, three allocation orders, seven cursor boundaries,
automatic interpolation on/off and three undo/redo cycles. The public table hash
is `45bef3b62b79d390`. Freed-slot padding/payload is not part of the typed PMM
comparison. No original entrypoint, window, GPU or audio device runs; UI refresh
calls are excluded explicitly, while model seeking is executed independently.

Public session coverage adds selection restoration, scoped history, native
frame-zero undo, invalid atomic rejection, detached-record saving/reopening,
seeking and further deletion, and unavailable commands during playback. The
original comparisons do not constitute full editor event-loop or C1 acceptance.

Local validation passed all 26 P4 tests and the scene-evaluation regression,
six Win32 format/editor tests, the legacy hidden-window undo and format tests,
and both Qt/legacy application builds. No tolerance was relaxed.

All three editor jobs at `b54e7c4` passed in
[run 41329300724](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41329300724),
along with the formats and rendering workflows.

## Marked-key deletion and root resets

`pmm_key_deletion.hpp` now handles all eight track types in native record order.
EditorSession groups deletion requests by table and maps addresses to stable
slots, including detached records. Bone deletion captures only the slots visited
by the original, applies automatic interpolation and restores selection through
the same partial history used for frame-line deletion. In particular, smoothing
can change a further neighbor that is not captured by native undo; the regression
retains that change through repeated undo/redo instead of restoring a whole track.

Light, self-shadow and gravity root resets cut the head link. A subsequently
deleted selected successor can reconnect an unselected successor, so resetting
roots after all sparse keys would produce a different result. Detached keys now
survive saving/reopening and can be copied or deleted. Global deletion evaluates
global tracks while preserving pending model poses, and retains frame limits.

The optional original-executable oracle calls model deletion at `CDE50` and global
deletion at `64B30`. New coverage comprises **510 bone deletion/history tables**,
**210 global deletion tables**, and **60 morph/display tables**. The corresponding
public frozen hashes are `1f2e2300834a6371`, `d07a4ddebc56687f`, and
`806f058af8b58aa1`. Inputs cover allocation order, root/sparse selections, automatic
interpolation, physics-bone root flags, both native shadow reset modes, IK/selector
reset payloads and repeated bone undo/redo. The global fixture supplies all 255
accessory table pointers required by original initialization. UI/renderer refresh
calls and the final UI tail are excluded; no entrypoint or desktop window runs.

Session tests cover detached light persistence, copying/deleting its retained
key, root/successor reconnection, preserved pending model edits, atomic expired
selection rejection and the uncaptured interpolation neighbor. This evidence
does not close all registration/clipboard/chained-operation or full C1 gates.

Validation passed all 26 local P4 tests (62.18 seconds), four Win32 editor
regressions, the extended original-executable oracle and the Qt application
build. All execution was offscreen or in memory; comparison tolerances are
unchanged.

## Bone registration capture and selection

The bone register command now uses `RegisterPmmBonePose`. Existing keys retain
their interpolation when automatic interpolation is off; new records get native
linear handles. Registration follows stable native slots and captures only the
first visit to a slot: self for overwrite, predecessor/free slot for append, and
predecessor/successor/free slot for insertion. Automatic smoothing can change an
uncaptured neighbor, which deliberately retains that change through undo/redo.

The selected-bone command clears this model's bone/Morph/display key marks before
history capture, visits selected bones in index order, then selects the registered
bone keys. Other models' selection survives. Partial history restores touched
record selection and retains subsequent selection of unrelated records. The
command also preserves native maximum-frame high-water values. Non-physics bone
overwrites retain their existing physics byte; newly allocated records start at
zero instead of copying the root's byte.

The optional original oracle executes the complete selected-bone routine at
`C7EA0`, including its `C82C0` registrar, allocation, selection clearing, physics
pose/flag branches and actual undo/redo. **1,176 persisted-table checkpoints**
passed, with public frozen hash `29b1779248f245cd`. Inputs cover three allocation
orders, seven insertion/overwrite frames, four selected-bone sets, automatic
interpolation on/off and all four physics modes. Physics notification loops have
zero bodies in this kernel fixture; physical world transitions are not covered.
No entrypoint/window/audio device runs. The independent 123-checkpoint native
history fixture also passed with its frame-zero input marks corrected to match
the newly verified registration envelope.

The public regression reproduces the original table hash, tests invalid input
atomically and checks real EditorSession neighbor retention, model isolation,
selection, frame limits and repeated history. Physics backup selection is checked
at the registrar boundary here; connecting the runtime's current/backup poses
and the Qt physics checkbox is the next integration step. Clipboard and full C1
acceptance remain open.

Local validation passed all 26 P4 regressions, five Win32 editor/format tests,
the original registration and history oracles and the Qt application build.
The preceding deletion batch (`47e440a`) passed all three platform editor jobs in
[run 41331565985](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41331565985),
along with the formats and rendering workflows.

## Physics pose registration and Qt option

Control 499 now defaults to checked, as in original initialization. It changes
the next bone registration's physics-enabled flag. Clicking it does not edit the
document, rebuild resources, advance physics or consume history. English,
Japanese and Chinese refreshes, New and Open retain this session-level option.

`SceneRuntime::BoneRegistrationPose` reads the last completed CPU frame and maps
PMM bone indices through the model's name bindings. Mode 0 uses the current
editable pose; mode 1 uses the physics backup for controlled bones; modes 2/3 use
that backup only when the bone's prior physics-disabled flag is clear. Registration
then applies the checkbox to the key and current physics flag. History captures
the live editable transforms before selecting backups, including completed IK
results, instead of treating the saved document pose as the CPU bone state.

The new public `editor_registration` regression passed 16 PMD/PMX mode/checkbox
cases, including distinct editable/physics poses, repeated undo/redo, persistence,
atomic rejection and reordered document bone names. The offscreen Qt regression
checks real control dispatch, the initial value and language changes without a
new simulation frame, changed body state or history entry. The existing original
registration oracle provides the independent mode/flag/pose-selection comparison;
these integration checks exercise generated physics models and the Qt host.

This does not establish uninterrupted physical-world continuity across every
editing command: committing document edits still constructs a replacement
runtime. Reused native key-slot payloads, chained operations and the remaining
P4/C1 gates need further acceptance.

Validation passed all 27 local P4 tests (69.32 seconds), five Win32 editor
tests, the 123-checkpoint original history comparison and the Qt application
build. The registration-capture batch (`1b8cbb6`) passed the three-platform editor
[run 41332621890](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41332621890)
and both formats/rendering workflows.


## Native unused key-slot payloads

Deleting a key does not zero every field in its native storage. `PmmKeyTrack`
now retains typed unused slots in the live session. Ordinary bone registration
reuses the first free index and retains its physics byte; first-use storage is
zero. Partial undo/redo captures and restores the actual free-slot payload.
Selection clearing includes unused bone slots. Serialization still writes only
used PMM records: save and ordinary edits preserve the live cache, while opening
a file reconstructs fresh storage, matching the native loading boundary.

Frame-line deletion now preserves the different retirement rules for bone,
Morph, camera and light records. In particular, the original frame-zero bone
merge clears the freed slot's links, position, quaternion and selection after
copying its payload to the root, but retains interpolation and physics. Earlier
persisted-record-only comparisons did not observe these discarded fields. The
new comparison checks the full retired payload and supports both subsequent
insertion and append; it does not invent a cyclic-link exception to native
registration.

The original-executable oracle passed **28 reused-slot checkpoints**, including
registration, deletion, reuse by an ordinary bone, and repeated undo/redo, with
frozen hash `7877c69c4e74a379`. It additionally compared retired slots for **168
frame-line deletion inputs**, hash `8256a293bb030ff3`, across all four track types,
allocation orders, cursor positions and automatic interpolation settings. All
previous persisted-table and 6,192 native seek comparisons retained their strict
results. Public tests freeze both new hashes and verify save/edit preservation,
reopen reset, invalid cache rejection and session history. No desktop or audio
output is used.

The preceding physics-registration revision (`9addcc7`) passed the three-platform
editor [run 41333566597](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41333566597)
and the formats, rendering and Bullet workflows. Live physics continuity across
commands, remaining clipboard/range semantics and full P4/C1 acceptance remain open.

Local validation passed all **27 P4 tests** (78.39 seconds), five additional
x64 scene/format tests, eight Win32 editor/format tests and five legacy Windows
regressions including undo. Both Qt and legacy applications rebuilt successfully.
The 123-checkpoint original history oracle also passed. Cache validation was
restricted to unused-slot bounds, zero frames and collisions, preserving the old
byte-preserving editor's acceptance of legacy serialized tracks. The optional
external PMM test had no fixture configured and was skipped; it is not counted
as a pass. Existing public PMM fixtures ran in the document/editor regressions.


## Bone-key clipboard registration

Bone-key paste now preserves the original registration contract separately from
the pose-register button. Rotation-only bones retain the destination slot's
translation during ordinary paste; mirrored paste writes reflected translation.
Both retain copied interpolation and physics flags, independent of automatic
interpolation and the registration checkbox. Overwrite/insertion compare the raw
paste offset against the existing maximum frame, while append uses the absolute
frame. Copy orders keys by native table index before paste allocation.

The session clears current-model bone/Morph/display key marks, captures live
editable poses and first-visited bone records, marks the pasted keys and seeks
the affected model at the existing cursor. History restores only captured
records. Missing names are successful no-ops. Mirroring searches CP932 side
markers anywhere in the name, compares opposite suffixes and falls back to the
original name. Fixed-axis correction follows the target PMD tail axis or PMX
explicit axis. Overflow rejects the whole command without publishing edits or
consuming history.

The new optional `editor_clipboard_oracle_test <original.exe>` calls original
`C9390`/`C9DB0` without an entrypoint or window. It passed **168 exact complete
slot/capture/maximum comparisons**, frozen hash `205473a38f34f6f0`, **168 fixed-axis
comparisons** covering PMD/PMX and FixedAxis/UnderIk flags, and **seven exact
mirrored-name cases**. Fixed-axis rotations use absolute component error at most
`2e-6`; the observed maximum was `5.96046448e-8`. All other fields retain exact
comparison. The rotation limit applies only to this newly covered math path,
not to key payload copying, frame limits, topology or existing output gates.

The public regression freezes the exact hash and checks PMD/PMX integration,
three undo/redo cycles, slot reuse, out-of-order frame allocation, current-cursor
interpolation, model switching, missing names and atomic overflow. It does not
claim original-executable coverage for the complete clipboard UI command,
Morph/display/global clipboard registrars, separate mode clipboards or range
editing. Those remain within the broader timeline acceptance gate. Physical-world
continuity across commands and the other full P4/C1 exit gates remain open.

The preceding unused-slot revision (`4276633`) passed all three platforms in the
editor [run 41336057664](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41336057664),
and the formats/rendering workflows.

Local validation passed **28 P4 tests** (82.06 seconds), five Win32 editor tests,
the new original clipboard oracle and the Qt application build. Tests remained
offscreen/in memory, with no local audio output. Linux/macOS execution is provided
by the editor workflow; results for this revision must be recorded separately.

## Preserve the live physical world through ordinary editing

Ordinary document edits and same-frame bone history now stage a checked document,
timeline, CPU pose and skinned snapshot before publishing to the existing scene
runtime. They retain Bullet bodies, contacts, velocities, solver state and the
gravity-noise clock. Only changed saved pose components replace live pose inputs;
unchanged document values cannot overwrite the current IK/physics result. Physics
enable changes use the shared native flag notification after successful staging.
No simulation frame is executed by this path; `editorUpdates` counts these
publications separately from `frames`.

Resource replacement, model/accessory structure, frame-number and physics-mode
changes, and edits during active playback still use the existing full preparation
path. Their complete native lifecycle parity is not established by this batch.
All model results are staged before any live publication. Missing current/track
parents and nonfinite generated geometry reject the whole edit without changing
the document, published vertices, physical world, revision or history.

`editor_physics_continuity` covers generated articulated PMD and PMX: editing a
light and a control bone, registration, paste, repeated undo/redo, invalid parents
and finite inputs that overflow during pose calculation. It also runs independent
edited/control worlds in lockstep before and after a metadata edit, comparing
body snapshots and clocks exactly through subsequent simulation. This checks
continued evolution, not only immediate positions. It is a continuity regression
against an unmodified shared runtime, not a new original-executable physics oracle.

The preceding clipboard commit `987a61b` passed all three editor jobs in
[run 41337185823](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41337185823),
as well as the formats, rendering and Bullet workflows. Full P4/C1 acceptance
remains open.

Local validation passed **29 P4 tests** (38.32 seconds), six Win32 editor/runtime
tests, the x64 scene-runtime test and all 123 original undo/redo checkpoints. The
Qt application also built successfully. Tests used offscreen or in-memory paths
and did not play audio. Three-platform CI results for this change follow separately.

## Paused Qt frames still run the shared physical simulation

Qt's timer previously rendered the frozen CPU snapshot whenever playback was
stopped. Positive elapsed paused frames now call `EditorSession::TickIdle`, which
uses the existing shared simulation scheduler. Modes 1/2 continue simulating;
mode 0 disables physics and mode 3 retains its existing active/reset/moved-body
gates. A paused frame never advances timeline tracks or the playback cursor, and
does not touch the document, revision, selection or history. A zero-duration
refresh does not evaluate a frame. Invalid deltas and attempts to call the idle
path during playback reject before simulation.

The Qt timer chooses exactly one playback/idle path. Audio position is consulted
only while playing. Rendering, layout, language and backend changes remain
separate from simulation; they cannot add a second CPU update. An idle runtime
error retains the diagnostic and does not repeatedly open modal dialogs.

The editor regression covers PMD/PMX in all four modes, 60 paused frames,
byte-identical PMM serialization, unchanged cursor/revision/history, invalid
deltas, playback/pause transitions and unloading. The offscreen Qt regression
checks four modes, gravity-induced motion versus stationary bodies, zero-delta
and language refreshes, and one update per playing/paused timer invocation.
The existing deterministic audio clock and CPU performance checks also pass.
These tests integrate the previously compared shared scheduler; they do not
establish every original UI selection/reset/body-drag transition or full C1 parity.

Local validation passed all **29 P4 tests** (36.68 seconds), the Win32 playback
and physics-continuity regressions, and the Qt application build. No local window
was exposed and audio remained on the deterministic test sink.

## Independent model/camera frame clipboards

The frame clipboard now follows the two native mode scopes in
`src/window/frame_clipboard.cpp`: bone/Morph/display data belongs to a shared
model-mode clipboard, while camera/light/shadow/gravity/accessory data belongs to
the camera-mode clipboard. Copy considers only the active model or active camera
scope. Switching model/mode or language retains both buffers. Copying an empty
selection successfully clears only the active buffer; a stale active-scope key
rejects without replacing either buffer. Accessory ordering visits record index
before slot, matching the recovered copy loop.

Empty paste is a no-op. Mirrored paste is available only for model-mode bone
content; Morph-only and camera buffers cannot enable it. Qt's Copy/Paste/Reverse
Paste buttons reflect the shared queries, including playback gating. Generated
integration tests cover interleaved mode copies, other-model selection exclusion,
copy failure, empty copy, model switching, language changes, playback and New.

This closes the prior shared-buffer/empty-copy mismatch. It uses the recovered
native command contract plus public integration regressions; the optional
original clipboard oracle still covers the bone registration kernels described
above, not the entire UI copy command. Remaining Morph/display/global registrar,
global paste application and chained-range parity remain separate exit work.

The physical-world reuse commit `4921c3e` passed all three editor jobs in
[run 41338618697](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41338618697),
and the formats, rendering and Bullet workflows.

Mode-clipboard validation passed all **29 local P4 tests** (37.53 seconds), three
Win32 editor regressions, the Qt application build and the existing original
clipboard oracle: 168 exact tables/captures, 168 fixed-axis cases and seven exact
name cases. The original hashes and numeric limits were unchanged. Linux/macOS
results for this commit must be recorded separately.

## Paused-physics and bone-edit CPU performance

The CPU preparation gate now distinguishes five workloads: zero-duration cached
refresh, camera navigation, playback, paused physics and continuous bone edits
within one gesture. The latter two verify simulation/editor publication counts,
unchanged paused cursor/revision and retained-frame immutability. Paused physics
uses the production Qt timer path; bone editing includes command validation,
history grouping, CPU pose publication, Qt refresh and shared frame preparation.
It does not include an additional timer physics step in the same sample.

The existing budgets are unchanged: local P95 at most `33.333334 ms` and maximum
`100 ms`; the shared CI profile uses `100/500 ms`. Both the generated 8,190-vertex
input and external 139,692-vertex Sour Miku passed. In the final five-case Sour
run, P95 was **24.82 ms playback**, **29.10 ms paused physics** and **30.14 ms bone
editing**. The cold frame preparation took **3,055 ms**. This records CPU cost;
GPU, presentation, cold texture budgets and combined event-loop/frame acceptance
remain separate work. Full measurements and allocation counts are retained in
[the editor-frame evidence](evidence/cross-platform-p4-editor-frames.json), without
redistributing the external model or textures.

The paused-frame implementation `c1d331f` passed all three editor jobs in
[run 41339142378](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41339142378),
and the formats/rendering workflows. P4/C1 remains in progress.

## Clipboard scene refresh and native deferred physical reset

Further command-envelope review identified a missing boundary after physical
world reuse: native frame paste, reverse paste, bone-pose paste, Undo and Redo
set `PhysicsResetPending`. These successful commands now retain the world and
body snapshots while latching a reseat for the next shared simulation frame.
That frame consumes the existing scheduler's reset/settle sequence. The command
itself does not step the world. Empty paste and failed commands do not set the
latch. This refines the earlier continuity regression: clocks and bodies remain
unchanged at dispatch except for the required reset flag.

Camera-mode frame paste also clears marks across all global/accessory tracks,
evaluates the pasted scene at the existing cursor and clears the camera's view
offsets. The legacy `ReloadModels` routine in this command is camera-track
evaluation, not a reload of every model. Only the remembered selected model is
sought; other models retain their pending poses. All new global values and CPU
outputs are validated before publication, and gravity is updated without a step.

Tests cover global camera/light/shadow/gravity/accessory values, pending model
poses, marks, current cursor, failed overflow and retained runtime identity.
PMD/PMX physics tests cover the latch before/after each subsequent frame and
unchanged bodies during paste, pose paste and repeated Undo/Redo. The command
order comes from the recovered Windows implementation; existing original kernel
oracles do not by themselves constitute an original UI command-loop comparison.

Local validation passed **29 P4 tests** (37.34 seconds), five Win32 editor tests
and the Qt application build. Physical continuity/reset coverage now includes
both PMD and PMX in modes 1, 2 and 3. The existing original history/clipboard
oracles retain their kernel-only scope.

The mode-clipboard commit `5d73716` passed all three editor jobs in
[run 41339639854](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41339639854).
The five-workload performance gate `5a39958` passed all three editor jobs in
[run 41339877600](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41339877600).
Both commits also passed the formats/rendering workflows.

## Independent viewport window

Native commands 280/281 now detach the Qt viewport and set its topmost preference.
The original 22 viewport controls move with it; the main timeline expands to the
client width minus three pixels and restores its previous sidebar width on
docking. Closing the separate window docks it, while closing the editor also
hides the separate window. Normal size, placement and topmost preference persist.
Language rebuilds retain the same controls, scene and shortcut ownership.

Each detach/dock creates a new native viewport and retires the previous GPU
controller. CPU document, model resources, physical world, selection, camera,
history and playback remain owned by the existing session. Qt's default geometry
restore clamps sizes to its current screen; the editor explicitly preserves the
native normal-size behavior, including windows larger than the available screen.

The shared separate/fullscreen aspect-fit helper is also used by the Win32 path.
The original x64 routine at RVA `A4DA0` was executed directly for **720 exact
rectangles**, covering odd/tiny client dimensions and five render aspect ratios.
The branch at `A4F03` is redirected to the original epilogue only after all fit
calculations and before device/window work. No entrypoint, window, desktop DC or
GPU is used. Frozen hashes are `7821e82aa0df027f` (separate) and
`70ae0dfcd4fc4c24` (fullscreen); the earlier 128,760 control rectangles retain
their exact hashes. Fullscreen geometry coverage does not yet implement or
accept the fullscreen UI command.

Qt offscreen coverage checks toolbar geometry, three languages, resize,
topmost state, docking during a header drag, playback and unloading. The native
presentation test additionally repeats detach/dock and device replacement on
each available backend. Local Windows uses never-shown windows; Linux/macOS
presentation remains restricted to explicitly isolated CI displays.

The preceding deferred-reset commit `669bbda` passed all three editor jobs in
[run 41340796548](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41340796548),
plus the formats and rendering workflows. P4/C1 remains in progress.

Final local validation for the independent-window batch passed all **29 P4
regressions** (37.44 seconds), the Win32 layout regression, both Qt/legacy editor
builds and the never-shown DX11/DX12/Vulkan lifecycle test. The 720 original fit
rectangles and earlier control-layout hashes remained exact. Cross-platform
native-window results are recorded after the corresponding CI run.

## Fullscreen viewport transitions

Command 290 and viewport Alt+Enter now enter fullscreen, with Esc/Alt+Enter
restoring the previous window mode. Entering from a separate viewport first
docks its controls; exiting restores that separate window and its size. The
main menu, editor panels, timeline and interpolation controls are hidden while
fullscreen; the viewport uses the independently checked fullscreen aspect fit.
Repeated key events and Alt+Enter inside a text field do not toggle the mode.
The latter is consumed at the text-field boundary so Qt cannot propagate it to
the parent canvas. Local tests never call showFullScreen or change display modes.

The expanded isolated presentation gate exposed two additional window-boundary
bugs in `2cbb913`: X11 lost the swapchain surface after changing the topmost
flag, and Cocoa focus changes could submit untouched, formatted readout text.
Topmost changes now retire the device before native parent replacement and
create a fresh viewport afterward. Window transitions suppress intermediate
render submissions. Text fields commit only user-modified text, so a focus-out
cannot seek the current frame or round the live pose. These failures remain
recorded in [run 41341871954](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41341871954);
new cross-platform results are required before accepting these changes.

The fullscreen/native-boundary revision passed **29 local P4 regressions**
(37.65 seconds), the Qt application build and never-shown Windows presentation
on DX11, DX12 and Vulkan. Fullscreen round trips, playback, unchanged document
and body state, field-focus behavior and both docked/separate restoration are
covered. These are local results; the corrected X11/Cocoa paths still require
an isolated CI run.

Cocoa fullscreen/detach acceptance passed in editor run `41342536406`. X11
still exposed a replacement-container visibility issue: a new child of an
already-visible widget is not automatically mapped. `CreateViewport` now
explicitly shows its container; hidden ancestors continue to keep local windows
invisible. The Qt menu/editor checks and all three never-shown Windows backend
lifecycle runs passed after this correction; the isolated X11/Wayland rerun is
still required.

## Original numeric dialog templates and camera conversion

The numeric command now uses the original six-field bone and seven-field camera
forms, including camera distance. `scripts/editor_dialogs.py` independently
decodes the committed original `RT_DIALOG` records and compares them with the
four English/Japanese source templates before generating Qt geometry and stable
translation IDs. Simplified Chinese supplies its own labels. Control order,
IDs, coordinates, default button, initial selection and three-decimal formatting
are preserved. Dialog base units (Arial `7/15`, MS PGothic `7/12` at 96 DPI) are
measured in a memory DC by the local original oracle; logical geometry stays
fixed across font fallbacks. This does not claim glyph raster equivalence on
systems without the original fonts.

Direct execution of the original x64 camera numeric-command branch found a
constant that differs from the toolbar: input angles divide by `3.141592f`,
while acceptance multiplies by `3.141595f` before division by 180, with float
rounding at each step. The shared helper preserves that distinction. The
explicit `editor_numeric_oracle_test` runs **256 exact input/output cases**
through original instructions, with hash **`a78d309a17788cb1`**. Its only dialog
call is replaced by a capture/input stub and its publication tail returns before
scene/UI work; no window, original entrypoint or GPU executes. The same frozen
hash gates all three portable editor builds.

`qt_numeric` compares actual Qt widgets to independently decoded binary template
JSON and checks tab order, formatting, decimal-prefix parsing, Enter/Esc, IME
preedit/commit, cancellation, distance publication and bone Undo. It requires the
`offscreen` QPA before creating dialogs. Numeric overflow and nonfinite values
remain rejected by the shared editor boundary. The bone quaternion conversion
continues to use the existing shared editor rotation helper; the new original
command oracle specifically covers camera values, not all bone command side
effects or full native dialog painting.

Final local numeric-dialog validation passed **30 P4 tests** (38.14 seconds),
the Qt application build and the 256-case original camera command oracle.
The original's 19-byte numeric read limit and floating-window dialog topmost
rule are retained. Glyph rasterization, native IME engines and all other dialog
commands remain within the open P4 input/dialog gates.

X11's in-place QWidget topmost change still failed after child-only replacement
in `2a76407`. Commit `b92b790` instead retires the entire parent hierarchy using
the existing detach/dock ownership path, preserving controls and session state.
Local menu/editor/numeric and three-backend hidden-window tests passed; the
corresponding isolated rerun [41344081405](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41344081405)
passed Windows, macOS, and both Linux X11/Wayland paths.

The initial numeric-dialog commit exposed a macOS libc++ limitation: floating
`from_chars` is unavailable on the supported baseline. Commit `814da78` uses an
explicit decimal-prefix grammar and C-locale Qt conversion, preserving the
19-byte read limit and rejection of nonfinite/overflow values. Regression cases
cover exponents, incomplete exponents, malformed signs, suffixes, underflow,
and a German default locale. [Run 41344709113](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41344709113)
passed all three editor jobs, including native viewport lifecycles.

## Morph and display registration command state

Morph/display registration now clears all bone, Morph and display marks in the
target model, including retained free slots, and selects the newly registered
keys. Other models and global tracks retain their marks. Mixed command batches
clear each model once and append only bone history. Auxiliary registration
explicitly suppresses implicit bone history from selection-byte changes, so
existing Undo/Redo remains available. Display registration captures current
visibility, IK and external-parent values rather than reusing root visibility.
The global last-registration bound includes the model's retained maximum frame.

The optional `editor_auxiliary_oracle_test <original.exe> <fixture>` maps the
hash-verified original and executes its Morph (RVA **`CA9C0`**) and display
(RVA **`CAF80`**) registrars without windows.
It passed **72 exact checkpoints** across PMD/PMX display and PMX Morph root
overwrites, inserts, appends and repeated overwrites, comparing physical slots,
links, weights, visibility, IK and maximum-frame state. The harness performs
the dispatcher's mark clearing itself; it does not claim independent execution
of the entire command dispatcher, free-slot exhaustion, or external-parent
registration. The PMD fixture has only a base Morph and excludes it from edits.

Shared regression checks cover free-slot marks, unrelated model/global marks,
failed-command atomicity, mixed duplicate addresses, redo preservation, saved
reload and unchanged CPU runtime/frame count. Actual Qt buttons run in all three
languages under `offscreen`. All 30 local P4 checks passed (the existing scope
test now explicitly clears registration marks before testing additive range
selection), and the three touched Win32 editor tests passed. P4/C1 remains open.

## Screen size dialog and output bounds

The screen-size menu command uses original English/Japanese dialog templates
653/607, checked against independent `RT_DIALOG` bytes by the same generator as
the bone/camera forms. A shared template widget preserves control IDs/order,
geometry, fonts, default button and focus while each form owns its own parsing
and publication. Chinese has a separate catalog. Width/height use original
`%3d` initialization, 19-byte integer-prefix input, cancellation and the
floating-window dialog topmost rule. Nonpositive/overflow input is rejected
before resource creation; the native dialog's acceptance of nonpositive values
is deliberately outside the valid render-size contract.

The RHI exposes render-target dimension limits for DX11, DX12 and Vulkan.
The Qt viewport reuses its active device or queries a 1x1 offscreen device before
first presentation. The dialog intersects those limits with the existing 4096
per-dimension portable output bound. This is an implementation bound, not a
claim that the GPU cannot create larger attachments. Allocation may still fail
within the reported limits, and the existing renderer reports that failure.
Changing output size updates docked/floating viewport aspect, retires GPU
targets/history for recreation, and persists through PMM saving. CPU world,
poses and per-model history remain owned by the editor session. The dedicated
`SetRenderSize` command preserves retained model/global maximum-frame bounds,
including after key deletion, rather than recalculating them as a document edit.

Local validation passed **30 P4 tests** (46.49 seconds), the rebuilt Win32
editor-session regression, and the Qt application build. The expanded offscreen
editor/render test passed on DX11, DX12 and Vulkan; all three never-shown Windows
native viewport runs passed aspect changes, resource recreation and repeat
submission. The tests compare all six dialog templates, exercise screen-size
cancel/accept in three languages and both window layouts, compare repeated
offscreen images strictly, and check PMM persistence and unchanged bone history.
[Run 41346704398](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41346704398)
passed Windows, macOS and Linux, including isolated X11/Wayland viewport lifecycles.
Full P4/C1 acceptance remains open.

## Morph menu commands and empty selectors

The four Morph register buttons now use an explicit optional channel. An empty
selector clears this model's marks without accidentally registering channel zero.
PMD's base Morph is excluded by resource identity, including PMM channel-name
reordering; a PMX panel-zero Morph remains registrable. The all-Morph menu records
all eligible current weights, then seeks only that model's current frame, matching
the native command's replacement of pending bone/display poses. Neither operation
adds bone history. Other models and the CPU world's frame count remain unchanged.

Morph reset clears live weights, preserves existing keyframes and Undo/Redo, and
preserves the document's pre-command modified flag. Publication revision advances
so cached geometry is invalidated. The four edit controls display the original
`0.00000`; slider positions are zero. Both menu commands follow model/playback
availability and are exercised under all three languages with PMD/PMX fixtures.

The original registrar oracle now includes a generated PMD facial Morph, passing
**120 exact checkpoints**. It additionally executes the original reset command
branch at RVA **`522F8`**, with only Win32 control calls replaced by recording
stubs and the dispatcher return adapted to the harness. Four PMD/PMX clean/dirty
cases compare all model-header/application bytes, weights, control identities,
text and slider messages. No window is created. This extends the kernel evidence;
the entire all-Morph dispatcher, table exhaustion and full event loop remain
outside this oracle's scope.

All 30 local P4 tests passed (48.37 seconds), as did the rebuilt Win32 registration,
history and session tests. The Qt application builds.
[Run 41347892063](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41347892063)
subsequently passed all three platform jobs. P4/C1 remains in progress.

## Name-bound editor channels

PMM channel order can differ from the loaded PMD/PMX resource. The shared runtime
now exposes both directions of its validated bone/Morph maps, including duplicate
name handling inherited from resource binding. Reverse maps are built once when
the binding layout changes. Invalid or unloaded lookups leave the caller's output
unchanged. PMD base-Morph exclusion now uses the resolved resource index.

Qt Morph categories, timeline rows and collapsed summaries, named range scopes
and IK selectors use document channels for edits and resource channels for labels
and category metadata. Timeline cache invalidation includes the bound names, even
when the immutable resource and group flags remain unchanged. The new offscreen
regression reverses PMM channels for four public PMD/PMX fixtures, exercises actual
controls in three languages, and checks persistence and unload invalidation.

Local validation passed all **31 P4 tests** (51.17 seconds), the separate scene
runtime test, both touched Win32 tests and the 124-checkpoint original auxiliary
oracle. The CPU performance gate remains unchanged. The application builds;
[Run 41348684659](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41348684659)
subsequently passed all three platform jobs, including the new channel-binding
test and native surface checks. Full P4/C1 acceptance remains open.

## Rotation reset and physics commands

Qt now routes rotation reset, physics-bone selection and physical-body reset to
shared commands. Rotation reset changes only the active bone, preserves its
translation and records one pose Undo; without an active bone it does nothing.
Camera mode clears all three angles without bone history and sets the modified
flag even when the angles were already zero. Physics selection replaces the
bone selection, uses validated document/resource mappings, and retains the
former active bone if the model has no physical bones. Selection does not advance
the scene or modify the document. Body reset only latches the existing next-frame
reseat, including during playback; repeated requests coalesce.

The optional auxiliary oracle now calls the original complete command dispatcher
at RVA **`3F550`** for commands 268, 273 and 302. Only view/panel refresh and the
history-capture call are stubbed; the original selection loop, D3DX quaternion
conversion and deferred-reset writes execute. **36 checks** cover PMD/PMX,
physical/nonphysical models, absent/first/last active bones and camera mode.
Actual history content remains covered by the separate history oracle. Together
with the Morph/display cases, this target passes **160 checkpoints** without
creating windows. Qt menu tests exercise the three languages, Undo/Redo, no-op
and playback gates. The continuity regression checks that the body reset keeps
the live world and poses until the next tick, which consumes the request once.

[Run 41349247733](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41349247733)
passed all three platform jobs for these commands.

## Modeless edge thickness

The Qt edge window uses the original English/Japanese binary dialog templates
(662/645) and translated catalog entries. It retains a single modeless instance;
reopening the menu preserves pending text. A slider movement applies immediately
to the currently selected model. Only Enter commits text, reading seven UTF-8
bytes with the original decimal-prefix behavior. Enter leaves the text unchanged;
the slider independently clamps to 0..200. Negative and greater-than-two finite
model values remain legal. Nonfinite values are rejected before document mutation.
Closing either the dialog or its owner discards uncommitted text.

The live-property path updates model metadata and publication revision without
seeking, pausing playback, resetting physics, evaluating CPU poses, recalculating
key watermarks or consuming bone Undo/Redo. Camera-mode interaction is a no-op;
changing the selected model while the dialog is open redirects subsequent edits.
The host's model-combo refresh replaces pending text and slider position with the
new model's saved edge value (including reselecting the same model). Camera-combo
selection closes the dialog. The low-level camera-mode setter remains a no-op.
Save/reopen preserves the accepted value. Detached viewports make newly opened
dialogs topmost, following the original rule.

The optional numeric oracle now executes the original dialog and edit procedures
at RVAs **56E50/57190**, replacing only their window-control calls. **213 exact
checkpoints** cover initialization, noncommitting messages, seven text cases,
all 201 slider positions, model/camera selection and close. The existing 256-case
camera corpus retains hash `a78d309a17788cb1`. These kernel calls create no window.

A subsequent probe of actual never-shown original EDIT controls exposed a second
rule: numeric prefill and slider replacement use `EM_REPLACESEL` without
`ES_AUTOHSCROLL`. The narrow edge edit stores `0.5` when supplied `0.57`; Enter
therefore commits `0.5` unless the user changes the text. The portable prefill path
now uses measured original Arial/MS PGothic ASCII advances and EDIT insets rather
than host fallback-font widths. This applies to all eight numeric templates.
Programmatic `setText`, like native `WM_SETTEXT`, remains distinct from insertion.
The optional oracle verifies the glyph tables in memory DCs and **522** prefill/
slider replacement results using actual hidden original controls. Full keyboard,
clipboard, IME and window-manager acceptance remain separate input gates.

Three-language offscreen tests cover the binary geometry and labels, focus loss,
Enter/close, repeated opening, model changes, playback, unloading, Undo/Redo and
PMM persistence. The generated two-sided `outline.pmx` additionally exercises
actual color output: a larger edge changes the image, restoring the value restores
the original bytes, and main deformation buffers/CPU evaluation counts stay
unchanged. This batch does not close the overall P4/C1 acceptance gates.

Local validation passed all **31 P4 tests** (53.04 seconds), the separate scene
runtime test, and rebuilt Win32 session/physics-continuity tests. The complete Qt
render regression also passed DX12 (27.90 seconds) and Vulkan (8.86 seconds),
including strict restored-outline output. The Qt application builds.
[Run 41351022451](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41351022451)
subsequently passed all three platform jobs, including numeric/menu/render tests
and native surface checks. The wider P4 gates remain open.

## Physics-enabled key selection

The original "select physics on frames" command now uses a shared chain walker
in the Win32 compatibility path and portable scene session. It clears selection
bytes across the selected model's bone, Morph and display pools, including retained
unused slots, then selects enabled keys on the forward chains of bones with rigid
bodies. The terminal key is included and the successor sentinel is tested as a
signed value. An empty chain touches only its own root. Resource/document name
maps determine which document bones own rigid bodies.

The Qt command updates both typed record marks and timeline addresses, retaining
unrelated model/global selection. It does not change revision, physics, poses,
key topology or per-model Undo/Redo. Camera mode is a no-op; the menu is unavailable
during playback or without a model. Saved PMM record marks survive reopening.
The broader synchronization of all timeline-selection operations remains pending.

The optional auxiliary oracle executes original command **274** through dispatcher
RVA **3F550**, stubbing only its panel paint tail. **18 cases** compare all **600000
bone, 20000 Morph and 1000 display** record marks for PMD/PMX, physical/nonphysical
models, ordinary/reversed bindings, empty chains and alternating physics flags.
The complete auxiliary oracle now passes **178 checkpoints/cases**. Local Qt menu,
portable selection and scene-runtime tests passed (10.75 seconds); the legacy
model-key-selection test and Qt application build also passed. The rebuilt Win32
portable selection test passed (4.10 seconds). No window is shown. Three-platform
CI for this selection batch passed all three platform jobs in
[run 41352087825](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41352087825).

## Edge-window model-combo integration

The numeric oracle additionally executes original model/camera combo branches
**677B6..678BB** and **671C3..671DC**, with recorded control calls and an adapted
branch continuation. Two model refreshes replace pending text and slider position
without publishing an edit; the camera branch destroys and clears the window.
These checks create no window. Qt regression now activates the actual model combo
under all three languages, including same-model reactivation, camera closure,
reopening, resource replacement on project load and playback after reopening.
Ordinary frame/dirty-state refreshes retain uncommitted text.
The rebuilt Qt numeric/menu/channel-binding tests passed (5.54 seconds), as did
the extended original numeric oracle. The application builds. Three-platform CI
for this host follow-up passed all three jobs in
[run 41352599572](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41352599572).

## Timeline selection metadata and persistence

Timeline clicks, Ctrl toggles, rectangle selections, scoped ranges and column
extension now publish selection bytes through a validated metadata-only scene
operation. Model selection and project opening restore the occupied record marks;
camera/global selection and each model retain independent marks. Save overlays
the active UI selection onto its private output document, without evaluating the
scene or changing history. Imported model root marks are immediately reflected
in the timeline. Other models and global tracks remain untouched by model edits.

The format-level address contract is shared by the editor and scene runtime.
Validation includes detached occupied records and rejects missing addresses or
invalid tracks before publishing any marks. Unused-slot marks survive additive
selection and saving. Clearing operations identify both track kind and slot so
selecting one accessory's range cannot clear another accessory's unused records.
Named/selected model rows follow active chains; full array sweeps also address
detached occupied records, matching the original distinction. Multiple records
can share a channel/frame address: range operations retain their actual record
identity, while unchanged UI addresses preserve each original mark during model
switching and saving. Selecting one visible diamond does not silently rewrite
an overlapping record that the range operation did not visit.

The auxiliary oracle now executes original range command **415** through the
full dispatcher, replacing only control-text queries and two UI refresh tails.
**22 chained command checkpoints** compare complete bone/Morph/display pools for
PMD and PMX, including root-only ranges, additive named rows, reversed bounds,
empty ranges, unused marks and detached/overlapping occupied records. All **200** auxiliary
checks/cases pass. The portable tests additionally cover atomic rejection,
per-model/camera selection, two accessories, save/reopen, unchanged revisions,
runtime identity, CPU update counts and history availability. Qt tests exercise
real mouse clicks and Ctrl toggles, then save/reopen after undo. Both targeted
tests passed again after the overlapping-record follow-up (12.94 seconds) with
no desktop interaction. The final revision passed all **31 P4 regressions**
(59.21 seconds), the scene-runtime regression (1.77 seconds), and rebuilt Win32
selection/session tests (8.31 seconds). The Qt application builds. Three-platform
CI for this batch passed all three editor jobs in
[run 41354546596](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41354546596).

Broader chained clipboard, capacity, keyboard/IME/DPI and full C1 output gates
remain open; this change is not P4 acceptance.

## Modeless gravity settings and registration

The gravity window now uses original binary templates 803/802, with English,
Japanese and Simplified Chinese catalogs. Reopening retains the single modeless
instance. Text changes immediately apply the original decimal-prefix conversion;
Enter is consumed by the edit and does not register a key. Direction sliders use
the native range, keyboard steps and accumulated wheel increments. These edits
have auto-scroll and retain their full text, unlike the edge-thickness field.
The noise checkbox enables its integer field; conversion preserves the native
signed-overflow result. Setting all direction components to zero nudges Y to
0.1. Nonfinite inputs are rejected before changing scene state.

Live changes update the existing physics world without evaluating a CPU pose,
recreating bodies, consuming bone history or marking the project modified.
Registration clears global/accessory key selection, inserts or overwrites the
current gravity key and marks the project modified; playback remains active.
Win32 and portable sessions share the same registration kernel. Overwriting does
not raise the timeline watermark, and a newly appended key retains the free
slot's successor payload. A full 10000-record pool still clears selection and
marks the project modified before reporting capacity, matching the original full
dialog command. Invalid topology or out-of-pool references remain atomic errors.
Closing restores the evaluated gravity at the current frame, including when a
never-shown test dialog or its owner closes.

The optional auxiliary oracle executes original routines **95B40**, **5DA60** and
**95DD0**, with only rendering/control tails replaced as appropriate. It checks
49 live channel changes, 24 complete gravity-pool registrations and the full-pool
failure command. Native Bullet set-gravity vectors are captured through a fake
world vtable; the portable normalizer retains a relative/absolute budget of
`2e-5 * max(1, abs(native component))`. This CPU normalization budget does not
change any image comparison threshold.

A second probe creates the actual original gravity dialog and EDIT/trackbar
controls without ever showing them. Eight frozen formatting cases execute real
EDIT notifications: a frame refresh rounds each field to two decimal places and
feeds that value back through the live setter, while first creation is quiet.
Qt reproduces the sequential refresh, including same-frame seeks. Native and Qt
also check eight keyboard steps and nine fractional/reversing wheel inputs.
The complete auxiliary oracle passes 282 command/value checkpoints, with the
trackbar checks additional. No DX9 device or desktop input is used.

Three-language Qt regression covers geometry, labels, live changes, all 201
direction slider positions, noise conversion, Enter, repeated opening, close,
playback, same-frame refresh, registration, bone Undo/Redo preservation and PMM
save/reopen. The portable session regression additionally checks full-pool
overwrite and invalid-index rejection. The final revision passed all **31 local
P4 tests** (61.69 seconds), scene runtime (1.85 seconds), rebuilt Win32 session
and physics-continuity tests (3.68 seconds), and the legacy global registrar
regression. The Qt application builds. Three-platform CI for this batch passed
all three editor jobs in
[run 41357549437](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41357549437).
P4 remains open.

## Delete unused model frames

Menu 222 now runs a shared native reduction predicate and the existing record-based
deletion/history path. It clears the active model's bone, Morph and display key
marks, then marks interior forward-chain keys whose values equal both original
neighbours. Roots and terminal keys survive. Inert-tip and effector bones are
excluded, with resource/document channel mappings applied before checking type.
Bone comparison uses position and quaternion components; curves and key physics
flags do not affect redundancy. Signed zeros compare equal; quaternion sign is
not normalized. Display comparison includes visibility, IK and parent selectors.
Adjacent candidates are all marked before deletion starts. Detached and unused
records are unselected, not mistaken for candidates at the same visible frame.

The command reevaluates the active model, preserves other models and global
selection, retains frame watermarks, and modifies the document even when no key
can be removed. Only bone-key deletions enter bone Undo/Redo; Morph/display
deletions remain outside that history. Camera mode is a no-op and playback gates
the menu. English and Japanese result text matches the original, with a separate
Chinese translation. Both ordinary deletion and reduction finish an active bone
drag before taking the document snapshot, retaining the restored edit mode.

The optional auxiliary oracle executes original command 222 at dispatcher RVA
**3F550** (branch **4C6AE**) and replaces only deletion and its result message.
**48 cases** compare every mark in the 600000/20000/1000-record pools and the
reported count against portable deletion results. Cases cover PMD/PMX, reversed
bindings, adjacent duplicates, unequal components, quaternion signs, Morph,
visibility/IK changes, empty chains and overlapping detached records. This is
independent evidence for candidate selection; the earlier frame-line oracle
remains the independent evidence for deletion slots and history captures.

Portable tests add repeated reduction, unaffected model/global marks, unchanged
runtime identity and simulation counters, bone Undo/Redo, save/reopen, playback
rejection and camera no-op. Three-language Qt tests exercise the actual action
and its gates. The legacy native-layout reduction corpus also passes after using
the shared predicate. The final revision passed all **31 local P4 regressions**
(69.83 seconds), scene runtime (1.76 seconds), rebuilt Win32 frame-line/history/
session tests (19.84 seconds), and the legacy reduction corpus (2.48 seconds).
The Qt application builds. All three editor jobs passed in
[run 41358526311](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41358526311);
this batch does not close the wider clipboard, input or C1 gates.

## Morph-key scale and offset

Menu 252 now opens original templates 661/643 with English, Japanese and Chinese
catalogs. The two fields start at `1.0` and `0.0`; the first receives focus and
selection. Acceptance reads nineteen UTF-8 bytes per field with native decimal
prefix conversion. Cancellation does not publish an edit. Input and arithmetic
results must remain finite; rejection retains keys, history and active drag state.

Win32 and the portable editor share the single-precision multiply/add operation.
It visits selected Morph records across the full pool, including retained free
slots and detached records, while preserving each overlapping record's own mark.
Identity retains the original float payload. Accepted identity and empty selection
still mark the project modified. The original apply tail seeks the active model,
so its pending bone/display poses are refreshed too. Other models and globals,
key links and watermarks, runtime identity and the bone Undo/Redo stacks survive.

The optional auxiliary oracle calls original apply RVA **9D120**, replacing
control-text retrieval and the seek/paint tails. **12 PMD/PMX cases** compare the
entire 20000-record Morph pool, including float bits, marks and links, and check
dirty/history flags. Cases include identity, zero, negative scales, fractional
offsets, suffix text, malformed prefixes and the nineteen-byte read boundary.
This increases the auxiliary command/value corpus to **342 checks/cases**, with
native trackbar checks additional. The native model-evaluation tail continues to
be covered separately by the CPU scene comparisons.

Portable and three-language offscreen Qt tests exercise the actual menu/modal
path, template geometry and labels, cancel/accept, history preservation,
save/reopen, empty selection, playback gating and atomic overflow rejection.
The final targeted Qt numeric/menu and session regressions passed (13.44 seconds),
as did the rebuilt Win32 session regression (4.23 seconds). The Qt application
and legacy Win32 application builds both pass. All three editor jobs passed in
[run 41359337435](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41359337435).
This does not complete P4.

## Camera-key scale and offset

Menu 242 uses original templates 657/623 and the three language catalogs. All
sixteen fields retain original order, geometry, initial focus, identity defaults
and nineteen-byte prefix conversion. Cancel leaves the session unchanged; model
mode and playback disable the command. The numeric resource generator now checks
fourteen templates against the original binary resources.

Win32 and the portable editor share the camera multiply/add kernel. It transforms
selected records across the 10000-record pool, including unused slots, while
preserving overlapping records' individual marks. Angle offsets use the original
single-precision truncated pi, with X negated. FOV converts the signed integer
through float and truncates back with the native overflow result. Identity skips
the arithmetic and preserves float payloads. Acceptance dirties even an empty
selection and reloads only the camera, resetting view offsets. Other global
values, pending model poses, frame watermarks and bone Undo/Redo survive. Invalid
input, nonfinite float output and out-of-pool indices are atomic failures.

This uncovered and fixes an independent camera evaluation bug: the PMM FOV DWORD
was treated as unsigned, so an ordinary 60-to-30 interpolation produced a huge
positive delta. Seek and playback now interpret the values and wrapping difference
as signed, with native float multiply/add. The optional playback oracle executes
original **59C30/69A20** on in-memory device recorders: **224 exact FOV cases** cover
ascending/descending/negative values, integer boundaries, nonlinear curves, exact
keys and terminal frames. Its existing **403200 playback value checkpoints** also
remain exact. No actual device or visible window is created.

The auxiliary oracle executes original apply **9C330**, replacing control-text
retrieval and seek/paint tails. **22 PMD/PMX cases** compare full camera pools,
float bits, links, marks and dirty flags; the separate camera oracle above covers
the evaluation tail. The auxiliary corpus now contains **364 checks/cases**,
with native trackbar checks additional. Three-language Qt regression exercises
the actual modal action, binary geometry/labels, tab order, cancellation,
acceptance and gates; session regression checks history, resource identity,
unchanged CPU counters, foreign state, save/reopen and rejected edits.

The final x64 numeric/session/evaluation regressions passed (8.90 seconds), as did
the menu regression, rebuilt playback/navigation/runtime tests (5.39 seconds),
and rebuilt Win32 session/evaluation tests (4.93 seconds). Both the Qt application
and legacy Win32 application build successfully. All three editor jobs passed in
[run 41360466522](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41360466522).
P4 remains in progress.

## Bone-key scale and offset

Menu 251 now uses original templates 660/636 in English, Japanese and Simplified
Chinese. Together with the other numeric windows, sixteen templates are checked
against original resource bytes. Modal cancellation, identity defaults, field
order, first focus, tab order and nineteen-byte prefix conversion are preserved.
The command is gated during playback and in camera mode.

Win32 and portable sessions share the selected-key multiply/add kernel. Positions
use float arithmetic; rotations decompose the original quaternion as Rz*Rx*Ry,
apply the native gimbal branch and signed degree offsets, then rebuild the
quaternion. Incoming quaternions are not normalized and angles are not wrapped.
Identity skips arithmetic. Selected roots, detached records and retained unused
slots participate; other records, interpolation curves and physics flags survive.
Only the active model is reevaluated after acceptance; foreign model/global state
and frame watermarks survive. Rejected nonfinite edits leave the session and an
active drag unchanged.

The original calls its selected-record snapshot routine on every acceptance, but
that routine returns without changing history if the pool has no selected record.
Selected identity edits therefore create undo entries; an empty selection retains
both stacks. Undo captures only touched slots plus the active model's live poses.
Partial key history now preserves physical selection marks, including detached
records sharing a frame and unused slots. Before capture, changed UI addresses
are synchronized without merging unchanged addresses; this also supports existing
command callers that modify selection directly. Repeated undo/redo preserves
unrelated selection changes made after the edit.

The new optional `editor_bone_scale_oracle_test <original.exe> <fixture>` executes
original **9C9C0**, its **CEBD0** snapshot and **CEF90/CFB80** undo/redo. Only text
retrieval and paint/evaluation tails are replaced. It compares all 600000 native
records in **64 PMD/PMX cases**, including two undo/redo cycles, identity, empty
selection, every position/rotation channel, nonunit input quaternions, both gimbal
signs and selection changes after apply. Positions, flags, curves, links and
untouched/identity rotations compare exactly. Changed rotations retain the
existing `2e-5` absolute CPU quaternion budget; observed maximum error was zero.
Pose production and model evaluation are separate existing CPU comparisons.

The final revision passed all **31 local P4 regressions** (76.30 seconds), the
scene-runtime regression (1.98 seconds), and rebuilt Win32 session/history/
frame-line tests (21.54 seconds). Qt and legacy Win32 application builds both
pass; the legacy host explicitly links the shared CPU math library. The native
oracle remained exact after adding selection changes between apply and undo.
All three editor jobs passed in
[run 41361587306](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41361587306).
P4 and C1 have not been accepted.

## Selected physics-key conversion

Menu 275 uses original templates 813/812 in all three languages. The generator
now verifies eighteen binary templates and carries each template's font family,
including the physics dialog's Japanese MS UI Gothic. The dropdown starts at
ON, uses the original ON/OFF labels, skips Tab navigation, and uses its measured
closed height rather than the expanded resource height. Cancellation leaves the
session unchanged; playback and camera mode disable the command.

The shared Win32/portable traversal visits selected records on physical bones'
forward chains once. Detached records, unused marked slots and nonphysical bones
are excluded. A nonempty conversion captures touched keys and live poses for
native partial Undo/Redo, including an identity conversion. No affected record
means a complete no-op. Conversion preserves the existing dirty flag, matching
the original command's lack of a SceneModified write; subsequent Undo/Redo keeps
the normal history behavior. Only the active model is reevaluated, retaining
foreign selection, live state, frame watermarks and runtime resources.

The optional `editor_physics_key_oracle_test <original.exe> <fixture>` executes
original **9ED40** and **CEF90/CFB80** with only paint/evaluation tails replaced.
It passed **48 PMD/PMX cases** comparing the complete 600000-record pool and two
Undo/Redo cycles: both ON/OFF values, clean/dirty documents, reordered document
bones, physical/nonphysical selection, detached records and unused marks. It also
executes original dialog procedure **9EC00** using actual never-shown templates
813/812 and controls, checking labels, initial choice, closed geometry, Tab style,
cancel/accept, dirty flag and resulting keys. All compared values are exact.
The bone-scale oracle shares the native fixture and still passes its 64 cases
with zero observed quaternion error.

The rebuilt x64 session test passed (15.39 seconds), three-language offscreen
numeric/menu tests passed (8.81 seconds), and the rebuilt Win32 session test
passed (8.07 seconds). Qt and legacy Win32 applications both build successfully.
All three editor jobs passed in
[run 41362527783](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41362527783).
P4 remains in progress.

## Model draw and calculation order

Menus 288/289 use original English/Japanese templates 805/804 and 811/810,
with a Simplified Chinese catalog. Twenty-two dialog templates now have binary
resource checks. Rows start unselected, skip Tab navigation, and move with their
stable slot IDs. The zero-size default button 633 consumes Enter without applying;
the visible OK button is 632. Original integral-row list height is reproduced.
The legacy draw dialog's last-row Down boundary was one row too permissive and
now matches original x64 57EDA (combo count minus two).

The document and runtime accept only complete permutations of loaded model slots.
Draw ranks start at one and calculation ranks at zero. New imports now initialize
both correctly; unload compacts each independently before a reused slot is added.
The Qt model/parent combos sort by draw rank, with localized model labels. Draw OK
switches to camera mode; calculation OK retains the current mode. Both preserve
dirty state, bone history, current live poses, resource identity and the existing
physics world. Reordering does not advance or regenerate the CPU frame. The next
evaluation consumes the new calculation order, and rendering reads draw ranks.

`editor_model_order_oracle_test <original.exe>` invokes original **57D70/58490**
using actual never-shown dialogs and controls, with only EndDialog and the model
combo refresh tail replaced. **24 cases** cover both languages, both order kinds,
cancel/accept, zero/one/three models, sparse slots through 254, labels, initial
selection, Enter's empty action, move boundaries, exact resulting order bytes,
dirty flag and scratch release. All pass. Camera-mode switching is covered by
the portable/offscreen path and the prior independent mode tests, not that stub.

The local x64 session regression passed (14.14 seconds), including invalid
permutations, history retention, save/reopen, playback gating and slot reuse.
The targeted Qt numeric/menu regressions passed (11.81 seconds). After rebuilding
all targets, the complete local P4 suite passed **31/31** (183.20 seconds). The new
distribution workflow additionally packages and verifies the production editor;
its results and remaining platform validation are recorded separately below.
The Win32 session regression also passed (17.78 seconds with concurrent builds),
and the legacy Win32 application builds successfully.

## Qt development distributions

The new `dist` target follows the independent deployment and relocated-package
verification pattern reviewed in Muffin. It uses Qt 6.8.3's deployment API, ships
Qt shared libraries, plugins, standard translations and renderer compiler runtimes,
and produces platform archives with SHA-256 and source revision metadata.
Windows explicitly installs the toolchain's MSVC redistributables. Linux uses
relative loader paths; the macOS bundle includes MoltenVK and an ad-hoc signature.
No original executable or user model is packaged.

The production editor's `--deployment-check` mode creates no visible window or
audio output. The package regression relocates it to a path with spaces, clears
SDK/environment search paths, opens generated PMM, switches three languages,
checks Qt's standard dialog translations, compiles a shader and renders offscreen.
The public fixture and image/report/log remain reproducible in CI.

`portable-editor.yml` builds and checks Windows x64, Linux x64 and macOS arm64,
then uploads archives only after the relocated-package check passes. Qt downloads,
pinned renderer tools and compiler object caches are separated; CMake build trees
are never cached. Version tags use the same jobs to prepare a draft release.
The legacy Windows workflow is manual to avoid competing tag publishers.
The local Windows package passed the clean-environment relocation check after
MSVC runtime deployment was made explicit; English/Japanese/Chinese standard
Cancel labels, bundled DXC, generated PMM and the 96x64 render passed. MoltenVK's
pinned archive and license extraction were also verified. These are development
packages, not P4/C1 acceptance.

Revision `b17fc2a` passed all three jobs in
[run 41366224450](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41366224450):
Windows **31/31**, Linux **35/35**, macOS **30/30** P4 regressions, followed by
native presentation and relocated production-package checks. Windows explicitly
executes DX11 and DX12 WARP; Linux runs Vulkan on isolated X11 and Wayland displays,
and macOS runs Vulkan through the bundled MoltenVK. Windows compiles and ships
Vulkan, but its hosted runner has no Vulkan driver; this is not a Windows Vulkan
execution result. Every package passes the bundled DXC compile check.

The run publishes `editor-dist-windows`, `editor-dist-linux`, and
`editor-dist-macos`, each containing its platform archive and SHA-256 file.
Qt and pinned-tool cache restores were exercised, and Linux also restored the
previous revision's compiler object cache. The two deployment issues found by
these checks are fixed: macOS temporary paths are canonicalized before comparison,
and its Qt configuration explicitly points to `Resources/translations`.
The Windows package includes MSVC runtime DLLs independently of developer-shell
environment variables. All three downloaded archives were checked against their
SHA-256 files and report clean source revision `b17fc2a`. The downloaded Windows
CI package additionally passed the relocated production check on the local
workstation. Full P4/C1 acceptance remains open.


## Selected-target paste and native WAV clock (2026-11-03)

Selected-target paste revision `5336222` passed all three editor jobs in
[run 41478321551](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41478321551),
including offscreen command tests, native surfaces and relocated distributions.

The explicit `qt_native_audio_test --native-silent` opens the platform audio sink
and supplies only float-zero PCM. It is intentionally absent from ordinary CTest
runs, which must remain safe for the developer's desktop and speakers. Native
initialization, a 120-second processed clock, stop, seek/restart, final EOF handoff
and the actual editor's jittered-timer clock selection are tested. Initialization
must finish within two seconds; after warmup the processed clock must stay within
100 ms of elapsed playback time, remain monotonic and advance regularly. The
jittered editor test allows at most three frames against elapsed time. These are
stream/clock bounds, not a measurement of acoustic speaker/display latency.

The first Windows run exposed Qt 6.8 WASAPI counting queued input as processed:
the clock led elapsed time by about 390 ms. The adapter now subtracts the actual
input-format device padding on Qt 6.8 Windows only. Applying that correction to
other platforms would count latency twice, so the version/platform condition is
explicit. A 40 ms queue then accumulated lateness under RDP timer scheduling;
100 ms provides adequate scheduling headroom without changing the clock bound.
The final **120-second Windows x64 Remote Audio** run passed: maximum lag
**80.936 ms**, no lead, exact EOF at **123 s**, and maximum jittered-editor error
**2.344 frames**. The [recorded result](evidence/cross-platform-p4-native-audio-windows.json)
identifies the device/profile and limits. No window was shown and no nonzero
sample was output. A device failure during playback now pauses without advancing
the scene and retains its diagnostic; the deterministic test verifies recovery.

The editor workflow now explicitly runs the same native silent check on all
three systems. Linux uses a real PulseAudio backend with a null sink. Windows
and macOS runners without audio endpoints write `unavailable`, emit a warning
and do not count as native acceptance; other failures fail the job. Reports are
retained beside the editor artifacts. CoreAudio/PulseAudio execution and physical
output qualification must not be inferred from the Windows result. P4/C1 remains
in progress for the other command, input, layout and full-frame gates.


## Display frame caps and timer precision (2026-11-03)

Original commands 234/235/236 now select 1000/30/60 display frames per second.
The shared session updates PMM's `view.fpsLimit` without marking the document
modified, capturing history, evaluating poses or rebuilding resources. Save
retains the rate, Open restores it and New resets it to 60, as in native
`ResetAppState`. Unrecognized loaded rates retain their stored value, select the
original 60 radio fallback and use a bounded timer interval; invalid user-set
rates reject without mutation. Display caps do not change the document's 30fps
time base. The optional original auxiliary oracle executes the complete native
command dispatcher for nine prior/new-rate pairs and compares the whole private
application state, including dirty, physics reset and cursor sentinels: only the
rate float changes. No window or device is created for that comparison.

Qt now uses a precise `QChronoTimer` with nanosecond intervals and elapsed-time
sampling. The previous millisecond `restart()` discarded a fractional millisecond
per tick, which could accumulate playback-speed error without an audio clock.
Frame caps preserve that elapsed interval across switches and export restores
the selected cadence instead of forcing 16 ms. The offscreen menu regression
checks all three languages/radios, shared-state invariants, persistence, invalid
rates and the real automatic timer at 30/60/1000: elapsed scene time stays within
50 ms of elapsed wall time for each 220 ms sampling window. The menu now exposes
65 of the original 103 commands; this count does not establish complete C1 parity.

The audio revision `c30add0` passed all 31 local P4 checks and the Windows CI
editor job. Its new PulseAudio test identified cold-start prebuffering, while
CoreAudio passed 120-second stream clock and EOF but failed the editor-versus-wall
startup assumption. The acceptance separates bounded startup from steady drift:
wait at most two seconds for the first processed samples, then retain the strict
100 ms steady bound. The editor test observes the same actual native adapter and
requires cursor agreement within one frame, recording wall latency separately.
It does not substitute wall time for the audio clock or classify startup as a
steady-state pass. Three-platform acceptance of this refinement remains pending.


The refined Windows native test also passed a complete 120-second run: startup
294 ms, steady maximum lag 35.984 ms / lead 5.461 ms, exact EOF and editor
agreement with the observed native clock to numerical precision (wall offset
62.243 ms). The versioned evidence now records the separate startup/steady
measurement and the unchanged 100 ms drift bound. The earlier CoreAudio result
had already passed its stream clock/EOF, but is retained as a failed overall
acceptance because the editor test had not passed. Linux/CoreAudio must pass
the refined test before their native qualification is closed.

## Modeless ground-shadow color (2026-11-03)

Original command 248 now uses templates 658/624 in all three languages. The
generator verifies twenty-four binary templates. Geometry, labels, the 0..200
trackbar and text insertion clipping follow the original resources and measured
font advances. Enter reads seven bytes and updates the color without moving the
slider or rewriting text: the original sends its position message to missing
control 458. Slider values use native single-precision division by 100. Focus
loss and closing discard pending text. New retains the color and modeless window;
opening a PMM restores its color without overwriting pending dialog text.

Color changes preserve dirty state, history, physics and CPU frame counters.
Save persists the scalar, Undo/Redo retain it, and the viewport invalidates its
prepared-frame cache immediately despite the unchanged document revision. The
optional original numeric oracle executes actual procedures 56A20/56D30 with
control-call recorders: **211 initialization/text/slider/close cases** compare
the complete private application state. Its never-shown original EDIT controls
also passed **954** width/prefill/slider checkpoints across ten templates. These
are independent command/control checks, not a full original event-loop comparison.

The Qt numeric and menu regressions passed locally in **14.20 seconds**, covering
English/Japanese/Chinese geometry, seven-byte input, slider publication, pending
text, playback, preserved history/counters, save/reopen/New, invalid numeric input
and prepared-frame reuse/invalidation. The Qt production application builds.
The menu now implements **66/103** original commands; P4/C1 remains in progress.

## Selected root-bone position bias (2026-11-03)

Command 219 now uses original modal templates 651/600 in all three languages.
Twenty-six numeric templates are checked against binary resources. Fields start
at `0.0`, select X initially, follow original tab order and read nineteen-byte
floating-point prefixes. Cancel, playback and camera-mode gates are preserved.

The session resolves each marked record's bone through its predecessor links
and adds offsets only when that bone's actual model parent is -1. Detached
records and marked retained free slots participate, while nonroot and unmarked
records retain their bytes. Addition executes even for zero offsets, preserving
native signed-zero behavior. Selected-record snapshot, live-pose capture, active
model evaluation and partial Undo/Redo are shared with bone-key scale; no CPU
frame advances, resource rebuilds, global tracks or foreign model poses change.
Invalid ancestry/nonfinite output rejects the entire edit before publication.

The original bone-scale oracle now also invokes the complete native command
dispatcher **3F550**, its original modal conversion and **CEBD0/CEF90/CFB80**:
**32 offset cases** compare every one of the 600000 bone records across both
languages, PMD/PMX, each axis, mixed/large/zero offsets, empty selection,
cancellation and two Undo/Redo cycles. Window/control calls are recorders and
paint/evaluation tails are replaced; no real window or device is created.
All comparisons are exact. Its existing **64 bone-scale cases** still pass,
with zero observed quaternion error after the shared helper refactor.

The x64 session regression passed in **11.80 seconds**; offscreen Qt numeric/menu
checks passed in **13.24 seconds**, including actual modal dispatch, geometry,
languages, cancellation, history, CPU counters, saving/reopening and gates.
The production editor builds. **67/103** original menu commands are implemented;
this is progress toward C1, not complete P4 acceptance.

## Native stop and Linux clock diagnosis (2026-11-03)

At `eb03d96`, Windows and macOS editor jobs passed in
[run 41483700893](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41483700893).
The native CoreAudio check completed **120 seconds**, seek/restart, exact EOF and
editor clock agreement: steady maximum lag **8.552 ms**, lead **6.279 ms**, startup
**72 ms** on Apple Virtual Sound Device. The [retained report](evidence/cross-platform-p4-native-audio-macos.json)
identifies the virtual endpoint; acoustic output latency is not qualified. Its
editor wall offset reached 1.554 seconds despite exact device-clock agreement,
so stop/restart responsiveness is being checked separately.

Linux's offscreen/native-surface regressions passed, but PulseAudio again failed
the strict native-clock budget. The first processed samples were followed by a
stall. The opt-in test now retains six seconds of timestamped samples after the
first violation and still fails, allowing prebuffering and ongoing drift to be
distinguished without hiding the error or relaxing 100 ms. Linux qualification
remains open.

Native pause/seek now calls `QAudioSink::reset()` to discard queued data rather
than `stop()`, whose PulseAudio implementation drains queued samples before
returning. This also avoids carrying previous playback into the next seek.
The explicit native test now requires stop within 100 ms. Local Windows silent
smoke passed with **3 ms stop**, steady lag **9.034 ms** and unchanged EOF/editor
clock checks; the deterministic transport regression also passed. Updated native
three-platform checks, including the Linux diagnostic, remain pending.

The Linux trace at `408dd9c` identified a more specific startup pattern: Qt first
extrapolated about 70 ms, held position during roughly 1.6 seconds of cold-server
prebuffering, then advanced at wall rate with a fixed offset. Startup acceptance
now requires 200 ms of continuous-rate progress within the existing two-second
budget, instead of accepting the first extrapolated samples. It records both
first progress and startup clock latency; the subsequent 120-second 100 ms
drift requirement is unchanged. A later CoreAudio run also exposed a post-start
underrun, so a single passing virtual-device run does not establish stability.

Qt's PulseAudio/CoreAudio pull refill timers formerly ran on the UI event loop.
Native output now owns a dedicated Qt thread and an independent QBuffer cursor
sharing immutable PCM storage. Device creation, refill, clocks and destruction
stay on that thread; editor work cannot starve refill. Pause/seek flushes its
queue before replacing the source. The native test deliberately blocks the UI
for 250 ms and requires clock progress while it is blocked. Local Windows silent
smoke passed: **250 ms progress**, **5 ms stop**, startup **425 ms**, maximum
steady lag **23.487 ms**; deterministic audio also passed. Linux/CoreAudio reruns
of the dedicated-thread implementation remain pending.

At `68359a5`, the macOS job in [41490908973](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41490908973)
passed its full **120-second** native silent check: maximum lag **13.114 ms**,
lead **4.197 ms**, startup **438 ms**, stop **97 ms**, 309 ms clock progress
while the UI was blocked and exact EOF. Its editor wall offset fell to 11.723 ms.
Local Windows also passed 120 seconds with **49.153 ms** lag, **6 ms** stop and
exact EOF. The versioned reports identify the virtual/RDP endpoints explicitly.

Linux passed the steady clock with **22.071 ms** lag / **68.041 ms** lead and
zero-ms stop, but failed EOF: the last reported position was 80 ms short. Qt's
PulseAudio implementation enters Idle when the server underflows while its
processed clock still subtracts device latency. The adapter now retains that
clock during the remaining tail, instead of handing playback to the UI timer
on the first Idle observation. It reads the final position after checking
completion. It neither substitutes elapsed time nor fabricates a processed
position at EOF. The existing strict native EOF test remains the regression;
Linux acceptance is pending its rerun. Local Windows deterministic audio and
the updated three-second silent native check passed, with **4 ms** stop.

## Outline color and live PMD state (2026-11-03)

Command 286 now opens the color picker in all three languages. It preserves the
original initialization packing, accepts RGB bytes and leaves dirty/history,
physics and CPU evaluation counters unchanged. The shared appearance revision
invalidates prepared frames immediately without inventing an editing operation.

The original D2240 kernel updates secondary-vertex colors only on currently
loaded PMDs. New imports start black, saving retains each live PMD color, and
opening a PMM reapplies its global color to all loaded PMDs. PMX material edge
colors remain independent. The fixed PMD path consumes vertex ARGB; the effect
path uses the native global RGB / 256 constants. Transient per-model color is
preserved through document validation and editing without adding a disk field.

The optional original numeric oracle passed **36** actual D2240 cases, including
PMD/PMX mode gates, zero/nonzero vertex counts and noncanonical input integers.
It compares all vertex bytes, the complete model record and buffer lock/unlock
calls. The offscreen render regression checks both PMD paths, PMX exclusion,
byte-exact repeated submission and restoration. Shared/Qt tests cover playback,
Undo/Redo, new imports, slot reuse, save/reopen, cancellation and all languages.
The menu now implements **68/103** original commands; C1 acceptance remains open.

The full local Release build, including the production editor, passed. All
**31** local P4 regressions then passed in **105.62 seconds**, using Qt offscreen
and DX11 WARP; no desktop window was shown.

## Independent self-shadow master switch (2026-11-03)

The PMM byte preceding the self-shadow track is the master switch, not the live
0/1/2 mode. They now have separate document fields. Track evaluation changes
only the live mode; both the topology writer and preservation writer serialize
the switch. Validation/rebuild retains unregistered mode, while successful file
load refreshes it from the current key, as the native self-shadow panel does.
Raw noncanonical switch bytes remain lossless until explicitly changed.

Command 279 and P toggle the independent switch in all languages, including
during playback. They preserve dirty/history, mode, keys, CPU and physics
counters. New retains the switch while resetting the mode; save/reopen and
Undo/Redo retain the switch. A visual revision refreshes the prepared frame.
The shared producer uses the fixed path while disabled and restores the same
effect path when enabled, without modifying track state.

The original dispatcher passed **18** switch cases, including contradictory
menu/state sentinels: only the switch and P-edge fields change in the complete
private application record. Menu calls are recorders; no window is created.
The format tests cover nine switch/mode combinations and lossless noncanonical
bytes. Offscreen rendering covers all three modes, byte-exact disable/restore
and the existing color/repeat checks. Shared/Qt regressions cover key transitions,
playback, keyboard repeat, languages, history, persistence and New. The menu now
implements **69/103** commands; remaining C1 gates stay open.

## Accessory order, names and model draw split

Command 249 now uses the original English 659 / Japanese 627 accessory dialog
geometry, labels and Enter behavior, with Simplified Chinese translations. The
shared session validates the complete slot permutation, CP932 names and split
before publishing. It preserves the runtime, simulation counters, model history
and clean/dirty state. Accessory order, names and the model draw split survive
PMM save/reopen; subsequent model undo/redo retains them. The existing shared
frame producer consumes that split to draw the chosen accessories before models.

The original hidden dialog procedure **572C0**, order builder **896E0** and
apply routine **89760** passed **8 additional independent cases**: both original
languages, one/three accessories and accept/cancel. Comparison includes all 100
name bytes, order, selected slot, registration row, split, scratch destruction
and the unchanged dirty flag. Native numeric prefix conversion and clamp text,
list selection, pending rename across moves, and rebuilt global scope labels
are checked. As in the original, the command is available in model mode too;
its global scope labels retain model-index dispatch until model-combo activation
restores the regular labels. Existing 24 model-order oracle cases still pass.

Local Qt offscreen numeric/menu and shared-session regressions passed, including
all three languages, actual modal accept/cancel, CP932 roundtrip, retained model
history and playback gating. The menu implements **70/103** original commands.
This closes this dialog's integration gap; the remaining P4/C1 gates stay open.
## Disable-WAV playback preference

Command 297 is a session preference independent of PMM's WAV reference. It gates
native audio startup and loop restarts, without changing a playing stream, scene
revision, dirty flag, history, pose or simulation counters. On a disabled loop,
the previous PCM may finish naturally while the editor releases that old clock;
reenabling the preference takes effect on the next loop/start. New/Open retain
the preference. All three Qt languages exercise these transitions with the
consuming-device adapter, including disabled startup with an injected device
failure, active-stream toggles, loop handoff and save/reopen.

The optional original full-dispatch oracle passed **12 independent command 297
cases**, comparing the entire application state for disabled bytes 0/1/7,
playing/stopped and WAV present/absent, plus the actual menu check write. Native
x64's D latch is at app+44. Existing 18 shadow-switch, nine FPS and all auxiliary
command/hidden-control cases still pass. Local Qt audio/menu regressions passed.
The menu implements **71/103** commands. This is not native audio qualification
or complete P4 acceptance.

## Complete offscreen frame cost, including GPU completion

The performance harness now has a separate GPU profile. It prepares the actual
Qt viewport frame and overlays, submits the shared scene renderer, waits for
completion and collects resources inside each sample. The generated fixture
tiles its 8,190 vertices rather than stacking thousands of coincident triangles.
Five modes retain the existing budgets: local p95 33.333334 ms / maximum 100 ms;
shared CI p95 100 ms / maximum 500 ms. There are 503 submissions, no timed image
readbacks, a byte-exact repeated final-color check, retained-frame immutability
and an unload/resize submission. Presentation latency remains a separate gate.

DX11 completion now waits on a native DXGI queue event instead of sleeping for
a Windows timer quantum. Ten local offscreen GPU/render/dynamic-buffer tests
passed. The generated WARP GPU profile passed in 4.89 seconds. Sour Miku on
TITAN Xp improved from 16.38 ms to 3.76 ms p95 for static DX11 frames and from
31.56 ms to 17.15 ms for navigation. Its animation, paused-physics and bone-edit
cases still exceed the local budget on one or more backends; all three hardware
reports correctly fail the unchanged gate. CPU update and geometry publication
remain optimization work. These results do not close the P4 performance gate.

The next geometry change assigns process-unique identities to CPU publications,
including edits at the same frame and replacement instances. The frame producer
uses that identity and exact outline inputs to skip unchanged channels; material
outline constants are evaluated per material. Selective publication preserves
the other channel's immutable storage. CPU skinning writes its unpublished
vertex slots directly, with inactive UV sets cleared when resources change.
Seven targeted regressions passed, including cached versus fresh-producer byte
comparisons, publication failure atomicity, cross-resource UV reuse and strict
GPU repeat checks. The generated GPU profile passed in 4.14 seconds. Sour Miku
navigation p95 was 10.14 / 14.34 / 10.84 ms on DX11 / DX12 / Vulkan respectively;
animation and bone-edit reports still fail the existing local frame budget.

Built-in atlas publication is now sealed before fingerprinting, so frame copies
share immutable HUD/helper pixels. Copy-on-write mutation tests preserve the
original atlas and cache/reset behavior. CPU evaluation now copies base vertices
only when an active vertex/UV morph needs private storage; bone/material-only
frames skin the immutable base directly. The existing 120-frame reuse regression
alternates zero/nonzero vertex, UV and material morph weights and compares fresh
evaluations, including replacement with a smaller UV layout.

All **33 local P4 tests passed in 91.29 seconds**, followed by five targeted
asset/geometry/runtime tests. Sour Miku (139,692 vertices, TITAN Xp, 640x480,
503 submissions/backend, no timed image readbacks) now passes every unchanged
local p95 33.333334 ms / maximum 100 ms case:

| Complete-frame p95, ms | DX11 | DX12 | Vulkan |
|---|---:|---:|---:|
| Static | 1.33 | 1.89 | 4.04 |
| Navigation | 2.80 | 3.96 | 7.31 |
| Animation | 19.49 | 22.11 | 27.81 |
| Paused physics | 18.29 | 21.36 | 23.10 |
| Bone edit | 30.77 | 22.11 | 25.68 |

The largest individual sample was 36.41 ms. Retained-frame immutability, strict
repeated final color and unload/resize submission passed on all three backends.
This qualifies the named local offscreen profile; it does not measure physical
display latency or imply that all original commands/C1 behavior are accepted.


## Native editor frame effect gate (2026-11-03)

The shared producer now follows the original frame predicate at x64 RVA
`0x2A427`: paused bone-select/box modes omit self-shadow, projected ground
shadows and model outlines. Tool modes and playback enable those families;
the self-shadow master is tested by its low bit independently of its mode.
Timeline/sequence producers without an editor context retain playback rendering.
No simulation update is performed while choosing or preparing this frame.

The optional `editor_scene_gate_oracle_test` maps the hash-verified original,
executes its unchanged 50-byte predicate in a register-preserving wrapper and
compares the produced frame's targets and draw families. **120 cases passed**:
five edit modes, playback on/off, raw master values 0/1/2/7 and modes 0/1/2.
The original application state remains byte-identical. No window or original
entry point is used. The same public cases run as `editor_scene_gate` on all
three platforms. Local frame-contract, editor gate, generated GPU performance
and Qt rendering regressions all passed (four tests, 19.64 seconds).

Linux run 41505954622 compiled the native PulseAudio changes, but stopped at
`p4_scene_frame_contract` because the workflow's explicit target list omitted
its executable. The other 36 tests passed; native audio was not reached. The
workflow now explicitly builds both scene-contract and editor-gate executables.
This failure does not establish any native audio result.


## Qualified native PulseAudio clock and package (2026-11-03)

[Linux run 41508686075](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41508686075)
at `485f5c6` passed all **38 P4 tests**, isolated X11/Wayland presentation,
cold three-second and full **120-second** native silent PCM checks, and relocated
package validation. The preceding native-surface material-history fixture now
explicitly enters tool mode, as required by the original effect-renderer gate;
bone-select mode correctly has no stock-effect history to advance.

The [native report](evidence/cross-platform-p4-native-audio-linux.json) records
maximum steady lag **7.743 ms**, lead **4.668 ms**, startup **219 ms**, stop **0 ms**,
**256.09 ms** of sample-clock progress while the UI was blocked and exact final
position **123 s**. The editor agrees with its observed native sample clock to
floating-point precision; its maximum wall offset was **48.082 ms**. EOF uses the
server's final PCM index and measured remaining device queue, independently of a
rewind-history-delayed drain acknowledgement. No nonzero PCM or visible window
was used.

This qualification explicitly uses `module-null-sink norewinds=1`: 50 ms maximum
queue, 45 ms configured sink latency. Earlier default-null-sink runs with a
two-second cold rewind block failed the unchanged two-second startup gate and
remain recorded above. The passing result does not qualify that configuration,
arbitrary PulseAudio/PipeWire hardware or physical speaker latency. Windows,
CoreAudio and this Linux profile now each have native output evidence; other
P4/C1 command, input, layout and full-scene gates remain open.


## Model self-shadow and additive controls (2026-11-03)

Model-panel controls 440/441 now toggle the live PMM model flags. Fresh PMD/PMX
imports initialize self-shadow to enabled, matching the original model initializer.
The changes preserve playback, pose publications, physics counters, dirty state
and bone history; appearance revisions invalidate the viewport cache. Save/reopen
retains both flags and each language refreshes the checkbox state.

The scene producer now selects fixed or effect materials per model and omits
self-shadow casting for models whose shadow switch is off. Additive models use
`SRCALPHA/ONE` and suppress outlines. The fixed transparent additive path draws
clockwise-culled back faces before counterclockwise-culled front faces; effect
materials retain their single draw. Projected shadows remain independent.

The hash-verified original full dispatcher passed **24 exact model-toggle cases**:
PMD/PMX, paused/playing, raw flag values 0/1/7, and both controls. Complete model
and application bytes were compared, with the previous auxiliary oracle cases
still passing. Public frame tests cover 24 combinations of material paths,
transparent backface order, shadow casting and outlines. Three-language offscreen
interaction and PMM round-trip tests passed. All **34 local P4 tests passed in
104.91 seconds**; DX11/DX12/Vulkan offscreen pixel tests then passed additive
changes, exact restoration and fixed-material selection. The added pixel cases
reuse one GPU device/renderer, avoiding repeated pipeline/device creation inside
the 60-second regression timeout. The explicit backend selector now also verifies
Vulkan instead of silently retaining Windows' default when that name is requested.
The existing hidden DX9 shader reference passed all **39 scene/history cases**.
These are command, frame-contract and shader checks, not full original-editor
scene/input parity or P4 completion.

## Model transparency and non-display controls (2026-11-03)

Commands 214/283 and the original V/Shift+V/Ctrl+V priority are connected in all
three Qt languages. Playback keeps these controls usable; Ctrl+V pastes only
while paused and falls through to the display switches during playback. The
commands preserve dirty state, bone history, simulation counters and live poses.
The menu implementation count is now **73/103**; this is not complete C1 parity.

Fixed materials replace their diffuse Alpha with **0.5**; effect materials use
the original global transparency parameter and retain their material culling.
Transparent model instances omit outlines and projected shadows. Shadow-map
casters remain governed by the original material/model flags. Non-display hides
model bodies and outlines only in the effect frame, including its per-model
fixed-material fallback; fixed frames and shadow casters remain unaffected.
Viewports, still images and sequence export consume the same live display state.

The global and per-model transparency values are separate: successful PMM loads
initialize model `displayState` to zero while retaining the global parameter;
ordinary model imports inherit it. New clears transparency and retains the
non-display switch. A fixed-size table preserves these distinctions without
allocating in cached static frames. An initial map implementation failed the
existing zero-allocation gate and was replaced; that gate was not weakened.

The original full dispatcher passed **24 exact display-command cases** with
raw 0/1/7 flags, paused/playing, empty and sparse model slots (0/4/254), comparing
complete application/model bytes. Public tests cover 96 display/material/frame
combinations, language changes, shortcuts, load/import/New, pixel changes and
exact restoration. The final local **34 P4 tests passed in 101.84 seconds**;
DX12 and hardware Vulkan offscreen render/export checks also passed. The local
Vulkan software-device request reported unavailable, so that driver profile is
not claimed. No local test displayed a window.

In CI, run 41508681840 passed Windows and Linux but its CoreAudio clock exceeded
the unchanged 100 ms steady bound once (**111.462 ms** at about 29.8 s). Commit
0289d58 retains the failing sample, read-time brackets and native device state
for diagnosis. Run 41510696016 passed Windows/Linux; its macOS job was cancelled
without starting because the hosted arm64 runner could not acquire capacity.
Neither event supplies a new macOS audio or complete P4 acceptance.

## CoreAudio queue timeline implementation (2026-11-03)

The diagnostic run 41513382554 retained an actual **132.512 ms** clock lag:
the failing clock read took only 41 microseconds and the source queue remained
full. Qt 6.8's Darwin adapter counts consumed callback frames while ignoring
the callback's native timestamp. The macOS adapter now uses AudioQueue and
`AudioQueueGetCurrentTime` instead. Three preallocated 50 ms buffers refill on
the native callback thread from immutable PCM, without UI timers. Pause/seek
discard queued audio; EOF completes only after the native asynchronous drain.
Native queue failures and timeline discontinuities remain diagnostic data.

The existing 100 ms steady clock bound, UI-block progress, startup, stop and
short-tail EOF checks are unchanged. This implementation requires a new macOS
build and native silent-device run; it does not itself qualify CoreAudio or P4.

## Morph category frame purge (2026-11-03)

Commands 226/228/231 now clear all active mouth/eye/eyebrow keys, including
frame-zero and terminal records. Category selection uses the Morph panel,
independently of its vertex/bone/material type. The shared kernel also corrects
the legacy dispatcher, whose earlier port used the wrong field and left the
last key behind. Detached occupied records and unrelated free-slot payloads
survive. The command keeps bone undo/redo, frame watermarks and other models'
pending poses, re-evaluates the selected model and marks the document dirty.
Confirmation text and the original English/Japanese title quirks have localized
catalog entries. Cancellation makes no document change.

The full original dispatcher passed **72 exact pool comparisons**: PMD/PMX,
three categories, zero/one/two successors, reordered PMM names and accept/cancel.
Only the native UI refresh/seek tail is stubbed in that harness; its pool and
application writes execute independently. Public integration covers **36**
category/chain/name-mapping combinations, persistence, re-registration, retained
redo and playback/camera gating. Three-language Qt offscreen menus passed;
implementation count is **76/103**. All **34 local P4 tests passed in 113.89 s**.
These close the purge command's gap, not the remaining complete P4 exit gates.

## Playback physics policy (2026-11-03)

Menu 271 now implements the native next-start policy: force On/Off mode at
playback initialization, retain that mode while the run is active and restore
the previous mode on pause, seek or terminal stop. Toggling the policy during
playback takes effect at the next start, including a loop restart. The editable
PMM mode, world ownership, dirty revision and history remain separate from this
live override. New/Open retain the application policy; new instances default off.

The original complete dispatcher passed **24 byte-exact application comparisons**
with independent menu check state, raw policy values, camera/model and playback
states. Public tests cover all four modes on PMD/PMX, actual falling rigid bodies,
next-start changes, pause/seek restoration, loop and terminal stop. Three-language
Qt menu tests passed; command count is **77/103**. All **34 local P4 tests passed
in 104.82 seconds**. The legacy Windows editor also built successfully after the
preceding shared Morph purge correction.

## CoreAudio and three-platform performance qualification (2026-11-03)

[Run 41518934198](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41518934198)
at `6dbc9cf` passed Windows, Linux and macOS jobs, including isolated native
presentation and relocated Qt distributions. The new CoreAudio queue timeline
passed the unchanged **120-second** silent-device gate: maximum lag **0.486 ms**,
lead **0.105 ms**, startup **351 ms**, stop **15 ms**, **321.73 ms** of clock
progress while the UI was blocked, and exact final position **123 s** after native
drain. The [native report](evidence/cross-platform-p4-native-audio-macos.json)
retains device/timeline diagnostics, including the initial discontinuity. Earlier
Qt callback-clock failures remain recorded above. Linux also passed its qualified
45 ms null-sink profile; this Windows hosted runner explicitly reported its audio
device unavailable, so its green job does not add new Windows device evidence.

The [full performance evidence](evidence/cross-platform-p4-editor-gpu.json)
contains generated CPU/GPU reports from all three platforms and the existing
local Sour Miku DX11/DX12/Vulkan measurements. Each GPU profile includes **503
submissions** and GPU completion in total time, with no timed image readbacks.
All five CI cases passed unchanged 100 ms P95 / 500 ms maximum budgets; the largest
P95 was **11.714 ms** on the virtual Metal adapter. Cached static CPU preparation
retained zero C++ allocations. The local 139,692-vertex profile passed the stricter
33.333334 ms P95 / 100 ms maximum limits on all three Windows backends. These
qualify the stated offscreen workloads, not physical presentation latency,
arbitrary audio hardware or the still-open complete C1 parity gates.

## Native audio UI-block timing brackets (2026-11-03)

The separate macOS 26 run
[41518937609](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41518937609)
passed native window exposure/presentation after the exposure wait fix, but failed
the audio UI-block assertion: the device advanced 355.896 ms during a requested
250 ms sleep. That test did not measure the actual blocked wall interval, so the
existing report cannot distinguish scheduler oversleep from device clock error.
The macOS 15 qualification above does not qualify this macOS 26 profile.

The test now records timestamps around both native position reads, derives the
minimum/maximum actual interval and compares clock advancement with that interval.
It retains the original 50 ms lag / 100 ms lead limits, minimum 200 ms progress
and independent 100 ms steady drift limit. Missing/nonfinite clocks and device
errors fail; native diagnostics are also captured for the blocked interval.
The rebuilt Windows test passed a three-second silent-device smoke test: 250 ms
clock advancement against a measured 250.059–250.947 ms interval. A new isolated
macOS 26 run is required before that profile can be accepted.

The new macOS 26 run
[41522505900](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41522505900)
at `bce9a01` passed the full 120-second gate, native presentation, relocated Qt
distribution and Metal-only package. Its [raw report](evidence/cross-platform-p4-native-audio-macos26.json)
measured **359.666 ms** clock advancement against an observed interval bracket
of **359.648–359.691 ms** for the requested 250 ms sleep. This confirms that the
fixed requested-sleep bound can reject correct device progress on this runner.
Maximum steady lag was **0.862 ms**, lead **0.075 ms**, stop **10 ms**, EOF position
**123 s**, and editor frame error below **7e-15**. This qualifies the stated
macOS 26 virtual audio profile alongside macOS 15; it does not cover arbitrary
physical devices. The revised local Windows Remote Audio test also passed
120 seconds with the same limits; P4/C1's other acceptance gates remain open.

## Mouth-frame shift and reused predecessor evaluation (2026-11-03)

Command 225 and control 616 now use the original mouth-shift dialog: 75 by 45
dialog units, initial selected zero, original focus/tab/default/cancel behavior,
and localized English/Japanese/Chinese strings. Parsing matches the original
20-byte ANSI buffer and Windows `atoi`, including decimal prefixes and signed
saturation. The independent native parser checks passed six boundary cases.
The template catalog now verifies 30 original dialogs; implemented menu commands
are **78/103**, with the remaining commands still explicitly unavailable.

The shared shift kernel is used by both frontends. It selects the Morph panel,
adds frame offsets with unsigned wrap and preserves the original root-link and
unrepaired predecessor behavior when removing nonpositive frames. Detached
occupied slots and unrelated free payloads survive. The portable track validator
accepts those specific native topologies while continuing to reject cycles,
invalid ranges, foreign active ownership and nonzero free predecessors.

Reusing a deleted predecessor revealed an additional arithmetic distinction:
integer seek wraps both frame differences to unsigned 32-bit, while playback
subtracts floating frame time and wraps only the denominator. Both paths now
follow their independently executed original routines. A shifted/reused fixture
seeks to **-0.125** and plays to **0.375**; public tests retain those exact golden
values instead of deriving both expected results from the implementation.

The hash-verified original dispatcher passed **240** complete record-pool cases
covering PMD/PMX, reordered channels, boundary shifts, native seek/playback and
subsequent registration into a freed slot. UI paint work is stubbed; the original
pool, seek, playback and registration routines execute independently. The public
regression covers 32 shift configurations, pending foreign poses, undo/redo,
watermarks, dirty state, save/reopen and playback gating. All **35 local P4 tests
passed in 128.81 seconds**, followed by the strengthened exact-value regression
in 11.61 seconds. The legacy application also built successfully. Tests used
offscreen or never-shown windows; no local desktop interaction occurred.
This closes the mouth-shift command, not the remaining C1 input/output gates.

## Native low-power idle frame gate (2026-11-03)

Command 299 and checkbox 552 now share the same live setting in Qt, independent
of WAV gain and document history. Input wakes the scene at state 1; two settling
frames advance through 2 and 3, then state 0 skips paused physics, scene rendering
and presentation. Playback always restarts the gate. Paint/update/timer events do
not wake it. Panel, timeline, interpolation, viewport, IME, resize and focus input
are connected; commands and explicit document refresh also wake the frame loop.
The setting survives language changes and detached-window rebuilding and is
stored in application settings, outside PMM and undo history.

The legacy frame driver and Qt use one gate kernel. The original executable's
actual gate instructions passed **432 exact full-application checkpoints**, with
raw states 0/1/2/3/7/UINT_MAX, noncanonical enable/play bytes and the original
English-name-dialog override. Only an unrelated stack spill is omitted from the
isolated native wrapper. The full original dispatcher passed **48 command cases**
for menu/checkbox, raw flag, menu authority, playback and floating-window owner;
checkbox message routing and all application bytes were checked.

Three-language Qt tests confirm zero idle updates after settling, no paint
self-wake, input wake, uninterrupted playback, resumed continuous updates and
unchanged revision/history on toggling. The implemented command count is now
**79/103**. All **35 local P4 tests passed in 122.69 seconds**, and the legacy
application rebuilt. The still-unimplemented English-name window must connect
its lifetime to the tested override when that dialog is added; complete native
input and C1 output acceptance remain open.

## Native automatic blink registration (2026-11-03)

Command 227 now registers the original four-key blink pattern through the shared
session. It requires the exact Japanese blink Morph name, uses the original
15-bit random divisor **32767**, and retains the phase between invocations.
The original unsigned range comparison, preexisting marks, reused slots, partial
capacity writes, per-key overflow notifications and scheduled-blink count are
preserved. Empty/reversed ranges retain the original dirty-state behavior.
PMD's base Morph remains protected. The shared registrar also serves the legacy
frontend and keeps the native **20,000-record** pool and stale slot payload rules.
Inputs outside the editable document's signed frame range are rejected explicitly;
native behavior for those unrepresentable frames is not qualified.

The Qt dialog uses original templates 656/617, initially empty fields and the
original 20-byte ANSI integer parsing boundary. Its three-language regression
covers focus, tab order, geometry, cancellation, registration and playback guards.
The template catalog now covers **32 original dialogs**, and implemented menu
commands number **80/103**. The native oracle executes the actual original menu
dispatcher and registrar with deterministic entropy; **32 comparisons passed**,
including both model formats, repeated commands, full/partially full pools,
missing Morph and cancellation. Complete pool bytes, entropy consumption,
notifications, counts, watermarks and dirty state are checked independently.

All **36 local P4 tests passed in 126.50 seconds**. The legacy application built,
and its existing key registrar regression passed. All local windows remained
offscreen or never shown. The preceding mouth-shift and low-power batches also
passed all three platform jobs in CI
[41528142979](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41528142979)
and [41530437314](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41530437314).
Remaining C1 input, complete command coverage and independent full-scene output
gates stay open.

## Independent original full-scene raster comparison (2026-11-03)

The optional `editor_scene_original_gpu_test` now loads the hash-verified original
executable without its entrypoint and invokes its actual model loading, pose,
skinning, self-shadow and scene drawing routines. Its standard SM3 effect source
is verified against the original binary resource. The reference receives no RHI
packets, portable CPU snapshots, material constants or generated vertex buffers.
Both paths independently consume the same public generated model files and
specified editing inputs. White PMX fragments are counted through Alpha as well,
so a white background cannot turn an empty draw into a passing scene.

The ordinary D3D9 HAL remains unavailable in this RDP session. An explicitly
selected **D3D9Ex HAL** can render on **NVIDIA TITAN Xp** using never-shown windows.
Its test adapter maps Managed texture allocations to dynamic Default allocations,
and Managed vertex/index buffers to Default. It hides the Ex interface query from
D3DX so the original allocation requests reach that adapter. Original evaluation,
shaders and draw calls are unchanged. This qualifies that raster profile; it does
not qualify Managed eviction or ordinary D3D9 reset behavior. An exploratory
WARP/9On12 request was ignored by the runtime and is **not** counted as software
driver coverage.

**48 independent comparisons passed**: PMD/PMX, opaque/0.375 Alpha, rest and
registered bone/Morph poses, 512/257 pixel outputs, both self-shadow modes and
DX11/DX12/Vulkan. All final color bytes match exactly. The maximum shadow error
is **5.96046448e-8**, with exact coverage, within the existing absolute **1e-6**
criterion. Each case creates a fresh backend device and submits the frozen frame
twice; repeated color and shadow outputs are byte exact, for **96 submissions**.
The original targets are recreated between modes. Raw numeric results and the
binary hash are recorded in
[the scene evidence](evidence/cross-platform-p4-original-scene.json).

Run explicitly, outside ordinary public CI (no proprietary binary is uploaded):

```powershell
$env:MDS_TEST_DX9EX = '1'
editor_scene_original_gpu_test.exe <original.exe> <res/assets> [dump-directory]
```

There is no Present, visible window, focus change or desktop input. Required
raster coverage is asserted for every original color/shadow output. Existing
comparison thresholds remain unchanged. Combined grid/accessory/background
scenes, input/IME/DPI/font metrics and the other C1 exit gates remain open.

## Ordinary Morph registration capacity (2026-11-03)

The four panel buttons and all-Morph command now use the same 20,000-slot native
registrar as automatic blinking. Exact-frame overwrite remains possible in a full
pool; new writes retain successful earlier channels when a later channel reaches
capacity. Each failed key produces the original localized, icon-free notification.
Selection clearing, modified state, maximum-frame updates and bone history retain
their native behavior. Reused free payloads and sparse/unused slots survive through
one shared document adapter; import/clipboard registrars remain separate paths.

An additional **48 independent original registrar comparisons passed**, covering
both model kinds, 0/1/2 free slots, frame zero, interior overwrite and append, and
single/all-channel commands. Complete pools, watermarks and overflow counts match.
The complete auxiliary command oracle passed after the change. The public tests
also cover partial writes, retained redo, save/reopen and invalid-channel atomicity.
After correcting a fixture's stale links, all **three affected tests passed in
29.05 seconds**; the other **35 P4 tests** had already passed in the full run.
Qt tests exercised all three languages without showing desktop windows.

## Per-category file dialog folders (2026-11-03)

Command 301 now controls folder history for project, model, accessory, motion,
pose, WAV and background file choosers. The switch defaults on, as in the
original startup. Confirmation stores the folder before loading the selected
file; cancellation and confirmation with the switch disabled preserve history.
Disabling the switch uses the original `UserFile` category defaults; reenabling
restores prior folders. Settings persist outside PMM and undo history.

The original dispatcher passed **14 independent checks**: both toggle states
while stopped/playing, and both initial-directory policies for project, pose,
motion, WAV and background dialogs. File choosers and unexpected native modal
UI are intercepted; no chooser or message window appears on the desktop. Pure
Qt tests cover all seven categories and settings serialization. Three-language
offscreen interaction covers confirmation, cancellation, separate categories,
disable/reenable and unchanged document revisions. Implemented menu commands
now number **81/103**. All **36 local P4 tests passed in 145.78 seconds**.

The previous Morph-capacity batch passed all three platform jobs in CI
[41537940779](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41537940779).
The remaining C1 command, combined scene and native input/DPI gates stay open.

## Native mip switch and base-level sampling (2026-11-03)

Command 298 now switches the live viewport and image export between the approved
portable trilinear profile and bilinear sampling of level zero. The enabled
profile retains its separately documented difference from native anisotropy.
The switch persists in application settings, outside PMM/history; playback does
not disable it. Appearance revision invalidates cached frames without advancing
animation or changing edited data. Menu coverage is **82/103**.

The hash-verified original dispatcher and actual renderer-state initializer
passed **24 checks** covering menu authority, stale raw policy bytes, playback,
anisotropy limits and complete application bytes. The original disabled policy
is MIPFILTER_NONE, not nearest mip selection. A test device intercepts state
writes; no GPU/audio device or native window is created by this oracle.

The new GPU gate compares disabled mip sampling on a full, explicitly colored
chain with a physically single-level texture. It requires exact final bytes,
including Alpha, and a visibly different enabled result. Each profile covers
**32 cases**: point/linear, wrap/clamp, built-in/shadow/generic implicit/generic
explicit LOD, odd width and fresh devices. Repeated submissions and switching
back to either policy also compare exactly. DX11 and DX12 hardware/WARP and
Vulkan hardware passed locally; Metal and Linux software execution are required
by their platform CI profiles. Portable scene tests also verify the editor policy
and exact image restoration without additional CPU evaluation.

All **41 local P4 tests passed in 163.67 seconds**, including those five GPU
profiles, and the complete original auxiliary-command oracle passed. The
additional short-path Qt run passed after fixing the folder test to compare
directory identity: Windows CI uses an 8.3 alias which the chooser expands.
The preceding folder batch passed Linux/macOS but exposed that Windows fixture
failure; it is not recorded as a three-platform pass. Independent combined scene,
complete command and native input/DPI acceptance continue.

The legacy Win32 application also rebuilt successfully. Its owned DX9 replay
conversion regression passed with a new MIPFILTER_NONE preservation check.

The mip/folder correction at `a7973cd` subsequently passed Windows, Linux and
macOS in [CI 41542716500](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41542716500).

## Wireframe display and native raster contract (2026-11-03)

Command 287 now controls the shared Qt viewport in all three languages,
including during playback. It is live application state, persisted separately
from PMM and undo history. Six independent original command cases cover raw
display values 0/1/7 and stopped/playing, requiring the complete application
bytes to remain unchanged except for the canonical toggled byte. The complete
auxiliary oracle also passed (163.23 seconds); no window or GPU is created by
the command comparison.

The producer marks only model/accessory body draws as wireframe. Background,
ground, projected shadows and self-shadow depth remain solid; model outlines
are omitted. Remaining resources retain identity/version and no CPU frame is
evaluated by the switch. An initial test incorrectly required the omitted
outline's unused geometry to remain published; the corrected check verifies
the identities of resources that are actually referenced. Its regression and
the unsupported-device preflight test passed after the other 39 P4 checks.

The public offscreen raster gate covers 36 cases per profile: built-in material,
self-shadow material and generic HLSL, both cull directions/no culling,
opaque/translucent, odd width and new devices. It requires nonempty triangle
edges with an empty interior, exact repeated submissions and exact restoration
in both switching directions. DX11/DX12 hardware and WARP and Vulkan hardware
passed locally. Native Metal and Linux lavapipe remain required platform CI
profiles. The texture/render-target contracts and output tolerances are unchanged.

The independent original raster oracle exposed a Vulkan difference that the
structural gate alone could not detect: default rectangular lines added 382
covered pixels to the first PMD case. The backend now requires and enables
`VK_EXT_line_rasterization` Bresenham lines for the wireframe capability, in
addition to `fillModeNonSolid`. Unsupported devices can still render solid
frames, and reject wireframe before mutation. No tolerance was relaxed.

With that correction, **48 independent original comparisons / 96 submissions**
passed on DX11, DX12 and Vulkan (29.57 seconds). Generated PMD/PMX, opaque and
translucent materials, rest and registered bone/Morph poses, both shadow modes
and 512/257 sizes are covered. Final color is byte exact in every case; shadow
maximum error is 5.96046448e-8 within the existing 1e-6 limit. The profile remains
explicit D3D9Ex HAL on TITAN Xp with the managed-allocation test adapter; no
Managed reset/eviction or other hardware qualification is inferred. See the
[wireframe evidence](evidence/cross-platform-p4-wireframe.json). The legacy
application build and the DX9 owned snapshot conversion regression also passed.

The final revision passed all **41 local P4 tests in 134.94 seconds**. The menu
now implements **83/103** original commands. This closes the wireframe display
gap; complete command/input/layout and combined-scene C1 acceptance remain open.

The wireframe revision `3727087` passed Windows, Linux and macOS in
[CI 41546361233](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41546361233),
including native Metal, lavapipe Bresenham wireframe, isolated presentation,
silent audio and relocated deployment checks.

## Native file-drop routing and Qt integration (2026-11-03)

Main and detached Qt windows now accept local file URL drops. Files execute in
the original order, with PMM replacing the scene before later imports. PMD/PMX,
static `.x`, VPD, VMD, WAV and BMP/JPG reuse the existing shared loaders. A failed
file does not prevent later imports. Pose drops require a selected model.
Dropping a project onto a dirty scene uses the original icon-free OK/Cancel
confirmation and stops the remainder on cancellation. Dialog text is translated
through the common English/Japanese/Chinese catalogs.

`ClassifyDroppedFile` preserves the original whole-path substring order and
explicit lower/upper/title-case variants, including the dotless `Vac` needle.
It deliberately does not replace this with case-insensitive suffix matching.
The previous-folder switch applies before loading, including failure; JPG
retains the original omission from background-folder history. Unknown files
are ignored; remote URLs are not opened. Classified video/VSQ/Kinect inputs
retain an explicit unsupported diagnostic until their own implementation.
VAC placement loading was added subsequently; its independent parser and PMM
coverage are recorded below.

The optional `editor_file_drop_oracle_test <original.exe>` invokes the actual
x64 dispatcher at RVA 978B0. It intercepts only shell enumeration/cleanup,
menu/message APIs and terminal loaders, without opening files or creating UI.
**172 routing cases** passed: 42 suffix variants × model/camera × folder policy,
plus four whole-path precedence/dotless-name cases. All seven original directory
buffers are checked, including JPG's omission. A separate multi-file case
verifies cancellation after pose/motion, skipped later loads and the native
missing `DragFinish` cleanup call. Qt retains its own safe URL ownership instead
of reproducing that native leak.

The original advertises 512 UTF-16 units to a 256-unit stack buffer. An isolated
long-path probe terminates through the native stack-cookie check, so it is not
treated as a successful compatibility result. The portable classifier bounds
its view without writing a fixed stack buffer; embedded NUL and overlong-query
classification have safe public regressions. Shared file/PMM representability
validation continues to reject paths it cannot store.

The new three-language Qt offscreen gate exercises main/canvas/detached targets,
unknown/remote files, ordered model/accessory/background/silent-WAV loading,
shared VPD/VMD parsers, failure followed by success, JPG directory retention,
project cancellation/confirmation and model-required diagnostics. No audio is
played and no local window is shown. Native desktop drag delivery remains part
of the wider isolated input/DPI qualification.

All **42 local P4 tests passed in 132.79 seconds**, followed by the final native
dispatcher check. P4/C1 remains in progress.

## VAC accessory wrappers and native initial state (2026-11-03)

The shared editor now reads CP932 VAC wrappers before parsing the referenced
`.x`. Scale, position, radian rotation and the inverted shadow switch initialize
both the current accessory and its frame-zero key. The title and bone line are
ignored, as in the actual object loader; importing a VAC does not select or
attach a model bone. The optional seventh field supports old six-line files.
The native low-byte interpretation of the integer shadow field is preserved
without reproducing its adjacent-memory write. Malformed transforms and
overlong fields fail atomically instead of publishing partially initialized data.

The optional `editor_vac_oracle_test <original.exe> <fixture-directory>` calls
the hash-verified x64 object loader at RVA DC560. **96 exact comparisons** cover
six scales, eight shadow forms, LF/CRLF, signed zero and radian rotations. It
executes the original file reads, `fscanf`, name conversion and path resolver.
The ANSI mesh locator supplies an explicit CP932 conversion before invoking the
original resolver; D3DX mesh loading is observed and bypassed, and execution
stops after the actual VAC `fclose`. This qualifies that parser and its mesh
request, not native mesh/material allocation or the complete accessory renderer.
No GPU device or visible window is created.

The same observation distinguishes native naming and defaults: a plain `.x`
keeps its filename including extension, starts without shadows, and has parent
model -1 / bone 0. A VAC's name retains its mesh path text, not its title. The
shared importer now uses these values. Tests that exercise accessory shadow
rendering explicitly enable that switch. The wrapper detection remains the
original case-sensitive `.vac` substring; uppercase drop classification does
not imply uppercase wrapper parsing.

Accessory source paths retain the wrapper, so textures resolve beside it even
when the mesh resides in a different subdirectory. Save As stores the wrapper
relative to the new project. Reopening parses its geometry while preserving
the saved PMM current state and keys, even if the VAC defaults subsequently
change. Public regression covers CP932 mesh names, atomic missing-mesh failure,
reopen/seek, original defaults and all 96 import cases. Qt drop regression now
loads a VAC in all three languages. The offscreen GPU gate compares a generated
VAC to the same direct mesh with registered placement, requiring identical PNG
pixels; the mesh subdirectory deliberately lacks its texture.

The preceding file-drop revision passed all three platform jobs in CI
[41548046861](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41548046861).
The local P4 run passed 41/43 gates; its two material-history fixtures had relied
on the old incorrect initial shadow switch. They now explicitly enable it and
register their off-camera root state. Both affected GPU gates and the VAC gate
passed the focused rerun, retaining their strict history/pixel assertions.
P4/C1 remains in progress; these checks do not close the remaining input,
combined-scene output, layout and command gates.

The first VAC CI revision exposed an existing shared-editor path-decoder
override: it decoded CP932 but left PMM's backslashes literal on Unix. Windows
passed while macOS/Linux failed the new Save As/reopen case. The editor now
retains `SceneResourceResolver`'s standard separator normalization and control
character validation. Unresolved accessories also report their slot explicitly.
The same correction applies to referenced model paths. The failure was not
reclassified as an unavailable platform or removed from the gate.

## About dialogs and modal ownership (2026-11-03)

Commands 201 and 267 are connected, bringing implemented original-menu entries
to **85/103**. Both use icon-free, plain-text OK dialogs. Physics information
retains the original Bullet 2.75 text and URL, localized title, main-window
ownership and always-on-top flag. Product information belongs to the detached
viewport when present; it identifies the actual NG build version from `VERSION`
and credits the original MMD 9.32 author, rather than identifying this fork as
the original executable. Modal time does not advance the editor's frame timer.

The optional auxiliary oracle's `--about` filter passed **16 original complete
command comparisons**: both commands × English/Japanese × docked/detached ×
paused/playing. It intercepts MessageBoxA, compares the actual title/body/flags/
owner and the entire application state, allowing only the native input latches.
Qt's matching offscreen gate checks all three translations and the same
ownership/playback combinations, with unchanged document, appearance and CPU
update count. No local window is shown. Native system message-box geometry is
not inferred from this behavior check; broader layout/font gates remain open.

## Material reload and missing texture residency (2026-11-03)

Command 278 now reloads model/accessory textures and shared/model toon files,
bringing implemented menu entries to **86/103**. The editor and image exporter
resolve shared toon overrides from the application's `data` directory. Reload
retains the live CPU scene, geometry resource identities, undo state, current
pose, physics world and successful screen/material history. Content changes
increment texture versions; repeating an unchanged reload does not upload new
geometry or change resource versions. Structural/preparation failure leaves the
previous material set intact.

Missing or undecodable named textures become unbound, retaining their paths for
later recovery. This matches the native cache rather than silently retaining a
stale image. Fixed-function texture cascades terminate at the first missing
texture. The self-shadow shader follows native PMD/PMX residency selection:
PMX can promote a surviving sphere, whereas PMD requires its primary texture.
A missing PMD custom toon remains unbound with a zero RGB shadow tag; a shared
toon override falls back to the corresponding embedded image.

The optional auxiliary oracle's `--texture-reload` filter passed **4 original
command cases** and **6 original cache profiles**, without a GPU or window.
The command cases check toon-before-cache dispatch and unchanged application
state while paused/playing. Cache profiles execute the actual original reload
loop with observed D3DX/COM boundaries: old texture release, autogen usage 1024,
usage-zero retries, 1x1 retry/release, missing-file unbinding and bottom-left BGR
sampling into the RGB tag. These are cache/dispatch checks, not an independent
missing-texture whole-scene image qualification.

The Qt offscreen gate passed **18 language/model/shadow profiles** on local
DX11 WARP: disk edits remain invisible until reload; changed images affect final
pixels; repeat reloads retain exact pixels and versions; removing/restoring a
file unbinds and recovers it. Shared toon changes update shadow colors using the
native 1/256 conversion. The existing screen-history gate additionally reloads
between successful captures and verifies that the preceding target survives.

CI at `b0d6e05` passed Windows/Linux, including the VAC Save As path fix. macOS
passed that fix but failed the new about-dialog contract. Message boxes now
explicitly use Qt's widget implementation and the test reports each observed
field on failure. The frame timer also restarts after a modal dialog when it
was previously active. Three-platform requalification is required for this
revision. P4/C1 remains in progress.

Local `ALL_BUILD` and the complete **44-test P4 label passed (142.55 seconds)**
after this change; the run uses Qt offscreen and contains no desktop interaction.

The reload gate additionally passed DX12 WARP (9.91 seconds) and Vulkan hardware
(6.20 seconds). The explicit Vulkan software profile could not create a device
on this machine and is not counted as a render pass. The complete auxiliary
original-executable suite also passed after the cache additions.

## WAV preview while moving frames (2026-11-03)

Command 284 is connected to the shared editor preference and three-language Qt
menu, bringing implemented entries to **87/103**. Manual frame stepping, numeric
navigation and timeline clicks can restart a paused WAV preview, gated by the
existing disable-WAV startup policy. The preference itself changes no document,
history, pose or playback state and survives a new project.

The native x64 start time is `double(float(unsigned(frame - 1)) / 30.f)`. It is
not the rational timestamp used for ordinary playback. Source block alignment
is retained by the WAV decoder so the native signed-byte seek/truncation can be
reproduced before addressing normalized float PCM. A signed overflow causes the
native negative `SetFilePointer(FILE_BEGIN)` request to fail, leaving its newly
opened stream at the data start; notably frame zero previews that beginning.
The bounded portable reader reproduces the resulting sample position without
issuing an invalid file seek. Positions beyond EOF remain silent.

Preview never supplies the editor playback clock and stops using a precise
single-shot **33ms** timer. Repeated frame moves restart that deadline; starting
normal playback cancels it. Refresh/translation does not stop a pending preview.
This describes the request/stop window, not physical speaker latency or native
DirectSound's buffered tail.

The optional auxiliary `--frame-audio` oracle passed **12 complete toggle
commands, 80 original time-computation blocks, 12 original restart sequences and
220 actual original file seeks**. The latter uses temporary files at four sample
rates and five alignments, observing `_beginthread` without creating a worker.
The restart observer confirms timer 100/33ms and the original invalid call of the
volume value to `SetFrequency`; no sound device or window is opened. The Qt
silent consuming sink checks three languages, exact offsets, repeated stepping,
EOF, timer expiry, playback handoff, disable policy and output failure. Its
focused audio/menu/format gates passed locally. Native frame-input eligibility
and physical output timing retain their broader acceptance boundaries.

The reload revision's macOS failure was isolated to QMessageBox's platform
implementation ignoring its title. The dialog now sets the QWidget/QDialog
title explicitly, preserving the existing cross-platform title assertion.

Local `ALL_BUILD` and all **44 P4 gates passed (146.10 seconds)** with the WAV
preview implementation. The about test additionally exercises a running frame
timer, requiring no modal-time CPU updates and an active timer after closing.

The WAV/reload/about revision `ea1d7c2` subsequently passed Windows, Linux and
macOS in [CI 41556525831](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41556525831).

## Native key-pool capacity and registration failures (2026-11-03)

PMM editing, reconstruction and reading now share the native operational record
limits, including frame-zero roots: bone **600000 on 64-bit / 300000 on 32-bit**,
Morph **20000**, model display/IK **1000**, and global/accessory **10000**.
This replaces the arbitrary shared 100000 ceiling, which rejected legitimate
large bone tables while permitting impossible insertions in smaller pools.
Boundary validation includes sparse indices, free-slot caches and partial Undo
records. An existing key can still be overwritten at capacity; deleting a key
makes its physical slot reusable. No partial document is published on malformed
topology or unrepresentable frame numbers.

Capacity exhaustion is a successful native command with rejected insertions.
Registration retains prior successful channels, clears the appropriate key
marks and marks the scene modified. Rejected model registration retains the
existing maximum-frame watermarks. Rejected bone registration also clears the
pending edit marker and retains its bone Undo entry, even with no captured key
records. Global registration clears all global/accessory marks and selects its
row. Auxiliary registration does not add bone history or advance CPU simulation.

The Qt path reports each rejected registration with an icon-free localized
dialog. Model and global English text intentionally differ in spacing and
punctuation. The native camera button's message says **600000** even though its
pool holds **10000**; camera paste uses 10000. That inconsistency is preserved.
The production dialog path is tested in all three languages using Qt offscreen
and temporary settings; no editor window is shown and no user settings change.

The new portable gate covers **40** full/near-full camera, light, shadow,
accessory and display cases: frame-zero overwrite, existing sparse overwrite,
interior/end insertion, overflow, marks, retained resources/CPU/history, saving,
reopening and seeking. The format gate exercises all eight pool boundaries and
full smaller pools; a **100002-sparse-bone** PMM roundtrip guards the previous
reader ceiling. The optional original-executable `--key-capacity` observer
executes complete display and global registration commands, checking exact
native slots, payloads, message bytes, marks, modification and watermarks.
It passed **16 display commands, 16 camera/light commands and a full
600000-slot selected-bone command**. The latter compares the complete native
bone table, rejected channels, cleared edit markers, retained watermark and
the empty-capture Undo entry; portable Undo/Redo remains usable afterward.
The full auxiliary original-executable regression also passed. Local
`ALL_BUILD` succeeded and 44/45 P4 gates passed on the initial full run; the
remaining session fixtures expected impossible camera/gravity indices to enter
the document. They now check atomic rejection at that earlier shared boundary;
the focused session rerun passed (11.78 seconds), completing the 45 gate results.

This increment does not qualify every malformed legacy PMM file or complete
the remaining clipboard, combined-scene, input/IME/DPI/font and C1 exit gates.

The capacity-registration revision `d6e1fd8` passed Windows, Linux and macOS in
[CI 41560341463](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41560341463).


## Partial clipboard paste at pool capacity (2026-11-03)

Paste now reports rejected pool kinds while retaining the successful prefix of
that kind, then continues other kinds. Once a kind exhausts its pool, later
records of that kind are skipped even if they could overwrite existing keys.
All accessory slots participate in one accessory paste loop. Name mapping,
selected-target mapping, typed limits, mark clearing, insertion-only auxiliary
watermarks and the native same-command seek remain intact. Morph/display-only
paste clears bone key marks without adding a bone Undo record. The Qt path shows
one localized notice per stopped kind; camera paste advertises 10000, retaining
the original distinction from its registration button's 600000 message.

The optional `--paste-capacity` original-executable observer passed **27 complete
camera/light commands and four complete model Morph/display commands**. It checks
physical record slots, links, payloads, marks, messages, modification, watermarks
and retained history. Their frozen complete-table hashes are respectively
`bdf068b38686a6b4` and `ef5ef02e35193905`; these are also enforced by the portable
regression. Model native cases start with one or two free records: the original
initially-full Morph/display cursor reads one past its table, so that undefined
path is not used as qualification evidence. Portable initially-full cases reject
safely, alongside the native-qualified mid-loop exhaustion cases.

The portable capacity gate now covers **76** registration/partial-paste cases,
including independent exhaustion across kinds, overwrite order, preserved CPU
state/history, PMM save/reopen and initially-full model pools. Production Qt
registration and paste notices run offscreen in all three languages with isolated
settings. The complete clipboard/input and C1 acceptance gates remain open.

Local `ALL_BUILD`, all **45 P4 gates (171.32 seconds)** and the final native
paste-capacity rerun passed for this revision.


The partial-paste revision `ae83ed8` passed all three Portable editor jobs in
[CI 41562703954](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41562703954).
The separate foundation workflow exposed an outdated Morph predecessor assertion
on all four profiles. `dad2174` updates it to accept the native-cleared first
predecessor while still rejecting a predecessor at the typed pool limit; the
focused local evaluation test passed. This records a test correction, not a
relaxation of the original executable clipboard comparisons.

## VSQ automatic lip-sync (2026-11-03)

Command 224 and VSQ drops now enter the same portable bounded SMF reader and
editor operation. The menu requires a stopped selected model, cancellation is a
no-op, and the native `UserFile/Vsq` directory is used independently of previous
folder history. English, Japanese and Chinese use the same operation. The menu
implementation count is **88/103**. Existing seven-category folder settings are
accepted alongside the new representation.

The reader retains native one-byte metadata lengths and eight-byte VSQ text
prefixes, sequential ID/handle lookup, CP932 lyric bytes, vowel/consonant
classification and report formatting. Unlike the older x86 reconstruction,
the original x64 uses float division, multiplication and accumulation throughout.
Its tempo traversal subtracts each next *absolute* tempo time, including for
three or more changes, and note duration uses the starting tempo segment. These
behaviors are retained. Mouth keys use truncated float seconds times 30, a
two-frame lead-in, A/I/U/O envelopes and synthesized E=A+I. Every model key mark
is cleared, only the selected model is sought, and bone history, other live
poses and CPU simulation counts survive. Missing mouth data retains document and
history but marks the scene modified, as the native menu does.

The command writes the native `.txt` analysis alongside the input, replacing
the first whole-path lowercase `.vsq` occurrence as in MMD. The public fixtures
contain no third-party model/music data. The optional auxiliary `--vsq` oracle
runs **16 complete original menu commands** over generated PMD and PMX models:
all consonant groups, all five vowels, silent phonemes, H inheritance, changed
tempos/divisions, full and nearly-full pools. Complete Morph tables, linked
slots, payloads, selection marks, model/application watermarks, seek/scroll,
capacity messages and report bytes match. Two additional missing-mouth commands
check exact native message/caption, retained pools, no seek and the modified
flag. Its frozen table hash
`6ba3f777ceba5cd4` is enforced by the portable regression.

The portable gate additionally checks every truncated prefix, corrupt headers,
missing-mouth state, report creation, play gating, retained resources/CPU/history,
other-model poses and PMM save/reopen. Offscreen Qt tests exercise menus,
cancellation, original file filter/folder, ordered drops and a detached viewport
in all three languages. Inputs with missing structures, unsupported events or
unrepresentable negative/overflowing lead-in frames return bounded errors;
original out-of-bounds/invalid-file behavior is not claimed as compatibility.
Native transient debug dumps on malformed analysis are not reproduced. Physical
file-permission dialogs and all malformed historical VSQ dialects remain outside
this evidence.


Local `ALL_BUILD` and all **46 P4 gates (194.44 seconds)** passed. After moving
the standalone reader into the formats library, the four touched integration
gates and final 16-command native observer passed again. Repeated P2 scene
qualification also passed twice: 31 scene/mode cases each, unchanged frozen
snapshots and input hashes, maximum absolute difference 5.603e-05 within the
existing relative contract. The fixture directory creation is now idempotent;
this fixes the separate foundation workflow's second-run failure without
changing any existing PMM/model baseline bytes. P4/C1 remains in progress.


The VSQ revision `57444a8` passed all three Portable editor jobs in
[CI 41565690734](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41565690734),
as well as Portable formats, Portable rendering and Bullet regressions.

## English-name editing (2026-11-03)

Command 259 now opens the original modeless English-name dialog, using verified
RT_DIALOG 666/665 geometry, control order and English/Japanese labels, plus the
Chinese catalog. The model-info field retains its editable multiline/scroll behavior. Tahoma's independently measured 96-DPI base units are
7 by 14; the Japanese template uses the existing 7 by 12 units. The generated
template gate now verifies 34 binary templates. The menu count is **89/103**.

Names are live immutable metadata snapshots, separate from immutable CPU/GPU
model resources and motion-track identities. English model names also update
PMM's existing sourceName field. Bone, Morph, group and comment changes survive
ordinary editing, undo/redo, seeks and PMM saving within the live session;
reopening PMM restores model-file metadata except its saved English model name,
as in the original format. No edit creates bone history, advances simulation or
marks the scene modified. The enhanced-model application flag and quit reminder
are independent of PMM's modified flag.

The dialog commits the old entry before changing a combo, wraps previous/next,
skips Morph slot zero, preserves the special bone types' deselection, and applies
pending fields on Cancel/close as well as OK. OK adds the original preserve-model
reminder and enhanced flag. Record widths are 19/49 CP932 bytes; comments truncate
the multiline ANSI representation at 255 bytes before stripping CR. Model
metadata alone permits explicit '?' replacement for unrepresentable Unicode;
strict path and motion-name encoding remains unchanged. Partial CP932 final
bytes are retained in model metadata and rendered with a replacement glyph.
Unqualified Windows best-fit Unicode mappings are not claimed by these checks.

PMD normalization now retains all four model text fields and the native Japanese
fallback for English names when no English section exists. Model import retains
the header names instead of substituting the filename. English panel, parent,
scope and timeline labels consume the same metadata without changing resource
identities or document-to-resource channel mapping. Duplicate/empty labels carry
stable scope identities so they cannot redirect an edit to another channel.

The optional `--english-names` observer executes **16 original apply commands**
over generated PMD/PMX models, including initialization, all three cursor loops,
wraparound, old-input commit, hidden-bone selection, full record truncation,
comment normalization, Cancel/OK, display-row mirrors, exact preserve reminder,
retained bone history and unchanged scene-modification flag. USER32 controls and
UI-only redraw tails are observed in memory; no original window is shown.
Portable regressions additionally exercise metadata preservation through history,
playback and PMM lifetime, invalid/stale targets and resource/CPU counters.
Offscreen Qt checks cover all three languages, original control geometry,
editable comment, menu gating, one-item loops, Cancel/OK and model unload.

Saving an enhanced PMD, toon/physics enhancement, complete original font/raster,
input/IME/DPI, chained clipboard and combined-scene/C1 qualification remain open.
This increment does not complete P4 or C1.

Local `ALL_BUILD` plus the optional native observer target passed. All **48 P4
gates passed (204.29 seconds)** after the final source changes; the six touched
integration gates and all 16 native observer profiles also passed separately.

The English-name revision `239f9fc` subsequently passed all three Portable editor
jobs in [CI 41570670275](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41570670275),
plus Portable formats, Portable rendering and Bullet regressions.

## Enhanced PMD saving (2026-11-03)

Command 263 now saves the loaded PMD snapshot, including live English names,
through the frontend's atomic writer. The original file may have changed or
disappeared after loading. The source document is retained as immutable model
data; saving restores base-Morph positions in private scratch, normalizes edge
bytes and remaps duplicate base-Morph references to their first matching entry.
English, toon and physics sections are written explicitly in little endian and
reparsed before publication. English metadata retains truncated CP932 bytes
through save/reload; its replacement-glyph decoder is display-only. Strict disk
path and motion-name codecs are unchanged.

The save dialog shares the original Model folder history, PMD filter/default
extension, overwrite confirmation and English/Japanese title, with a Chinese
translation. Confirmed saves update the model's PMM reference and absolute save
path. The loaded texture directory remains unchanged, matching the original's
separate modelDirectory. Canceled dialogs retain both modification flags. After
an attempted write, the original command clears the enhanced-model reminder and
marks the PMM modified, including an open/write failure. Failed writes preserve
paths, poses, resources and bone history. Invalid/unsupported input is rejected
before the writer is invoked. The enhancement submenu is PMD-only in MMD; the
Qt frontend now also disables it for PMX, while shared metadata APIs retain
their independently tested PMX support. Menu implementation is **90/103**.

The optional `--enhanced-save` observer runs **16 complete original dispatcher
commands** with four generated models, Cancel/OK and injected open errors. It
executes the original PMD writer at RVA B59B0; CRT IO is captured in memory and
dialogs/menu calls are intercepted. No original window is shown. All defined
stream bytes match, including geometry, material fields, IK, Morphs, display
groups, English names, toons, rigid bodies and shuffled joint limits. The native
writer leaves stack residue after NUL in its 20-byte material filenames; only
that undefined suffix is zeroed before comparison. The four canonical stream
hashes are frozen as `355e96c5890e21aa`, `612e73fbdf2dae67`, `911300e0acc5fca2`
and `5efddf4560884ffa`. No floating-point tolerance is applied to serialization.

This comparison exposed an existing x64 legacy ABI error: RigidRecord::bodyId
is an int32 at offset 96, rather than intptr_t. Native save reads linear/angle
damping, restitution and friction at 100/104/108/112. The corrected declaration
now pins all four offsets; body pointer 120, inverse transform 128, record size
192 and the x86 layout are retained. This is a legacy record-layout correction,
not a change to the portable physics solver or its numerical baselines.

The public regression covers fixed stream hashes, malformed count/reference
rejection, base-Morph preservation, truncated CP932, English save/reload, PMM
save/reopen, unavailable source files, failed atomic writes, playback and unload.
Offscreen Qt tests cover three-language save/cancel dialogs, PMX gating and
reload without CPU evaluation or history changes. Toon/physics enhancement,
combined scenes and complete input/layout/C1 acceptance continue separately.

Validation: all **50 local P4 gates passed (203.57 seconds)**. Final serializer
changes passed the six affected integration/runtime gates, and the display-only
CP932 follow-up passed all four affected Qt gates (16.19 seconds). Both original
16-case observers passed. The final Qt application and legacy Win32 application
built successfully; a separate x86 compilation/execution confirmed the 172-byte
rigid record and all four damping/contact offsets. The P2 31-case report retained
its input hashes and existing numerical contract. No visible window or audible
test output was used.


## Toon enhancement (2026-11-03)

Command 261 now uses the original English/Japanese templates (682/681), with a
Chinese catalog. There are 36 independently decoded binary dialog templates and
91/103 implemented menu commands. The PMD-only/stopped gate remains active.
The collector retains successful entries when another entry fails, reports each
missing input, and closes on either result. Entering the default name for that
slot skips both probing and storage, so it does not reset an existing custom
name. Only complete success sets the enhanced-model reminder. PMM modification,
bone history, current pose and CPU evaluation counters are unchanged.

Live toon names and decoded CPU images are owned independently from immutable
model geometry. They feed ordinary rendering, backend replacement, export and
enhanced PMD saving. The original loaded texture directory survives saving the
PMD to another location. Removing a resident file does not invalidate its cached
image; explicit texture restoration discards cached image data and reloads the
current names. No GPU objects enter editor history. Shared toon detection now
matches the current material slot only: toon01.bmp in slot 2 is a private model
file, matching the original renderer.

The optional `--toon` original observer passed 16 complete dispatcher commands,
including both languages, Cancel/OK, default skip, multiple failures, cross-slot
default names, dialog prefill and reminder/PMM flags. It executes the actual
original dialog procedure/collector; only USER32 calls, text encoding and the GPU
file probe are intercepted. Portable BMP decoding and actual rendered material
changes have separate public offscreen Qt regressions. Inputs over the original
100-byte storage limit are rejected rather than invoking its CRT failure path.
The existing portable texture decoder's supported formats remain unchanged.

The preceding enhanced-save revision 47610be passed Windows/macOS/Linux Portable
editor [CI 41575490189](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41575490189).
Physics enhancement, combined scenes and full input/layout/C1 gates continue.

Local ALL_BUILD passed. The 52-test P4 sweep passed all 51 other gates; an
incorrect toon assertion that compared overlay versions across resizing was
corrected, then all five affected gates passed (19.90 seconds). The final toon
regression additionally passed on DX11 WARP, DX12 WARP and hardware Vulkan,
including a real second device and strict rebound-image equality after deleting
the resident private toon file. The optional 16-case original observer passed
again. The final application build remains part of this increment.


## PMD physics staging and selected-owner replacement (2026-11-03)

Menu 262 now opens the original modeless physics editor in all three languages.
The English/Japanese templates 684/683, all 119 controls and original control
ordering are extracted from the original resource bytes; the template gate now
checks 38 templates. Shape, dynamic/static and body/joint page radios use separate
exclusive groups. Model selection and playback are disabled while the dialog is
active. Unloading or replacing its model invalidates the dialog. Cancel restores
the selected model from the current frame; accepted edits replace only its
immutable PMD resource and Bullet owner, preserving the shared world, other
models, frame clock and per-model bone history. The enhanced-PMD writer includes
the new body/joint tables. Neither staging nor acceptance creates PMM undo entries
or advances simulation. Physics copy buffers survive New/Open like the original.

`editor_auxiliary_oracle_test original.exe fixture --model-physics` executes the
hash-verified original dialog/init/collector/subclass routines with intercepted
USER32 controls and no window. Its 100 snapshots are frozen in
`tests/reference/model-physics-original.json`. `qt_model_physics` replays them in
English, Japanese and Chinese, comparing control text and selection plus body and
joint float bit patterns. Cases cover add/copy/paste/delete, repeated deletion,
retained collision masks, scratch-slot reuse, duplicate ordinal resolution, shape
changes, group and attachment selection, page changes and Y-angle limits.

The comparison exposed differences from the old Win32 reconstruction: ordinary
angle edits calculate in double using float-truncated pi, numeric bulk reads take
nine bytes, and Enter on Y-angle limits defers validation until bulk collection.
The new shared implementation follows the observed executable. Body paste retains
the destination name, group, collision mask, restitution and friction. Deleted
scratch slots and visible combo rows remain separate, including the original's
stale-selection behavior. Invalid nonfinite physical values, unsafe fixed-buffer
names and unresolved body references are rejected before publishing a new model;
original memory-corruption behavior is outside the accepted input domain.

The CPU regression covers two models through playback modes 1/2/3, cancel and
accept, atomic rejection, PMD saving/reopening, retained history, unloading, PMX
gating and continued playback. Both new local gates pass. The Qt menu now marks
92/103 commands implemented; the staging window's viewport debug rendering and
menu 264 are the next rendering increment. Antialiasing, combined-scene and full
input/layout/C1 acceptance remain open. This increment does not accept P4/C1.

Validation: local `ALL_BUILD` and all **54 P4 gates passed (193.72 seconds)**.
The application and optional native observer were rebuilt after formatting;
100 original snapshots and the Qt physics test passed again, including the
disabled-playback keyboard dispatch guard. Tests created no visible windows.
