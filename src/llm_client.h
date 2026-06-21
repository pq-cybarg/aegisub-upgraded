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

/// @file llm_client.h
/// @brief libcurl HTTP transport for the provider-agnostic LLM client.

#pragma once

#include <libaegisub/llm.h>

#include <functional>
#include <string>

namespace agi { namespace llm {

/// Perform a single blocking chat completion against the configured provider.
///
/// This does real network I/O and must be called off the UI thread. The
/// @p is_cancelled predicate is polled during the transfer; returning true
/// aborts the request promptly (reported as an agi::UserCancelException).
///
/// @throws LLMError on configuration, network, HTTP, or API errors.
/// @throws agi::UserCancelException if @p is_cancelled becomes true.
std::string complete(Config const& cfg, Request const& req,
                     std::function<bool()> const& is_cancelled);

} }
