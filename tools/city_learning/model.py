"""Feature accumulators for semantic city-learning corpora."""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
from typing import Any, Callable, Iterable


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
        self.tile_count = 0

    def add_tile(self, tile: dict[str, Any]) -> None:
        position = tile["position"]
        x, y, z = position["x"], position["y"], position["z"]
        self.bounds.include(x, y)
        self.positions_by_floor[z][y].append(x)
        self.tile_count += 1
        tags = tile_semantic_tags(tile)
        self.tag_counts.update(tags)
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
        return {
            "source": self.source,
            "town": self.town,
            "districtIndex": self.district.get("index"),
            "districtRole": self.district.get("role"),
            "house": self.house,
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
        }
        return profile, templates


def _items_including_ground(tile: dict[str, Any]) -> Iterable[dict[str, Any]]:
    ground = tile.get("ground")
    if ground:
        yield ground
    yield from tile.get("items", ())
