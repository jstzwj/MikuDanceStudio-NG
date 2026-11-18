# Background video lifecycle and `screen.bmp`

This records the native lifecycle before changing the portable session. The
source comparison uses MikuDanceStudio `4d5300681aa89b8e6a35a9ea8d9fe7dd5ae9be94`
and the recovered v9.32 addresses in that source. A source trace is not a new
native executable qualification; the VFW numbered-frame oracle separately
qualifies sample selection.

| Operation | Native action | Portable requirement |
| --- | --- | --- |
| Load AVI | `LoadAviFile` enables display and calls `AviBgOverlayRefresh` at the current scene frame. | Decode once even if the next operation hides the background. |
| Hide | `command_dispatch.cpp`, command 0x4871F9, changes the visibility flag only. | Keep an immutable CPU frame; hiding must not discard the media. |
| Seek/step while hidden | `ui_frame_step.cpp` and `ui_mouse_misc.cpp` gate refresh on visibility. | Preserve the frozen sample while moving the scene cursor. |
| Show | The toggle only restores the flag; subsequent frame refreshes use the scene cursor. | Resume scene-time decoding without changing model/physics time. |
| Save | `pmm_save.cpp` writes path, placement and visibility, not video pixels or the frozen time. | Do not add private pixels to PMM or silently persist a different freeze time. |
| Reopen hidden | `pmm_load_v2.cpp` reads `currentFrame` before calling `LoadAviFile`, then restores the saved hidden flag. V1 has the same load-before-visibility ordering. | Initialize the frozen sample at the **saved scene cursor**, even when hidden. It can differ from the old in-memory frozen sample; that difference is native behavior. |
| Mode 3 | `accessory.cpp::AccessoryScreenTexture` directly returns `AviBackgroundTexture`, independent of visibility. `CaptureAccessoryScreenTexture` copies the scene only for modes 1/2. | An initialized 1024-square video target must precede its consumers. Mode 3 never copies the scene or advances the video. |
| New/unload | `reset_app_state.cpp` closes the stream and releases the texture. | Invalidate pending publications and clear the session lease. |
| Device replacement | `device_reset.cpp` releases default-pool video resources. | Recreate GPU resources from the session CPU lease, with no resampling. |

An important exception: `command_view_menu.cpp`, command 276 (render a **still
image**), explicitly calls `AviBgOverlayRefresh` when mode 3 is selected, even
if the background is hidden. This is a refresh event, not a reason to advance
every hidden video on every paint. `playback_state.cpp` also explicitly refreshes
in the bone-box stop path. These explicit calls require a loaded decoder, not a
visible background. The older NG DX9 guard incorrectly included visibility.

The portable sequence/movie export contract captures an immutable session
snapshot on the owner thread. Hidden video uses that snapshot's frozen sample
throughout warm-up and output; visible video follows each exact output time.
A hidden project that has not decoded yet initializes at its recorded load time,
not at the export range start. Still-image export retains the explicit native
mode-3 refresh. Neither export may borrow or mutate the live viewport's GPU
cache. A missing hidden media file is harmless after a frame is resident; after
a cold reopen it must report a decoder error instead of sampling an undefined
attachment. Retry is explicit and must not let an obsolete decode replace a
new project, a reloaded file at the same path, or a newer requested sample.

The portable refresh request carries a session identity, path, scene seconds
and AVI timing mode. Publishing a decoded frame does not dirty the PMM, add an
undo entry, or update animation/physics. Preview, offscreen rendering and export
share immutable CPU image leases. Visibility remains the native PMM flag.
