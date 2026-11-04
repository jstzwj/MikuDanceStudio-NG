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
  follow `src/window/ui_palette.cpp` (`PaintViewportStatus`, formerly
  `HandlePaletteChanged2`). Captions are
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

## Loaded-model language-switch crash

The observed crash was an access violation in `Qt6Widgets.dll` (6.8.3,
offset `0x7c207`). The local user dump retains Qt's mouse-event dispatch and popup
forwarding frames. `SetLanguage` called `BuildMenus`, which synchronously deleted
the menu tree, including the active menu's `QWidgetWindow`, from its own action
callback. Qt still needed that window while returning from the click. This is a
menu lifetime defect; it is not a PMX-format parsing failure.

Menus are now constructed once. Translation keys are retained on menus/actions,
and language changes update titles, captions, tooltips and language checkmarks in
place. Backend selection and separate-viewport changes also retain the menu tree;
separate windows attach/detach the existing undo/redo shortcuts explicitly.

`qt_language_switch` and Windows `qt_language_switch_native` load public generated
PMD/PMX fixtures. They cover direct calls, action activation, mouse events sent
only to a never-shown menu window, the original language command and renderer
selection. Both test and production modes are exercised with isolated settings.
They check menu/action identities and absence of duplicate application entries,
language changes, backend checkmarks and unchanged model resources, scene revision,
dirty state and update count. Qt's lazy internal overflow/context menus are not
application-owned entries. The original code fails the dispatch-lifetime check;
the repaired code passes. The hidden test establishes the lifetime regression,
without claiming to reproduce the user's exact visible-popup crash timing.

The external Sour Miku PMX is checked locally with offscreen and never-shown
Windows QPA; it is not distributed. The original crash dump stays local. These
checks use neither desktop input nor shown windows, and do not close the existing
full visible-control, IME or scene-composite qualification gaps.

The local Windows Release build passes **77/77** P4-labelled tests after this
repair, including the two new language-switch profiles. Sour Miku passes both
production-mode profiles. Local logs and the read-only dump analysis are retained
under `out/build`; no hosted workflow was requested for this repair.

The 2026-11-04 upstream sync also brings the Japanese selected-bone title into
Qt: model and bone names use a fullwidth colon; English retains its spaced
ASCII colon. The loaded PMD/PMX language tests check the title consumed by the
paint path as well as menu lifetime. See `UPSTREAM_SYNC_2026-11-04.md` for the
source commits and the separate legacy compatibility validation scope.
