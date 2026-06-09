# Change Connected Ground Style Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a contextual editor command that previews and replaces an entire connected urban `GroundBrush` component on the clicked floor while preserving surrounding grounds and recomputing affected borders.

**Architecture:** Introduce a focused `ChangeConnectedGroundStyleService` that owns urban-brush filtering, unbounded connected-component discovery, composition warnings, deterministic overlay simulation and action application. Introduce a matching dialog that reuses `ChangeBuildStylePreview` for contextual rendering and connects to `MapCanvas`, `ActionIdentifier` and the existing CMake target.

**Tech Stack:** C++23, wxWidgets, existing RME `GroundBrush`/`Tile`/`BaseMap`/`Action` APIs, CMake/MSVC build.

---

## File Layout

- Create `source/change_connected_ground_style.h`: service and catalog-facing result types.
- Create `source/change_connected_ground_style.cpp`: component discovery, catalog classification, simulation and apply logic.
- Create `source/change_connected_ground_style_window.h`: list and dialog declarations.
- Create `source/change_connected_ground_style_window.cpp`: destination list, warning UI and contextual preview.
- Modify `source/map_display.h` and `source/map_display.cpp`: popup command handler and visibility.
- Modify `source/gui_ids.h`: menu command identifier.
- Modify `source/action.h`, `source/action.cpp` and `source/actions_history_window.cpp`: undo-history action type and icon mapping.
- Modify `source/CMakeLists.txt`: compile the new source files.

There is no existing C++ unit-test target in this repository. The behavioral verification for this UI-integrated service therefore consists of a complete compile plus specified map-level smoke checks; the code is structured as a service so a future C++ test target can exercise it without depending on the dialog.

### Task 1: Add Service Boundary And Urban Catalog

**Files:**
- Create: `source/change_connected_ground_style.h`
- Create: `source/change_connected_ground_style.cpp`

- [ ] **Step 1: Define the service API**

Create types exposing the logical source brush, connected positions, adjacent urban brushes, preview and apply:

```cpp
struct ChangeConnectedGroundAdjacency {
	GroundBrush* brush = nullptr;
	size_t tileCount = 0;
};

class UrbanGroundStyleCatalog {
public:
	static bool isUrbanGround(const GroundBrush* brush);
	static wxString familyLabel(const GroundBrush* brush);
	static std::vector<GroundBrush*> styles(const GroundBrush* source);
};

class ChangeConnectedGroundStyleService {
public:
	ChangeConnectedGroundStyleService(Editor &editor, const Position &origin);
	bool isValid() const noexcept;
	GroundBrush* getSourceBrush() const noexcept;
	const Position &getOrigin() const noexcept;
	const PositionVector &getPositions() const noexcept;
	const std::vector<ChangeConnectedGroundAdjacency> &getAdjacentUrbanBrushes() const noexcept;
	bool buildPreview(GroundBrush* target, BaseMap &previewTiles, wxString &reason) const;
	bool applyPreview(const BaseMap &previewTiles, wxString &reason);
private:
	PositionVector findComponent() const;
	void detectAdjacentUrbanBrushes();
	bool simulate(GroundBrush* target, BaseMap &working, std::set<Position> &changed, wxString &reason) const;
	Editor &editor;
	Position origin;
	GroundBrush* sourceBrush = nullptr;
	PositionVector positions;
	std::vector<ChangeConnectedGroundAdjacency> adjacentUrbanBrushes;
};
```

- [ ] **Step 2: Implement a curated urban catalog**

Use a static set of approved brush names, including:

```cpp
const std::set<std::string> urbanGroundNames = {
	"cobblestone", "dark cobblestone", "ugly cobblestone",
	"grassy cobblestone",
	"yellow pavement", "dark pavement", "venore cobblestone",
	"venore plaster", "terracotta", "roshamuul pavement",
	"oramond pavement", "oramond other pavement",
	"new grass pavement", "new ornamented pavement", "sandstone",
};
```

Collect targets from `g_brushes.getMap()`, require `isGround()`,
`visibleInPalette()`, catalog membership and `target != source`, then sort by
name.

- [ ] **Step 3: Implement component discovery and composition detection**

Use an unbounded orthogonal queue:

```cpp
while (!pending.empty()) {
	const Position position = pending.front();
	pending.pop();
	if (!position.isValid() || !visited.insert(position).second) {
		continue;
	}
	Tile* tile = editor.getMap().getTile(position);
	if (!tile || tile->getGroundBrush() != sourceBrush) {
		continue;
	}
	result.push_back(position);
	for (const Position &direction : cardinalDirections) {
		pending.push(position + direction);
	}
}
```

Using `Tile::getGroundBrush()` deliberately includes inline
`ground_equivalent`/super-border tiles because the XML loader assigns their
item types to the owning `GroundBrush`.

Inspect eight neighbours of all selected positions. Count an adjacent brush
once per neighbouring position when it is urban, not the source brush, and not
already part of the component.

- [ ] **Step 4: Implement deterministic simulation and action application**

Collect component positions as `changedPositions`. Collect their
eight-neighbour ring as read-only `contextPositions`, and deep-copy existing
context tiles into the working `BaseMap`; only component positions are later
committed. For component tiles:

```cpp
target->draw(&working, tile, nullptr);
```

This replaces only `tile->ground`, retaining its items. Borderize every
component tile after all ground replacements:

```cpp
for (const Position &position : component) {
	if (Tile* tile = working.getTile(position)) {
		tile->borderize(&working);
	}
}
```

Do not borderize or commit neighbouring tiles: destination outer borders can
otherwise be generated over bridges, fountains, house interiors and wall
transitions. Build preview using the completed working overlay.
`applyPreview()` transfers the already rendered component tile output from
the current overlay to `ACTION_CHANGE_CONNECTED_GROUND_STYLE`, avoiding a
preview/application mismatch from rerandomizing the destination brush. The
read-only context ring is never added to the action.

### Task 2: Add Dialog And Preview Workflow

**Files:**
- Create: `source/change_connected_ground_style_window.h`
- Create: `source/change_connected_ground_style_window.cpp`
- Reuse: `source/change_build_style_window.h`

- [ ] **Step 1: Define the destination list and dialog**

Use a `wxVListBox` holding `GroundBrush*` and a dialog containing:

```cpp
ChangeConnectedGroundStyleService service;
std::vector<GroundBrush*> allStyles;
wxTextCtrl* search;
wxChoice* family;
ChangeConnectedGroundStyleListBox* styleList;
wxStaticText* sourceLabel;
wxStaticText* componentLabel;
wxStaticText* compositionWarning;
ChangeBuildStylePreview* preview;
wxStaticText* status;
wxButton* applyButton;
```

- [ ] **Step 2: Build the UI**

Use title `Change Connected Ground Style`. Display source brush, component
tile count, search field and candidate list. Reuse
`ChangeBuildStylePreview(this, editor, origin)`, call `fitBuilding` with the
component positions and then `centerOn(origin)` so the clicked tile is the
initial center.

- [ ] **Step 3: Display composition warnings**

When the service reports adjacent urban brushes, show text such as:

```cpp
"Composite urban path detected. Only the selected venore plaster component "
"will change; neighbouring urban grounds remain unchanged: venore cobblestone, terracotta."
```

The warning never disables preview or `Apply`.

- [ ] **Step 4: Refresh and apply**

Filter target styles by search/family, build preview on selection changes, and
enable apply only when simulation succeeds. On apply, pass the current preview
overlay to `applyPreview()` and close the modal when an action is added.

### Task 3: Integrate Command And Undo History

**Files:**
- Modify: `source/gui_ids.h`
- Modify: `source/map_display.h`
- Modify: `source/map_display.cpp`
- Modify: `source/action.h`
- Modify: `source/action.cpp`
- Modify: `source/actions_history_window.cpp`

- [ ] **Step 1: Introduce identifiers**

Add:

```cpp
MAP_POPUP_MENU_CHANGE_CONNECTED_GROUND_STYLE,
ACTION_CHANGE_CONNECTED_GROUND_STYLE,
```

and map the new action label to `Change Connected Ground Style`.

- [ ] **Step 2: Register and implement the handler**

Include the dialog header, add an event table binding, declare
`OnChangeConnectedGroundStyle`, and implement:

```cpp
void MapCanvas::OnChangeConnectedGroundStyle(wxCommandEvent &) {
	if (editor.getSelection().size() != 1) {
		return;
	}
	Tile* tile = editor.getSelection().getSelectedTile();
	if (!tile || !UrbanGroundStyleCatalog::isUrbanGround(tile->getGroundBrush())) {
		return;
	}
	ChangeConnectedGroundStyleDialog dialog(this, editor, tile->getPosition());
	if (dialog.isValid() && dialog.ShowModal() == wxID_OK) {
		Refresh();
	}
}
```

- [ ] **Step 3: Add contextual menu visibility**

In both single-tile popup branches, after `Select Groundbrush`, append:

```cpp
if (tile->hasGround() && UrbanGroundStyleCatalog::isUrbanGround(tile->getGroundBrush())) {
	Append(MAP_POPUP_MENU_CHANGE_CONNECTED_GROUND_STYLE,
		"Change connected ground style...",
		"Replace the connected urban ground component on this floor");
}
```

- [ ] **Step 4: Add history icon fallback**

Map `ACTION_CHANGE_CONNECTED_GROUND_STYLE` to the existing `change_bitmap` so
the history list displays it consistently with other property/style changes.

### Task 4: Compile And Validate Map Scenarios

**Files:**
- Modify: `source/CMakeLists.txt`

- [ ] **Step 1: Register new compilation units**

Add `change_connected_ground_style.cpp` and
`change_connected_ground_style_window.cpp` adjacent to the existing
`change_build_style` files in `target_sources`.

- [ ] **Step 2: Build the application**

Run:

```powershell
cmake --build build --config Release
```

Expected: target `canary-map-editor` or `canary-map-editor-x64` builds without
compiler or linker errors.

- [ ] **Step 3: Inspect the exact diff and working tree**

Run:

```powershell
git diff --check
git status --short --branch
```

Expected: no whitespace errors; only the new feature files/integration edits
plus the pre-existing uncommitted `Change Build Style` work remain.

- [ ] **Step 4: Perform editor smoke checks**

In an open sample/global map:

1. Thais: click a `cobblestone` tile (`870`), open the command, select another
   pavement and confirm the entire connected component previews/applies while
   nearby grounds and objects remain.
2. Ankrahmun: click the `sandstone` route; confirm the component count and
   preview include the dark contour forms (`924` to `935`) rather than leaving
   them behind.
3. Venore: click `venore plaster`; confirm the dialog warns about adjacent
   urban brushes and changes only the clicked component, retaining surrounding
   `venore cobblestone`/`terracotta`.
4. Undo once; confirm each applied conversion is reverted as one history
   operation.
