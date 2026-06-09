# City Corpus Exporter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the incoherent experimental city generation entry point with a read-only JSON exporter for semantic urban learning samples.

**Architecture:** Add a focused `city_corpus` module that classifies loaded map tiles/items using editor-native metadata and writes a deterministic JSON sample. Wire two export actions into the existing File/Export menu: current selection and the known Thais preset. Leave aggregation and synthesis for the subsequent increment after exported samples can be inspected.

**Tech Stack:** C++20, wxWidgets dialogs, `nlohmann::json`, existing `Map`/`Selection`/`ItemType`/`House`/`Town` APIs.

---

### Task 1: Remove The Unsafe Entry Point

**Files:**
- Modify: `data/menubar.xml`
- Modify: `source/CMakeLists.txt`
- Modify: `source/main_menubar.h`
- Modify: `source/main_menubar.cpp`
- Modify: `source/action.h`
- Modify: `source/action.cpp`
- Modify: `source/actions_history_window.cpp`
- Delete: `source/city_generation.h`
- Delete: `source/city_generation.cpp`

- [ ] **Step 1: Remove `GENERATE_CITY` from the menu and action dispatch**

Delete the `Generate City` menu item, its menu action ID and `OnGenerateCity`
handler so `Ctrl+K` cannot invoke a known-invalid generator.

- [ ] **Step 2: Remove the obsolete undo action and translation unit**

Delete `ACTION_GENERATE_CITY` cases and replace `city_generation.cpp` in the
CMake source list with the new `city_corpus.cpp` unit introduced in Task 2.

- [ ] **Step 3: Verify the removed symbols are gone**

Run:

```powershell
rg -n "GenerateThaisCity|GENERATE_CITY|ACTION_GENERATE_CITY|city_generation" source data
```

Expected: no output.

### Task 2: Add Semantic JSON Export Core

**Files:**
- Create: `source/city_corpus.h`
- Create: `source/city_corpus.cpp`

- [ ] **Step 1: Define the public export API**

Create functions:

```cpp
namespace CityCorpus {
bool ExportSelection(Editor &editor, wxWindow* parent);
bool ExportThaisPreset(Editor &editor, wxWindow* parent);
}
```

Both functions are read-only with respect to `Map`.

- [ ] **Step 2: Implement deterministic item classification**

For each ground or stacked item emit stable JSON containing:

```json
{
  "id": 4515,
  "name": "grass",
  "brush": "grass",
  "tags": ["ground", "grass"]
}
```

Tags must use native properties first (`isGroundTile`, `isWall`, `isDoor`,
`isDepot`, `isTeleport`, `isFloorChange`, border flags) and conservative name
or brush checks only for semantic subtypes such as `grass`, `road`, `water`,
`roof`, `window`, `fence`, `railing` and `vertical_connector`.

- [ ] **Step 3: Export tiles and summary**

Traverse bounds in `z`, `y`, `x` order and emit only non-empty tiles. Record
absolute coordinates, map flags, house ID, ground, stacked items and tile
tags. Record per-floor tile counts, per-tag counts and per-item counts in the
summary.

- [ ] **Step 4: Export verified functional metadata**

For houses with at least one tile in bounds, emit ID, name, town ID, exit,
full size and tile count inside the sample. Emit towns referenced by those
houses or with temple positions inside bounds.

- [ ] **Step 5: Implement save dialogs**

`ExportSelection` uses the selected `x/y` projection across all map floors and
requests a sample name plus `.json` destination. `ExportThaisPreset` checks
the fixed bounds `{32226..32461, 32134..32314}` contain tiles before exporting
the preset named `Thais`.

### Task 3: Wire Safe Export Actions

**Files:**
- Modify: `data/menubar.xml`
- Modify: `source/CMakeLists.txt`
- Modify: `source/main_menubar.h`
- Modify: `source/main_menubar.cpp`

- [ ] **Step 1: Add export menu actions**

Add actions named `EXPORT_CITY_SAMPLE` and `EXPORT_THAIS_SAMPLE` below
`Export Tilesets...`, with labels:

```xml
<item name="Export $City Learning Sample..." action="EXPORT_CITY_SAMPLE" help="Export the selected area as a semantic city learning JSON sample."/>
<item name="Export $Thais Learning Preset..." action="EXPORT_THAIS_SAMPLE" help="Export the known Thais area as a semantic city learning JSON sample."/>
```

- [ ] **Step 2: Invoke only read-only export functions**

Register handlers calling `CityCorpus::ExportSelection` and
`CityCorpus::ExportThaisPreset`; enable selection export only when an editor
and selection exist and preset export when an editor is open.

### Task 4: Static Verification And Runtime Follow-up

**Files:**
- Verify: all files changed in Tasks 1-3

- [ ] **Step 1: Check patch integrity**

Run:

```powershell
git diff --check
rg -n "GENERATE_CITY|ACTION_GENERATE_CITY|GenerateThaisCity|city_generation" source data
rg -n "EXPORT_CITY_SAMPLE|EXPORT_THAIS_SAMPLE|CityCorpus::ExportSelection|CityCorpus::ExportThaisPreset" source data
```

Expected: `git diff --check` reports no errors; removed generator symbols are
absent; new export symbols are present.

- [ ] **Step 2: Report compile limitation**

The repository policy in `AGENTS.md` prohibits running a compilation command
without explicit user authorization. Report that source-level verification was
performed and request compile authorization before claiming build success.

- [ ] **Step 3: Define runtime validation**

After a compiled editor is available, open `otservbr.otbm`, run the Thais
preset export, inspect `format`, `version`, multi-floor counts and house list,
then export a manually selected region from `canary.otbm` for the next learner
increment.
