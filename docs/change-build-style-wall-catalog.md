# Change Build Style Wall Catalog

This catalog documents the curated wall sets used by Change Build Style.
It was cross-checked against `data/materials/brushs/walls.xml`,
`data/items/items.xml`, and `D:/kl/canary/data/items/items.xml`.

## Selection Rules

- Building wall styles must come from the `Walls` tileset.
- Building wall styles must have the core wall shapes: horizontal, vertical,
  corner, and pole.
- Doodad brushes that happen to use `type="wall"` are excluded from building
  style conversion.
- Boundary styles are only allowed from `Architecture` when their names are
  railings, fences, bars, rebar, cords, palisades, or equivalent boundary
  pieces.
- Full-match filtering checks the selected construction, not just the style:
  every required wall shape, door, window, hatch window, magic/quest/locked
  door, and archway must exist in the target style.

## Explicitly Excluded False Wall Brushes

- ant trails
- blood pipe
- buoy line
- cracks
- fishing net
- jade floor ornaments
- lava pipe
- lava stream
- railways
- small stream blue / green
- store counter
- venorean store counter
- zao floor ornaments

## Curated Building Wall Sets

- stone wall: hatch window, locked door, magic door, normal door, quest door, window
- framework wall: archway, hatch window, locked door, magic door, normal door, quest door, window
- brick wall: archway, hatch window, locked door, magic door, normal door, quest door, window
- egypt wall: hatch window, locked door, magic door, normal door, quest door, window
- egypt stone wall: hatch window, window
- ruin wall: hatch window, locked door, magic door, normal door, quest door, window
- marble wall: hatch window, magic door, normal door, quest door, window
- rock wall
- wooden wall: archway, hatch window, locked door, magic door, normal door, quest door, window
- low stone-wooden wall
- wood wall: hatch window, locked door, magic door, normal door, quest door, window
- plant wall: archway, any door, any window
- plant wall2
- croft wall: hatch window, locked door, magic door, normal door, quest door, window
- alabaster wall: locked door, magic door, normal door, quest door
- stone wall2: window
- metal wall
- paravent
- lava wall
- bamboo wall: hatch window, locked door, magic door, normal door, quest door, window
- grass wall: window
- palisade
- bone wall: window
- frozen wall: hatch window, locked door, magic door, normal door, quest door
- fur wall: hatch window, locked door, normal door
- stone wall3: locked door, magic door, normal door, quest door, window
- iron wall: locked door, magic door, normal door, quest door
- stone wall4: hatch window, window
- limestone wall: hatch window, locked door, magic door, normal door, quest door, window
- dark framework wall: hatch window, locked door, magic door, normal door, quest door, window
- stone wall5
- blue palisade
- stone: locked door, window
- timber wall: locked door, normal door, window
- light desert wall
- dark desert wall
- rocky wall
- glass wall
- icy bones: window
- muddy stone wall
- muddy stone wall2
- brown stone wall
- red stone wall
- ornate wall
- ornate wall2
- venore wall: hatch window, locked door, magic door, normal door, quest door, window
- roshamuul wall: window
- oramond wall: locked door, normal door, window
- oramond wall2
- oramond wall3
- zaoan wall2: hatch window, window
- zaoan wall1: hatch window, window
- zaoan wall3: hatch window, magic door, normal door, quest door, window
- zaoan wall4: hatch window, window
- zaoan wall5: hatch window, window
- zaoan wall6: hatch window, window
- zaoan wall7: hatch window, window
- zaoan wall8: hatch window, window
- zaoan wall9: hatch window, window
- corrupted wall1: magic door, normal door, quest door, window
- corrupted wall2: locked door, normal door, quest door, window
- dark stone wall: normal door, quest door
- dark block wall: normal door
- white stone wall: normal door
- cracked wall: normal door
- messy wall: normal door
- hands wall: normal door
- plant wall 3: normal door
- clean marble wall: hatch window, normal door
- gray brick wall: hatch window, normal door
- archwood brick wall
- strange metal wall
- stone wall (bege): hatch window, locked door, normal door
- heavy stones wall: hatch window, locked door, magic door, normal door
- stone covered with blue: normal door
- stone covered with purple: normal door
- ornamented wall: hatch window, normal door, quest door
- new frozen wall: hatch window, normal door, quest door
- short stone wall: normal door
- massive stone wall: normal door
- grass stone wall: normal door
- monument wall: hatch window, normal door
- dragon lair wall: normal door

## Archway-Capable Sets

Only these sets currently expose semantic archway mappings through wall brush
metadata:

- framework wall
- brick wall
- wooden wall
- plant wall

Other `items.xml` archway items exist, but they are raw construction items or
loose visual pieces without reliable wall-brush orientation metadata. They are
recognized as loose archways only when they are bracketed by the same selected
source wall brush on both opposite sides of a straight wall line. One-sided
portal/decorative archways near a building are intentionally ignored. Loose
archways are not promoted into target full matches unless the target wall brush
has an explicit semantic archway entry.
