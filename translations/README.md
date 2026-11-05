# Qt translations

The editor uses Qt Linguist ID-based translation (`QT_TRID_NOOP`, `QTranslator`,
`lupdate`, `lrelease -idbased`). The directory follows Muffin's
`translations/<application>_<locale>.ts` convention. Supported languages are
English, Japanese, Simplified Chinese, Traditional Chinese, Korean, French,
German, Spanish, Italian, Portuguese (Brazil), Russian, Dutch, Polish, Turkish
and Vietnamese, matching Muffin's complete language list. The original English,
Japanese and Simplified Chinese texts are preserved, including intentionally
empty Japanese controls.

- `mikudancestudio_<locale>.ts`: editable Qt Linguist catalogs, 773 IDs each.
- `messages.hpp`: generated ID/source declarations for extraction and English
  fallback. Stable IDs let original MMD numeric controls and generated dialog
  descriptions share the same Qt translation mechanism.
- `.qm` files are generated under `out/build/translations`, embedded into the
  application and installed into the distribution's `translations` directory.
  Generated binaries are not committed.
- Qt's `qtbase_<locale>.qm` files come from the configured Qt SDK, are embedded
  at `:/i18n/qt`, and translate standard buttons and file dialogs without an SDK
  or external translation search path at runtime.
- `qtbase_vi.ts` is a project-owned supplement for Qt 6.8 standard buttons,
  file dialogs, file-system labels and text-edit menus. Qt does not ship a
  Vietnamese catalog in this SDK; this source is compiled with regular
  context-based `lrelease`, embedded and deployed alongside the app catalogs.
  Other Qt modules continue to use their upstream source text where untranslated.

Open **File → Preferences… → Language** to select a native language name or
**Follow system language**. Apply/OK updates the existing editor in place and
saves the preference; Cancel discards the pending selection. Loading a model,
editing animation, undo history and GPU resources are unaffected. Regional
locales resolve to the supported base language; Traditional Chinese locales
resolve separately. Portuguese locales use the Brazilian Portuguese catalog.
Unsupported system locales use English. Existing `language`
settings remain compatible. The original MMD Japanese/English command remains
available for original-command compatibility.
New-language button captions fit within the original control rectangles and
expose their complete text as tooltips. The three original language profiles
retain their qualified control font metrics.

## Updating strings

Edit translations in Qt Linguist. For a new dynamic ID, add its English source
and translation to `mikudancestudio_en.ts`, then run:

```sh
cmake --build out/build --target update_translations --config Release
# Translate newly extracted messages in the other TS files with Qt Linguist.
cmake --build out/build --target release_translations --config Release
python scripts/translation_catalog.py
```

The English TS source is authoritative for extraction. `messages.hpp` is
regenerated before `lupdate`; no JSON catalog remains. `lupdate` warnings about
empty original control captions are expected. Do not translate axis symbols,
format placeholders, key names, product names or `tex01`–`tex10` texture IDs.
Generated MMD menus and numeric dialogs use `scripts/editor_menu.py` and
`scripts/editor_dialogs.py`, which now update/check TS catalogs directly.

For another locale, copy the English TS with the appropriate `language`
attribute, translate it, add the native display name to `Language::Available`,
and supply the matching Qt base translation. CMake discovers the TS and embeds
its QM automatically. Keep the runtime/package language coverage tests in sync.
An unfinished translation falls back to the English source at runtime;
the catalog acceptance check requires all current messages to be finished.

## Verification

`qt_translation_catalogs` checks IDs, placeholders, generated sources and an
unchanged native `lupdate` round trip. `qt_translation_runtime` compares every
compiled QM entry against its TS, tests `qtTrId`, and checks Qt standard-dialog
translations. Loaded PMD/PMX language-switch tests exercise File-menu mouse
dispatch, Apply/OK/Cancel, system selection, persisted preferences and restart
without showing any test window. The relocated distribution check exercises all
fifteen languages with SDK search paths removed.
