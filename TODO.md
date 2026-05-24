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
- [ ] Validate `door type` compatibility with the selected `item id`
- [x] Block saving a brush with an invalid or unexpected `type` in the Brush Workspace
- [ ] Harden selection transitions in dynamically recreated grids
- [ ] Add more specific save and reload logs
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
- `Stage 9` is not finished yet; the Brush Workspace is close, but there are still remaining selection/scroll, logging, and provenance tasks before calling it complete
- `Stage 9` is not finished yet; the Brush Workspace is close, but there are still remaining selection/scroll tasks before calling it complete
- Brush Workspace already has: initial variations editing, stable save/revert flow, rename-safe palette sync, dirty state, modified badges, selection guard, and stronger save-time validation for `item id`, `lookId`, `serverLookId`, and `type`
- Brush palette runtime sync already updates renamed brushes and saved `lookId` changes without full runtime reload
- The current modified-field highlight works functionally but still needs a more professional visual treatment
- Recent progress: variation list refreshes now preserve the visible list position more consistently during `Clear()`/`Append()` rebuilds
- Recent progress: reloading the same brush now preserves more of the active variation editor context, including internal selection state and visible list position
- Recent progress: parent/child variation transitions now keep more existing selection context instead of clearing child selections eagerly before refresh
- Recent progress: brush load/save/revert and controller catalog reload now emit more specific logs with brush identifiers and reload context
- Recent progress: brush metadata and inspector now show `Storage: materials.db` separately from legacy `Imported from`
- Recent progress: saving the same brush now restores more of the active variation editor context instead of rebuilding the panel more bluntly
- Recent progress: navigation tree refreshes now preserve expanded groups and the current selection more often instead of jumping back to the first node
- Recent progress: brush reload now preserves variation context by brush id even if rename or refresh changes the brush index in navigation
- Remaining blockers for closing `Stage 9`: preserve selection/scroll more consistently across the remaining reload paths and harden any last unstable dynamic selection transitions in recreated UI controls
- Best next implementation cut: finish the remaining selection/scroll preservation paths in the Brush Workspace, then reassess whether `Stage 9` is ready to close
- If a follow-up task must be split, keep the next task scoped to `Stage 9` only; do not jump to previews or Stage 10 polish until the items above are closed
- Avoid reintroducing full runtime reload on brush or palette save; keep using targeted sync paths because the global reload path previously crashed in `Brushes::clear()`
