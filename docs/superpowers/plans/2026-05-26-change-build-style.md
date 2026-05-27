# Change Build Style Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement connected, multi-floor `WallBrush` style conversion with a visual selection dialog and preview.

**Architecture:** Add a focused `ChangeBuildStyleService` for component discovery, destination compatibility and batched application. Add a wxWidgets dialog that enumerates loaded structural wall brushes and renders a sprite preview from the service result. Wire the dialog through the existing map popup menu.

**Tech Stack:** C++17, wxWidgets, existing Canary Map Editor `WallBrush`, `Map`, `BatchAction`, and sprite rendering APIs.

---

### Task 1: Wall brush conversion API and service

**Files:**
- Create: `source/change_build_style.h`
- Create: `source/change_build_style.cpp`
- Modify: `source/wall_brush.h`
- Modify: `source/wall_brush.cpp`
- Modify: `source/action.h`
- Modify: `source/action.cpp`
- Modify: `source/CMakeLists.txt`

- [x] Add safe `WallBrush` queries for structural/opening item selection by alignment, type and open state.
- [x] Implement same-brush orthogonal component discovery and vertical overlap detection.
- [x] Implement compatibility reporting and two-phase batched conversion with `ACTION_CHANGE_BUILD_STYLE`.

### Task 2: Style selection and preview dialog

**Files:**
- Create: `source/change_build_style_window.h`
- Create: `source/change_build_style_window.cpp`
- Modify: `source/CMakeLists.txt`

- [x] Enumerate loaded non-decoration `WallBrush` destinations with text/category filtering.
- [x] Reject incompatible destinations and present the missing opening reason.
- [x] Render selected-floor preview using target wall/opening sprites and provide floor arrows, floor inclusion, and `Only current floor`.
- [x] Apply the chosen target through the service and refresh the active map.

### Task 3: Context menu integration and validation

**Files:**
- Modify: `source/gui_ids.h`
- Modify: `source/map_display.h`
- Modify: `source/map_display.cpp`

- [x] Add `Change build style...` only for structural wall selections.
- [x] Open the dialog from the current selected wall component.
- [ ] Validate with `git diff --check` and focused static inspection; run a build and visual map test only when compilation is approved for this repository.
