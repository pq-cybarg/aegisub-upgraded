# Aegisub — LLM-upgraded

A fork of [Aegisub](https://github.com/arch1t3cht/Aegisub) (the actively
maintained arch1t3cht fork) that adds **LLM-powered subtitling tools** and
**Whisper transcription**, built natively into the editor and verified on modern
macOS (Apple Silicon).

## ✨ What's new

A new **LLM** menu (and right-click entry) operating on the selected lines,
preserving timing, ASS override tags (`{\...}`) and `\N` breaks, applying edits
as a single undo step:

- **Translate** selected lines into a configured target language
- **Condense for reading speed** — shorten lines that exceed a target CPS
- **Proofread** — fix grammar/spelling/punctuation in place
- **Rephrase** — turn stiff or machine-translated lines into natural dialogue
- **Custom prompt** — apply your own instruction to each line
- **Generate subtitles from audio (Whisper)** — transcribe the loaded
  audio/video to timed lines via an OpenAI-compatible `/audio/transcriptions`
  endpoint (OpenAI, whisper.cpp server, faster-whisper-server, or a
  [subgen](https://github.com/McCloudS/subgen)-style server)

Plus **Tap-to-time** (Timing ▸ Tap to time) — tap at each line's start to time
it from the playing audio (bind a key under Preferences → Hotkeys).

**Provider-agnostic** backend (libcurl) with built-in presets for **Anthropic
(Claude)**, **OpenAI**, and local **Ollama** and **llama.cpp** servers — any
OpenAI-compatible endpoint works, so you can run fully on-device. Requests run
off the UI thread with a cancelable progress dialog. A bundled Lua automation
script mirrors the text features for hotkeys/scripting.

- 📖 **Feature & setup guide:** [`LLM_FEATURES.md`](LLM_FEATURES.md)
- 🔒 **Security / opsec / crypto audit:** [`SECURITY_AUDIT.md`](SECURITY_AUDIT.md)
- 🧬 **Fork feature scan & integration:** [`FORK_FEATURES.md`](FORK_FEATURES.md)

Implementation: pure, unit-tested request/response logic in
`libaegisub/common/llm.cpp`; libcurl transport in `src/llm_client.cpp`; commands
in `src/command/llm.cpp` and `src/command/tap.cpp`; Preferences → LLM page.
**21 LLM/transcription unit tests, 374 total passing.** TLS is verified and
hardened; API keys can be supplied via environment variables
(`ANTHROPIC_API_KEY` / `OPENAI_API_KEY`) so they need never be written to disk.

## Building on macOS

```bash
brew install cmake ninja pkg-config meson libass boost zlib ffms2 fftw hunspell wxwidgets icu4c
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig"
meson setup build --buildtype=release -Dbuild_osx_bundle=true
ninja -C build
```

This fork also includes build fixes for modern Apple clang (reference-member and
return-value diagnostics, shallow-clone versioning, and a runtime-subclassing
fix so the Scintilla IME shim links against Homebrew wxWidgets). For Linux /
Windows build instructions, see the upstream notes in
[`docs/HISTORY-upstream-readmes.md`](docs/HISTORY-upstream-readmes.md).

## Fork lineage, references & licenses

This fork's base is **arch1t3cht/Aegisub**, itself a curated superset of the
other major Aegisub forks. See [`FORK_FEATURES.md`](FORK_FEATURES.md) for the
full cross-fork feature scan and what was integrated.

| Project | Role here | License |
| --- | --- | --- |
| [Aegisub / TypesettingTools](https://github.com/TypesettingTools/Aegisub) | Original project; eventual upstream | BSD-3-Clause |
| [arch1t3cht/Aegisub](https://github.com/arch1t3cht/Aegisub) | **Direct base** of this fork | BSD-3-Clause |
| [wangqr/Aegisub](https://github.com/wangqr/Aegisub) | Features merged via base (high-DPI, time-video, XAudio2…) | BSD-3-Clause |
| [Ristellise/AegisubDC](https://github.com/Ristellise/AegisubDC) | Features merged via base (`misc_dc`) | BSD-3-Clause |
| [Aeg-dev/Aegisub-Japan7](https://github.com/Aeg-dev/Aegisub-Japan7) (odrling) | Inspiration for tap-to-time (orig. EaterOA [#95](https://github.com/Aegisub/Aegisub/pull/95)) | BSD-3-Clause |
| [Myaamori/aegisub-cli](https://github.com/Myaamori/aegisub-cli) | Referenced (headless runner; not integrated) | BSD-3-Clause |
| [McCloudS/subgen](https://github.com/McCloudS/subgen) | Inspiration for Whisper transcription | MIT |

All Aegisub source above is **BSD-3-Clause** (`LICENCE`, "Copyright (c)
2004-2012, Aegisub Project") with per-component exceptions: MIT (`src/gl/`,
`Yutils.lua`), BSDL (`MatroskaParser.c/h`), MPL-1.1 (`universalchardet/`).
subgen is MIT; it was used only as design inspiration (no code copied).

## License

All files in this repository are licensed under various GPL-compatible BSD-style
licenses; see [`LICENCE`](LICENCE) and the individual source files. The LLM and
transcription additions in this fork carry the same permissive header. Official
Windows/macOS *binaries* are GPL-2.0 due to bundling FFTW3.

The original upstream README content (arch1t3cht + TypesettingTools) is preserved
verbatim at [`docs/HISTORY-upstream-readmes.md`](docs/HISTORY-upstream-readmes.md).
