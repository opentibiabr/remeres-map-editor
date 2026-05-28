# TODO - Materials Workbench

## Current State
- [x] `materials.db` is the editing source of truth
- [x] `palette_groups` drive top-level organization in Workbench and runtime
- [x] `Palettes` edit composition, while `Brushes` edit behavior and metadata
- [x] `Borders` and `Walls` already have dedicated workspaces with save/revert and targeted runtime sync
- [x] Stage 10D core palette flow is in place: add, move, remove, reorder, custom groups, preserved selection/scroll, better tree UX, overview manuals, and contextual tooltips

## Next
- [x] Border Workspace visual pass: align `Preview Matrix` to the same geometry as `Slot Grid`
- [x] Border Workspace visual pass: show a real visual `Center Ground` using `groundEquivalent`
- [x] Border Workspace visual pass: upgrade the preview from loose icons to a stronger composed border preview

## Visual Polish
- [ ] Add visual metadata previews for `lookId` and `serverLookId` in the Brush Workspace
- [ ] Add richer visual previews inside `variations`, including per-item thumbnails and domain-specific preview feedback
- [x] Improve Border Workspace presentation with stronger contrast, clearer slot-to-preview correspondence, a compact metadata panel, a centered compact slot grid, and a denser preview-first layout
- [ ] Improve Wall Workspace presentation with richer visual previews for wall parts, doors, and composed wall results
- [ ] Reduce spacing and padding across workspaces where the layout still feels too loose
- [ ] Continue small product-language and workflow refinements across Workbench texts and actions
- [ ] Standardize technical labels such as `lookId`, `serverLookId`, `zOrder`, and `partType`

## Future UX
- [ ] Add search and filtering in the navigation tree
- [ ] Add safer creation and removal flows for core entities directly inside the Workbench
- [ ] Add duplication flows for entities such as palettes, border sets, and wall part sets where it improves authoring speed
- [ ] Add a lightweight reference view for understanding where a brush is used

## Nice To Have
- [ ] Add a real composed preview for `wall brush`, beyond the current parts grid
- [ ] Add a mini-scene preview for `border` application, beyond the slot matrix
- [ ] Add local per-session edit history for fast undo-style recovery inside the Workbench
- [ ] Add manual runtime sync controls only if a future workflow really needs them
- [ ] Revisit XML deprecation messaging once the visual Workbench flow feels complete and clearly superior

## Delivered Milestones
- [x] Stage 8: `variations` support landed inside the Brush Workspace
- [x] Stage 9: dirty state, validations, save/revert flow, loss prevention, and origin metadata review are complete
- [x] Stage 10A-10C: palette navigation and editing were rebuilt around a real DB-first palette workflow
- [x] Stage 10D core UX: palette composition is now professional, fast, and inline, without exposing backend-only `section` concepts in the main UI
- [x] Stage 10E: palette saves refresh runtime palettes and navigation state together so Workbench and runtime stay aligned
- [x] Stage 10F base: first import bootstraps the base palette groups from legacy XML into `materials.db`
- [x] Stage 10G: palette grouping is DB-first and shared by Workbench and runtime, including custom groups

## Delivered Highlights
- [x] Brush Workspace: metadata, `variations`, dirty state, save/revert, runtime-owner hints, and validation for `lookId`, `serverLookId`, and variation item ids
- [x] Palette Workspace: inline add/move/remove/reorder flow, support for `brush` and DB-backed `item` entries, custom destination groups, preserved selection/scroll, and better destination feedback
- [x] Palette Workspace performance: visible-range painting, local selection invalidation, cached preview bitmaps, and lighter runtime palette reload ordering from DB
- [x] Border Workspace: dirty state, save/revert, runtime-safe validation, canonical 5x5 slot geometry, selected-slot preservation, targeted runtime refresh, a runtime-style composed preview matrix with visual `Center Ground`, consistent `groundEquivalent` import, and runtime-truthful global border center preview via linked brush usage contexts
- [x] Wall Workspace: dirty state, save/revert, guarded selection changes, selected part/item/door preservation, and door compatibility validation
- [x] Navigation and UX: slimmer sidebar, click-on-label expand/collapse, contextual overview manuals, clearer `Palette Categories` terminology, and rich tooltips in tree and palette grids
- [x] Runtime alignment: Workbench and runtime now share DB-first palette groups, including custom groups, with targeted sync paths instead of unsafe full reloads

## Guardrails
- [x] Keep `section` as backend/runtime compatibility storage, not as a first-class UX concept
- [x] Keep palette saves on targeted sync paths; do not reintroduce unsafe full runtime reload on save
- [x] Keep the Workbench DB-first, with legacy XML only as bootstrap/compatibility support during the transition
