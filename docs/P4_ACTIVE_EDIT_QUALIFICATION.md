# P4 active EDIT and multiline continuation

P4/C1 remains **in progress**. Complete-scene RGB uses the user-approved visual
policy; this does not waive original UI appearance or native input qualification.

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
Earlier failures during actual native-window destruction remain valid historical
evidence. Local development does not install/switch input methods or touch the
user's desktop. Physical monitor/DPI transitions still require separate real
environment qualification and cannot be inferred from `QT_SCALE_FACTOR`.
