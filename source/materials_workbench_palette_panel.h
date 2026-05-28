#ifndef RME_MATERIALS_WORKBENCH_PALETTE_PANEL_H_
#define RME_MATERIALS_WORKBENCH_PALETTE_PANEL_H_

#include <functional>
#include <utility>

#include <wx/panel.h>
#include <wx/string.h>

#include "brush_database.h"

class MaterialsWorkbenchBrushGridPanel;
class MaterialsWorkbenchController;
class wxButton;
class wxChoice;
class wxStaticText;

struct MaterialsWorkbenchAvailableBrushSource {
	wxString familyKey;
	wxString paletteLabel;
	std::vector<BrushRecord> brushes;
};

class MaterialsWorkbenchPalettePanel : public wxPanel {
public:
	MaterialsWorkbenchPalettePanel(wxWindow* parent, MaterialsWorkbenchController &controller);

	void ClearWorkspace(const wxString &message);
	bool LoadPalette(const TilesetStorageRecord &tileset);
	void SetOnPaletteSaved(std::function<void(const wxString &)> callback);

private:
	void BuildLayout();
	void RefreshWorkspace();
	void RefreshPaletteGroupChoice();
	void RefreshSectionChoice();
	void RefreshSectionEntries();
	void RebuildAvailableBrushSources();
	void RefreshAvailableBrushFamilies();
	void RefreshAvailableBrushPalettes();
	void RefreshAvailableBrushes();
	void RefreshMoveDestinationFamilies();
	void RefreshMoveDestinationPalettes();
	void RefreshSelectionFeedback();
	bool IsSelectedMovableEntry() const;
	bool ResolveSelectedMovableEntry(int &sectionIndex, int &entryIndex, TilesetEntryRecord &entry) const;
	bool ResolveMoveDestinationPalette(TilesetStorageRecord &outTileset, wxString &outDisplayLabel) const;
	int FindSectionIndexByName(const TilesetStorageRecord &tileset, const wxString &sectionName) const;
	void NormalizePaletteOrdering(TilesetStorageRecord &tileset) const;
	int GetSelectedVisibleEntryIndex() const;
	bool ResolveVisibleEntryLocation(int visibleIndex, int &sectionIndex, int &entryIndex) const;
	bool MoveSelectedEntryByOffset(int offset, const wxString &directionLabel);
	void UpdateButtonState();
	void SetStatusMessage(const wxString &message);
	bool CommitPalette(const wxString &successMessage, const wxString &previousPaletteName = wxString(), const wxString &selectionPaletteName = wxString());
	wxString RecommendBrushGroupForCurrentSection() const;
	const BrushRecord* FindAvailableBrushRecord() const;
	int FindPaletteGroupChoiceIndexByName(const wxString &groupName) const;
	const PaletteGroupRecord* GetSelectedPaletteGroup() const;
	bool PromptForPaletteName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName);
	bool PromptForPaletteGroupName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentName, wxString &outName);
	bool PromptForNewSectionType(const wxString &title, const wxString &caption, wxString &outSectionType);
	bool PromptForSectionName(const wxString &title, const wxString &caption, const wxString &initialValue, const wxString &currentSectionType, wxString &outSectionType);

	void OnCreatePalette(wxCommandEvent &event);
	void OnRenamePalette(wxCommandEvent &event);
	void OnDeletePalette(wxCommandEvent &event);
	void OnPaletteGroupChanged(wxCommandEvent &event);
	void OnCreatePaletteGroup(wxCommandEvent &event);
	void OnRenamePaletteGroup(wxCommandEvent &event);
	void OnDeletePaletteGroup(wxCommandEvent &event);
	void OnSectionChanged(wxCommandEvent &event);
	void OnAddSection(wxCommandEvent &event);
	void OnRenameSection(wxCommandEvent &event);
	void OnDeleteSection(wxCommandEvent &event);
	void OnAvailableBrushFamilyChanged(wxCommandEvent &event);
	void OnAvailableBrushPaletteChanged(wxCommandEvent &event);
	void OnMoveDestinationFamilyChanged(wxCommandEvent &event);
	void OnMoveDestinationPaletteChanged(wxCommandEvent &event);
	void OnAddBrush(wxCommandEvent &event);
	void OnMoveBrushToPalette(wxCommandEvent &event);
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
	std::vector<MaterialsWorkbenchAvailableBrushSource> availableBrushSources_;
	std::vector<BrushRecord> currentAvailableBrushes_;
	std::vector<wxString> availableBrushFamilyKeys_;
	std::vector<int> availableBrushPaletteSourceIndexes_;
	std::vector<wxString> moveDestinationFamilyKeys_;
	std::vector<int> moveDestinationPaletteIndexes_;
	std::vector<wxString> paletteGroupKeys_;
	std::vector<std::pair<int, int>> visibleEntryLocations_;
	bool preserveSectionGridViewStart_ = false;

	wxStaticText* titleLabel_ = nullptr;
	wxStaticText* sourceLabel_ = nullptr;
	wxChoice* paletteGroupChoice_ = nullptr;
	wxChoice* currentSectionChoice_ = nullptr;
	wxStaticText* sectionSummaryLabel_ = nullptr;
	wxStaticText* selectionSummaryLabel_ = nullptr;
	MaterialsWorkbenchBrushGridPanel* sectionBrushGrid_ = nullptr;
	wxChoice* availableBrushFamilyChoice_ = nullptr;
	wxChoice* availableBrushPaletteChoice_ = nullptr;
	wxChoice* moveDestinationFamilyChoice_ = nullptr;
	wxChoice* moveDestinationPaletteChoice_ = nullptr;
	wxStaticText* availableBrushSummaryLabel_ = nullptr;
	MaterialsWorkbenchBrushGridPanel* availableBrushGrid_ = nullptr;
	wxButton* addBrushButton_ = nullptr;
	wxButton* moveToPaletteButton_ = nullptr;
	wxButton* removeBrushButton_ = nullptr;
	wxButton* moveUpButton_ = nullptr;
	wxButton* moveDownButton_ = nullptr;
	wxButton* createPaletteButton_ = nullptr;
	wxButton* renamePaletteButton_ = nullptr;
	wxButton* deletePaletteButton_ = nullptr;
	wxButton* createPaletteGroupButton_ = nullptr;
	wxButton* renamePaletteGroupButton_ = nullptr;
	wxButton* deletePaletteGroupButton_ = nullptr;
	wxButton* addSectionButton_ = nullptr;
	wxButton* renameSectionButton_ = nullptr;
	wxButton* deleteSectionButton_ = nullptr;
	wxStaticText* statusLabel_ = nullptr;
};

#endif
