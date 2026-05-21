#ifndef RME_SQLITE_MATERIALS_INSPECTOR_H_
#define RME_SQLITE_MATERIALS_INSPECTOR_H_

#include "brush_database.h"

#include <wx/dialog.h>

class wxChoice;
class wxCommandEvent;
class wxListBox;
class wxNotebook;
class wxTextCtrl;
class wxWindow;

class SQLiteMaterialsInspectorDialog : public wxDialog {
public:
	explicit SQLiteMaterialsInspectorDialog(wxWindow* parent);

private:
	void ReloadData();
	void RefreshSummary();
	void RefreshBrushList();
	void RefreshBrushDetails();
	void RefreshTilesetList();
	void RefreshTilesetDetails();

	void OnReload(wxCommandEvent &event);
	void OnBrushTypeChanged(wxCommandEvent &event);
	void OnBrushSelected(wxCommandEvent &event);
	void OnTilesetSelected(wxCommandEvent &event);

	wxNotebook* notebook_ = nullptr;
	wxTextCtrl* summaryText_ = nullptr;
	wxChoice* brushTypeChoice_ = nullptr;
	wxListBox* brushList_ = nullptr;
	wxTextCtrl* brushDetailsText_ = nullptr;
	wxListBox* tilesetList_ = nullptr;
	wxTextCtrl* tilesetDetailsText_ = nullptr;

	BrushDatabase inspectorDatabase_;
	MaterialsDatabaseAuditReport auditReport_;
	std::vector<BrushRecord> currentBrushes_;
	std::vector<TilesetStorageRecord> tilesets_;
};

#endif
