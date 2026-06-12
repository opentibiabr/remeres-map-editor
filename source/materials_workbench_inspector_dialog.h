#ifndef RME_MATERIALS_WORKBENCH_INSPECTOR_DIALOG_H_
#define RME_MATERIALS_WORKBENCH_INSPECTOR_DIALOG_H_

#include <functional>
#include <vector>

#include <wx/dialog.h>

class wxButton;
class wxChoice;
class wxListCtrl;
class wxNotebook;
class wxPanel;
class wxSearchCtrl;
class wxSplitterWindow;
class wxTextCtrl;
class wxWindow;
class SQLiteMaterialsInspectorPanel;

class MaterialsWorkbenchInspectorDialog : public wxDialog {
public:
	using GoToHandler = std::function<bool(const wxString &entityKind, int64_t entityId, const wxString &entityName)>;

	MaterialsWorkbenchInspectorDialog(wxWindow* parent, GoToHandler goToHandler);

	void SelectSqliteTab();

private:
	struct WarningRow {
		wxString severity;
		wxString domain;
		wxString entityKind;
		int64_t entityId = 0;
		wxString entityName;
		wxString issue;
		int count = 0;
		wxString status;
		wxString details;
	};

	void BuildLayout();
	void BuildHealthTab(wxNotebook* notebook);
	void BuildSqliteTab(wxNotebook* notebook);
	void ReloadWarnings();
	void RebuildIssueFilterChoices();
	void ApplyWarningFilter();
	void UpdateWarningDetails();
	void GoToSelectedWarning();

	GoToHandler goToHandler_;

	wxNotebook* notebook_ = nullptr;
	wxSearchCtrl* warningSearchCtrl_ = nullptr;
	wxChoice* warningSeverityChoice_ = nullptr;
	wxChoice* warningDomainChoice_ = nullptr;
	wxChoice* warningIssueChoice_ = nullptr;
	wxButton* warningRescanButton_ = nullptr;
	wxButton* warningGoToButton_ = nullptr;
	wxListCtrl* warningList_ = nullptr;
	wxTextCtrl* warningDetails_ = nullptr;

	std::vector<WarningRow> warnings_;
	std::vector<size_t> filteredWarningIndices_;

	SQLiteMaterialsInspectorPanel* sqlitePanel_ = nullptr;
};

#endif
