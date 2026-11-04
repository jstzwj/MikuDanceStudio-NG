# P4 dialog EDIT parity correction

Status: partial qualification. This record does not accept P4/C1 or complete
EDIT appearance and input behavior.

Latest: the [active EDIT continuation](P4_ACTIVE_EDIT_QUALIFICATION.md) records
the corrected shown border qualification (5,060 exact states), active-field
repairs, never-shown capture replays, multiline page behavior and current IME
limitations. Historical remaining-qualification paragraphs below retain the
scope and failures of their original revisions.

The subsequent multiline layout repair uses the original English/Japanese
resources' `EM_GETRECT` and `EM_POSFROMCHAR`, observed without showing or focusing
windows. The original client origin is `(6, 3)` in outer-control pixels and the
default Tab interval is 56 logical pixels. Qt previously used `(5, 5)` and 80.
The document margin, viewport inset and actual two-pixel frame now share those
native metrics, including drawing, hit testing, caret and IME geometry. The new
`qt_multiline_layout` gate passes **438** original character/tab/line positions;
navigation, numeric-dialog and English-name rechecks also pass. This qualifies
unscrolled character geometry, not active selection/scroll paint or shown IME
candidate placement.

2026-11-04 keyboard correction: the new [original EDIT navigation corpus](P4_EDIT_NAVIGATION.md)
supersedes the `1.5`-only Ctrl+Delete generalization below. Mixed-text single/multiline
navigation, Ctrl+A, Ctrl+Backspace, Ctrl+Delete and selection direction now have
144,135 passing local comparisons; shown paint and native IME remain separate.

## Reference correction

The former public control oracle constructed `EDIT` windows directly with the
resource's `WS_BORDER` flag. That is not equivalent to creating the original
dialog. The Windows dialog manager removes `WS_BORDER` and adds
`WS_EX_CLIENTEDGE` for these resources. The former hidden border check painted
an independent `FrameRect`; it did not capture the original nonclient area.

Consequently, the 460 empty EDIT results from runs 41637849989, 41671549143 and
their subsequent reruns do **not** qualify original-MMD EDIT borders. Those runs
really passed their then-current public reference, but that reference was wrong
for dialog EDITs. The earlier button/list/combo/trackbar results are separate.

The public executable now compiles `res/templates/dialog/dialogs.rc` and uses
`CreateDialogParamW`/`GetDlgItem`, just like the optional original-resource
observer. Existing resource gates verify that RC source against the frozen
original binary resources. The private observer additionally maps the actual
original executable, verifies SHA-256
`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`,
and never runs its entrypoint.

`tests/reference/original-edit-layouts.json` freezes the private observer's 230
single-line controls across 38 English/Japanese templates at 96 DPI. It records
post-creation styles, outer/client dimensions, client offset, `EM_GETRECT` and
`EM_GETMARGINS`. Both public and private paths must match this independent data.

## Production correction and corpus

Dialog edits now use the two-pixel client edge. Their text starts at outer
coordinates (6, 3) in English and (4, 3) in Japanese, rather than being vertically
centered. Native character/caret origins are (6, 2) and (4, 2). Qt's text engine
still executes its paint/layout step before the native glyph overlay, retaining
current scroll offsets and cursor/IME geometry. The 230 initial character/caret
positions match independent native `EM_POSFROMCHAR` observations after accounting
for Qt 6.8.3's five-pixel cursor repaint halo. This is a logical caret-position
comparison, not qualification of a shown IME candidate popup. The existing
no-auto-scroll insertion width remains independently gated.
Inactive selections remain available to copy/undo but are not highlighted, as the
original styles omit `ES_NOHIDESEL`. Windows inactive, unscrolled plain Latin/CJK
text uses the same GDI font and clipping as native EDIT; active composition,
scrolling and complex text continue through Qt's text engine.

The new hidden `qt_edit_content` corpus compares actual `WM_PRINTCLIENT` output:
230 fields × five numeric/Latin/Japanese/Chinese/mixed/long-text variants × four
enabled/disabled and inactive-selection states = **4,600 exact RGB cases**.
Long auto-scroll inputs use a short unscrolled string; scrolling is not qualified
by this corpus. All mismatches and one passing sample per template/content/state
are retained. Client cropping uses the real native client origin, rather than
inventing a hidden border capture. Empty client regions have 460 separate cases.

Complete visible borders and populated controls are tested only by explicit
`--isolated-nonclient`/`--isolated-edit-text` modes on an isolated GitHub runner.
They are excluded from the developer's normal local P4 suite. Local tests neither
show windows nor send hardware input, activate applications or read the desktop.

## Composition cancellation

Qt 6.8.3's Windows input context can synchronously commit its cached preedit from
`reset()` before cancelling native composition. Escape now discards that commit
only during the reset call, then clears preedit without replacing the edit text.
Independent line-edit and multiline tests inject the reset-time commit, including
replacement ranges and selected text, and check text, cursor, selection and
undo/redo preservation. Deletion of the target during reset is also covered.
This fixes editor cancellation semantics; it does not establish that the separate
Windows native-IME shutdown hang has been fixed.

Both original English-name comment controls omit `ES_WANTRETURN`; the English
`0x0400` flag is `ES_OEMCONVERT`, not `ES_WANTRETURN` (`0x1000`). Ordinary Enter
therefore invokes OK, while Ctrl+Enter inserts a newline. The Qt dialog now
honors those resource flags. An independent never-shown native-dialog test runs
`IsDialogMessageW` and drains only that test dialog's message queue, verifying
both commands and resulting text in both languages. Its temporary keyboard-state
table belongs only to the calling thread; it does not attach to another input
queue or send desktop input. Qt repeats the behaviors in all three UI languages,
including newline undo.

## Remaining qualification

Consolidated run [41704895198](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41704895198)
at `0af9e30` passed 74 Windows, 70 Linux and 65 macOS ordinary P4 tests.
Linux libpinyin/Anthy and macOS Kotoeri again completed all three UI languages
and natural client exit. Linux/macOS native silent audio and package relocation
also passed. Windows Japanese input passed the English scenario but still hung
in Microsoft IME during `QApplication` native-window destruction. The production
manifest and input-service preparation did not resolve that failure.

The corrected resource-based **visible** Windows gate did not pass. Native
dialog-show processing restored focus to its initial EDIT, including a caret,
while Qt was explicitly inactive. Whole-dialog `PrintWindow` also included
overlapping body/joint controls over independently rendered Qt controls. The
observer now explicitly clears native focus for inactive profiles and excludes
intersecting siblings during capture; actual-focus profiles remain separate.
These observer changes need isolated visible requalification.

The disabled EDIT border has a genuine production difference: USER32 retains
a white inner client-edge pixel, whereas Qt fills that pixel with the disabled
background. Shown dialog edits now share the native theme-color border drawing
helper with listboxes, using the Edit theme's own part/state. Hidden classic
client-edge tests remain separate; the shown repair is not yet qualified.

Ctrl+Delete now removes the remainder of a single-line edit, while an existing
selection deletes only that range. Native fit-to-field inputs have **1,610**
text/selection/undo/redo comparisons across 230 controls; both the public resource
gate and actual original-resource observer pass without showing a window.
The Qt regression repeats seven independently observed selections, both writable
and read-only, in three languages for dialog and main-window fields, preserving
scene state and undo/redo. Native read-only key rejection can emit `MessageBeep`,
so that part runs only through Qt's offscreen regression. Overflowing native
no-autoscroll `WM_SETTEXT` can have virtual display rows; it is not qualified by
the fitting-value deletion corpus. Word navigation, Ctrl+A and Ctrl+Backspace
also require separate legacy behavior work; Qt's Unicode line boundaries are
not a general replacement for native EDIT word boundaries.

Focused and horizontally scrolled EDIT rendering, multiline appearance, hover
transitions, physical DPI changes and non-Windows glyph/theme parity remain open.
The Windows native input client now uses the production application manifest.
Its isolated runner also records/enables the native text-input services and
records whether language installation needs a reboot. That isolated Server 2022
profile still fails natural exit; successful synthetic tests cannot accept it.
