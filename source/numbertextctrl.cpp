//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"
#include "numbertextctrl.h"

BEGIN_EVENT_TABLE(NumberTextCtrl, wxTextCtrl)
EVT_KILL_FOCUS(NumberTextCtrl::OnKillFocus)
EVT_TEXT_ENTER(wxID_ANY, NumberTextCtrl::OnTextEnter)
EVT_TEXT(wxID_ANY, NumberTextCtrl::EnsureOnlyNumbers)
END_EVENT_TABLE()

void NumberTextCtrl::OnKillFocus(wxFocusEvent &evt) {
	CheckRange();
	evt.Skip();
}

wxString NumberTextCtrl::TextFilterDigits(const wxString &text) {
	wxString newText;
	for (size_t position = 0; position < text.size(); ++position) {
		if (text[position] >= '0' && text[position] <= '9') {
			newText.Append(text[position]);
		}
	}

	return newText;
}

void NumberTextCtrl::EnsureOnlyNumbers(wxCommandEvent &evt) {
	const auto newText = TextFilterDigits(GetValue().ToStdString());

	ChangeValue(newText);
}

void NumberTextCtrl::OnTextEnter(wxCommandEvent &evt) {
	CheckRange();
}

void NumberTextCtrl::SetIntValue(long value) {
	// Will generate events
	SetValue(wxString::Format("%i", value));
}

long NumberTextCtrl::GetIntValue() {
	long l;
	return GetValue().ToLong(&l) ? l : 0;
}

void NumberTextCtrl::SetMinValue(long value) {
	if (value == minValue) {
		return;
	}
	minValue = value;
	CheckRange();
}

void NumberTextCtrl::SetMaxValue(long value) {
	if (value == maxValue) {
		return;
	}
	maxValue = value;
	CheckRange();
}

void NumberTextCtrl::CheckRange() {
	auto text = GetValue().ToStdString();

	auto newText = TextFilterDigits(text);

	// Check that value is in range
	long v;
	if (newText.size() != 0 && newText.ToLong(&v)) {
		if (v < minValue) {
			v = minValue;
		} else if (v > maxValue) {
			v = maxValue;
		}

		newText.clear();
		newText = wxString::Format("%i", v);
		lastValue = v;
	} else {
		newText.clear();
		newText = wxString::Format("%i", lastValue);
	}

	// Check if there was any change
	if (newText != text) {
		// ChangeValue doesn't generate events
		ChangeValue(newText);
	}
}
