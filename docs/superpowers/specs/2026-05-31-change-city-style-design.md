# Change City Style Design

## Goal

Add a map-wide city styling tool that lets a mapper pick a town and replace the
detected city's structural wall style without losing the existing custom RME
work for connected building and ground style changes.

## Scope

The first version changes structural building walls, doors, windows, hatch
windows, and semantic wall-brush archways. It does not change ground styles yet.
Ground replacement should be added later by reusing `ChangeConnectedGroundStyle`
once wall replacement is stable.

## City Boundary Rule

The city is inferred from the selected town id:

- Houses with the selected `townid` are the hard anchors.
- The temple position is a soft anchor when it is valid.
- The analyzer builds an expanded envelope around all town houses and the
  temple.
- Urban ground connected to house exits and nearby house tiles forms the public
  city surface.
- Wall components inside the envelope are accepted when they touch a selected
  town house, touch the connected urban surface, or are close to the temple.
- Wall components that touch a house belonging to a different town are rejected.

This is intentionally conservative. It should style the actual town while
avoiding distant structures connected by roads or unrelated buildings from a
nearby town.

## UI

The tool is opened from `Map > Change City Style...`.

The dialog contains:

- a town selector populated from `Map::towns`;
- a style search field;
- a full-match filter;
- a list of compatible wall styles;
- a summary of detected houses, city walls, floors, source wall styles, and
  conflicts;
- a preview pane using the same panning, floor navigation, and zoom behavior as
  `Change Build Style`;
- conflict handling on apply: keep incompatible openings or replace them with
  the target wall.

## Replacement Rules

Every detected wall item keeps its original wall alignment. For regular wall
items the target wall item for that alignment is used. For doors, windows, hatch
windows, magic doors, quest doors, locked doors, and archways the target
wall-brush door mapping is used when available. Door ids are preserved when the
converted item is still a door.

If a target style lacks an opening mapping, the style remains selectable but is
marked as incomplete. Applying an incomplete style asks the user whether to keep
old openings or replace unresolved openings with normal target walls.

## Validation

The feature must compile in the Windows release build, keep the existing city
learning tests passing, and keep the existing `Change Build Style` behavior
intact.
