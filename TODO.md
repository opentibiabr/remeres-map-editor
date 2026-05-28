# TODO - Materials Workbench Finalization

## Completed Foundation
- [x] Materials Workbench is the main editing module
- [x] Catalog reads from `materials.db`
- [x] Visual `palette` editing exists
- [x] Visual `brush` metadata editing exists
- [x] Initial visual Border Workspace exists
- [x] Initial visual Wall Workspace exists
- [x] SQLite persistence is the base for future evolution

## Completed Recently
- [x] Initial `variations` editing inside the Brush Workspace
- [x] `ground` variations editing
- [x] `carpet` variations editing
- [x] `table` variations editing
- [x] `doodad` variations editing
- [x] Brush save persists metadata and variations together
- [x] Brush save preserves rename references safely in SQLite
- [x] Brush save no longer crashes when renaming
- [x] Brush rename updates palette labels safely without full runtime reload
- [x] Palette save no longer crashes by reloading the full brush runtime
- [x] Initial Brush Workspace `dirty state`
- [x] Initial brush selection-change guard with pending edits
- [x] Initial Brush Workspace `Save`, `Revert`, and `Reload from DB` behavior
- [x] Initial rename of legacy `Source` semantics to `Imported From` in brush metadata
- [x] Brush metadata `lookId` and `serverLookId` now follow the real item-catalog limit
- [x] Brush save now blocks unknown `lookId` and `serverLookId` values
- [x] Variation list refreshes now preserve the visible list position more consistently
- [x] Reloading the same brush now preserves more of the current variation editor context
- [x] Variation parent/child selection transitions now preserve child selection more often instead of resetting it blindly
- [x] Brush load/save/revert and catalog reload now emit more specific diagnostic logs
- [x] Brush metadata and inspector now separate `Storage: materials.db` from legacy `Imported from`
- [x] Saving the same brush now preserves more of the current variation editor context
- [x] Navigation tree refreshes now preserve expanded groups and current selection more often
- [x] Brush reload now preserves variation context by brush id even when refresh changes the item index
- [x] Navigation tree refreshes now preserve the first visible region more often instead of jumping to the top
- [x] Wall Workspace now tracks local dirty state and only enables save/revert when needed
- [x] Wall Workspace now guards selection changes and window close against losing pending edits
- [x] Wall Workspace now preserves the selected wall part/item/door more often across save and reload
- [x] Wall Workspace now validates door type compatibility against the selected item id and runtime wall-door metadata
- [x] Wall Workspace now preserves item and door grid scroll more often when refreshing dynamically recreated selections
- [x] Saving a border set now refreshes runtime palettes from `materials.db` without forcing the unsafe global brush reload
- [x] Border set save now updates the existing record by SQLite id instead of duplicating inline entries or losing XML border id changes
- [x] Border slot preview now reacts immediately when editing the selected item id
- [x] Border set navigation labels now reflect XML border ids and type more clearly instead of only showing the SQLite id
- [x] Border Workspace layout now gives metadata and selected-slot editing enough space on narrower window widths
- [x] Saving a wall brush now refreshes the runtime wall brush state from `materials.db` without using the unsafe global reload path
- [x] Saving regular brushes now refreshes the runtime brush state so updated lookId and variation items are used immediately while painting
- [x] Brush save validation now blocks variation items that already belong to a different runtime brush, avoiding silent reload failures that left the brush drawing nothing
- [x] Brush variation editors now show the current runtime brush owner for selected `item id` values before save
- [x] Brush metadata now shows informational runtime-owner hints for `lookId` and `serverLookId` without blocking valid shared usage
- [x] Border Workspace now tracks local dirty state, enables save/revert only when needed, and warns before losing pending edits on selection change or window close
- [x] Border Workspace now shows `modified` badges in the navigation tree for dirty border set edits
- [x] Border Workspace no longer marks entries as `modified` from programmatic selection/loading refreshes; the badge now reflects real local edits only
- [x] Border Workspace now validates border slot `item id` values before save and blocks reusing the same runtime item across multiple border edges
- [x] Border Workspace now uses a canonical 5x5 slot geometry: outer corners in the true corners, diagonals in the inner corner cells, and cardinals centered on each side
- [x] Border Workspace now preserves the selected slot per border set across reloads and navigation switches instead of reusing only one global slot context
- [x] Border Workspace now blocks saving `global` or `inline` sets whose metadata would make the targeted runtime refresh path unsupported after save
- [x] Applying a border slot now validates the candidate layout immediately, so duplicate or unknown slot items are rejected before the user reaches Save
- [x] Palette Workspace brush grids now reuse tile widgets instead of destroying/recreating them on every refresh, reducing GTK CSS/layout churn during palette and section switches
- [x] Palette Workspace brush grids now also support an owner-drawn grid path that removes per-item `wxPanel`/`BrushButton`/`wxStaticText` widgets from the Workbench brush lists, cutting GTK CSS/update-event churn much more aggressively
- [x] Item-family sections in the Palette Workspace now default to a combined `Item Brushes` source and fall back to catalog `lookId` previews when a runtime `Brush*` is unavailable, keeping `wall`, `carpet`, and `table` brushes visible together inside the Workbench
- [x] Palette Workspace section grids now render both `brush` and `item` tileset entries, so `Item Palette` categories in the Workbench no longer go blank while the runtime still shows raw-item content
- [x] The owner-drawn Workbench preview tiles now use a lighter 32x32 outline instead of the old heavy black frame, keeping the faster grid while cleaning up the icon presentation
- [x] Runtime icon palettes now reuse a stable `BrushButton` pool across page switches instead of rebuilding the full icon grid every time
- [x] Runtime palette pagination now avoids unnecessary `Fit`/AUI relayout churn and only relayouts icon pages when the visible button count actually changes
- [x] Runtime palette brush selection now uses cached brush-to-page and brush-to-index lookups plus lighter current-page relayout, reducing extra scans and layout churn when switching categories or restoring the selected brush

## Remaining Before Calling It Ready
- [x] Extend `dirty state` beyond the Brush Workspace
- [x] Highlight modified fields visually in the Brush Workspace
- [x] Show `modified` badges in the navigation tree for dirty brush edits
- [x] Preserve selection and scroll more consistently across reloads
- [x] Improve validation before save across the remaining Workbench domains
- [ ] Add richer `border` and `wall` previews
- [ ] Support entity creation and removal, not only editing
- [ ] Improve semantic clarity for origin metadata
- [ ] Polish the UI visually and compact workspace layouts
- [ ] Implement a professional XML deprecation flow

## UI And UX Polish
- [ ] Reduce spacing and padding across all workspaces
- [x] Unify headers, status bars, and action buttons across the Workbench
- [x] Improve status text quality and usefulness
- [ ] Add rich tooltips with `id`, type, source, and relationships
- [ ] Improve preview grid contrast and alignment
- [ ] Standardize technical labels such as `lookId`, `serverLookId`, `zOrder`, and `partType`
- [x] Reorganize the left navigation tree into clearer professional categories

## Semantics And Provenance
- [x] Use `Imported From` in the UI where the field represents legacy origin
- [x] Keep `Imported From` as the clearer Stage 9 wording for legacy origin metadata
- [x] Defer any additional legacy-origin wording polish to later UI cleanup if needed
- [x] Show `Storage: materials.db` separately
- [x] Show `Imported from: ...` separately

## Robustness And Validation
- [x] Validate missing or unknown `item id` before save in the Brush Workspace
- [x] Validate `lookId` and `serverLookId` against the real item catalog in the Brush Workspace
- [x] Validate invalid duplicate usage in border slots where applicable
- [x] Validate `door type` compatibility with the selected `item id`
- [x] Block saving a brush with an invalid or unexpected `type` in the Brush Workspace
- [x] Harden selection transitions in dynamically recreated grids
- [x] Add more specific save and reload logs
- [x] Revisit runtime refresh for `walls` and `borders`
- [ ] Add focused tests for SQLite serialization of `wallParts`, `borderSetItems`, and `tilesets`

## New Features
- [ ] Add `variations` editing for additional domains where still missing
- [ ] Add a real composed preview for `wall brush`, not only a parts grid
- [ ] Add a mini-scene preview for `border` application, not only a slot matrix
- [ ] Allow duplicating `border set`, `wall part set`, or `palette`
- [ ] Allow creating new `brush`, `palette`, and `border set` directly from the Workbench
- [ ] Allow removing entities with safe confirmation and reference validation
- [ ] Add real-time search and filtering in the navigation tree
- [ ] Add a reference inspector showing where a brush is used
- [ ] Add local per-session edit history
- [ ] Allow targeted export/import for `brush`, `palette`, or `border set`
- [ ] Add a manual `sync runtime` command after save
- [ ] Add `DB vs XML` comparison during the hybrid phase

## Palette Editor Roadmap
- [x] Stage 10A: Rebuild `Palettes` navigation around runtime families (`Terrain Palette`, `Doodad Palette`, `Item Palette`) with clear entry points into a real palette editor
- [x] Stage 10B: Add a fast `Palette Editor` model/view for palette sections and entries, keeping editing inside the `Materials Workbench`
- [x] Stage 10C: Support structural editing of palettes and sections: create, rename, delete, move, and reorder
- [x] Stage 10C result: the `Palette Workspace` now centers the UX on `palette_groups`, palettes, and brush membership; runtime sections remain in the backend but are no longer exposed as a primary editing concept in the UI
- [ ] Stage 10D: Close brush membership editing inside palettes as a professional `Group -> Palette -> Brushes` flow: add, remove, reorder, move comfortably, and polish product-language UX without bringing `section` back to the foreground
- [x] Stage 10D progress: the brush library now follows `Family -> Palette` inside a dedicated `Source Library` panel, so source browsing stays grouped under `terrain`, `doodad`, `item`, and `other/raw` without flattening everything into one selector
- [x] Stage 10D progress: moving brushes between palettes is now an inline flow inside `Palette Brushes`, with persistent destination family/palette selectors and selection feedback instead of a modal destination picker
- [x] Stage 10D progress: palette brush actions now live inside the left composition panel and source filters live inside the right library panel, replacing the old toolbar-like layout with a clearer professional split
- [x] Stage 10D progress: `Palette Brushes` now shows only the active family-scoped composition for the current palette, while backend-only entries from other families stay hidden from the main UX
- [x] Stage 10D progress: the primary XML -> SQLite tileset import now expands ranged `item` entries into regular per-item rows before they ever reach `materials.db`, so palettes no longer start life with grouped item storage in the database
- [x] Stage 10D progress: moving entries between palettes now accepts both `brush` and DB-backed `item` rows shown in `Palette Brushes`, and routes moved entries into the visible destination family/section instead of hiding them under the source section type
- [x] Stage 10D progress: palette composition actions now preserve selection and grid viewport much more consistently during add/remove/reorder/move flows, avoiding jumps back to the first visible entry after save-driven refreshes
- [x] Stage 10D progress: the move destination selector now works with real `palette_groups`, including custom groups, while still using each group's `runtime_family` to place moved entries into the correct destination section
- [x] Stage 10D progress: the `Palette Brushes` grid now paints only the visible tile range and invalidates selection changes locally, reducing repaint cost during navigation and composition actions
- [x] Stage 10D progress: the brush grids now cache rendered sprite previews per `lookId` and invalidate that cache only when preview size changes, reducing repeated `DrawTo` and image resampling during paint
- [x] Stage 10D progress: SQLite-backed runtime palette reload now trusts the DB entry order directly during `loadFromStorage()`, avoiding extra `afterBrushName`/`RAWBrush::getName()` reordering work on every palette refresh
- [x] Stage 10D progress: the palette tree and workspace terminology now align on `Palette Categories`, `Palette Category`, `Destination Category`, and entry-focused action text, making the authoring flow clearer and more concise
- [x] Stage 10D progress: the Workbench tree now opens with a slimmer initial sidebar, group overviews use space more usefully, and category/editor nodes expand or collapse when their text is clicked
- [x] Stage 10D progress: overview panels now act as concise contextual manuals, explaining what each area is for, when to use it, and the recommended workflow instead of wasting space with raw counts alone
- [x] Stage 10E: After every palette save, repopulate the navigation tree and refresh runtime palette state so runtime and Workbench stay aligned
- [ ] Stage 10F: Keep XML-first onboarding working: first import may come from legacy XML, then `materials.db` remains the primary editable source
- [x] Stage 10F progress: first import now boots the 4 base palette groups from legacy XML into `materials.db`, after which the DB stays as the editable source of truth
- [x] Stage 10G: Move palette grouping to a DB-first global model shared by Workbench and runtime palette trees, with built-in and custom groups coming directly from `palette_groups`
- [x] Stage 10G progress: the `Palette Workspace` now exposes CRUD for custom `palette_groups`, protects built-in groups from rename/delete, and lets the current palette move between DB-backed groups directly from the workspace without a fake runtime-family selector
- [x] Stage 10G runtime follow-up: the runtime palette top-level now comes from `palette_groups` themselves, so built-ins plus customs appear as first-class pages and mirror the Workbench grouping model directly
- [x] Stage 10G UX follow-up: custom runtime groups now use the same grid-style palette presentation as the main visual groups, while hidden backend sections are assigned automatically when brushes are added
- [x] Stage 10G runtime fix-up: moving a palette between built-in groups like `terrain`, `doodad`, `item`, and `other` now also reflects correctly in the runtime palette instead of staying tied to the palette's old internal content category

## Variations Status
- [x] `variations` are now a delivered functional milestone for the Brush Workspace
- [x] Core functional value from the old editor has started to return
- [x] The new implementation avoids reviving the old spaghetti structure
- [ ] Extend the same level of completeness where more domains still need it
- [ ] Add richer previews and UX around the current variations editors

## Suggested Roadmap
- [x] Stage 8: Variations support inside existing workspaces
- [x] Stage 9: Dirty state, validations, save workflow, loss-prevention, and origin metadata review
- [ ] Stage 10: Heavy visual polish and richer previews (in progress)
- [ ] Stage 11: XML deprecation flow and migration/sync utilities

## Executive Summary
- [x] Close the main functional gap: `variations` in the Brush Workspace
- [ ] Finish fine UX and UI polish
- [x] Finish Stage 9 validations and robustness
- [x] Finish Stage 9 professional editing UX across all workspaces
- [ ] Finish the professional transition to deprecated XML editing

## Next Task Handoff
- `Stage 9` is complete; the remaining open work is `Stage 10` polish or later features
- `palette_groups` are now the top-level source of truth in both Workbench and runtime, including custom groups
- `section` remains only as backend/runtime compatibility storage and should stay out of the main UX
- `Palettes` is now clearly the composition editor, while `Brushes` stays the authoring editor
- The current focus is still `Stage 10D`, but the remaining work is now narrower: confirm closure criteria, polish any last rough edges in continuous brush editing, and decide whether any explicit cross-family workflow still belongs in the palette UX
- Keep palette saves on targeted sync paths; do not reintroduce full runtime reload on save
