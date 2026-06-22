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

/// @file subs_edit_ctrl_native.cpp
/// @brief A native (wxTextCtrl) subtitle edit control.

#include "subs_edit_ctrl_native.h"

SubsTextEditCtrlNative::SubsTextEditCtrlNative(wxWindow *parent, wxSize size, long style, agi::Context *)
: wxTextCtrl(parent, -1, "", wxDefaultPosition, size,
             style | wxTE_MULTILINE | wxTE_RICH2 | wxTE_NOHIDESEL)
{
}

void SubsTextEditCtrlNative::SetTextTo(std::string const& text) {
	// ChangeValue (unlike SetValue) does not generate a wxEVT_TEXT, so loading
	// the active line's text doesn't look like a user edit.
	ChangeValue(wxString::FromUTF8(text.data(), text.size()));
}

std::string SubsTextEditCtrlNative::GetTextUTF8() const {
	auto buf = GetValue().utf8_str();
	return std::string(buf.data(), buf.length());
}
