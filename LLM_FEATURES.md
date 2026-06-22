# Aegisub LLM Features

This build of Aegisub adds LLM-powered subtitling tools, integrated **both** as
native menu commands and as a bundled Lua automation script, backed by a
provider-agnostic client that works with cloud APIs and local models.

## What you get

A new **LLM** menu (also in the right-click grid context menu) with:

| Command | What it does |
| --- | --- |
| **Translate selected lines** | Translate the selection into the configured target language, preserving timing, ASS override tags (`{\...}`) and `\N` breaks. |
| **Condense for reading speed** | Rewrite only the lines that exceed your target characters-per-second so they fit, keeping the meaning. |
| **Proofread selected lines** | Fix spelling/grammar/punctuation in place without rephrasing. |
| **Rephrase selected lines** | Turn stiff or machine-translated lines into natural dialogue. |
| **Custom prompt…** | Apply your own instruction to each selected line. |
| **Generate subtitles from audio…** | Transcribe the loaded audio/video to timed lines via Whisper (see below). |
| **Configure…** | Jump to Preferences → LLM. |

All operations run off the UI thread with a cancelable progress dialog and apply
their edits as a single, undo-able change.

## Providers

Configure under **Preferences → LLM**. Four presets are built in:

| Preset | Wire format | Default endpoint |
| --- | --- | --- |
| Anthropic (Claude) | Anthropic Messages API | `https://api.anthropic.com` |
| OpenAI | OpenAI Chat Completions | `https://api.openai.com/v1` |
| **Ollama (local)** | OpenAI-compatible | `http://localhost:11434/v1` |
| **llama.cpp server (local)** | OpenAI-compatible | `http://localhost:8080/v1` |

Any OpenAI-compatible server (LM Studio, vLLM, text-generation-webui, …) works
too — pick **OpenAI** and set the **Endpoint** to its base URL.

### Settings

- **Provider** — one of the presets above.
- **Endpoint** — leave blank to use the preset default, or override it.
- **Model** — e.g. `claude-sonnet-4-6`, `gpt-4o`, `llama3.1`, `qwen2.5`.
- **API key** — leave blank to fall back to the `ANTHROPIC_API_KEY` /
  `OPENAI_API_KEY` environment variable. Local servers usually need no key.
- **Temperature**, **Max response tokens**, **Batch size** (lines per request),
  **Target language** (Translate), **Target reading speed in CPS** (Condense).

## Using a local model

### Ollama

```bash
ollama serve              # starts the API on :11434
ollama pull llama3.1
```

In Preferences → LLM: Provider = **Ollama (local)**, Model = `llama3.1`. Done —
no API key needed.

### llama.cpp

```bash
# llama.cpp's server exposes an OpenAI-compatible API
llama-server -m model.gguf --port 8080
```

Provider = **llama.cpp server (local)**, Model = whatever the server reports
(often just the file name). This is also how you run a **raw `.gguf`** model:
serve it with `llama-server` and point Aegisub at it. (Aegisub does not embed an
inference engine; it talks to your local server.)

## Transcription (Whisper)

**LLM → Generate subtitles from audio…** uploads the currently-loaded audio or
video file to a Whisper transcription endpoint and inserts the returned timed
segments as new lines. It uses the OpenAI-compatible multipart
`/audio/transcriptions` API (`response_format=verbose_json`), which is spoken by
OpenAI, **whisper.cpp's `whisper-server`**, **faster-whisper-server**, LocalAI,
and [subgen](https://github.com/McCloudS/subgen)-style servers — so it can run
fully locally.

Configure under **Preferences → LLM → Transcription (Whisper)**:
- **Endpoint** — blank uses OpenAI; set e.g. `http://localhost:8080/v1` for a
  local whisper.cpp server.
- **Model** — e.g. `whisper-1` (OpenAI) or whatever your local server reports.
- **Language hint** — optional ISO-639-1 code; blank auto-detects.

The API key falls back to `OPENAI_API_KEY`. Local servers usually need none.
Segment-timestamp parsing is unit-tested; the actual transcription needs a
reachable endpoint.

## How robustness is handled

- Lines are sent in batches (configurable) as a JSON array, and the model is
  asked to return a JSON array of the same length. The response parser tolerates
  code fences and surrounding prose, and ignores brackets inside strings.
- If a batch comes back with the wrong number of items, the command
  automatically retries that batch **one line at a time**, so a single bad line
  can't fail the whole selection.
- Network/HTTP/API errors are surfaced in a dialog; cancellation is immediate.

## The Lua automation script

`automation/autoload/aegisub-llm.lua` provides Translate / Proofread / Rephrase
macros under the **Automation** menu. It shells out to `curl` and is configured
through environment variables (`AEGISUB_LLM_PROVIDER`, `AEGISUB_LLM_MODEL`,
`AEGISUB_LLM_KEY`, `AEGISUB_LLM_ENDPOINT`, `AEGISUB_LLM_TARGET_LANG`). It's handy
for hotkeys and as a starting point for your own scripts.

## Architecture

- `libaegisub/common/llm.cpp` (+ `include/libaegisub/llm.h`) — pure, testable
  logic: request-body building, response parsing, the batch protocol. No
  networking, no UI.
- `src/llm_client.{h,cpp}` — libcurl HTTP transport.
- `src/command/llm.cpp` — the menu commands, provider presets, threading.
- `src/preferences.cpp` — the Preferences → LLM page.
- `tests/tests/llm.cpp` — unit tests for the pure logic.

## Tests

```bash
meson test -C build      # runs the gtest suite, including the LLM tests
```
