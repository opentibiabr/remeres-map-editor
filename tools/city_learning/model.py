"""Feature accumulators for semantic city-learning corpora."""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import Any, Callable, Iterable


STREET_GROUND_TERMS = (
    "road",
    "street",
    "pavement",
    "cobble",
    "sidewalk",
    "path",
)
STREET_GROUND_NAMES = {
    "sandstone",
    "venore plaster",
    "terracotta",
    "drawbridge",
}
SEMANTIC_TAG_ORDER = (
    "house_tile",
    "wall",
    "door",
    "window",
    "roof",
    "vertical_connector",
    "depot",
    "teleport",
    "road",
    "water",
    "grass",
    "floor",
    "terrain",
    "border",
    "protection_zone",
    "unclassified",
)
SEMANTIC_TAGS = set(SEMANTIC_TAG_ORDER)


def tile_semantic_tags(tile: dict[str, Any]) -> tuple[str, ...]:
    """Return relevant tile/item tags in a stable semantic order."""
    tags = set(tile.get("tags", ()))
    ground = tile.get("ground")
    if ground:
        tags.update(ground.get("tags", ()))
    for item in tile.get("items", ()):
        tags.update(item.get("tags", ()))
    return tuple(tag for tag in SEMANTIC_TAG_ORDER if tag in tags)


def semantic_signature(tile: dict[str, Any]) -> str:
    """Create the compact spatial signature stored in RLE layouts."""
    tags = tile_semantic_tags(tile)
    return "|".join(tags) if tags else "occupied"


def is_street_tile(tile: dict[str, Any]) -> bool:
    """Infer external, traversable urban surfaces from tags and brush metadata."""
    tags = set(tile_semantic_tags(tile))
    if tile.get("houseId") or "house_tile" in tags:
        return False
    ground = tile.get("ground")
    if not ground:
        return False
    if "road" in set(ground.get("tags", ())):
        return True
    identity = ground.get("brush") or ground.get("name")
    names = {str(identity).strip().lower()} if identity else set()
    if names & STREET_GROUND_NAMES:
        return True
    return any(term in text for text in names for term in STREET_GROUND_TERMS)


def building_role(house: dict[str, Any], tag_counts: Counter[str] | dict[str, int]) -> str:
    """Classify an anchor building using explicit objects and stable name clues."""
    name = str(house.get("name", "")).lower()
    if "depot" in name:
        return "depot"
    if any(term in name for term in ("temple", "sanctuary", "shrine")):
        return "temple"
    if "bank" in name:
        return "bank"
    if any(term in name for term in ("guildhall", "guild hall", "clanhall", "clan hall")):
        return "guildhall"
    if any(term in name for term in ("shop", "store", "market", "trade", "merchant")):
        return "shop"
    if any(term in name for term in ("library", "academy", "palace", "castle", "hall", "post office")):
        return "public"
    return "residence"


def ranked_counter(counter: Counter[Any], limit: int | None = None) -> list[dict[str, Any]]:
    """Represent a counter deterministically with highest-frequency values first."""
    ranked = sorted(counter.items(), key=lambda item: (-item[1], str(item[0])))
    if limit is not None:
        ranked = ranked[:limit]
    return [{"value": value, "count": count} for value, count in ranked]


def _runs(values: Iterable[int]) -> list[list[int]]:
    ordered = sorted(set(values))
    result: list[list[int]] = []
    for value in ordered:
        if result and value == result[-1][0] + result[-1][1]:
            result[-1][1] += 1
        else:
            result.append([value, 1])
    return result


@dataclass
class _Bounds:
    min_x: int | None = None
    max_x: int | None = None
    min_y: int | None = None
    max_y: int | None = None

    def include(self, x: int, y: int) -> None:
        self.min_x = x if self.min_x is None else min(self.min_x, x)
        self.max_x = x if self.max_x is None else max(self.max_x, x)
        self.min_y = y if self.min_y is None else min(self.min_y, y)
        self.max_y = y if self.max_y is None else max(self.max_y, y)

    def as_json(self) -> dict[str, int]:
        assert self.min_x is not None and self.max_x is not None
        assert self.min_y is not None and self.max_y is not None
        return {
            "minX": self.min_x,
            "maxX": self.max_x,
            "minY": self.min_y,
            "maxY": self.max_y,
        }


class HouseAccumulator:
    """Accumulate one anchor house as a reconstructable building prototype."""

    def __init__(self, source: str, town: dict[str, Any], district: dict[str, Any], house: dict[str, Any]) -> None:
        self.source = source
        self.town = town
        self.district = district
        self.house = house
        self.bounds = _Bounds()
        self.positions_by_floor: dict[int, dict[int, list[int]]] = defaultdict(lambda: defaultdict(list))
        self.tag_counts: Counter[str] = Counter()
        self.item_ids_by_tag: dict[str, Counter[int]] = defaultdict(Counter)
        self.door_positions: list[dict[str, int]] = []
        self.tile_count = 0

    def add_tile(self, tile: dict[str, Any]) -> None:
        position = tile["position"]
        x, y, z = position["x"], position["y"], position["z"]
        self.bounds.include(x, y)
        self.positions_by_floor[z][y].append(x)
        self.tile_count += 1
        tags = tile_semantic_tags(tile)
        self.tag_counts.update(tags)
        if "door" in tags:
            self.door_positions.append({"x": x, "y": y, "z": z})
        for item in _items_including_ground(tile):
            for tag in item.get("tags", ()):
                if tag in SEMANTIC_TAGS:
                    self.item_ids_by_tag[tag][item["id"]] += 1

    def finalize(self) -> dict[str, Any] | None:
        if self.tile_count == 0:
            return None
        bounds = self.bounds.as_json()
        footprint = []
        for z in sorted(self.positions_by_floor):
            rows = []
            for y in sorted(self.positions_by_floor[z]):
                relative_runs = [
                    [start - bounds["minX"], length]
                    for start, length in _runs(self.positions_by_floor[z][y])
                ]
                rows.append([y - bounds["minY"], relative_runs])
            footprint.append({"z": z, "rows": rows})
        entrances = list(self.door_positions)
        if not entrances and self.house.get("exit"):
            entrances.append(dict(self.house["exit"]))
        return {
            "source": self.source,
            "town": self.town,
            "districtIndex": self.district.get("index"),
            "districtRole": self.district.get("role"),
            "house": self.house,
            "role": building_role(self.house, self.tag_counts),
            "entrances": entrances,
            "bounds": bounds,
            "dimensions": {
                "width": bounds["maxX"] - bounds["minX"] + 1,
                "height": bounds["maxY"] - bounds["minY"] + 1,
                "floorCount": len(self.positions_by_floor),
                "tileCount": self.tile_count,
            },
            "footprint": footprint,
            "tagCounts": dict(sorted(self.tag_counts.items())),
            "itemsByTag": {
                tag: ranked_counter(counter)
                for tag, counter in sorted(self.item_ids_by_tag.items())
            },
        }


class DistrictAccumulator:
    """Reduce district tiles to statistical and reconstructable semantic features."""

    def __init__(
        self,
        source: str,
        town: dict[str, Any],
        district: dict[str, Any],
        layout_sink: Callable[[dict[str, Any]], None],
    ) -> None:
        self.source = source
        self.town = town
        self.district = district
        self.layout_sink = layout_sink
        self.houses: dict[int, dict[str, Any]] = {}
        self.anchor_houses: dict[int, HouseAccumulator] = {}
        self.tile_count = 0
        self.floor_counts: Counter[int] = Counter()
        self.signature_counts: Counter[str] = Counter()
        self.tag_counts: Counter[str] = Counter()
        self.item_ids_by_tag: dict[str, Counter[int]] = defaultdict(Counter)
        self.street_positions_by_floor: dict[int, set[tuple[int, int]]] = defaultdict(set)
        self.street_styles_by_floor: dict[int, Counter[str]] = defaultdict(Counter)
        self._row_key: tuple[int, int] | None = None
        self._row_runs: list[list[Any]] = []

    def add_house(self, house: dict[str, Any]) -> None:
        house_id = house["id"]
        self.houses[house_id] = house
        if house.get("anchor"):
            self.anchor_houses[house_id] = HouseAccumulator(self.source, self.town, self.district, house)

    def add_tile(self, tile: dict[str, Any]) -> None:
        position = tile["position"]
        signature = semantic_signature(tile)
        self._add_layout_tile(position["x"], position["y"], position["z"], signature)
        self.tile_count += 1
        self.floor_counts[position["z"]] += 1
        self.signature_counts[signature] += 1
        tags = tile_semantic_tags(tile)
        self.tag_counts.update(tags)
        if is_street_tile(tile):
            self.street_positions_by_floor[position["z"]].add((position["x"], position["y"]))
            ground = tile.get("ground", {})
            style = ground.get("brush") or ground.get("name") or "tagged-road"
            self.street_styles_by_floor[position["z"]][str(style)] += 1
        for item in _items_including_ground(tile):
            for tag in item.get("tags", ()):
                if tag in SEMANTIC_TAGS:
                    self.item_ids_by_tag[tag][item["id"]] += 1
        house_id = tile.get("houseId")
        if house_id in self.anchor_houses:
            self.anchor_houses[house_id].add_tile(tile)

    def _add_layout_tile(self, x: int, y: int, z: int, signature: str) -> None:
        row_key = (z, y)
        if self._row_key != row_key:
            self._flush_row()
            self._row_key = row_key
        if self._row_runs and x == self._row_runs[-1][0] + self._row_runs[-1][1] and signature == self._row_runs[-1][2]:
            self._row_runs[-1][1] += 1
        else:
            self._row_runs.append([x, 1, signature])

    def _flush_row(self) -> None:
        if self._row_key is None or not self._row_runs:
            return
        z, y = self._row_key
        self.layout_sink({
            "source": self.source,
            "townId": self.town["id"],
            "districtIndex": self.district.get("index"),
            "z": z,
            "y": y,
            "runs": self._row_runs,
        })
        self._row_runs = []

    def finalize(self) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        self._flush_row()
        templates = []
        for house_id in sorted(self.anchor_houses):
            template = self.anchor_houses[house_id].finalize()
            if template is not None:
                templates.append(template)
        bounds = self.district.get("bounds", {})
        area = (
            (bounds["maxX"] - bounds["minX"] + 1) *
            (bounds["maxY"] - bounds["minY"] + 1)
            if bounds else 0
        )
        street_graph = _build_street_graph(self.street_positions_by_floor, self.street_styles_by_floor, templates)
        role_counts = Counter(template["role"] for template in templates)
        urban_quality = _urban_quality(self.district, street_graph, templates)
        profile = {
            "source": self.source,
            "town": self.town,
            "index": self.district.get("index"),
            "role": self.district.get("role"),
            "confidence": self.district.get("confidence"),
            "inferenceMethod": self.district.get("inferenceMethod"),
            "bounds": bounds,
            "area": area,
            "tileCount": self.tile_count,
            "observedHouseCount": len(self.houses),
            "anchorHouseCount": len(self.anchor_houses),
            "floorCounts": {str(key): value for key, value in sorted(self.floor_counts.items())},
            "signatureCounts": dict(sorted(self.signature_counts.items())),
            "tagCounts": dict(sorted(self.tag_counts.items())),
            "itemsByTag": {
                tag: ranked_counter(counter, limit=50)
                for tag, counter in sorted(self.item_ids_by_tag.items())
            },
            "buildingRoles": dict(sorted(role_counts.items())),
            "streetGraph": street_graph,
            "urbanQuality": urban_quality,
        }
        return profile, templates


def _items_including_ground(tile: dict[str, Any]) -> Iterable[dict[str, Any]]:
    ground = tile.get("ground")
    if ground:
        yield ground
    yield from tile.get("items", ())


def _build_street_graph(
    positions_by_floor: dict[int, set[tuple[int, int]]],
    street_styles_by_floor: dict[int, Counter[str]],
    templates: list[dict[str, Any]],
) -> dict[str, Any]:
    if not positions_by_floor:
        return {
            "primaryFloor": None,
            "roadTileCount": 0,
            "componentCount": 0,
            "mainComponentSize": 0,
            "mainComponentRatio": 0.0,
            "intersectionCount": 0,
            "deadEndCount": 0,
            "accessibleBuildingCount": 0,
            "inaccessibleBuildingCount": len(templates),
            "styleBrushes": [],
        }

    primary_floor = sorted(
        positions_by_floor,
        key=lambda z: (-len(positions_by_floor[z]), z),
    )[0]
    positions = positions_by_floor[primary_floor]
    remaining = set(positions)
    components: list[set[tuple[int, int]]] = []
    while remaining:
        pending = [remaining.pop()]
        component: set[tuple[int, int]] = set()
        while pending:
            current = pending.pop()
            component.add(current)
            x, y = current
            for neighbour in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if neighbour in remaining:
                    remaining.remove(neighbour)
                    pending.append(neighbour)
        components.append(component)
    components.sort(key=len, reverse=True)
    main = components[0]
    degrees = []
    for x, y in main:
        degrees.append(sum(
            neighbour in main
            for neighbour in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
        ))

    accessible = 0
    for template in templates:
        if any(
            entrance.get("z") == primary_floor and
            any(abs(entrance["x"] - x) + abs(entrance["y"] - y) <= 1 for x, y in main)
            for entrance in template.get("entrances", ())
        ):
            accessible += 1
    road_count = len(positions)
    return {
        "primaryFloor": primary_floor,
        "roadTileCount": road_count,
        "componentCount": len(components),
        "mainComponentSize": len(main),
        "mainComponentRatio": round(len(main) / road_count, 4),
        "intersectionCount": sum(degree >= 3 for degree in degrees),
        "deadEndCount": sum(degree <= 1 for degree in degrees),
        "accessibleBuildingCount": accessible,
        "inaccessibleBuildingCount": len(templates) - accessible,
        "styleBrushes": ranked_counter(street_styles_by_floor[primary_floor], limit=12),
    }


def _urban_quality(
    district: dict[str, Any],
    street_graph: dict[str, Any],
    templates: list[dict[str, Any]],
) -> dict[str, Any]:
    reasons: list[str] = []
    warnings: list[str] = []
    confidence = district.get("confidence")
    inference_method = str(district.get("inferenceMethod", ""))
    if confidence == "low" or "fallback" in inference_method:
        reasons.append("low-confidence district inference")
    bounds = district.get("bounds", {})
    area = (
        (bounds["maxX"] - bounds["minX"] + 1) *
        (bounds["maxY"] - bounds["minY"] + 1)
        if bounds else 0
    )
    minimum_main_component = 64 if area >= 4096 else 4
    if street_graph["mainComponentSize"] < minimum_main_component:
        if area >= 4096:
            reasons.append("street core is too small for captured region")
        else:
            reasons.append("insufficient recognized street network")
    if street_graph["roadTileCount"] and street_graph["mainComponentRatio"] < 0.6:
        warnings.append("fragmented observed street network")
    if templates and street_graph["accessibleBuildingCount"] == 0:
        warnings.append("no anchor building frontage detected")
    penalty = 25 * len(reasons) + 10 * len(warnings)
    score = max(0, min(100, 100 - penalty))
    return {
        "eligibleForGeneration": not reasons,
        "score": score,
        "reasons": reasons,
        "warnings": warnings,
    }
