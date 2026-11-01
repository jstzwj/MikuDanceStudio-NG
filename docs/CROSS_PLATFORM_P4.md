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
| Qt original layout and multilingual interface | Implementation underway; three-platform UI validation pending |
| C1 scene rendering | Outlines, projected shadows, accessory shadows, screen history and material tints still pending |
| WAV and synchronization | Pending |
| Fixed-step image sequence export and cancellation | Local generated 30fps sequence, strict repeat and cancellation passed; other platforms pending |
| Three-platform open/edit/play/save/reopen/export | Pending |
| MMD v9.32 independent geometry, behavior and output comparison | Pending |
| Input, IME, DPI, font metrics and lifecycle acceptance | Pending |
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
