#ifndef RME_MATERIALS_WORKBENCH_BORDER_PANEL_H_
#define RME_MATERIALS_WORKBENCH_BORDER_PANEL_H_

#include <functional>
#include <map>
#include <memory>

#include <wx/panel.h>
#include <wx/string.h>

#include "brush_database.h"

class ItemButton;
class ItemToggleButton;
class MaterialsWorkbenchController;
class wxButton;
class wxChoice;
class wxGrid;
class wxPanel;
class wxSpinCtrl;
class wxStaticText;
class wxTextCtrl;
class wxGridEvent;

class MaterialsWorkbenchBorderPanel : public wxPanel {
public:
	MaterialsWorkbenchBorderPanel(wxWindow* parent, MaterialsWorkbenchController &controller);
	~MaterialsWorkbenchBorderPanel() override;

	void ClearWorkspace(const wxString &message);
	bool LoadBorderSet(const wxString &contextKey, int itemIndex);
	void SetOnBorderSetSaved(std::function<void(int64_t)> callback);
	void SetOnBorderSetDeleted(std::function<void(int64_t, const wxString &)> callback);
	void SetOnBorderSetStateChanged(std::function<void()> callback);
	void SetOnOpenLinkedBrush(std::function<void(int64_t)> callback);
	bool HasPendingChanges() const;
	bool IsCurrentBorderSelection(const wxString &contextKey, int itemIndex) const;
	wxString GetCurrentBorderSetDisplayName() const;
	bool ResolvePendingChangesBeforeSwitch(wxWindow* parent, const wxString &targetLabel);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

#endif
