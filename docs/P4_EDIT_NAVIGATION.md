# Original EDIT keyboard behavior

The public `qt_edit_navigation` offscreen gate compares the editor with frozen
observations of MMD v9.32's actual English/Japanese dialog resources. The reference
executable SHA-256 is
`07516fd3bf1e6b1339836b6773a156f61bdd6f848eeb621fdda012375df313a1`.
`tools/observe_edit_navigation.py` verifies that hash and maps only resource data;
it never runs the original entrypoint. It creates dialogs with `CreateDialogParamA`,
matching the original host, and changes only its own thread's keyboard-state table.
Windows remain hidden, foreground/focus must remain unchanged, and native read-only
rejection is excluded because USER32 can beep. Qt's read-only checks are offscreen.

The frozen corpora include all ASCII punctuation between letters/digits, spaces,
tabs, decimal/exponent examples, Japanese/Chinese, combining characters and emoji.
They cover Ctrl+Left/Right with and without Shift, Ctrl+A, Ctrl+Backspace and
Ctrl+Delete; multi-key sequences observe selection anchors in both directions.
Multiline ASCII cases additionally cover line breaks, empty lines and indentation.
Qt repeats writable/read-only checks in English, Japanese and Simplified Chinese,
including actual text, selection, undo and redo. The original ANSI multiline
control can lose non-ACP characters; the Unicode frontend deliberately preserves
Unicode instead of reproducing that data loss. Non-ASCII multiline appearance
remains a separate qualification.

The new shared filter corrects these observed differences:

- Ctrl+arrows use legacy line-break opportunities, including punctuation/sign
  tailoring, rather than Qt's word-boundary rules. UTF-16 positions and grapheme
  boundaries are retained; IME preedit bypasses the filter.
- Ctrl+A preserves the active end of an already complete selection. Subsequent
  Shift+arrows therefore retain the original selection anchor.
- Ctrl+Backspace inserts DEL in single-line EDIT. The English ANSI-font multiline
  resource displays the DOS DEL glyph `U+2302`; the Japanese resource keeps DEL.
  The insertion replaces the selection and remains undoable.
- Ctrl+Delete deletes through the next native boundary, or the existing selection.
  Multiline navigation/deletion crosses CRLF as one break and preserves the observed
  leading/trailing-space rules. The frontend represents line breaks as LF.

The previous `1.5`-only Ctrl+Delete corpus cannot distinguish word deletion from
deletion to the end. Its 1,610 observations are valid for that fitting numeric
input, but its earlier generalization to every line was incorrect and is superseded
by the mixed-text corpus. The observed key behavior is now implemented separately
from the frontend's business commands and paint code.

The local 2026-11-04 run passed **144,135** independent navigation, selection,
read-only and undo/redo comparisons, plus 30 IME-preedit filter-bypass checks,
without showing a window. Complete cross-platform qualification remains pending.
The subsequent Windows multiline corpus adds 576 independent native observations
and **864** three-language replay checks for Up/Down, Home/End and PageUp/PageDown,
including Shift selections and clamped page overlap. See the
[active EDIT continuation](P4_ACTIVE_EDIT_QUALIFICATION.md) for exact scope.

This result does not qualify shown focus/caret paint, mouse word selection,
IME candidate placement or actual monitor/DPI transitions, and does not accept P4.

Reproduce the public gate after building `qt_edit_navigation_test`:

```powershell
ctest --test-dir out/build -C Release -R '^qt_edit_navigation$' --output-on-failure
```

The reference generator takes `--original original.exe --output file.json` and an
optional `--multiline`. It has no input injection, display-switch or activation API.
