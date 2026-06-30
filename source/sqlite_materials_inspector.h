#ifndef RME_SQLITE_MATERIALS_INSPECTOR_H_
#define RME_SQLITE_MATERIALS_INSPECTOR_H_

#include "brush_database.h"

#include <wx/panel.h>
#include <wx/dialog.h>

class wxChoice;
class wxCommandEvent;
class wxListBox;
class wxNotebook;
class wxTextCtrl;
class wxWindow;

class SQLiteMaterialsInspectorPanel : public wxPanel {
public:
	explicit SQLiteMaterialsInspectorPanel(wxWindow* parent);
	void ReloadData();

private:
	void OnResetDatabaseFromXml();

	void RefreshSummary();
	void RefreshBrushList();
	void RefreshBrushDetails();
	void RefreshTilesetList();
	void RefreshTilesetDetails();

	wxNotebook* notebook_ = nullptr;
	wxTextCtrl* summaryText_ = nullptr;
	wxChoice* brushTypeChoice_ = nullptr;
	wxListBox* brushList_ = nullptr;
	wxNotebook* brushDetailsNotebook_ = nullptr;
	wxTextCtrl* brushDetailsText_ = nullptr;
	wxTextCtrl* brushRuntimeXmlText_ = nullptr;
	wxListBox* tilesetList_ = nullptr;
	wxTextCtrl* tilesetDetailsText_ = nullptr;

	BrushDatabase inspectorDatabase_;
	MaterialsDatabaseAuditReport auditReport_;
	std::vector<BrushRecord> currentBrushes_;
	std::vector<TilesetStorageRecord> tilesets_;
};

class SQLiteMaterialsInspectorDialog : public wxDialog {
public:
	explicit SQLiteMaterialsInspectorDialog(wxWindow* parent);

private:
	SQLiteMaterialsInspectorPanel* panel_ = nullptr;
};

#endif
