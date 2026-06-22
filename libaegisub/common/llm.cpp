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

/// @file llm.cpp
/// @brief Provider-agnostic LLM request building and response parsing.

#include "libaegisub/llm.h"

#include "libaegisub/cajun/elements.h"
#include "libaegisub/cajun/writer.h"
#include "libaegisub/json.h"

#include <sstream>

namespace {
using namespace agi::llm;

// Trim a single trailing '/' so base + "/path" never doubles the slash.
std::string trim_trailing_slash(std::string s) {
	while (!s.empty() && s.back() == '/')
		s.pop_back();
	return s;
}

// Templated so callers can pass a json::Object/json::Array directly without
// constructing a (move-only, non-copyable) json::UnknownElement from an lvalue.
template<typename T>
std::string serialize(T const& value) {
	std::ostringstream ss;
	agi::JsonWriter::Write(value, ss);
	return ss.str();
}

json::UnknownElement parse_json(std::string const& body) {
	std::istringstream ss(body);
	return agi::json_util::parse(ss);
}

// Return a short, single-line snippet of a body for use in error messages.
std::string snippet(std::string const& body, size_t n = 300) {
	std::string out = body.substr(0, n);
	for (char &c : out)
		if (c == '\n' || c == '\r') c = ' ';
	if (body.size() > n) out += "...";
	return out;
}

// Look up a string field in an object, returning "" if absent/non-string.
std::string get_string(json::Object const& obj, std::string const& key) {
	auto it = obj.find(key);
	if (it == obj.end()) return "";
	try { return static_cast<json::String const&>(it->second); }
	catch (...) { return ""; }
}
} // namespace

namespace agi { namespace llm {

const char *provider_to_string(Provider p) {
	switch (p) {
		case Provider::OpenAI:    return "openai";
		case Provider::Anthropic: return "anthropic";
	}
	return "anthropic";
}

Provider provider_from_index(int i) {
	return i == 1 ? Provider::OpenAI : Provider::Anthropic;
}

Provider provider_from_string(std::string const& s) {
	if (s == "openai" || s == "OpenAI" || s == "1") return Provider::OpenAI;
	return Provider::Anthropic;
}

std::string default_endpoint(Provider p) {
	switch (p) {
		case Provider::OpenAI:    return "https://api.openai.com/v1";
		case Provider::Anthropic: return "https://api.anthropic.com";
	}
	return "https://api.anthropic.com";
}

std::string request_url(Config const& cfg) {
	std::string base = cfg.endpoint.empty() ? default_endpoint(cfg.provider)
	                                         : trim_trailing_slash(cfg.endpoint);
	switch (cfg.provider) {
		case Provider::Anthropic:
			// Allow a base that already points at the messages endpoint.
			if (base.size() >= 12 && base.compare(base.size() - 12, 12, "/v1/messages") == 0)
				return base;
			return base + "/v1/messages";
		case Provider::OpenAI:
			if (base.size() >= 17 && base.compare(base.size() - 17, 17, "/chat/completions") == 0)
				return base;
			return base + "/chat/completions";
	}
	return base;
}

std::vector<std::pair<std::string, std::string>> request_headers(Config const& cfg) {
	std::vector<std::pair<std::string, std::string>> h;
	h.emplace_back("Content-Type", "application/json");
	switch (cfg.provider) {
		case Provider::Anthropic:
			if (!cfg.api_key.empty())
				h.emplace_back("x-api-key", cfg.api_key);
			h.emplace_back("anthropic-version", "2023-06-01");
			break;
		case Provider::OpenAI:
			if (!cfg.api_key.empty())
				h.emplace_back("Authorization", "Bearer " + cfg.api_key);
			break;
	}
	return h;
}

std::string build_request_body(Config const& cfg, Request const& req) {
	json::Object root;
	root["model"] = cfg.model;
	root["temperature"] = cfg.temperature;
	root["max_tokens"] = cfg.max_tokens;

	if (cfg.provider == Provider::Anthropic) {
		if (!req.system.empty())
			root["system"] = req.system;
		json::Object user_msg;
		user_msg["role"] = "user";
		user_msg["content"] = req.user;
		json::Array messages;
		messages.push_back(std::move(user_msg));
		root["messages"] = std::move(messages);
	}
	else { // OpenAI-compatible
		json::Array messages;
		if (!req.system.empty()) {
			json::Object sys_msg;
			sys_msg["role"] = "system";
			sys_msg["content"] = req.system;
			messages.push_back(std::move(sys_msg));
		}
		json::Object user_msg;
		user_msg["role"] = "user";
		user_msg["content"] = req.user;
		messages.push_back(std::move(user_msg));
		root["messages"] = std::move(messages);
	}

	return serialize(root);
}

std::string parse_response_text(Provider provider, std::string const& body) {
	json::UnknownElement parsed;
	try {
		parsed = parse_json(body);
	}
	catch (std::exception const& e) {
		throw LLMError("Could not parse LLM response as JSON: " + std::string(e.what())
		               + " -- body was: " + snippet(body));
	}

	try {
		json::Object const& root = parsed;

		// Both providers report errors under an "error" object.
		auto err = root.find("error");
		if (err != root.end()) {
			try {
				json::Object const& eo = err->second;
				std::string msg = get_string(eo, "message");
				if (!msg.empty()) throw LLMError("LLM API error: " + msg);
			}
			catch (LLMError const&) { throw; }
			catch (...) {}
			throw LLMError("LLM API returned an error: " + snippet(body));
		}

		if (provider == Provider::Anthropic) {
			json::Array const& content = root.at("content");
			std::string out;
			for (auto const& block : content) {
				json::Object const& bo = block;
				if (get_string(bo, "type") == "text")
					out += get_string(bo, "text");
			}
			if (out.empty())
				throw LLMError("LLM response contained no text content: " + snippet(body));
			return out;
		}
		else {
			json::Array const& choices = root.at("choices");
			if (choices.empty())
				throw LLMError("LLM response contained no choices: " + snippet(body));
			json::Object const& first = choices[0];
			json::Object const& message = first.at("message");
			std::string out = get_string(message, "content");
			if (out.empty())
				throw LLMError("LLM response message was empty: " + snippet(body));
			return out;
		}
	}
	catch (LLMError const&) {
		throw;
	}
	catch (std::exception const&) {
		throw LLMError("Unexpected LLM response shape: " + snippet(body));
	}
}

std::string build_batch_user_prompt(std::string const& instruction,
                                    std::vector<std::string> const& lines) {
	json::Array arr;
	for (auto const& l : lines)
		arr.push_back(json::UnknownElement(l));

	std::ostringstream ss;
	ss << instruction << "\n\n"
	   << "You are given " << lines.size() << " input line(s) as a JSON array of strings.\n"
	   << "Process each line independently in order and reply with ONLY a JSON array "
	   << "of exactly " << lines.size() << " string(s) — the result for each input line.\n"
	   << "Do not add commentary, explanations, or markdown code fences.\n"
	   << "Preserve every ASS override tag (text inside braces such as {\\i1} or "
	   << "{\\an8}) and every \\N hard line break exactly as they appear; only change "
	   << "the spoken text as instructed. If a line should be left unchanged, return it verbatim.\n\n"
	   << "Input lines:\n"
	   << serialize(arr);
	return ss.str();
}

// Extract the first balanced top-level JSON array from arbitrary text, ignoring
// brackets that appear inside JSON string literals.
namespace {
std::string extract_first_array(std::string const& s) {
	size_t start = s.find('[');
	if (start == std::string::npos)
		throw LLMError("No JSON array found in LLM response: " + snippet(s));

	int depth = 0;
	bool in_str = false, esc = false;
	for (size_t i = start; i < s.size(); ++i) {
		char c = s[i];
		if (in_str) {
			if (esc) esc = false;
			else if (c == '\\') esc = true;
			else if (c == '"') in_str = false;
		}
		else {
			if (c == '"') in_str = true;
			else if (c == '[') ++depth;
			else if (c == ']') {
				if (--depth == 0)
					return s.substr(start, i - start + 1);
			}
		}
	}
	throw LLMError("Unterminated JSON array in LLM response: " + snippet(s));
}
} // namespace

std::vector<std::string> parse_batch_array(std::string const& response, size_t expected) {
	if (expected == 0) return {};

	std::string arr_text = extract_first_array(response);

	json::UnknownElement parsed;
	try {
		parsed = parse_json(arr_text);
	}
	catch (std::exception const& e) {
		throw LLMError("LLM response was not valid JSON: " + std::string(e.what()));
	}

	std::vector<std::string> out;
	try {
		json::Array const& arr = parsed;
		out.reserve(arr.size());
		for (auto const& el : arr)
			out.push_back(static_cast<json::String const&>(el));
	}
	catch (std::exception const&) {
		throw LLMError("LLM response array did not contain plain strings: " + snippet(arr_text));
	}

	if (out.size() != expected)
		throw LLMError("LLM returned " + std::to_string(out.size()) +
		               " items but " + std::to_string(expected) + " were expected");
	return out;
}

namespace {
// Read a JSON number (Integer or Double) as a double; returns false if absent
// or not a number.
bool get_number(json::Object const& obj, std::string const& key, double &out) {
	auto it = obj.find(key);
	if (it == obj.end()) return false;
	try { out = static_cast<json::Double const&>(it->second); return true; }
	catch (...) {}
	try { out = static_cast<double>(static_cast<json::Integer const&>(it->second)); return true; }
	catch (...) {}
	return false;
}
} // namespace

std::string transcribe_url(Config const& cfg) {
	std::string base = cfg.endpoint.empty() ? default_endpoint(Provider::OpenAI)
	                                         : trim_trailing_slash(cfg.endpoint);
	const std::string path = "/audio/transcriptions";
	if (base.size() >= path.size() &&
	    base.compare(base.size() - path.size(), path.size(), path) == 0)
		return base;
	return base + path;
}

std::vector<TranscriptSegment> parse_transcription(std::string const& body) {
	json::UnknownElement parsed;
	try {
		parsed = parse_json(body);
	}
	catch (std::exception const& e) {
		throw LLMError("Could not parse transcription response as JSON: " +
		               std::string(e.what()) + " -- body was: " + snippet(body));
	}

	try {
		json::Object const& root = parsed;

		auto err = root.find("error");
		if (err != root.end()) {
			try {
				json::Object const& eo = err->second;
				std::string msg = get_string(eo, "message");
				if (!msg.empty()) throw LLMError("Transcription API error: " + msg);
			}
			catch (LLMError const&) { throw; }
			catch (...) {}
			throw LLMError("Transcription API returned an error: " + snippet(body));
		}

		auto seg_it = root.find("segments");
		if (seg_it == root.end())
			throw LLMError("Transcription response had no timed segments; request "
			               "response_format=verbose_json. Body: " + snippet(body));

		json::Array const& segments = seg_it->second;
		std::vector<TranscriptSegment> out;
		out.reserve(segments.size());
		for (auto const& s : segments) {
			json::Object const& so = s;
			double start = 0, end = 0;
			get_number(so, "start", start);
			get_number(so, "end", end);
			TranscriptSegment seg;
			seg.start_ms = static_cast<int>(start * 1000.0 + 0.5);
			seg.end_ms = static_cast<int>(end * 1000.0 + 0.5);
			seg.text = get_string(so, "text");
			// Whisper segments usually carry a leading space; trim edges.
			size_t b = seg.text.find_first_not_of(" \t\r\n");
			size_t e = seg.text.find_last_not_of(" \t\r\n");
			seg.text = (b == std::string::npos) ? "" : seg.text.substr(b, e - b + 1);
			if (!seg.text.empty())
				out.push_back(std::move(seg));
		}
		if (out.empty())
			throw LLMError("Transcription returned no usable segments: " + snippet(body));
		return out;
	}
	catch (LLMError const&) {
		throw;
	}
	catch (std::exception const&) {
		throw LLMError("Unexpected transcription response shape: " + snippet(body));
	}
}

} }
