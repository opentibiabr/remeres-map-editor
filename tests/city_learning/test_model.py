import unittest

from tools.city_learning.model import DistrictAccumulator, semantic_signature


class CityLearningModelTests(unittest.TestCase):
	def test_semantic_signature_orders_relevant_tags(self):
		tile = {
			"tags": ["house_tile", "protection_zone"],
			"ground": {"id": 100, "tags": ["item", "ground", "grass"]},
			"items": [
				{"id": 200, "tags": ["item", "door", "wall"]},
				{"id": 201, "tags": ["item", "roof"]},
			],
		}

		self.assertEqual(
			semantic_signature(tile),
			"house_tile|wall|door|roof|grass|protection_zone",
		)

	def test_layout_merges_adjacent_tiles_with_the_same_signature(self):
		rows = []
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 8, "name": "Thais"},
			district={"index": 1, "role": "main", "bounds": {"minX": 10, "maxX": 13, "minY": 20, "maxY": 20}},
			layout_sink=rows.append,
		)
		accumulator.add_tile({"position": {"x": 10, "y": 20, "z": 7}, "tags": ["grass"], "items": []})
		accumulator.add_tile({"position": {"x": 11, "y": 20, "z": 7}, "tags": ["grass"], "items": []})
		accumulator.add_tile({"position": {"x": 13, "y": 20, "z": 7}, "tags": ["water"], "items": []})

		profile, templates = accumulator.finalize()

		self.assertEqual(templates, [])
		self.assertEqual(profile["tileCount"], 3)
		self.assertEqual(
			rows,
			[{
				"source": "synthetic.json",
				"townId": 8,
				"districtIndex": 1,
				"z": 7,
				"y": 20,
				"runs": [[10, 2, "grass"], [13, 1, "water"]],
			}],
		)

	def test_anchor_house_produces_relative_multi_floor_template(self):
		accumulator = DistrictAccumulator(
			source="synthetic.json",
			town={"id": 8, "name": "Thais"},
			district={"index": 1, "role": "main", "bounds": {"minX": 9, "maxX": 15, "minY": 19, "maxY": 24}},
			layout_sink=lambda row: None,
		)
		accumulator.add_house({
			"id": 77,
			"name": "Harbour House",
			"townId": 8,
			"anchor": True,
			"exit": {"x": 9, "y": 20, "z": 7},
		})
		accumulator.add_house({"id": 78, "name": "Context Only", "townId": 8, "anchor": False})
		accumulator.add_tile({
			"position": {"x": 10, "y": 20, "z": 7},
			"houseId": 77,
			"tags": ["house_tile"],
			"items": [{"id": 300, "tags": ["item", "wall", "door"]}],
		})
		accumulator.add_tile({
			"position": {"x": 11, "y": 20, "z": 7},
			"houseId": 77,
			"tags": ["house_tile"],
			"items": [{"id": 301, "tags": ["item", "wall"]}],
		})
		accumulator.add_tile({
			"position": {"x": 10, "y": 20, "z": 6},
			"houseId": 77,
			"tags": ["house_tile"],
			"items": [{"id": 302, "tags": ["item", "roof"]}],
		})
		accumulator.add_tile({
			"position": {"x": 14, "y": 22, "z": 7},
			"houseId": 78,
			"tags": ["house_tile"],
			"items": [{"id": 303, "tags": ["item", "wall"]}],
		})

		_, templates = accumulator.finalize()

		self.assertEqual(len(templates), 1)
		template = templates[0]
		self.assertEqual(template["house"]["id"], 77)
		self.assertEqual(template["dimensions"], {"width": 2, "height": 1, "floorCount": 2, "tileCount": 3})
		self.assertEqual(
			template["footprint"],
			[
				{"z": 6, "rows": [[0, [[0, 1]]]]},
				{"z": 7, "rows": [[0, [[0, 2]]]]},
			],
		)
		self.assertEqual(template["tagCounts"], {"door": 1, "house_tile": 3, "roof": 1, "wall": 2})


if __name__ == "__main__":
	unittest.main()
