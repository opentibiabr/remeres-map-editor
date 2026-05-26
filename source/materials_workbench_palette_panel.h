#ifndef RME_MATERIALS_WORKBENCH_PALETTE_PANEL_H_
#define RME_MATERIALS_WORKBENCH_PALETTE_PANEL_H_

#include <functional>

#include <wx/panel.h>
#include <wx/string.h>

#include "brush_database.h"

class MaterialsWorkbenchBrushGridPanel;
class MaterialsWorkbenchController;
class wxButton;
class wxChoice;
class wxStaticText;

class MaterialsWorkbenchPalettePanel : public wxPanel {
public:
	MaterialsWorkbenchPalettePanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadPalette(const TilesetStorageRecord &tileset);
	void SetOnPaletteSaved(std::function<void(const wxString &)> callback);

private:
	void BuildLayout();
	void RefreshWorkspace();
	void RefreshSectionChoice();
	void RefreshSectionEntries();
	void RefreshAvailableBrushGroups();
	void RefreshAvailableBrushes();
	void UpdateButtonState();
	void SetStatusMessage(const wxString &message);
	void NormalizePaletteOrdering();
	bool CommitPalette(const wxString &successMessage, const wxString &previousPaletteName = wxString(), const wxString &selectionPaletteName = wxString());
	wxString RecommendBrushGroupForCurrentSection() const;
	const BrushRecord* FindAvailableBrushRecord() const;
	int FindSectionIndexByName(const wxString &sectionName) const;
	bool PromptForPaletteName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName);
	bool PromptForNewSectionType(const wxString &title, const wxString &caption, wxString &outSectionType);
	bool PromptForSectionName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentSectionType, wxString &outSectionType);

	void OnCreatePalette(wxCommandEvent &event);
	void OnRenamePalette(wxCommandEvent &event);
	void OnDeletePalette(wxCommandEvent &event);
	void OnSectionChanged(wxCommandEvent &event);
	void OnAddSection(wxCommandEvent &event);
	void OnRenameSection(wxCommandEvent &event);
	void OnDeleteSection(wxCommandEvent &event);
	void OnAvailableBrushGroupChanged(wxCommandEvent &event);
	void OnAddBrush(wxCommandEvent &event);
	void OnRemoveBrush(wxCommandEvent &event);
	void OnMoveBrushUp(wxCommandEvent &event);
	void OnMoveBrushDown(wxCommandEvent &event);

	MaterialsWorkbenchController &controller_;
	std::function<void(const wxString &)> onPaletteSaved_;
	TilesetStorageRecord palette_;
	bool hasPalette_ = false;
	int currentSectionIndex_ = 0;
	int selectedSectionEntryIndex_ = -1;
	int selectedAvailableBrushListIndex_ = -1;
	std::vector<BrushRecord> currentAvailableBrushes_;
	std::vector<wxString> availableBrushGroupKeys_;

	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* sourceLabel_ = nullptr;
	wxChoice* currentSectionChoice_ = nullptr;
	wxStaticText* sectionSummaryLabel_ = nullptr;
	MaterialsWorkbenchBrushGridPanel* sectionBrushGrid_ = nullptr;
	wxChoice* availableBrushGroupChoice_ = nullptr;
	MaterialsWorkbenchBrushGridPanel* availableBrushGrid_ = nullptr;
	wxButton* addBrushButton_ = nullptr;
	wxButton* removeBrushButton_ = nullptr;
	wxButton* moveUpButton_ = nullptr;
	wxButton* moveDownButton_ = nullptr;
	wxButton* createPaletteButton_ = nullptr;
	wxButton* renamePaletteButton_ = nullptr;
	wxButton* deletePaletteButton_ = nullptr;
	wxButton* addSectionButton_ = nullptr;
	wxButton* renameSectionButton_ = nullptr;
	wxButton* deleteSectionButton_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
