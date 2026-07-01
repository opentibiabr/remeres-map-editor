# All Town Corpus Exporter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Export every inferable city in the open OTBM as a semantic JSON corpus segmented into evidence-backed districts.

**Architecture:** Reuse `city_corpus.cpp` item/tile serialization and replace the single Thais preset with automatic `Town` discovery. Houses are clustered by spatial distance and anchored to their town temple; towns without houses use a low-confidence temple-radius fallback. The output is streamed district by district into one JSON file to control memory usage.

**Tech Stack:** C++20, wxWidgets dialogs, `nlohmann::json`, existing `Map`/`Town`/`House`/`Tile` APIs.

---

### Task 1: Update Export Action Contract

**Files:**
- Modify: `source/city_corpus.h`
- Modify: `source/main_menubar.h`
- Modify: `source/main_menubar.cpp`
- Modify: `data/menubar.xml`

- [ ] Replace `ExportThaisPreset` and `EXPORT_THAIS_SAMPLE` with
  `ExportAllTowns` and `EXPORT_ALL_TOWN_CORPUS`.
- [ ] Keep `ExportSelection` for manual curation.
- [ ] Verify removed identifiers with
  `rg -n "ExportThaisPreset|EXPORT_THAIS_SAMPLE|ThaisBounds" source data`.

### Task 2: Infer Districts From Town Evidence

**Files:**
- Modify: `source/city_corpus.cpp`

- [ ] Add `HouseExtent` and `District` structures holding ids, bounds and
  temple distance.
- [ ] Collect houses by `townid`, including all house tile positions and each
  house exit in its extent.
- [ ] Compute connected components where the Chebyshev separation between
  house extents is at most `24`.
- [ ] Mark the component closest to `templePosition` as `main`, remaining
  components as `satellite`, then expand each by `16` tiles.
- [ ] For towns without houses, generate one `main` district at a `96` tile
  radius around the temple and mark it `temple_fallback` / `low`.

### Task 3: Stream Complete Corpus JSON

**Files:**
- Modify: `source/city_corpus.cpp`

- [ ] Generalize tile serialization to produce one district JSON object with
  house/town data and tile/item summaries.
- [ ] Write top-level corpus fields and each `cities[]` / `districts[]` entry
  incrementally to the selected output stream.
- [ ] Report town count, district count and exported tile count at completion.

### Task 4: Verify On Both Reference Maps

**Files:**
- Verify: changed source and menu files

- [ ] Run `git diff --check` and symbol searches.
- [ ] Build through `VsDevCmd.bat` and `cmake --build build/windows-release --config Release --parallel`.
- [ ] Launch the compiled exporter workflow on `otservbr.otbm` and
  `canary.otbm`, then inspect corpus headers and city/district counts.
