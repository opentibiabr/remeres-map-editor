import gzip
import json
import tempfile
import unittest
from pathlib import Path

from tools.city_learning.extract_city_learning import CorpusError, extract_corpora


class CityLearningExtractionTests(unittest.TestCase):
	def test_streams_corpus_to_model_buildings_and_layouts(self):
		corpus = {
			"format": "rme-city-corpus",
			"version": 2,
			"sourceMap": "synthetic.otbm",
			"mapVersion": 4,
			"cities": [{
				"town": {"id": 8, "name": "Thais", "templePosition": {"x": 10, "y": 10, "z": 7}},
				"districts": [{
					"index": 1,
					"role": "main",
					"confidence": "high",
					"inferenceMethod": "house_clusters_with_temple_anchor",
					"bounds": {"minX": 10, "maxX": 12, "minY": 20, "maxY": 20},
					"houses": [{
						"id": 77,
						"name": "House",
						"townId": 8,
						"anchor": True,
						"exit": {"x": 9, "y": 20, "z": 7},
					}],
					"tiles": [{
						"position": {"x": 10, "y": 20, "z": 7},
						"houseId": 77,
						"tags": ["house_tile"],
						"ground": {"id": 100, "tags": ["item", "ground", "floor"]},
						"items": [{"id": 200, "tags": ["item", "wall", "door"]}],
					}, {
						"position": {"x": 11, "y": 20, "z": 7},
						"tags": ["grass"],
						"items": [],
					}],
					"summary": {"tileCount": 2},
				}],
			}],
			"summary": {"cityCount": 1, "districtCount": 1},
		}
		with tempfile.TemporaryDirectory() as temporary_directory:
			root = Path(temporary_directory)
			input_path = root / "corpus.json"
			output_path = root / "learning"
			input_path.write_text(json.dumps(corpus), encoding="utf-8")

			model = extract_corpora([input_path], output_path)

			self.assertEqual(model["version"], 2)
			self.assertEqual(model["summary"]["sourceCount"], 1)
			self.assertEqual(model["summary"]["cityCount"], 1)
			self.assertEqual(model["summary"]["districtCount"], 1)
			self.assertEqual(model["summary"]["buildingTemplateCount"], 1)
			self.assertEqual(model["districts"][0]["tagCounts"]["door"], 1)
			saved_model = json.loads((output_path / "city-learning-model.json").read_text(encoding="utf-8"))
			self.assertEqual(saved_model, model)
			with gzip.open(output_path / "building-templates.jsonl.gz", "rt", encoding="utf-8") as stream:
				buildings = [json.loads(line) for line in stream]
			with gzip.open(output_path / "semantic-layouts.jsonl.gz", "rt", encoding="utf-8") as stream:
				layouts = [json.loads(line) for line in stream]
			self.assertEqual(buildings[0]["house"]["id"], 77)
			self.assertEqual(
				layouts[0]["runs"],
				[[10, 1, "house_tile|wall|door|floor"], [11, 1, "grass"]],
			)

	def test_rejects_an_incompatible_corpus_version(self):
		with tempfile.TemporaryDirectory() as temporary_directory:
			root = Path(temporary_directory)
			input_path = root / "corpus.json"
			input_path.write_text(json.dumps({"format": "rme-city-corpus", "version": 1}), encoding="utf-8")

			with self.assertRaisesRegex(CorpusError, "version 2"):
				extract_corpora([input_path], root / "learning")


if __name__ == "__main__":
	unittest.main()
