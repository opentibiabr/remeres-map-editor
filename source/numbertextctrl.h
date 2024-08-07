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

#ifndef _RME_NUMBER_TEXT_CTRL_H_
#define _RME_NUMBER_TEXT_CTRL_H_

// Text ctrl that only allows number input
class NumberTextCtrl : public wxTextCtrl {
public:
	// Please avoid using wxTextValidator rather than wxFILTER_NONE, because it prevents the event clipboard paste to be fired.
	NumberTextCtrl(wxWindow* parent, wxWindowID id, long value, long minValue, long maxValue, const wxPoint &pos, const wxSize &sz, long style, const wxString &name) :
		wxTextCtrl(parent, id, wxString::Format("%i", value), pos, sz, style, wxTextValidator(wxFILTER_NONE), name),
		minValue(minValue), maxValue(maxValue), lastValue(value) { }
	// Please avoid using wxTextValidator rather than wxFILTER_NONE, because it prevents the event clipboard paste to be fired.
	NumberTextCtrl(wxWindow* parent, wxWindowID id, long value, long minValue, long maxValue, long style, const wxString &name, const wxPoint &pos, const wxSize &sz) :
		wxTextCtrl(parent, id, wxString::Format("%i", value), pos, sz, style, wxTextValidator(wxFILTER_NONE), name),
		minValue(minValue), maxValue(maxValue), lastValue(value) { }
	~NumberTextCtrl() = default;

	void OnKillFocus(wxFocusEvent &);
	void OnTextEnter(wxCommandEvent &);
	void EnsureOnlyNumbers(wxCommandEvent &evt);

	wxString TextFilterDigits(const wxString &string);

	long GetIntValue();
	void SetIntValue(long value);

	void SetMinValue(long value);
	void SetMaxValue(long value);

protected:
	void CheckRange();

	long minValue, maxValue, lastValue;
	DECLARE_EVENT_TABLE();
};

#endif
