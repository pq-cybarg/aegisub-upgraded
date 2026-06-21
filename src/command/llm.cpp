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

/// @file command/llm.cpp
/// @brief LLM-powered subtitle commands (translate, condense, proofread, ...).
/// @ingroup command

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../compat.h"
#include "../dialog_progress.h"
#include "../include/aegisub/context.h"
#include "../llm_client.h"
#include "../options.h"
#include "../preferences.h"
#include "../selection_controller.h"

#include <libaegisub/exception.h>
#include <libaegisub/format.h>
#include <libaegisub/llm.h>
#include <libaegisub/make_unique.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include <wx/msgdlg.h>
#include <wx/textdlg.h>

namespace {
using cmd::Command;
namespace llm = agi::llm;

// --- Provider presets -----------------------------------------------------
//
// The two underlying wire formats (Anthropic, OpenAI-compatible) are exposed
// as friendlier presets that also supply a sensible default endpoint. Ollama
// and llama.cpp's server both speak the OpenAI chat-completions API, so they
// are first-class presets pointing at their usual local ports.
struct Preset {
	const char *label;
	llm::Provider transport;
	const char *endpoint; // "" means use the transport's public default
};

const Preset presets[] = {
	{"Anthropic (Claude)",      llm::Provider::Anthropic, ""},
	{"OpenAI",                  llm::Provider::OpenAI,    "https://api.openai.com/v1"},
	{"Ollama (local)",          llm::Provider::OpenAI,    "http://localhost:11434/v1"},
	{"llama.cpp server (local)", llm::Provider::OpenAI,   "http://localhost:8080/v1"},
};
const int preset_count = sizeof(presets) / sizeof(presets[0]);

// Assemble the active configuration from the options system, applying the
// selected preset and falling back to a provider-appropriate environment
// variable when no API key is stored.
llm::Config load_config() {
	int idx = static_cast<int>(OPT_GET("LLM/Provider")->GetInt());
	if (idx < 0 || idx >= preset_count) idx = 0;

	llm::Config cfg;
	cfg.provider = presets[idx].transport;

	cfg.endpoint = OPT_GET("LLM/Endpoint")->GetString();
	if (cfg.endpoint.empty())
		cfg.endpoint = presets[idx].endpoint; // may stay "" -> library default

	cfg.model = OPT_GET("LLM/Model")->GetString();

	cfg.api_key = OPT_GET("LLM/API Key")->GetString();
	if (cfg.api_key.empty()) {
		const char *env = std::getenv(cfg.provider == llm::Provider::Anthropic
		                              ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY");
		if (env) cfg.api_key = env;
	}

	cfg.temperature = OPT_GET("LLM/Temperature")->GetDouble();
	cfg.max_tokens = static_cast<int>(OPT_GET("LLM/Max Tokens")->GetInt());
	return cfg;
}

std::string target_language() { return OPT_GET("LLM/Target Language")->GetString(); }
int cps_target()              { return static_cast<int>(OPT_GET("LLM/CPS Target")->GetInt()); }
int batch_size()              { int b = static_cast<int>(OPT_GET("LLM/Batch Size")->GetInt()); return b < 1 ? 1 : b; }

// Count Unicode codepoints in a UTF-8 string (continuation bytes excluded).
size_t codepoint_count(std::string const& s) {
	size_t n = 0;
	for (unsigned char c : s)
		if ((c & 0xC0) != 0x80) ++n;
	return n;
}

bool require_selection(const agi::Context *c) {
	if (!c->selectionController->GetSelectedSet().empty()) return true;
	wxMessageBox(_("Select one or more lines first."), _("LLM"), wxOK | wxICON_INFORMATION);
	return false;
}

bool require_model(llm::Config const& cfg) {
	if (!cfg.model.empty()) return true;
	wxMessageBox(_("No model is configured. Open Preferences \xE2\x86\x92 LLM to set a provider and model."),
	             _("LLM"), wxOK | wxICON_WARNING);
	return false;
}

// Refuse to silently send an API key to a non-local plaintext http endpoint,
// where it would travel in clear text. Local servers (Ollama/llama.cpp) are
// allowed without prompting; https is always fine.
bool endpoint_is_safe(llm::Config const& cfg) {
	if (cfg.api_key.empty()) return true;
	std::string url = llm::request_url(cfg);
	if (url.compare(0, 7, "http://") != 0) return true;
	std::string host = url.substr(7);
	host = host.substr(0, host.find_first_of("/:"));
	if (host == "localhost" || host == "127.0.0.1" || host == "::1") return true;
	return wxYES == wxMessageBox(
		wxString::Format(_("The endpoint \"%s\" uses unencrypted HTTP, so your API key "
		                   "would be sent in clear text. Send it anyway?"), to_wx(host)),
		_("LLM"), wxYES_NO | wxICON_WARNING);
}

// Process one chunk of lines through the model. Tries a single batched request
// first; if the model returns the wrong number of items (or malformed JSON),
// falls back to one request per line so a single bad line can't fail the batch.
std::vector<std::string> process_chunk(llm::Config const& cfg,
                                       std::string const& system_prompt,
                                       std::string const& instruction,
                                       std::vector<std::string> const& lines,
                                       agi::ProgressSink *ps) {
	auto cancel = [ps] { return ps->IsCancelled(); };

	llm::Request req{system_prompt, llm::build_batch_user_prompt(instruction, lines)};
	try {
		std::string reply = llm::complete(cfg, req, cancel);
		return llm::parse_batch_array(reply, lines.size());
	}
	catch (agi::UserCancelException const&) { throw; }
	catch (agi::Exception const&) {
		// Batch failed: degrade to per-line requests.
		std::vector<std::string> out;
		out.reserve(lines.size());
		for (auto const& line : lines) {
			if (ps->IsCancelled()) throw agi::UserCancelException("cancelled");
			llm::Request one{system_prompt, llm::build_batch_user_prompt(instruction, {line})};
			std::string reply = llm::complete(cfg, one, cancel);
			auto arr = llm::parse_batch_array(reply, 1);
			out.push_back(arr.front());
		}
		return out;
	}
}

// Shared driver for the batch-style operations (translate/proofread/rephrase/
// custom). Gathers the selection, runs the model off the UI thread with a
// cancelable progress dialog, then applies all edits as one undo-able commit.
void run_batch_operation(agi::Context *c, wxString const& undo_msg,
                         std::string const& system_prompt,
                         std::string const& instruction) {
	llm::Config cfg = load_config();
	if (!require_selection(c) || !require_model(cfg) || !endpoint_is_safe(cfg)) return;

	std::vector<AssDialogue *> sel = c->selectionController->GetSortedSelection();
	std::vector<std::string> inputs;
	inputs.reserve(sel.size());
	for (auto *line : sel)
		inputs.push_back(line->Text.get());

	std::vector<std::string> results;
	std::string error;
	bool cancelled = false;

	DialogProgress progress(c->parent, _("LLM"), _("Contacting language model\xE2\x80\xA6"));
	progress.Run([&](agi::ProgressSink *ps) {
		try {
			const int bs = batch_size();
			results.reserve(inputs.size());
			for (size_t start = 0; start < inputs.size(); start += bs) {
				if (ps->IsCancelled()) { cancelled = true; return; }
				size_t end = std::min(inputs.size(), start + bs);
				std::vector<std::string> chunk(inputs.begin() + start, inputs.begin() + end);
				ps->SetMessage(agi::format("Processing lines %d\xE2\x80\x93%d of %d",
				                           (int)start + 1, (int)end, (int)inputs.size()));
				auto out = process_chunk(cfg, system_prompt, instruction, chunk, ps);
				for (auto &s : out) results.push_back(std::move(s));
				ps->SetProgress((int64_t)end, (int64_t)inputs.size());
			}
		}
		catch (agi::UserCancelException const&) { cancelled = true; }
		catch (agi::Exception const& e) { error = e.GetMessage(); }
		catch (std::exception const& e) { error = e.what(); }
	});

	if (cancelled) return;
	if (!error.empty()) {
		wxMessageBox(to_wx(error), _("LLM error"), wxOK | wxICON_ERROR);
		return;
	}
	if (results.size() != sel.size()) return; // defensive; should not happen

	int changed = 0;
	for (size_t i = 0; i < sel.size(); ++i) {
		if (sel[i]->Text.get() != results[i]) {
			sel[i]->Text = results[i];
			++changed;
		}
	}
	if (changed)
		c->ass->Commit(undo_msg, AssFile::COMMIT_DIAG_TEXT);
}

// --- Individual commands --------------------------------------------------

std::string translate_system() {
	return "You are an expert subtitle translator. You translate dialogue naturally "
	       "and idiomatically for on-screen reading, keeping the register and tone of "
	       "the original. You never translate or alter ASS override tags.";
}

struct llm_translate final : public Command {
	CMD_NAME("llm/translate")
	STR_MENU("&Translate selected lines\xE2\x80\xA6")
	STR_DISP("Translate selected lines")
	STR_HELP("Translate the selected lines into the configured target language using an LLM")
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return !c->selectionController->GetSelectedSet().empty();
	}
	void operator()(agi::Context *c) override {
		std::string lang = target_language();
		if (lang.empty()) lang = "English";
		run_batch_operation(c, _("LLM translate"), translate_system(),
		                    "Translate each line into " + lang + ".");
	}
};

struct llm_proofread final : public Command {
	CMD_NAME("llm/proofread")
	STR_MENU("&Proofread selected lines")
	STR_DISP("Proofread selected lines")
	STR_HELP("Fix grammar, spelling and punctuation in the selected lines using an LLM")
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return !c->selectionController->GetSelectedSet().empty();
	}
	void operator()(agi::Context *c) override {
		run_batch_operation(c, _("LLM proofread"),
		                    "You are a meticulous subtitle proofreader.",
		                    "Correct any spelling, grammar, capitalization and punctuation "
		                    "mistakes in each line. Do not rephrase or change the meaning, "
		                    "and do not translate. Leave already-correct lines unchanged.");
	}
};

struct llm_rephrase final : public Command {
	CMD_NAME("llm/rephrase")
	STR_MENU("&Rephrase selected lines")
	STR_DISP("Rephrase selected lines")
	STR_HELP("Rewrite awkward or machine-translated lines into natural dialogue using an LLM")
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return !c->selectionController->GetSelectedSet().empty();
	}
	void operator()(agi::Context *c) override {
		run_batch_operation(c, _("LLM rephrase"),
		                    "You are a subtitle editor who turns stiff or machine-translated "
		                    "dialogue into natural, fluent speech.",
		                    "Rewrite each line so it reads as natural spoken dialogue while "
		                    "keeping the same meaning. Keep it concise. Do not translate.");
	}
};

struct llm_custom final : public Command {
	CMD_NAME("llm/custom")
	STR_MENU("&Custom prompt\xE2\x80\xA6")
	STR_DISP("Custom prompt")
	STR_HELP("Apply a custom instruction to the selected lines using an LLM")
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return !c->selectionController->GetSelectedSet().empty();
	}
	void operator()(agi::Context *c) override {
		if (!require_selection(c)) return;
		wxTextEntryDialog dlg(c->parent,
		                      _("Instruction to apply to each selected line:"),
		                      _("LLM custom prompt"), "", wxOK | wxCANCEL | wxTE_MULTILINE);
		if (dlg.ShowModal() != wxID_OK) return;
		std::string instruction = from_wx(dlg.GetValue());
		if (instruction.empty()) return;
		run_batch_operation(c, _("LLM custom prompt"),
		                    "You are a helpful subtitle editing assistant.", instruction);
	}
};

// Condense is per-line: each line's character budget depends on its own
// duration, so it cannot use the uniform batch protocol.
struct llm_condense final : public Command {
	CMD_NAME("llm/condense")
	STR_MENU("&Condense for reading speed")
	STR_DISP("Condense for reading speed")
	STR_HELP("Shorten lines that exceed the target characters-per-second using an LLM")
	CMD_TYPE(COMMAND_VALIDATE)
	bool Validate(const agi::Context *c) override {
		return !c->selectionController->GetSelectedSet().empty();
	}
	void operator()(agi::Context *c) override {
		llm::Config cfg = load_config();
		if (!require_selection(c) || !require_model(cfg) || !endpoint_is_safe(cfg)) return;

		const int cps = cps_target();
		std::vector<AssDialogue *> sel = c->selectionController->GetSortedSelection();

		// Pre-compute which lines exceed the budget and by how much.
		struct Job { AssDialogue *line; int budget; };
		std::vector<Job> jobs;
		for (auto *line : sel) {
			int dur_ms = (int)line->End - (int)line->Start;
			if (dur_ms <= 0) continue;
			int budget = cps * dur_ms / 1000;
			if (budget < 1) continue;
			if ((int)codepoint_count(line->GetStrippedText()) > budget)
				jobs.push_back({line, budget});
		}
		if (jobs.empty()) {
			wxMessageBox(_("No selected line exceeds the target reading speed."),
			             _("LLM"), wxOK | wxICON_INFORMATION);
			return;
		}

		std::vector<std::string> results(jobs.size());
		std::string error;
		bool cancelled = false;

		DialogProgress progress(c->parent, _("LLM"), _("Condensing lines\xE2\x80\xA6"));
		progress.Run([&](agi::ProgressSink *ps) {
			auto cancel = [ps] { return ps->IsCancelled(); };
			try {
				for (size_t i = 0; i < jobs.size(); ++i) {
					if (ps->IsCancelled()) { cancelled = true; return; }
					ps->SetMessage(agi::format("Condensing line %d of %d",
					                           (int)i + 1, (int)jobs.size()));
					std::string instruction = agi::format(
						"Shorten this subtitle line to at most %d characters (currently "
						"longer) while preserving its meaning and tone. Keep it natural.",
						jobs[i].budget);
					llm::Request req{
						"You condense subtitles to fit reading-speed limits without losing meaning.",
						llm::build_batch_user_prompt(instruction, {jobs[i].line->Text.get()})};
					std::string reply = llm::complete(cfg, req, cancel);
					results[i] = llm::parse_batch_array(reply, 1).front();
					ps->SetProgress((int64_t)i + 1, (int64_t)jobs.size());
				}
			}
			catch (agi::UserCancelException const&) { cancelled = true; }
			catch (agi::Exception const& e) { error = e.GetMessage(); }
			catch (std::exception const& e) { error = e.what(); }
		});

		if (cancelled) return;
		if (!error.empty()) {
			wxMessageBox(to_wx(error), _("LLM error"), wxOK | wxICON_ERROR);
			return;
		}

		int changed = 0;
		for (size_t i = 0; i < jobs.size(); ++i) {
			if (!results[i].empty() && jobs[i].line->Text.get() != results[i]) {
				jobs[i].line->Text = results[i];
				++changed;
			}
		}
		if (changed)
			c->ass->Commit(_("LLM condense"), AssFile::COMMIT_DIAG_TEXT);
	}
};

struct llm_configure final : public Command {
	CMD_NAME("llm/configure")
	STR_MENU("&Configure\xE2\x80\xA6")
	STR_DISP("Configure LLM")
	STR_HELP("Open the LLM preferences page")
	void operator()(agi::Context *c) override {
		Preferences(c->parent).ShowModal();
	}
};
} // namespace

namespace cmd {
	void init_llm() {
		reg(agi::make_unique<llm_translate>());
		reg(agi::make_unique<llm_condense>());
		reg(agi::make_unique<llm_proofread>());
		reg(agi::make_unique<llm_rephrase>());
		reg(agi::make_unique<llm_custom>());
		reg(agi::make_unique<llm_configure>());
	}
}
