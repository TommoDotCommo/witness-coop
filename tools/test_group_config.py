#!/usr/bin/env python3
"""Capacity and rejection tests for the user-facing group config generator."""
import unittest
from group_config import configs

class GroupConfigTests(unittest.TestCase):
    def test_full_capacity(self):
        roster = [dict(id=i, host='127.0.0.1', port=48000+i) for i in range(1,17)]
        result = configs(roster)
        self.assertEqual(len(result), 16)
        for p in roster:
            text = result[p['id']]
            self.assertEqual(text.count('\npeer='), 15)
            self.assertNotIn(f"peer={p['id']},", text)
            self.assertIn(f"listen={p['port']}\n", text)
            self.assertIn('save_dir=instance\n', text)
    def test_invalid_rosters(self):
        good = [dict(id=1,host='127.0.0.1',port=48001),dict(id=2,host='127.0.0.1',port=48002)]
        bad = [good[:1], good*9, [good[0],dict(good[1],id=1)], [good[0],dict(good[1],port=48001)],
               [good[0],dict(good[1],host='example.invalid')], [good[0],dict(good[1],port=65536)]]
        for roster in bad:
            with self.subTest(roster=roster), self.assertRaises(ValueError): configs(roster)

if __name__ == '__main__': unittest.main()
