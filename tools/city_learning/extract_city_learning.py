"""Extract compact learning artifacts from large RME city corpus JSON files."""

from __future__ import annotations

import argparse
import gzip
import json
import os
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Callable, Iterable

try:
    from .model import DistrictAccumulator, ranked_counter
except ImportError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from tools.city_learning.model import DistrictAccumulator, ranked_counter


class CorpusError(RuntimeError):
    """Raised when an input corpus cannot be used for learning."""


class LearningModel:
    """Aggregate district profiles while large geometries stream to sidecars."""

    def __init__(self) -> None:
        self.sources: list[dict[str, Any]] = []
        self.districts: list[dict[str, Any]] = []
        self.cities: list[dict[str, Any]] = []
        self._cities_by_key: dict[tuple[str, int], dict[str, Any]] = {}
        self.tag_counts: Counter[str] = Counter()
        self.signature_counts: Counter[str] = Counter()
        self.item_ids_by_tag: dict[str, Counter[int]] = defaultdict(Counter)
        self.building_role_counts: Counter[str] = Counter()
        self.tile_count = 0
        self.building_template_count = 0
        self.eligible_district_count = 0

    def add_source(self, source: dict[str, Any]) -> None:
        self.sources.append(source)

    def add_district(self, profile: dict[str, Any], templates: list[dict[str, Any]]) -> None:
        district_reference = len(self.districts)
        self.districts.append(profile)
        key = (profile["source"], profile["town"]["id"])
        if key not in self._cities_by_key:
            city = {
                "source": profile["source"],
                "town": profile["town"],
                "districts": [],
            }
            self._cities_by_key[key] = city
            self.cities.append(city)
        self._cities_by_key[key]["districts"].append(district_reference)
        self.tile_count += profile["tileCount"]
        self.tag_counts.update(profile["tagCounts"])
        self.signature_counts.update(profile["signatureCounts"])
        for tag, ranked in profile["itemsByTag"].items():
            for item in ranked:
                self.item_ids_by_tag[tag][int(item["value"])] += item["count"]
        self.building_role_counts.update(profile.get("buildingRoles", {}))
        if profile.get("urbanQuality", {}).get("eligibleForGeneration"):
            self.eligible_district_count += 1
        self.building_template_count += len(templates)

    def as_json(self) -> dict[str, Any]:
        return {
            "format": "rme-city-learning-model",
            "version": 2,
            "sources": self.sources,
            "cities": self.cities,
            "districts": self.districts,
            "catalog": {
                "tagCounts": dict(sorted(self.tag_counts.items())),
                "signatureCounts": dict(sorted(self.signature_counts.items())),
                "itemsByTag": {
                    tag: ranked_counter(counter, limit=100)
                    for tag, counter in sorted(self.item_ids_by_tag.items())
                },
                "buildingRoleCounts": dict(sorted(self.building_role_counts.items())),
            },
            "summary": {
                "sourceCount": len(self.sources),
                "cityCount": len(self.cities),
                "districtCount": len(self.districts),
                "tileCount": self.tile_count,
                "buildingTemplateCount": self.building_template_count,
                "eligibleDistrictCount": self.eligible_district_count,
            },
        }


class _JsonlWriter:
    def __init__(self, stream: Any) -> None:
        self.stream = stream

    def write(self, record: dict[str, Any]) -> None:
        self.stream.write(json.dumps(record, ensure_ascii=True, sort_keys=True, separators=(",", ":")))
        self.stream.write("\n")


def _ijson_module() -> Any:
    try:
        import ijson
    except ImportError as error:
        raise CorpusError(
            "The streaming extractor requires ijson. Install it with: "
            "python -m pip install -r tools/city_learning/requirements.txt"
        ) from error
    return ijson


def _validate_header(header: dict[str, Any], corpus_path: Path) -> None:
    if header.get("format") != "rme-city-corpus" or header.get("version") != 2:
        raise CorpusError(
            f"{corpus_path} is not an rme-city-corpus version 2 file; "
            "export it again with Export All Town Learning Corpus."
        )


def _consume_object(
    parser: Iterable[tuple[str, str, Any]],
    first_event: tuple[str, str, Any],
    ijson: Any,
) -> dict[str, Any]:
    builder = ijson.ObjectBuilder()
    _, event, value = first_event
    builder.event(event, value)
    depth = 1
    for _, event, value in parser:
        builder.event(event, value)
        if event in ("start_map", "start_array"):
            depth += 1
        elif event in ("end_map", "end_array"):
            depth -= 1
        if depth == 0:
            return builder.value
    raise CorpusError("Unexpected end of JSON while reading an object.")


def _stream_corpus(
    corpus_path: Path,
    model: LearningModel,
    layout_writer: _JsonlWriter,
    building_writer: _JsonlWriter,
    progress: Callable[[dict[str, Any]], None] | None,
) -> None:
    ijson = _ijson_module()
    header: dict[str, Any] = {"corpusFile": str(corpus_path)}
    current_town: dict[str, Any] | None = None
    current_district: dict[str, Any] | None = None
    accumulator: DistrictAccumulator | None = None
    header_validated = False

    with corpus_path.open("rb") as stream:
        parser = iter(ijson.parse(stream))
        for prefix, event, value in parser:
            if prefix in ("format", "version", "sourceMap", "mapVersion") and event in ("string", "number"):
                header[prefix] = value
                continue
            if prefix == "cities" and event == "start_array":
                _validate_header(header, corpus_path)
                model.add_source(header)
                header_validated = True
                continue
            if prefix == "cities.item.town" and event == "start_map":
                current_town = _consume_object(parser, (prefix, event, value), ijson)
                continue
            if prefix == "cities.item.districts.item" and event == "start_map":
                if current_town is None:
                    raise CorpusError(f"{corpus_path} contains a district without a town.")
                current_district = {}
                accumulator = DistrictAccumulator(
                    source=header.get("sourceMap", str(corpus_path)),
                    town=current_town,
                    district=current_district,
                    layout_sink=layout_writer.write,
                )
                continue
            if accumulator is not None and prefix.startswith("cities.item.districts.item."):
                relative_prefix = prefix.removeprefix("cities.item.districts.item.")
                if relative_prefix in ("index", "role", "confidence", "inferenceMethod", "templeDistance") and event in ("string", "number"):
                    current_district[relative_prefix] = value
                    continue
                if relative_prefix.startswith("bounds.") and event == "number":
                    current_district.setdefault("bounds", {})[relative_prefix.removeprefix("bounds.")] = value
                    continue
                if relative_prefix == "houses.item" and event == "start_map":
                    accumulator.add_house(_consume_object(parser, (prefix, event, value), ijson))
                    continue
                if relative_prefix == "tiles.item" and event == "start_map":
                    accumulator.add_tile(_consume_object(parser, (prefix, event, value), ijson))
                    continue
            if prefix == "cities.item.districts.item" and event == "end_map":
                if accumulator is None:
                    raise CorpusError(f"{corpus_path} contains an incomplete district.")
                profile, templates = accumulator.finalize()
                for template in templates:
                    building_writer.write(template)
                model.add_district(profile, templates)
                if progress:
                    progress({
                        "source": header.get("sourceMap", str(corpus_path)),
                        "town": profile["town"]["name"],
                        "district": profile["index"],
                        "tiles": profile["tileCount"],
                        "templates": len(templates),
                    })
                accumulator = None
                current_district = None

    if not header_validated:
        _validate_header(header, corpus_path)
        model.add_source(header)
    if accumulator is not None:
        raise CorpusError(f"{corpus_path} ended before the current district was completed.")


def extract_corpora(
    corpus_paths: Iterable[Path | str],
    output_directory: Path | str,
    progress: Callable[[dict[str, Any]], None] | None = None,
) -> dict[str, Any]:
    """Extract deterministic training artifacts from one or more v2 corpora."""
    inputs = [Path(path) for path in corpus_paths]
    if not inputs:
        raise CorpusError("At least one corpus JSON file is required.")
    output_path = Path(output_directory)
    output_path.mkdir(parents=True, exist_ok=True)
    targets = {
        "model": output_path / "city-learning-model.json",
        "buildings": output_path / "building-templates.jsonl.gz",
        "layouts": output_path / "semantic-layouts.jsonl.gz",
    }
    temporaries = {key: path.with_suffix(path.suffix + ".tmp") for key, path in targets.items()}
    model = LearningModel()
    try:
        with gzip.open(temporaries["buildings"], "wt", encoding="utf-8", newline="\n") as building_stream, \
                gzip.open(temporaries["layouts"], "wt", encoding="utf-8", newline="\n") as layout_stream:
            building_writer = _JsonlWriter(building_stream)
            layout_writer = _JsonlWriter(layout_stream)
            for input_path in inputs:
                _stream_corpus(input_path, model, layout_writer, building_writer, progress)
        model_data = model.as_json()
        with temporaries["model"].open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(model_data, stream, ensure_ascii=True, sort_keys=True, indent=2)
            stream.write("\n")
        for key, target in targets.items():
            os.replace(temporaries[key], target)
        return model_data
    except Exception:
        for temporary in temporaries.values():
            temporary.unlink(missing_ok=True)
        raise


def _print_progress(event: dict[str, Any]) -> None:
    print(
        f"[city-learning] {event['town']} district {event['district']}: "
        f"{event['tiles']} tiles, {event['templates']} building templates"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus", nargs="+", type=Path, help="rme-city-corpus v2 JSON input file(s)")
    parser.add_argument("--output", required=True, type=Path, help="directory for generated learning artifacts")
    parser.add_argument("--quiet", action="store_true", help="do not print progress for each district")
    arguments = parser.parse_args(argv)
    try:
        model = extract_corpora(
            arguments.corpus,
            arguments.output,
            progress=None if arguments.quiet else _print_progress,
        )
    except (CorpusError, OSError) as error:
        print(f"[city-learning] error: {error}", file=sys.stderr)
        return 1
    summary = model["summary"]
    print(
        f"[city-learning] wrote {summary['cityCount']} cities, "
        f"{summary['districtCount']} districts and "
        f"{summary['buildingTemplateCount']} building templates to {arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
