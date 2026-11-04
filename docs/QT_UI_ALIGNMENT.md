# Qt main-window alignment

The supplied MMD v9.32 and NG screenshots exposed main-window chrome differences
that were not covered by the dialog-control gates. This change addresses the
visible control types and missing canvas elements. It does not accept P4/C1 or
claim an independent, pixel-identical full-window comparison.

## Corrections

- Honor `BS_PUSHLIKE` from the recovered control specifications. Perspective,
  self-shadow modes, axis and other toggles retain their behavior while appearing
  as push buttons, without a checkbox indicator. A checked, disabled native
  push-like button retains its pressed theme.
- Apply the existing common-control trackbar geometry to main-window sliders,
  including the vertical volume control. Use the native channel, pointed thumb
  and endpoint ticks. Windows theme painting inherits the panel gradient instead
  of introducing an opaque dialog-colored rectangle.
- Restore the two white empty waveform rows above the key grid. The grid remains
  a separate widget; this does not synthesize an audio waveform.
- Restore the viewport camera/light/accessory or model/bone title, playback title,
  and camera/bone-position and angle readout captions. Their anchors and sizes
  follow `src/window/ui_palette.cpp` (`HandlePaletteChanged2`). Captions are
  included in the English, Japanese and Simplified Chinese catalogs.
- Split the former 2,308-line Qt window implementation into painting, viewport,
  file operations, dialog and input translation units. The session/command owner
  and shared layout formulas are retained. See [source review](SOURCE_STRUCTURE.md).

## Comparing equivalent scene state

The supplied original screenshot uses a wider output aspect, a 30-degree FOV,
an unrotated camera and a narrower sidebar than the supplied NG screenshot.
Those are document/editor settings, not control-style differences. Do not reset
an edited scene to reproduce a screenshot. The new capture test explicitly sets
1280x720 output, a 30-degree FOV and the relevant sidebar widths in its own
temporary sessions. The actual viewport size also determines the editor HUD's
scale and letterboxing.

The NG settings menu and application/document title identify this application;
the original screenshot's MMEffect menu belongs to its separate effect plugin.
Changing that label would not establish MME compatibility.

## Validation scope

All local validation uses offscreen or never-shown test-owned windows, without
desktop capture, activation, focus stealing, keyboard injection or audio output.

- The independent native BUTTON corpus covers **1,560** font, height, caption,
  enabled, checked and keyboard-focus states with exact pixel comparison.
- The native TRACKBAR corpus covers **1,728** horizontal/vertical geometry cases.
  `TBM_GETCHANNELRECT` uses horizontal coordinates even for `TBS_VERT`; only that
  documented query result is transposed before comparing client geometry.
- `qt_editor_chrome` renders six own-window profiles (three languages, two
  window sizes), including a production-prepared RHI viewport and editor overlays.
  It checks the empty waveform rows, output-aspect geometry, hidden lifecycle
  and unchanged scene revision. Captures are under `out/build/editor-chrome`;
  the never-shown Windows-QPA check writes `editor-chrome-windows` alongside it.
- Existing editor, numeric, control, input, scene and backend P4 gates remain in
  the local regression set. Distribution validation relocates the complete
  package and runs with developer SDK paths removed.

The local Release build passed **75/75** P4-labelled tests, the never-shown
Windows-QPA capture profiles and the relocated package check. The package check
also passed after deleting obsolete builds. Cleanup removed **1,225,024,289 bytes**
and preserved historical evidence with a relocation manifest. See the scoped
[local evidence summary](evidence/qt-ui-output-2026-11-04.json).

The six generated captures are a review aid, not an independent original-MMD
full-window oracle. Visible native controls, cross-platform font/raster parity,
real Windows IME shutdown and strict software/Metal scene composites retain their
existing acceptance boundaries in [P4 status](CROSS_PLATFORM_P4.md). This task
does not request another hosted workflow run.
