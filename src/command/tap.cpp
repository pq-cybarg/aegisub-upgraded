// Copyright (c) 2026, Aegisub contributors
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

/// @file command/tap.cpp
/// @brief Tap-to-time: time lines by tapping along to the audio.
/// @ingroup command
///
/// This is a lightweight, opt-in revival of the "tap timing" workflow found in
/// the EaterOA / Aegisub-Japan7 lineage. It is implemented entirely on top of
/// the existing public audio/selection APIs as a normal command, so it adds the
/// feature without touching the audio timing controllers and cannot affect
/// regular timing unless the user explicitly invokes it (e.g. via a bound key).
///
/// Workflow: start audio playback, then tap the bound key at the start of each
/// line. Each tap sets the active line's start to the current playback
/// position, ends the previous line at that same position (so lines are
/// gap-free), and advances to the next line.

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../audio_controller.h"
#include "../include/aegisub/context.h"
#include "../selection_controller.h"

#include <libaegisub/make_unique.h>

namespace {
using cmd::Command;

/// Return the dialogue line immediately before/after @p line in document order.
AssDialogue *neighbour(agi::Context *c, AssDialogue *line, bool want_next) {
	AssDialogue *prev = nullptr;
	for (auto &d : c->ass->Events) {
		if (want_next) {
			if (prev == line) return &d;
		}
		else if (&d == line) {
			return prev;
		}
		prev = &d;
	}
	return nullptr;
}

struct time_tap final : public Command {
	CMD_NAME("time/tap")
	STR_MENU("&Tap to time")
	STR_DISP("Tap to time")
	STR_HELP("Tap at the start of each line to time it from the playing audio")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return c->selectionController->GetActiveLine() != nullptr;
	}

	void operator()(agi::Context *c) override {
		AssDialogue *active = c->selectionController->GetActiveLine();
		if (!active) return;

		int pos = c->audioController->GetPlaybackPosition();

		active->Start = pos;
		// Keep a sane (non-negative) duration until the next tap closes it.
		if (active->End < active->Start)
			active->End = pos;

		// End the previous line where this one starts, so lines stay gap-free.
		if (AssDialogue *prev = neighbour(c, active, false))
			if (pos > prev->Start)
				prev->End = pos;

		c->ass->Commit(_("tap to time"), AssFile::COMMIT_DIAG_TIME);

		if (AssDialogue *next = neighbour(c, active, true))
			c->selectionController->SetSelectionAndActive({next}, next);
	}
};
}

namespace cmd {
	void init_tap() {
		reg(agi::make_unique<time_tap>());
	}
}
