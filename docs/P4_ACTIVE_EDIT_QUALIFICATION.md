# P4 active EDIT and multiline continuation

P4/C1 remains **in progress**. Complete-scene RGB uses the user-approved visual
policy; this does not waive original UI appearance or native input qualification.

Current result (2026-11-05): [41870574500](evidence/p4-active-41870574500.json)
at `65c34af` passes five hidden observers, **72 actual focused multiline client
captures, 72 actual caret rectangles and 90 OS mouse gestures**, all exact.
Actual focus is settled before each capture, caret ownership is checked and a
thread-local read-only message observer verifies delivery of requested moves.
The final production repair erases the entire client background, matching a
shown native EDIT; the hidden paint reference now initializes the same erase
color before WM_PRINTCLIENT. The 432 hidden states also pass exactly.

The Linux/macOS menu-font follow-up [41868268491](evidence/p4-active-41868268491.json)
passes four targeted frontend gates per platform, including macOS 26. New
screenshots were reviewed. Outside-client autoscroll, complex multiline paint,
complete non-Windows shown appearance and physical mixed-DPI qualification remain
open. Earlier failures below are retained as history, not the current result.

## Earlier qualification increments

Baseline run [41843478422](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41843478422)
at `40daf18` now passes Windows **85/85** ordinary gates and all seven actual shown
control profiles: 460 empty, 4,600 populated inactive, 3,450 active/selection/scroll,
230 actual hover EDIT and 102 list/combo/focus comparisons have zero RGB differences.
Cursor geometry has zero mismatches. Linux/macOS each have only the system-menu
font inheritance failure (77/78 and 72/73 ordinary gates); the menu now explicitly
resolves all its system font properties rather than inheriting Noto metrics from
its parent. All three scene-visual and relocated-package gates pass. See
[run evidence](evidence/p4-ui-41843478422.json) and [current P4 status](P4_ACCEPTANCE_STATUS.md).

The menu-font follow-up [41851148562](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41851148562)
passes Linux **78/78**, including the original font failure. Windows passes 85/86,
including fonts and multiline hit tests; its remaining fail is the hidden observer's
process-foreground assertion. The observer follow-up records its foreground before
QApplication and forbids activation for non-focus reference windows. It also verifies
the Ctrl modifier immediately around each native deletion; a refreshed input table
retries the same case within a fixed bound and never counts ordinary Delete as Ctrl.
macOS was not acquired by a hosted runner after multiple attempts, due to capacity;
no current macOS source qualification is claimed and no capacity retry is scheduled.
[Run evidence](evidence/p4-menu-41851148562.json) retains the actual failure/cancellation.

Local ALL_BUILD and 85/86 ordinary tests pass. The only failed native-language client
timed out at the unchanged 45-second limit; a separate unchanged-code recheck passes
all four generated PMD/PMX unit/production clients in 22.79 seconds. The initial fail
is retained, not rewritten as an all-green first run. Updated package relocation
passes. [Local evidence](evidence/p4-menu-multiline-local.json) records binary/source hashes.

The continuation also adds [50,232 original multiline hit tests and 475 directly
addressed Shift-click checks](P4_MULTILINE_HIT.md), all passing without desktop
input or exposure. Physical DPI and actual shown multiline/drag qualification
remain separate; the historical increments below retain their measured scope.

The subsequent [multiline client paint gate](P4_MULTILINE_PAINT.md) adds 432 exact
hidden client states, including readonly/disabled color, inactive selection,
horizontal/vertical scroll and overhanging/clipped glyphs. It preserves the earlier
character, hit-test and page-navigation gates. The four hidden observer families
passed locally after `29cee2d`. Source consolidation at `105695e` in run
41857117726 passes Linux 78/78 and Windows 86/87; only hidden native activation
fails. macOS again never acquired a runner. The `1e75a3a` observer uses a
thread-local CBT activation/focus veto; NOACTIVATE alone proved insufficient.
Child resources stay identical and actual shown qualifications have no hook.
Reobserved 34,224 hits and 576 navigation records are unchanged. A local text
paint timeout and its unchanged-limit passing recheck are both retained in
[observer evidence](evidence/p4-hidden-observer-local.json). The single Windows
follow-up 41861823315 qualifies these observers and the new actual multiline test;
it does not rebuild all platforms or rerun already-passing IME qualification.
Actual shown multiline focus/drag
and physical DPI requirements are still not accepted by these memory captures.

Run 41861823315 is now complete: **all five hidden observers pass**. The actual
multiline test executed 72 focused paints and 90 OS mouse gestures, with 72 and
38 differences respectively. The failures are preserved in the
[updated run record](evidence/p4-observer-41861823315.json). Focused multiline
paint previously bypassed the native glyph renderer; its selection/caret geometry
and raster path now share the inactive implementation. ASCII mouse selection uses
the already-qualified native hit mapping and legacy word boundary policy.

The original active observer also used WM_PRINTCLIENT, which does not include
the actual caret, and ran independent gestures inside the OS double-click interval.
The follow-up captures the shown test-owned dialog with PrintWindow and separates
gestures by GetDoubleClickTime. No desktop capture, color remapping, pixel masking
or threshold relaxation is used. Revision `8014411` passes the four existing local
multiline gates. Run 41868268491 targets Windows observers/actual multiline and
four relevant Linux/macOS frontend gates only; it is not a new full-suite,
native-IME, audio, packaging or original-composite qualification.

## Consolidated evidence

Run [41828328950](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41828328950),
at `487847a`, passes **83/83 Windows, 77/77 Linux and 72/72 macOS** ordinary P4
tests. All three scene-visual gates and relocated distributions pass. Linux
libpinyin/Anthy and macOS Kotoeri pass all three UI languages and natural exit.
The [machine-readable evidence](evidence/p4-edit-native-41828328950.json) records
artifact hashes, the passing subsets and the Windows failures separately.

Windows now passes all **460 empty and 4,600 populated shown EDIT** comparisons,
including the outer border. Theme image rendering with the correct `EPSN_*`
states replaces the fallback-color property query. The 68 populated list/combo,
six focused list and 28 focused combo states also pass exactly.

The newly added **3,450 active EDIT** cases failed in that run. They were not part
of the older inactive qualification. Their saved native captures exposed:

- Qt text/selection/caret one pixel above native paint. `EM_POSFROMCHAR` reports
  a character origin one pixel above the actual painted native caret.
- Qt's IME rectangle contains a five-pixel left damage halo. Windows consumes
  its left/top verbatim; the frontend now exposes the actual one-pixel caret.
- Native EDIT inverts the caret over selected text. It retains a trailing
  selection pixel and clamps the caret at the formatting boundary.
- Qt reserves an additional trailing caret pixel when scrolling. The shared
  text margins now account for it; native clipped, scrolled glyphs retain the
  original formatting margin without clipping unscrolled glyph overhangs.

Windows paints simple legacy text/selection/caret through the same memory-DC
adapter already used by inactive fields. Qt retains the real text layout,
selection, undo, hit testing and IME/preedit handling. The focused caret follows
the configured blink period. Complex shaping and active preedit retain Qt paint.
No native control/window is embedded or kept alive by this drawing adapter.

The corrected implementation passes **3,450/3,450 saved native client replays**
locally, without showing/focusing windows or sending desktop input. The replay
crops the two-pixel theme border because local and hosted OS themes differ. It
qualifies client raster/geometry against saved references, **not a fresh shown
focus/IME scenario**. The guarded isolated profile remains the final shown gate.

## Multiline and fonts

`observe_multiline_navigation.py` freezes 576 independent original English and
Japanese resource observations. It maps only the verified executable's resources,
creates never-shown dialogs, restores its thread keyboard-state table and checks
foreground/focus invariance. Page scrolling is explicit: unfocused
`EM_SCROLLCARET` cannot establish focused scrolling behavior.

`qt_multiline_navigation` replays **864** selection/page checks across English,
Japanese and Chinese using Windows QPA. Native pages overlap one complete row;
caret motion follows the actual clamped scroll delta. Up/Down at the document
edge retain the active column and correctly collapse an unshifted selection.
This supplements the 144,135 Ctrl-navigation comparisons; it does not replace
shown multiline/caret/mouse qualification or claim portable native page metrics.

Packaged Noto CJK fonts use their typographic line metrics. Their 12-pixel em now
has a 12-pixel control line box, matching the original fixed rows instead of the
17-pixel fallback box. This applies only when the resolved family is Noto CJK;
installed Windows original fonts and their measured metrics remain unchanged.
Cross-platform qualification of the updated font rendering remains separate.

Follow-up run 41836114770 at `c811c4c` exposes one common font-policy regression
on Linux and macOS: their ordinary suites pass 76/77 and 71/72 respectively,
but frame navigation inherits the parent Noto typographic strategy when switching
languages. That class-default font must retain its own metrics. Font strategies
are now explicitly resolved, including the default, for both controls and painted
glyphs. A local regression puts navigation and Latin controls under actual Noto
parents, without depending on installed Windows language fonts.

Fresh platform screenshots also expose Fusion's additional dropdown-label
padding. Portable dropdowns now use the original `GetComboBoxInfo` item rectangle
and text inset, without the additional label padding/elision. The frozen 22
native rectangles, with source/capture hashes, are replayed as **33** three-language
checks. This qualifies the content contract, not identical cross-OS theme images.
The two repairs pass **39/39** affected local Qt gates after ALL_BUILD; the newly
rebuilt `out/dist` also passes relocation. Remote qualification is still pending.

The Japanese self-shadow-off caption is restored to `影なし`. In the verified
original executable (SHA recorded in the frozen corpora), RVA `0x17679` loads
the UTF-16 literal at `0x12b820` for control 562 and `0x176c2` calls
`CreateWindowExW`; `0x1a0c5` subsequently calls `SetWindowTextW` with that literal.
Reading its bytes as ANSI produced the erroneous `q_j0W0` reconstruction.

## Windows IME boundary

The pure Win32 EDIT reference in run 41828328950 successfully composed, converted,
committed and canceled Japanese without `QApplication` or QPA. Its explicit
test-owned `DeactivateProfile` then blocked inside Microsoft IME. The subsequent
Qt conversion failed; a broker left blocked by the reference may contaminate that
scenario. Neither result qualifies Windows IME or proves a new Qt regression.

The follow-up runs the editor first. Natural-exit tests close composition and
then destroy windows/COM normally, without injecting `DeactivateProfile`:
`ActivateProfile` selects a profile, it does not acquire a reference requiring
that operation. No forced exit, process leak or disabled IME counts as a pass.
Run [41836114770](https://github.com/jstzwj/MikuDanceStudio-NG/actions/runs/41836114770)
on **Windows Server 2025** now passes all three editor UI languages with actual
Microsoft IME and natural application exit. Its English/Japanese/Chinese logs
reach dialog, session and QApplication destruction. The independent Win32 EDIT
also completes COM teardown and returns zero. This qualifies that OS/input-method
profile; the older Server 2022 failure remains a historical/environmental limit.
Linux/macOS native IMEs again pass all three languages. All scene visual and
package relocation checks pass; Windows ordinary tests pass 84/84. Linux/macOS
have the single font-inheritance failure described above. The
[run evidence](evidence/p4-active-41836114770.json) records each result separately.

The newer Windows theme reveals rounded EDIT corners blending with the white
client instead of the parent dialog. The memory image now starts with native
`COLOR_BTNFACE`, as USER32 parent-background painting does. Another observer
defect selected all Qt text with `TabFocusReason`, while Win32 `SetFocus` preserved
the requested state-0 cursor. Both paths now request raw focus; actual Tab policy
remains separate. All 4,600 populated inactive clients were exact; the 206 active
client failures were confined to this first-focus selection mismatch. No failure
is removed from the historical reports. Hover images and per-profile logs are
now retained, and control reports upload before optional Windows language servicing.

Local development does not install/switch input methods or touch the user's
desktop. Physical monitor/DPI transitions still require separate real environment
qualification and cannot be inferred from `QT_SCALE_FACTOR`.
