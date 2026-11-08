# Qt MME editor closure (P5.5)

The Qt editor exposes **MikuDanceStudio-NG → MME effects…**. Select a model or
accessory, then the whole object or a material/subset. Assign an ordinary `.fx`,
enable/disable it, reload it, or edit its numeric parameters. Semantic parameters
and immutable declarations are read-only. Numeric vectors, matrices and arrays
are entered as whitespace-separated components; float values must be finite.
Effect undo/redo is separate from the original bone-edit history.

The dialog follows all fifteen Qt translations. Changing language, rebuilding a
viewport or selecting another backend does not own or destroy the project data.
The compatibility report retains the actual frontend/compiler/scheduler or
device error. Unsupported frames do not automatically become basic-material
frames; **Use basic materials** is an explicit, saved project choice. A failed
viewport shows an error surface and status text instead of its last good scene.
This starts a fresh renderer scene lifetime, allowing unchanged immutable CPU
resources to be uploaded again after recovery without reviving retired versions.

## Ownership and frame transaction

`EditorSession` owns only `EffectDocument`: object/subset addresses, source paths,
enable flags and logical numeric overrides. It has no native shader, GPU handle,
compiler, DX9 device or effect instance. Unloading an object removes its effects
from both the live document and effect history, preventing slot reuse from
resurrecting an old assignment.

`render::SceneEffects` owns the frozen definitions, distinct per-assignment
instances, shared parameter pool, bounded compiler cache, target registry and
pending schedule. Defaults recursively load declared `DefaultEffect` children.
The host freezes camera/material/light inputs and requested bone/morph control
items from the already published CPU scene. Accessory control items use the live
evaluated panel values, including the original 10x `Si` scale and `Tr` opacity;
parent attachment also follows that evaluated frame. It does not seek or advance physics.

`SceneFrameProducer` tags material draws with stable logical object/subset IDs.
The same assembly service is used by the viewport, image export and sequence
export. Host geometry, shadow prepasses and editor overlays remain in the shared
RHI graph. Native backends consume the assembled frame; ordinary presentation
does not read the frame back to the CPU. Effect/script writes commit only after
successful GPU submission. Preparation failures abandon them. Sequence export
freezes the effect document and uses a separate runtime, advances once per output
frame (including necessary history warmup), and publishes its staging directory
only on success; cancellation cannot alter the editing session.

## PMM companion files

Saving `project.pmm` writes the original PMM format unchanged and stores the NG
assignments in **`project.pmm.ng-effects`**. Copy both files when moving a project,
along with its referenced FX/includes. Paths are relative to the PMM when possible;
Save As rewrites them for the new location. Numeric values preserve their exact
32-bit components. No compiled/native resource is serialized.

The bounded, versioned UTF-8 companion includes a PMM content identity. Duplicate
addresses/parameters, invalid shapes/values and a mismatched project are rejected
before replacing a live session. Before publishing a changed pair, a matching
`.ng-effects.previous` is written atomically; an interrupted PMM write can reopen
the previous matching pair. When only effects changed and PMM bytes are identical,
only the companion needs publication. A reported failed write does not silently
change its effective project state. The previous file is a recovery journal,
not an additional file required for copying a successfully saved project.

## Public examples and deployment

`assets/effects/editor` contains CC0 material tint and two-pass scene examples,
including an ordinary relative `Common.fxh` include. Portable packages install
these under `assets/effects` (inside Resources on macOS) and bundle DXC and its
runtime/licenses for raw FX preprocessing and reflected layouts on desktop APIs.
The native material/MME effect description remains shared HLSL/SPIR-V.

`tests/editor_effects_test.cpp` covers project publication/recovery and logical
lifetime. `tests/qt_mme_test.cpp` runs the editor/parameter UI, strict two-pass
copy, save/reopen, playback/seek, device recreation, export/cancellation and
unsupported/reload recovery with generated PMD/PMX/accessory inputs. It runs
offscreen. `tests/qt_mme_present_test.cpp` separately submits the effect graph to
native presentation, then covers error surfaces and reload, resize, seek, device
replacement, actual surface destruction and unloading. Its Windows window is
never shown; Cocoa/X11 runs require an explicitly isolated display.
`tests/test_editor_distribution.py --mme` relocates the production
package to a path containing spaces, removes SDK/Qt search paths, and runs its
`--mme-deployment-check` without showing windows, loading the packaged raw FX,
include and compiler and saving/reopening/exporting the result.

The manually dispatched **MME editor closure** workflow selects all desktop
platforms or just an affected platform. It builds only the production editor and
the focused persistence/MME/existing-render gates, then validates the portable
package. Evidence and exact qualified platforms are recorded in the P5 plan;
implementation alone does not close cross-platform acceptance.

## Declared boundary

This batch integrates the P5.1–P5.4 supported ordinary FX9/SAS subset. It does
not claim arbitrary FX9, complete MME, full Ray, legacy EMM automatic project
import, vertex textures, arbitrary script expressions, media textures or every
render state. Explicit D24S8 remains a native capability requirement; it is not
silently replaced on Metal. Host `screen.bmp` history graphs not representable
by this scheduler report an unsupported host pass. Android has the UI/project
state, but runtime raw FX requires an executable preprocessing/compiler tool;
this desktop closure does not qualify mobile MME. These are reported boundaries,
not automatic basic-material fallback or promises of complete Ray compatibility.
