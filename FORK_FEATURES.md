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
| Native editor (wxTextCtrl) toggle | wangqr | Cross-platform | **Deferred** — see below. |
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

## Deferred: native editor (wxTextCtrl) toggle

wangqr added an option to swap the Scintilla (`wxStyledTextCtrl`) edit control
for a native `wxTextCtrl`, primarily to improve IME input on Windows. We did not
port it because:
1. Its main benefit (IME) is already solved here via the macOS IME shim.
2. The subtitle edit control (`subs_edit_ctrl.cpp`) is `wxStyledTextCtrl`
   throughout (syntax highlighting, calltips, spell-check squiggles); a runtime
   toggle means maintaining a parallel native control — a large, invasive change
   to a core editing path.
3. It is inherently interactive and cannot be behaviorally verified in this
   headless build environment, so shipping it blind would risk regressing the
   editor.

It is recorded here as a known, sourced gap with a clear rationale rather than
shipped untested.

## Verification note

The LLM and transcription features have unit tests (`tests/tests/llm.cpp`, 21
tests) and build green. `time/tap` is additive and compiles green, but its
real-time behavior should be confirmed interactively (no GUI in this build
environment).
