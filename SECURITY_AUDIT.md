# Security, Opsec & Cryptography Audit

Scope: Aegisub (`src/`, `libaegisub/`) and the LLM features added in this fork.
Bundled third-party code (`subprojects/`: luajit; `vendor/`: luabins) and system
libraries (boost, libcurl, wxWidgets, libass, ffms2, icu) are noted but not
deeply audited. Date: 2026-06-21.

## TL;DR

- **No broken or legacy cryptography** is used anywhere in Aegisub's own code or
  the additions: no MD5, SHA-1, RC4, DES, or homegrown ciphers. There is in fact
  **no asymmetric crypto, signing, or key exchange implemented in the app at all.**
- The **only cryptography exercised** is **TLS**, and only by the new LLM feature
  when it calls an HTTPS chat-completions API. That TLS is **classical, not
  post-quantum** (see §4).
- The only hashing that is persisted is **CRC-32 used to name cache files**
  (FFMS2 / BestSource index caches). It is internal, not part of any file
  format, and is the one place that could be **dropped in for SHA-3** (§5).
- Several **opsec/security hardening issues in the new code were found and
  fixed** in this pass (§2, §3).

## 1. Memory safety / leaks (added code)

| Area | Finding | Status |
| --- | --- | --- |
| `src/llm_client.cpp` | libcurl easy handle and header list are owned by RAII wrappers (`CurlEasy`, `CurlHeaders`); freed on every path including exceptions/cancel. `curl_global_init` runs once via `std::call_once`. | OK — no leak |
| `src/llm_client.cpp` | Response body was buffered into a `std::string` with no upper bound → a hostile/runaway endpoint could exhaust memory. | **Fixed**: capped at 64 MiB; the write callback aborts the transfer past that. |
| `libaegisub/common/llm.cpp` | All JSON handling uses value-type cajun objects (RAII); no raw `new`/`delete`. | OK |
| `src/command/llm.cpp` | `make_unique`, `std::vector`, stack `DialogProgress`; worker thread only computes strings, mutation/commit happens on the main thread after `Run` returns. | OK |

No `new`/`malloc` without ownership, no manual buffer arithmetic, and no raw
pointers escaping in the added code.

## 2. Opsec concerns

| # | Finding | Status |
| --- | --- | --- |
| O-1 | **Lua macro leaked the API key via `ps`** — it passed `-H 'x-api-key: KEY'` on the `curl` command line, visible to any local process. | **Fixed**: the Lua script now writes a `curl -K` config file (key + URL + headers) and invokes `curl -K <file>`; nothing secret reaches `argv`. |
| O-2 | **Shell injection in the Lua macro** — endpoint/key/URL were interpolated into a shell string run by `io.popen`. | **Fixed**: only a random temp path is interpolated now; all values live in the config file, never parsed by the shell. Values are escaped for the config-file quoting. |
| O-3 | **API key sent over plaintext HTTP** to a non-local host would travel in clear text. | **Fixed (native)**: `endpoint_is_safe()` blocks/prompts before sending a key to a non-localhost `http://` endpoint. |
| O-4 | **Temp files** (Lua) had predictable names and default permissions; the config file holds the key. | **Fixed**: `chmod 600`, RNG seeded, wide random suffix. (Aegisub's `?temp` is already a per-user directory.) |
| O-5 | **API key stored in plaintext** in the user config JSON (`LLM/API Key`). | **Mitigated**: leaving it blank falls back to `ANTHROPIC_API_KEY` / `OPENAI_API_KEY`, so the key need never be written to disk. Recommend an OS-keychain backend as a future enhancement. |
| O-6 | **Subtitle text is transmitted to a third-party API** during Translate/Proofread/etc. This is the intended function, but it is data egress the user should understand. | By design; documented in `LLM_FEATURES.md`. Local Ollama/llama.cpp keeps everything on-device. |

## 3. Other security issues

| # | Finding | Status / Assessment |
| --- | --- | --- |
| S-1 | TLS peer/host verification | **Hardened**: `CURLOPT_SSL_VERIFYPEER=1`, `CURLOPT_SSL_VERIFYHOST=2` set explicitly. |
| S-2 | Protocol/redirect downgrade | **Hardened**: `CURLOPT_PROTOCOLS_STR="http,https"`, `CURLOPT_REDIR_PROTOCOLS_STR="https"` so a redirect can't jump to `file:`/`gopher:` or downgrade to cleartext. |
| S-3 | Prompt injection from malicious subtitle text | **Low risk**: model output only ever replaces subtitle *text*; it is never executed, eval'd, or used as a path/command. Worst case is a poor edit, reverted with Undo. |
| S-4 | Update checker (`dialog_version_check.cpp`) | **Not active**: compiled but gated by `WITH_UPDATE_CHECKER`, and `enable_update_checker` defaults to **false** in this build. If ever enabled, verify it authenticates the update payload before trusting it. |
| S-5 | `.ass` attachment handling uses UU/Base64 encoding (`libaegisub/ass/uuencode`) | Encoding, not crypto; no integrity claim. No issue. |

## 4. Cryptography that is not post-quantum / not collision-resistant

**There is exactly one piece of live cryptography: TLS for the LLM HTTPS calls,
performed by libcurl's TLS backend.**

- **Not post-quantum.** The TLS handshake uses classical key exchange
  (X25519 / ECDHE / RSA) and classical signatures. This is true for whatever
  backend Homebrew's `libcurl` is linked against (OpenSSL/LibreSSL or, on a
  Secure-Transport build, Apple's stack). None of these negotiate a
  post-quantum KEM by default.
  - **To make it PQ-capable** you would link `libcurl` against a TLS library
    that negotiates a hybrid PQ key exchange — e.g. **OpenSSL ≥ 3.5 with
    `X25519MLKEM768`** (ML-KEM / FIPS 203). No application code change is needed;
    it is a build/link-time choice for the curl dependency. The provider
    endpoint must also support the hybrid group (Anthropic/OpenAI fronted by
    CDNs increasingly do; local Ollama/llama.cpp do not, but that traffic never
    leaves the machine).
- **Collision resistance is not a security property anywhere in this software** —
  no signatures, certificates pinning by digest, or content-integrity hashes are
  computed by Aegisub. So there is no MD5/SHA-1 collision exposure to worry
  about. (Certificate-chain signature validation happens inside the TLS library,
  out of scope here.)
- **Aegisub implements no symmetric or asymmetric primitives of its own**, so
  there is nothing else to migrate to PQ.

## 5. Hashes swappable to SHA-3 (do not affect SHA-2-bound file formats)

The question: identify hashes that are **not** part of a file format relying on
SHA-2 and could be drop-in replaced with SHA-3. There is **no SHA-2 usage in the
codebase at all**, so nothing is SHA-2-bound. The persisted hashes are:

| Location | Primitive | Purpose | File-format bound? | SHA-3 drop-in? |
| --- | --- | --- | --- | --- |
| `src/ffmpegsource_common.cpp:186` | `boost::crc_32_type` (CRC-32) | Names the FFMS2 index cache file: `?local/ffms2cache/<crc>_<len>_<mtime>.ffindex` | **No** — purely Aegisub's private cache naming | **Yes** |
| `src/bestsource_common.cpp:85` | `boost::crc_32_type` (CRC-32) | Names the BestSource index cache: `?local/bsindex/<name>_<crc>_<mtime>` | **No** | **Yes** |

Both are **internal cache keys**, combined with file length and modified-time.
Replacing CRC-32 with **SHA-3 (e.g. SHA3-256, truncated)** is safe and changes no
on-disk subtitle/video/codec format — the only effect is a one-time cache
regeneration (old cache files simply stop matching). They are the concrete answer
to the request.

Caveats:
- This is a **robustness/anti-collision** change, not a security fix: these keys
  aren't a trust boundary, and they already include length+mtime, so accidental
  CRC-32 collisions are already negligible.
- **SHA-3 is not currently a dependency.** A true drop-in needs a SHA-3 routine
  added (small vendored Keccak, or a crypto lib); boost provides MD5/SHA-1 detail
  but **no SHA-3**. If wanted, this can be implemented.

Not candidates: `std::hash` in `src/flyweight_hash.h` and the `unordered_map` in
`src/ass_file.cpp` are **in-memory only**, never serialized — swapping them to
SHA-3 would only cost performance and serves no purpose.

## 6. Incidental finding

- `src/main.cpp:248` calls `srand(time(nullptr))`, but **`rand()` is never called
  anywhere** in `src/` or `libaegisub/`. The seed is vestigial (predictable, but
  harmless because unused). Safe to delete; not a vulnerability.

## Summary of changes made in this audit

- `src/llm_client.cpp`: response size cap; explicit TLS verify; protocol/redirect
  restriction.
- `src/command/llm.cpp`: `endpoint_is_safe()` guard against cleartext key egress.
- `automation/autoload/aegisub-llm.lua`: curl `-K` config file (no key in argv,
  no shell injection); `chmod 600` + seeded RNG for temp files.

Open recommendations (not yet implemented): OS-keychain storage for the API key;
optionally swap the CRC-32 cache keys to SHA-3; link curl against OpenSSL ≥ 3.5
for hybrid PQ TLS if post-quantum transport is required.
