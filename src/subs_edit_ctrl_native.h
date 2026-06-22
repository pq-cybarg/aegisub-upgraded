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

/// @file subs_edit_ctrl_native.h
/// @brief A native (wxTextCtrl) subtitle edit control.
/// @ingroup main_ui
///
/// An opt-in alternative to the Scintilla-based SubsTextEditCtrl, selected via
/// the "Subtitle/Edit Box/Native" option. It trades Aegisub's editor extras
/// (override-tag syntax highlighting, calltips, inline spell-check squiggles)
/// for the host OS's native text widget, which gives better system integration
/// (dictation, the Services menu, accessibility) on some platforms. The default
/// editor remains Scintilla; this control is only constructed when the option
/// is enabled.

#include <string>

#include <wx/textctrl.h>

namespace agi { struct Context; }

class SubsTextEditCtrlNative final : public wxTextCtrl {
public:
	SubsTextEditCtrlNative(wxWindow *parent, wxSize size, long style, agi::Context *context);

	/// Replace the contents without emitting a change event (used when the
	/// active line changes), mirroring SubsTextEditCtrl::SetTextTo.
	void SetTextTo(std::string const& text);

	/// Return the current contents as UTF-8 bytes.
	std::string GetTextUTF8() const;
};
