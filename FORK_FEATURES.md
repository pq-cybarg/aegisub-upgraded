# Aegisub fork feature scan & integration

This fork's base is **arch1t3cht/Aegisub** (`feature` branch), which is itself a
curated superset that already merges the notable features of the other major
Aegisub forks. This document records an exhaustive scan of the fork landscape,
what we already have, what was pulled in, and what was deliberately not — with
sources and licenses.

## New in this fork (beyond arch1t3cht)

- **LLM tools** — Translate / Condense-for-CPS / Proofread / Rephrase / Custom
  prompt, provider-agnostic (Anthropic, OpenAI, local Ollama / llama.cpp). See
  [`LLM_FEATURES.md`](LLM_FEATURES.md).
- **Whisper transcription** — *Generate subtitles from audio* uploads the loaded
  media to an OpenAI-compatible `/audio/transcriptions` endpoint (OpenAI,
  whisper.cpp server, faster-whisper-server, or a subgen-style server) and
  inserts the timed segments. Inspired by [McCloudS/subgen](https://github.com/McCloudS/subgen) (MIT).
- **Tap-to-time** (`time/tap`) — tap at each line's start to time it from the
  playing audio; a lightweight, additive revival of the EaterOA / Japan7 "tap
  timing" workflow. Bind a key under Preferences → Hotkeys.
- **Opt-in native editor** (`Subtitle/Edit Box/Native`) — use the OS-native
  `wxTextCtrl` instead of Scintilla for the edit box (see "Native editor mode"
  below for the tradeoffs).

## Fork inventory (sources + licenses)

All Aegisub forks share the same licensing: source is **BSD-3-Clause**
(`LICENCE`, "Copyright (c) 2004-2012, Aegisub Project") with per-component
exceptions — MIT (`src/gl/`, `Yutils.lua`), BSDL (`MatroskaParser.c/h`),
MPL-1.1 (`universalchardet/`). Official Windows/macOS *binaries* are effectively
**GPL-2.0** because they bundle FFTW3 (GPL).

| Fork | URL | License | Status |
| --- | --- | --- | --- |
| TypesettingTools/Aegisub (eventual upstream) | https://github.com/TypesettingTools/Aegisub | BSD-3-Clause | Active (v3.4.2, 2025) |
| arch1t3cht/Aegisub (**our base**) | https://github.com/arch1t3cht/Aegisub | BSD-3-Clause | Active; migrating into TSTools |
| wangqr/Aegisub | https://github.com/wangqr/Aegisub | BSD-3-Clause | Dormant (v3.3.3, 2022) |
| Ristellise/AegisubDC | https://github.com/Ristellise/AegisubDC | BSD-3-Clause | Archived (2022) |
| odrling / Aeg-dev/Aegisub-Japan7 | https://github.com/Aeg-dev/Aegisub-Japan7 | BSD-3-Clause | Live (self-hosted) |
| Myaamori/aegisub-cli | https://github.com/Myaamori/aegisub-cli | BSD-3-Clause | Stale |
| McCloudS/subgen (not a fork; ASR tool) | https://github.com/McCloudS/subgen | MIT | Active |

## Gap analysis — what we did with each candidate

| Feature | Source | Platform | Disposition |
| --- | --- | --- | --- |
| Tap-to-time timing | Japan7 / EaterOA [#95](https://github.com/Aegisub/Aegisub/pull/95) | Cross-platform | **Pulled in** as an additive `time/tap` command (does not touch the timing controllers). |
| Audio→subtitle transcription | subgen (concept) | Cross-platform | **Pulled in** as the Whisper transcription feature. |
| Negative margins | wangqr | Cross-platform | **Already present** — `ass_dialogue.cpp` parses `mid(-9999, …, 99999)` and the style-editor spin range is `-9999..9999`. |
| VP9/WebM playback | AegisubDC (patched FFMS2) | Cross-platform | **Already covered** — our FFMS2 5.0 decodes VP9; the DC patch targeted old FFMS2. |
| WWXD keyframe generation | AegisubDC | Cross-platform | **Already available** via the bundled VapourSynth `wwxd` plugin. |
| High-DPI, video panning, spectrum/freq mapping, stereo/XAudio2, folding, extended Lua API, vector-clip hotkeys, window-restricted color picker, wangqr time-video tool | wangqr / AegisubDC / moex3 / EleonoreMizo | — | **Already in base** (merged by arch1t3cht). |
| Lua 5.3 / moonjit, old libass/VSFilter swaps | AegisubDC | — | **Superseded** by the base's modern dependencies — intentionally not ported. |
| FontBase "loaded fonts" | AegisubDC | Windows (GDI) | **Cross-platform-equivalent already works** — see below. |
| Native editor (wxTextCtrl) option | wangqr | Cross-platform | **Pulled in** as an opt-in `Subtitle/Edit Box/Native` mode (default off; the Scintilla editor is unchanged). |
| aegisub-cli (headless runner) | Myaamori | Cross-platform | **Out of scope** — a separate standalone binary, not an editor feature. |

## Windows-only features → Mac/Linux assessment

- **FontBase / font-manager "loaded fonts"** (AegisubDC, Windows GDI): on macOS
  the CoreText font provider and on Linux the fontconfig provider already
  enumerate user-activated fonts, so fonts activated by a font manager are
  already visible to Aegisub on those platforms. No port needed for Mac/Linux.
- **IME in the subtitle edit box** (the motivation behind wangqr's native-editor
  toggle on Windows): already addressed cross-platform here on macOS via the
  runtime `wxNSView` subclassing in `src/osx/scintilla_ime.mm` (also a build fix
  for Homebrew wxWidgets).

## Native editor mode (opt-in) — implemented

wangqr's fork *replaced* the Scintilla (`wxStyledTextCtrl`) edit control with a
native `wxTextCtrl` (`SubsTextEditCtrl final : public wxTextCtrl`), primarily for
better IME input on Windows. Here it is implemented as an **opt-in option**
(`Subtitle/Edit Box/Native`, default off, under Preferences → Interface → Edit
Box), so the Scintilla editor remains the default and is untouched:

- New control `src/subs_edit_ctrl_native.{h,cpp}` (`SubsTextEditCtrlNative`).
- `subs_edit_box` constructs Scintilla *or* the native control and routes the
  ~handful of control-specific operations (`GetTextRaw`, `SetTextTo`, the
  `wxEVT_STC_MODIFIED` vs `wxEVT_TEXT` change event, the IME shim) accordingly.
  `TextSelectionController` (Scintilla-typed and null-safe) is simply left unset
  in native mode.
- Verified: both the default and native modes launch without crashing.

**Tradeoffs (why it's opt-in):** native mode gains OS text-input integration
(dictation, Services, accessibility) but **loses override-tag syntax
highlighting, calltips, and inline spell-check squiggles**.

**Important macOS finding:** on macOS the *Scintilla* editor already provides the
two things this option is usually wanted for — native spell-checking
(`SpellCheckerFactory` already uses `CreateCocoaSpellChecker` / `NSSpellChecker`,
with Scintilla rendering the squiggles) and working IME (the runtime `wxNSView`
subclass in `src/osx/scintilla_ime.mm`). So on macOS the native mode's marginal
benefit is mostly dictation/Services/accessibility, and most users are better
served by the default Scintilla editor.

**Caveat:** the editor's full interactive behavior (typing, IME composition,
line-switch text sync) in native mode should be confirmed hands-on; this build
environment can verify launch but cannot drive the GUI.

## Verification note

The LLM and transcription features have unit tests (`tests/tests/llm.cpp`, 21
tests) and build green. `time/tap` is additive and compiles green, but its
real-time behavior should be confirmed interactively (no GUI in this build
environment).
