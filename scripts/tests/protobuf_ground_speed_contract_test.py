#!/usr/bin/env python3
"""Read-only regression checks for ground speed initialization and bank bounds."""
import ast
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ProtobufGroundSpeedContractTest(unittest.TestCase):
    def test_sprite_constructor_initializes_ground_speed(self):
        source = (ROOT / 'source/graphics.cpp').read_text(encoding='utf-8')
        constructor = source.split('GameSprite::GameSprite() :', 1)[1].split('{', 1)[0]
        self.assertRegex(constructor, r'\bground_speed\(0\)')

    def test_protobuf_bank_speed_is_defined_and_cannot_wrap(self):
        source = (ROOT / 'source/items.cpp').read_text(encoding='utf-8')
        expression = re.search(r't->sprite->ground_speed\s*=\s*([^;]+);', source)
        self.assertIsNotNone(expression, 'Protobuf loader must set ground speed')
        match = re.fullmatch(
            r'object\.flags\(\)\.has_bank\(\)\s*\?\s*static_cast<uint16_t>\((.+)\)\s*:\s*0',
            expression.group(1))
        self.assertIsNotNone(match, 'Absent bank must produce zero')
        expression = match.group(1).replace('std::min<uint32_t>', 'minimum')
        expression = expression.replace('object.flags().bank().waypoints()', 'waypoints')
        expression = expression.replace('std::numeric_limits<uint16_t>::max()', '65535')
        tree = ast.parse(expression, mode='eval')
        allowed = (ast.Expression, ast.Call, ast.Name, ast.Load, ast.Constant)
        self.assertTrue(all(isinstance(node, allowed) for node in ast.walk(tree)))
        for value, expected in ((0, 0), (100, 100), (65535, 65535),
                                (65536, 65535), (100000, 65535), (4294967295, 65535)):
            with self.subTest(waypoints=value):
                self.assertEqual(eval(compile(tree, '<source bank speed>', 'eval'),
                    {'__builtins__': {}, 'minimum': min}, {'waypoints': value}), expected)

    def test_legacy_dat_speed_assignment_remains(self):
        source = (ROOT / 'source/graphics.cpp').read_text(encoding='utf-8')
        self.assertIn('sType->ground_speed = speed;', source)


if __name__ == '__main__':
    unittest.main()
