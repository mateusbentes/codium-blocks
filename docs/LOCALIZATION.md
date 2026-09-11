# Localization and UI Text

Codium::Blocks uses a small, explicit localization layer for the native wxWidgets workbench. The design keeps the application independent of Electron, gettext runtime files, and locale-specific code branches while allowing the same UI implementation to serve multiple desktop languages.

## Supported languages

The shipped catalogs are:

| Locale | Catalog | Status |
|---|---|---|
| English (United States) | `locales/en-US.tsv` | Complete reference catalog and mandatory fallback |
| Portuguese (Brazil) | `locales/pt-BR.tsv` | Complete first-party translation of the native workbench catalog |

English is the canonical source language for code-facing strings, protocol names, command identifiers, diagnostic identifiers, documentation, and legal text. Brazilian Portuguese is the first translated desktop UI because it is the project's primary user community. Additional languages should be added as complete catalogs rather than as scattered conditional strings.

## Selection and precedence

The language selected by the application follows this order:

1. The `CODIUM_BLOCKS_LANGUAGE` environment variable, when present.
2. The persisted preference in the platform data directory at `ui-language.tsv`.
3. The operating-system locale, reduced to the supported language family.
4. English.

`en`, `en-US`, and `English` select English. `pt`, `pt-BR`, and `Portuguese` select Brazilian Portuguese. Other system locales intentionally fall back to English until a reviewed catalog exists; the application does not pretend that a partial translation is complete.

The **View > Language** menu writes the preference and asks for a restart. Restarting is deliberate: native menus, notebook page labels, controls, accelerator labels, and accessibility descriptions are constructed together during window creation. Rebuilding them in place would risk losing editor state and event bindings. Dialogs opened after a language change use the newly selected catalog immediately, while the complete workbench is applied on the next launch.

## Catalog format

Catalogs are UTF-8 TSV files with one stable key and one translated value per line:

```text
menu.file\t&File
status.ready\tReady
```

The first tab separates the key from the value. Additional tabs are allowed in a value so menu accelerators can remain part of the translated label. Blank lines and lines beginning with `#` are ignored. Keys are stable identifiers, not English sentences; this prevents a wording change from silently becoming a missing translation.

`en-US.tsv` is loaded first. The selected catalog overlays it key by key, so an incomplete or newly added translation safely falls back to English. A missing English key is displayed as the key itself during development, making untranslated UI easy to detect instead of hiding it behind an empty label.

## Scope and invariants

The native workbench catalog covers menus, command-palette entries, buttons, notebook pages, navigator headings, filters, status-bar labels, modal dialog prompts, editor inputs, debugger controls, terminal controls, extension actions, accessibility tooltips, and operational status messages. File names, shell commands, compiler output, LSP/DAP method names, Code::Blocks SDK identifiers, task names, target names, source paths, and user-provided expressions are never translated.

The About dialog deliberately preserves the product identity and legal wording in English. `Codium::Blocks`, `GNU GPL-3.0-only`, SPDX identifiers, copyright attribution, protocol identifiers, and compatibility claims are not translated or localized by user preference. This avoids changing the legal or technical meaning of the application while allowing the surrounding UI to be translated.

## Runtime and installation

Catalogs are installed below the read-only resource directory at `locales/`. User preferences are written to the platform data directory, never beside the executable and never into the source tree. `CODIUM_BLOCKS_DATA` can redirect that writable directory for portable deployments and deterministic tests. `DetectProjectRoot()` resolves the development tree, shared Linux resources, macOS bundle resources, and Windows installation resources without hard-coding a POSIX separator.

The package smoke test loads both catalogs from the source tree, selects Portuguese, reloads the preference, verifies English fallback, and confirms that invariant product text remains unchanged. The CMake install layout includes the catalogs in Linux, macOS, and Windows package staging.

## Contribution policy

A localization change should update both the reference English catalog and every shipped translation, add or update a deterministic smoke assertion when behavior changes, and document terminology that may be ambiguous. Translators should preserve `%s`, `%d`, `%lu`, and `%zu` placeholders exactly, keep accelerator markers such as `&` meaningful for the target desktop platform, and avoid translating protocol or product identifiers.

The English catalog remains the review authority. A contribution that cannot provide a complete, reviewed catalog should remain an explicit future language proposal rather than a partially translated menu. Use `git diff --check`, the localization smoke test, the full portable CTest suite, and a GUI startup check before committing catalog or UI changes.
