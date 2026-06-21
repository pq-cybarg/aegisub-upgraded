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

#include <main.h>

#include <libaegisub/llm.h>

using namespace agi::llm;

TEST(lagi_llm, provider_roundtrip) {
	EXPECT_EQ(Provider::Anthropic, provider_from_index(0));
	EXPECT_EQ(Provider::OpenAI, provider_from_index(1));
	EXPECT_EQ(Provider::Anthropic, provider_from_index(99));
	EXPECT_EQ(Provider::OpenAI, provider_from_string("openai"));
	EXPECT_EQ(Provider::Anthropic, provider_from_string("anthropic"));
	EXPECT_STREQ("anthropic", provider_to_string(Provider::Anthropic));
	EXPECT_STREQ("openai", provider_to_string(Provider::OpenAI));
}

TEST(lagi_llm, request_url_defaults_and_paths) {
	Config a; a.provider = Provider::Anthropic;
	EXPECT_EQ("https://api.anthropic.com/v1/messages", request_url(a));

	Config o; o.provider = Provider::OpenAI;
	EXPECT_EQ("https://api.openai.com/v1/chat/completions", request_url(o));

	// Local OpenAI-compatible servers (Ollama / llama.cpp).
	Config ollama; ollama.provider = Provider::OpenAI; ollama.endpoint = "http://localhost:11434/v1";
	EXPECT_EQ("http://localhost:11434/v1/chat/completions", request_url(ollama));

	Config llamacpp; llamacpp.provider = Provider::OpenAI; llamacpp.endpoint = "http://localhost:8080/v1/";
	EXPECT_EQ("http://localhost:8080/v1/chat/completions", request_url(llamacpp));
}

TEST(lagi_llm, headers_per_provider) {
	Config a; a.provider = Provider::Anthropic; a.api_key = "sk-ant";
	bool saw_key = false, saw_ver = false;
	for (auto const& kv : request_headers(a)) {
		if (kv.first == "x-api-key") { saw_key = true; EXPECT_EQ("sk-ant", kv.second); }
		if (kv.first == "anthropic-version") saw_ver = true;
	}
	EXPECT_TRUE(saw_key);
	EXPECT_TRUE(saw_ver);

	Config o; o.provider = Provider::OpenAI; o.api_key = "sk-oai";
	bool saw_auth = false;
	for (auto const& kv : request_headers(o))
		if (kv.first == "Authorization") { saw_auth = true; EXPECT_EQ("Bearer sk-oai", kv.second); }
	EXPECT_TRUE(saw_auth);

	// No key (local server) => no auth header emitted.
	Config local; local.provider = Provider::OpenAI;
	for (auto const& kv : request_headers(local))
		EXPECT_NE("Authorization", kv.first);
}

TEST(lagi_llm, request_body_escapes_and_shapes) {
	Config a; a.provider = Provider::Anthropic; a.model = "claude-sonnet-4-6";
	Request r{"sys", "line with \"quotes\" and \\N break"};
	std::string body = build_request_body(a, r);
	// Must be valid, escaped JSON containing the model and an escaped payload.
	EXPECT_NE(std::string::npos, body.find("\"model\""));
	EXPECT_NE(std::string::npos, body.find("claude-sonnet-4-6"));
	EXPECT_NE(std::string::npos, body.find("\\\"quotes\\\""));
	EXPECT_NE(std::string::npos, body.find("\\\\N"));
	EXPECT_NE(std::string::npos, body.find("\"system\""));

	Config o; o.provider = Provider::OpenAI; o.model = "gpt-x";
	std::string ob = build_request_body(o, r);
	EXPECT_NE(std::string::npos, ob.find("\"role\""));
	EXPECT_NE(std::string::npos, ob.find("\"system\""));
}

TEST(lagi_llm, parse_anthropic_response) {
	std::string body = R"({"content":[{"type":"text","text":"Bonjour"}],"role":"assistant"})";
	EXPECT_EQ("Bonjour", parse_response_text(Provider::Anthropic, body));
}

TEST(lagi_llm, parse_openai_response) {
	std::string body = R"({"choices":[{"message":{"role":"assistant","content":"Hola"}}]})";
	EXPECT_EQ("Hola", parse_response_text(Provider::OpenAI, body));
}

TEST(lagi_llm, parse_error_response_throws) {
	std::string body = R"({"error":{"type":"invalid_request_error","message":"bad key"}})";
	EXPECT_THROW(parse_response_text(Provider::Anthropic, body), LLMError);
	try {
		parse_response_text(Provider::Anthropic, body);
	} catch (LLMError const& e) {
		EXPECT_NE(std::string::npos, e.GetMessage().find("bad key"));
	}
}

TEST(lagi_llm, parse_garbage_throws) {
	EXPECT_THROW(parse_response_text(Provider::OpenAI, "not json at all"), LLMError);
}

TEST(lagi_llm, batch_prompt_mentions_count_and_lines) {
	std::vector<std::string> lines{"a", "b", "c"};
	std::string p = build_batch_user_prompt("Translate.", lines);
	EXPECT_NE(std::string::npos, p.find("Translate."));
	EXPECT_NE(std::string::npos, p.find("3"));
}

TEST(lagi_llm, batch_array_plain) {
	auto v = parse_batch_array(R"(["x","y"])", 2);
	ASSERT_EQ(2u, v.size());
	EXPECT_EQ("x", v[0]);
	EXPECT_EQ("y", v[1]);
}

TEST(lagi_llm, batch_array_with_fences_and_prose) {
	std::string resp = "Sure! Here you go:\n```json\n[\"hello\", \"world\"]\n```\n";
	auto v = parse_batch_array(resp, 2);
	ASSERT_EQ(2u, v.size());
	EXPECT_EQ("hello", v[0]);
	EXPECT_EQ("world", v[1]);
}

TEST(lagi_llm, batch_array_preserves_brackets_in_strings) {
	auto v = parse_batch_array(R"(["{\\an8}text [note]"])", 1);
	ASSERT_EQ(1u, v.size());
	EXPECT_EQ("{\\an8}text [note]", v[0]);
}

TEST(lagi_llm, batch_array_wrong_count_throws) {
	EXPECT_THROW(parse_batch_array(R"(["only one"])", 2), LLMError);
}

TEST(lagi_llm, batch_array_empty_expected) {
	auto v = parse_batch_array("anything", 0);
	EXPECT_TRUE(v.empty());
}

TEST(lagi_llm, batch_array_no_array_throws) {
	EXPECT_THROW(parse_batch_array("there is no array here", 1), LLMError);
}
