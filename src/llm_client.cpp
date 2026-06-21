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

/// @file llm_client.cpp
/// @brief libcurl HTTP transport for the provider-agnostic LLM client.

#include "llm_client.h"

#include <libaegisub/exception.h>

#include <curl/curl.h>

#include <mutex>

namespace {
std::once_flag curl_init_flag;

void ensure_curl_init() {
	std::call_once(curl_init_flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Bound the response we will buffer so a malicious or runaway endpoint cannot
// exhaust memory. 64 MiB is far larger than any legitimate chat completion.
constexpr size_t kMaxResponseBytes = 64u * 1024 * 1024;

size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
	auto *out = static_cast<std::string *>(userdata);
	size_t n = size * nmemb;
	if (out->size() + n > kMaxResponseBytes)
		return 0; // signal an error to libcurl, aborting the transfer
	out->append(ptr, n);
	return n;
}

int xfer_cb(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
	auto *is_cancelled = static_cast<std::function<bool()> *>(clientp);
	return (*is_cancelled && (*is_cancelled)()) ? 1 : 0;
}

// RAII wrappers so we never leak the easy handle or the header list.
struct CurlEasy {
	CURL *h = curl_easy_init();
	~CurlEasy() { if (h) curl_easy_cleanup(h); }
};
struct CurlHeaders {
	curl_slist *list = nullptr;
	~CurlHeaders() { if (list) curl_slist_free_all(list); }
	void add(std::string const& name, std::string const& value) {
		list = curl_slist_append(list, (name + ": " + value).c_str());
	}
};
} // namespace

namespace agi { namespace llm {

std::string complete(Config const& cfg, Request const& req,
                     std::function<bool()> const& is_cancelled) {
	if (cfg.model.empty())
		throw LLMError("No model configured. Set one in Preferences -> LLM.");

	ensure_curl_init();

	CurlEasy easy;
	if (!easy.h)
		throw LLMError("Failed to initialise HTTP client (libcurl).");

	std::string url = request_url(cfg);
	std::string body = build_request_body(cfg, req);

	CurlHeaders headers;
	for (auto const& kv : request_headers(cfg))
		headers.add(kv.first, kv.second);

	std::string response;
	std::function<bool()> cancel_copy = is_cancelled;

	// TLS hardening: verify the peer certificate and that the hostname matches.
	// These are libcurl's defaults, but we set them explicitly so the behaviour
	// can't be weakened by a build/environment that flips the defaults.
	curl_easy_setopt(easy.h, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(easy.h, CURLOPT_SSL_VERIFYHOST, 2L);
	// Only ever speak http/https (http is needed for local Ollama/llama.cpp),
	// and never let a redirect downgrade or jump to another protocol.
	curl_easy_setopt(easy.h, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(easy.h, CURLOPT_REDIR_PROTOCOLS_STR, "https");

	curl_easy_setopt(easy.h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(easy.h, CURLOPT_POST, 1L);
	curl_easy_setopt(easy.h, CURLOPT_POSTFIELDS, body.c_str());
	curl_easy_setopt(easy.h, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
	curl_easy_setopt(easy.h, CURLOPT_HTTPHEADER, headers.list);
	curl_easy_setopt(easy.h, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(easy.h, CURLOPT_WRITEDATA, &response);
	curl_easy_setopt(easy.h, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(easy.h, CURLOPT_XFERINFOFUNCTION, xfer_cb);
	curl_easy_setopt(easy.h, CURLOPT_XFERINFODATA, &cancel_copy);
	curl_easy_setopt(easy.h, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(easy.h, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(easy.h, CURLOPT_TIMEOUT, 300L);
	curl_easy_setopt(easy.h, CURLOPT_USERAGENT, "Aegisub-LLM/1.0");
	curl_easy_setopt(easy.h, CURLOPT_ACCEPT_ENCODING, "");

	CURLcode res = curl_easy_perform(easy.h);

	if (res == CURLE_ABORTED_BY_CALLBACK)
		throw agi::UserCancelException("LLM request cancelled");
	if (res != CURLE_OK)
		throw LLMError(std::string("Network error contacting LLM provider: ") +
		               curl_easy_strerror(res) + " (" + url + ")");

	long status = 0;
	curl_easy_getinfo(easy.h, CURLINFO_RESPONSE_CODE, &status);

	// Providers return a JSON error body on 4xx/5xx; parse_response_text turns
	// that into a descriptive LLMError. Only fall back to a bare status message
	// when there is no body to interpret.
	if (response.empty()) {
		if (status >= 400)
			throw LLMError("LLM provider returned HTTP " + std::to_string(status) +
			               " with an empty body (" + url + ")");
		throw LLMError("LLM provider returned an empty response (" + url + ")");
	}

	return parse_response_text(cfg.provider, response);
}

} }
