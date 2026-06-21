// Copyright (c) 2026, Aegisub LLM contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file llm.h
/// @brief Provider-agnostic LLM request building and response parsing.
/// @ingroup libaegisub
///
/// This header contains only pure, side-effect-free logic: it knows how to
/// turn a request into a provider-specific HTTP body/URL/headers and how to
/// extract the assistant's text back out of a provider response. It performs
/// no networking itself, which keeps it unit-testable without a live API. The
/// actual HTTP transport lives in the Aegisub application (see src/llm_client).

#pragma once

#include <libaegisub/exception.h>

#include <string>
#include <utility>
#include <vector>

namespace agi { namespace llm {

DEFINE_EXCEPTION(LLMError, Exception);

/// Supported request/response wire formats.
enum class Provider {
	/// Anthropic Messages API (POST /v1/messages).
	Anthropic,
	/// OpenAI-compatible Chat Completions API (POST {base}/chat/completions).
	/// Covers OpenAI, Ollama, llama.cpp's llama-server, LM Studio, etc.
	OpenAI,
};

/// Map a provider enum to its stable, lowercase wire name.
const char *provider_to_string(Provider p);
/// Parse a provider from a name or numeric index (as stored in options).
/// Falls back to Anthropic for anything unrecognised.
Provider provider_from_string(std::string const& s);
Provider provider_from_index(int i);

/// Everything needed to address a provider. `endpoint` is a base URL; when it
/// is empty the provider's public default is used (see default_endpoint).
struct Config {
	Provider provider = Provider::Anthropic;
	std::string endpoint;
	std::string model;
	std::string api_key;
	double temperature = 0.3;
	int max_tokens = 4096;
};

/// A single-turn chat request.
struct Request {
	std::string system;
	std::string user;
};

/// The public base URL used when Config::endpoint is empty.
std::string default_endpoint(Provider p);

/// Build the full request URL (base endpoint + provider-specific path).
std::string request_url(Config const& cfg);

/// Build the provider-specific HTTP headers as (name, value) pairs.
/// The api_key is only emitted when non-empty (local servers often need none).
std::vector<std::pair<std::string, std::string>> request_headers(Config const& cfg);

/// Serialise the request to a provider-specific JSON body. All string values
/// are JSON-escaped, so subtitle text containing quotes/backslashes is safe.
std::string build_request_body(Config const& cfg, Request const& req);

/// Extract the assistant's text from a provider response body.
/// @throws LLMError if the body reports an error or cannot be understood.
std::string parse_response_text(Provider provider, std::string const& body);

// --- Batch protocol -------------------------------------------------------
//
// Subtitle operations process many lines at once. To preserve cross-line
// context (essential for good translation) and minimise round-trips, lines are
// sent as a numbered batch and the model is asked to reply with a JSON array of
// the same length. These helpers build that prompt and robustly recover the
// array from a possibly-chatty response (tolerating code fences and prose).

/// Compose a user prompt embedding `lines` as a numbered list and instructing
/// the model to return a JSON array of exactly `lines.size()` strings.
std::string build_batch_user_prompt(std::string const& instruction,
                                     std::vector<std::string> const& lines);

/// Recover a JSON array of strings from a model response. Scans for the first
/// balanced top-level array, parses it, and verifies its length.
/// @throws LLMError if no array of `expected` strings can be found.
std::vector<std::string> parse_batch_array(std::string const& response, size_t expected);

} }
