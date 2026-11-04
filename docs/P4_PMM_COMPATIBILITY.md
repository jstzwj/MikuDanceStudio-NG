# PMM sample compatibility and P4 gap acceptance

This is the retained PMM-repair investigation snapshot. Later keyboard and
user-approved visual acceptance changes are recorded in
[EDIT navigation](P4_EDIT_NAVIGATION.md) and
[complete-scene visual review](P4_VISUAL_COMPOSITE.md); the numerical failures
below describe the unchanged strict profile, not the current visual policy.

This change repairs project loading. **P4/C1 remains unaccepted.** Ordinary
headless tests, never-shown language switching and a successful project open do
not qualify real IME shutdown, complete control appearance or strict GPU output.

## Reproduction and native reference

The private MMD v9.32 installation's `UserFile` contains three PMM v1 projects:
`sample.pmm`, `sample(follow_camera).pmm` and `sample(AllStar).pmm`. Before this
change they fail resource resolution, duplicate-name validation or accessory
import. Once those failures are repaired, AllStar exceeds the Vulkan backend's
fixed frame budget. No private model, project or texture is added to this repo.

The reference is the original MDS `main@0e96995` implementation:

| Native behavior | Portable defect | Repair |
|---|---|---|
| `src/model/path_resolve.cpp`, `ResolveAnsiUserFile` / `0x4089F0`, relocates the saved `UserFile` suffix; `src/io/pmm_load_dispatch.cpp` determines the project root | Saved `G:` paths and old `HB\DownLoad\...` installation paths are treated as ordinary absolute/relative paths | Shared typed resolver probes explicit installation roots and the PMM's `UserFile` root, then saved paths and the type's basename fallback; explicit reselections retain priority |
| Bone indices identify bones; duplicate names are permitted | `ModelResource::Create` rejects a legal duplicate name | Retain index identity; PMM names match unused occurrences; VMD retains first-name binding |
| `src/render/accessory.cpp` uses `D3DXLoadMeshFromXW`, clones to FVF 274 and calls `D3DXComputeNormals` when the imported FVF differs | Ordinary `.x` import requires triangles and explicit normals and rejects vertex colors | Triangulate polygons with source-face material/normal mapping; drop colors at the native FVF boundary and generate angle-weighted normals when the native loader would regenerate them |
| Scene complexity is not limited to 512 render draws | DX12/Vulkan descriptor pools and uniform arenas have fixed capacities | Validate/reserve the frame's draw and constant budget; grow each completed frame slot before reuse, preserving pending resources; Metal uses the same constant-arena reservation |

CP932 decoding precedes separator normalization: the trailing `0x5c` byte in
characters such as `表` is not a directory separator. Foreign Windows drive
paths do not become relative paths on POSIX. Model, accessory, wave and picture
lookup share the same policy. Loading remains transactional, and errors now
include the failing model/accessory slot and source path.

Capacity growth is bounded (8,192 draws / 32 MiB of constants for DX12/Vulkan;
Metal retains its 4,096-draw limit). These are advertised limits, not unlimited
scene support. A failed reservation preserves previously prepared scenes. Frame
slots are replaced only after their previous GPU work completes. The macOS
implementation has not been executed on this Windows host.

## Regression coverage

Public generated tests cover PMM v1/v2, migrated paths, CP932, explicit search
priority/reselection, duplicate bones, polygon materials and normals, generated
smooth normals, color removal, playback/seek, save-as/reopen and failed-open
preservation. The GPU budget regression renders 1,024 draws, alternates large
and small prepared scenes through all frame slots, rejects impossible bounded
budgets and requires repeated images to match exactly.

The three private PMMs pass loading, playback, seeking, save-as and reopening:

| Project | Models | Accessories |
|---|---:|---:|
| sample | 3 | 28 |
| sample(follow_camera) | 9 | 29 |
| sample(AllStar) | 11 | 28 |

Each project also passes the never-shown production `--deployment-check` on
DX11, DX12 and Vulkan: nine profiles, including all three UI languages and
96×64 rendering. This is a load/lifecycle smoke check, not full-size image or
performance qualification. Local reports are in `out/build/pmm-deployment`.

The continuous-render profiles use the PMM v2 files saved by that actual
`EditorSession` import, then run six frames at 1280×720 / 1284×722 with one
device replacement on each backend. All nine profiles pass: five CPU advances,
exact scene-to-final copy, resource unload, and old-device frame rejection.
`sample.pmm` uses `--shadow 0`, matching its disabled scene self-shadow setting.
The lower-level P3 CLI intentionally rejects an unimported v1 camera parent;
requesting a shadow readback for a scene that disables it also fails that CLI's
diagnostic. Those initial diagnostic failures are retained, not counted as
successful profiles. Reports are in
`out/build/pmm-frame-lifecycle/editor-import`. These private rendered frames
have no frozen original reference and are not accepted as strict image parity.

The full 80-test ordinary P4 run passed 79 tests and exposed one obsolete test
assertion that ordinary imports must reject the operation-axis asset's missing
normals/colors. It now verifies ordinary import success and separately retains
the operation-specific material/normal and image checks. All six affected
contract/overlay rechecks pass. Both the original JUnit and the final recheck
are retained; this is not represented as an initial clean 80/80 run.
The Release distribution is rebuilt in `out/dist`; relocation with SDK paths
removed passes both offscreen and never-shown Windows profiles, and the deployed
Vulkan executable also opens the original AllStar PMM successfully.

The explicitly built `accessory_import_dx9_test` compares imported meshes
against D3DX. Its `--normals-only` profile invokes the actual
`D3DXComputeNormals` export on documented CPU mesh/buffer implementations, with
no device, window, vtable interception or copied native output. All 14 shipped
accessories parse, but the strict normal comparison remains unaccepted: the
largest measured component difference is about `3.4e-4` on `stage01.x`.
The device-backed full-import comparison is unavailable when the RDP session
does not permit DX9 initialization. Neither outcome is counted as a pass or
used to relax an existing image threshold.

## Separate P4 exit gates

| Gap | Acceptance decision | Evidence / remaining work |
|---|---|---|
| Windows native IME | **Not accepted** | Last isolated Microsoft IME run completed the English composition scenario, then blocked during natural Qt/native-window destruction inside MSCTF / Microsoft Japanese IME. No three-language natural-exit result exists. Offscreen language switching does not replace it. |
| Strict original UI parity | **Partially qualified; not accepted** | Corrected hidden resource-control/client tests pass. The corrected shown observer and disabled EDIT border repair still need isolated visible requalification; focus/scroll/multiline/hover, actual DPI and non-Windows font appearance remain open. |
| Text keyboard semantics | **Partially qualified; not accepted** | The independently observed Ctrl+Delete fitting-field corpus is qualified. Word navigation, Ctrl+A and Ctrl+Backspace remain unqualified; Qt defaults do not establish native EDIT equivalence. |
| Strict composite frames | **Not accepted** | Hardware DX11/DX12/Vulkan have separate strict-reference evidence. WARP, Metal and software Vulkan retain failures, excluded from ordinary P4 tests. RGB=1, exact Alpha/coverage and exact repeat rules remain unchanged. |

Fresh local strict-reference runs pass both frames on DX11/DX12/Vulkan hardware.
DX11 and DX12 WARP both still fail with 141 out-of-tolerance components / five
coverage differences in rest and 24 components / zero coverage differences in
posed. Local software Vulkan reports `software_device_unavailable`; Metal and
its changed arena code cannot be executed/compiled on this Windows host.
No isolated CI jobs are dispatched in this investigation, so the pending shown
control and native IME qualifications are not described as newly rerun.
Machine-readable scope, counts and hashes are in
[this change's evidence](evidence/p4-pmm-compatibility.json).

Historical isolated-run details and their limits are retained in
[EDIT parity](P4_EDIT_CLIENT_PARITY.md),
[position transport isolation](P4_POSITION_TRANSPORT.md) and
[consolidated evidence](evidence/p4-consolidated-41704895198.json).
Real input/visible tests must run in an isolated environment; this investigation
does not activate windows, inject input or capture the developer's desktop.
