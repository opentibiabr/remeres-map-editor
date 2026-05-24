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

## Remaining Before Calling It Ready
- [ ] Extend `dirty state` beyond the Brush Workspace
- [x] Highlight modified fields visually in the Brush Workspace
- [x] Show `modified` badges in the navigation tree for dirty brush edits
- [ ] Preserve selection and scroll more consistently across reloads
- [ ] Improve validation before save across the remaining Workbench domains
- [ ] Add richer `border` and `wall` previews
- [ ] Support entity creation and removal, not only editing
- [ ] Improve semantic clarity for origin metadata
- [ ] Polish the UI visually and compact workspace layouts
- [ ] Implement a professional XML deprecation flow

## UI And UX Polish
- [ ] Reduce spacing and padding across all workspaces
- [ ] Unify headers, status bars, and action buttons across the Workbench
- [ ] Improve status text quality and usefulness
- [ ] Add rich tooltips with `id`, type, source, and relationships
- [ ] Improve preview grid contrast and alignment
- [ ] Standardize technical labels such as `lookId`, `serverLookId`, `zOrder`, and `partType`

## Semantics And Provenance
- [x] Use `Imported From` in the UI where the field represents legacy origin
- [ ] Decide whether `Legacy XML Source` is clearer than the current wording
- [ ] Decide whether `Origin File` is clearer than the current wording
- [ ] Show `Storage: materials.db` separately
- [ ] Show `Imported from: ...` separately

## Robustness And Validation
- [x] Validate missing or unknown `item id` before save in the Brush Workspace
- [x] Validate `lookId` and `serverLookId` against the real item catalog in the Brush Workspace
- [ ] Validate invalid duplicate usage in border slots where applicable
- [x] Validate `door type` compatibility with the selected `item id`
- [x] Block saving a brush with an invalid or unexpected `type` in the Brush Workspace
- [ ] Harden selection transitions in dynamically recreated grids
- [x] Add more specific save and reload logs
- [ ] Revisit runtime refresh for `walls` and `borders`
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

## Variations Status
- [x] `variations` are now a delivered functional milestone for the Brush Workspace
- [x] Core functional value from the old editor has started to return
- [x] The new implementation avoids reviving the old spaghetti structure
- [ ] Extend the same level of completeness where more domains still need it
- [ ] Add richer previews and UX around the current variations editors

## Suggested Roadmap
- [x] Stage 8: Variations support inside existing workspaces
- [ ] Stage 9: Dirty state, validations, save workflow, loss-prevention, and origin metadata review
- [ ] Stage 10: Heavy visual polish and richer previews
- [ ] Stage 11: XML deprecation flow and migration/sync utilities

## Executive Summary
- [x] Close the main functional gap: `variations` in the Brush Workspace
- [ ] Finish fine UX and UI polish
- [ ] Finish validations and robustness
- [ ] Finish a professional editing UX across all workspaces
- [ ] Finish the professional transition to deprecated XML editing

## Next Task Handoff
- Current focus remains `Stage 9`
- Brush Workspace is now near the end of its `Stage 9` pass: variations, dirty state, save/revert flow, rename-safe palette sync, stronger validation, provenance clarity, navigation preservation, and context-preserving refreshes are already in place
- Brush palette runtime sync already updates renamed brushes and saved `lookId` changes without full runtime reload
- The current modified-field highlight works functionally but still needs a more professional visual treatment
- Remaining Brush Workspace follow-up is now small and mostly polish-level: final reassessment of any residual selection/scroll edge cases and whether the modified highlight needs one more visual pass
- `Wall Workspace` has now started its `Stage 9` pass with dirty state, save/revert consistency, selection-change protection, close protection, navigation badge integration, and basic context preservation across save/reload
- Recommended next task goal: reassess the remaining `Stage 9` runtime edge cases after the latest brush and wall refresh fixes, then decide what still blocks the move to previews
- If a follow-up task must be split, keep the next task scoped to `Stage 9` only; do not jump to previews or Stage 10 polish until the items above are closed
- Avoid reintroducing full runtime reload on brush or palette save; keep using targeted sync paths because the global reload path previously crashed in `Brushes::clear()`
