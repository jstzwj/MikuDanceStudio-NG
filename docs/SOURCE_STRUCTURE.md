# Source organization review

The main boundaries are portable formats/math, model and scene runtime, editor
commands/session, scene-frame production, RHI/backends, and platform frontends.
The Qt frontend adapts shared editor state; rendering resources stay out of its
undo/document state. Win32 compatibility and original-binary oracles remain
separate adapters. Third-party sources are not included in this size review.

The layering is useful, but the project still contains large implementation
files. It is not yet a uniformly small, well-separated codebase. A line limit
alone is not a reason to fragment a recovered binary layout or a serialized
record table.

## Qt window split

The former 2,308-line `editor_window.cpp` mixed painting, file dialogs, viewport
lifecycle, commands and input. Its implementation is now split by responsibility:

| File | Responsibility |
|---|---|
| `editor_window.cpp` | Window/session lifecycle, refresh orchestration, language and frame clock |
| `editor_canvas.cpp` | Panel backgrounds, empty waveform and viewport title/readout labels |
| `editor_viewport.cpp` | Native viewport creation/replacement, docking, fullscreen and geometry |
| `editor_files.cpp` | Open/save, atomic writes, import/export and file drops |
| `editor_dialogs.cpp` | Numeric, appearance and model/physics dialog orchestration |
| `editor_input.cpp` | Main controls, keyboard/mouse dispatch and edit events |
| `editor_panels.cpp` | Panel values, scope choices and panel commands |
| `editor_menus.cpp` | Menu construction, enablement and menu commands |

This is a translation-unit split around the existing session owner, not a new
hierarchy of mutually dependent controller objects. File-local helpers stay in
their owning translation unit; shared model-name presentation uses
`model_labels.hpp`. No source implementation is included through another `.cpp`.

## Remaining first-party files over 2,000 lines

Counts below are physical lines at the time of this change, including comments
and data tables. Excludes generated build files and vendored dependencies.

| Lines | File | Appropriate next boundary |
|---:|---|---|
| 3,612 | `tests/editor_auxiliary_oracle_test.cpp` | Split independent command/oracle families while retaining the independent reference setup. |
| 2,889 | `src/render/rhi_vulkan.cpp` | Separate device/resource ownership from pipeline compilation and command submission; retain one private backend implementation. |
| 2,725 | `src/io/pmm_load_v2.cpp` | Serialized phases and compatibility adapter; extract explicit phase readers with existing binary/layout gates. |
| 2,388 | `src/editor/editor_session.cpp` | Move import/document I/O and key clipboard operations behind the existing private State/Commit boundary. |
| 2,249 | `src/render/rhi_d3d12.cpp` | Separate device/resource ownership from pipeline compilation and command submission; retain one private backend implementation. |
| 2,119 | `src/render/frame_scene.cpp` | Separate update preparation, backend selection and draw orchestration with frame-count regression. |
| 2,084 | `src/render/rhi_d3d11.cpp` | Separate device/resource ownership from pipeline compilation and command submission; retain one private backend implementation. |
| 2,045 | `include/mikudancestudio/mmd_control_specs.hpp` | Recovered 168-control data table; preserve IDs, order, styles and literal bytes rather than split solely for size. |
| 2,038 | `src/window/command_file_menu.cpp` | Group file/command families without changing original dispatch or save/cancel behavior. |

These remaining files have not been mechanically divided in this UI/build-output
change. Each later extraction should retain its behavioral oracle and relevant
backend/device-lifetime tests. Formatting is controlled by `.clang-format` and
the pinned formatter; see [formatting commands](FORMATTING.md).
