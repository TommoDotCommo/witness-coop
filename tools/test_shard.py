#!/usr/bin/env python3
"""Game-free tests for the parallel suite's shard plan, shard count and summary (tools/shard.py). python3 tools/test_shard.py"""
import json, os, tempfile, unittest
from shard import plan, shards_that_fit, parse_times, summary, suite_names, load_durations, DEFAULT_TABLE

class PlanTests(unittest.TestCase):
    def test_longest_first_beats_round_robin(self):
        names = ['a', 'b', 'c', 'd', 'e', 'f']; d = dict(a=300, b=100, c=290, d=100, e=110, f=100)
        bins = plan(names, d, 2); loads = [sum(d[x] for x in b) for b in bins]
        self.assertEqual(sorted(loads), [500, 500])
        rr = [names[k::2] for k in range(2)]      # the old k % n split: a, c, e on one shard
        self.assertEqual(max(sum(d[x] for x in b) for b in rr), 700)
    def test_every_scenario_exactly_once_and_deterministic(self):
        names = suite_names(); d = load_durations()
        self.assertGreater(len(names), 20)
        for n in (1, 2, 3, 4, 7):
            bins = plan(names, d, n)
            self.assertEqual(len(bins), n)
            self.assertEqual(sorted(x for b in bins for x in b), sorted(names))
            self.assertEqual(bins, plan(list(names), dict(d), n))
            for b in bins: self.assertEqual(b, [x for x in names if x in b])      # suite order inside a shard
    def test_greedy_bound(self):
        names = suite_names(); d = load_durations()
        for n in (2, 3, 4):
            loads = [sum(d.get(x, 120) for x in b) for b in plan(names, d, n)]
            self.assertLessEqual(max(loads) - min(loads), max(d.values()))      # longest-first never leaves a gap above one scenario
    def test_names_select_like_the_suite(self):
        self.assertEqual(suite_names(['baseline']), ['baseline'])
        self.assertEqual(len(suite_names(['late join'])), 2)      # the scenario and its mutant
        self.assertEqual(suite_names(['no such scenario']), [])
    def test_unknown_scenarios_use_the_fallback(self):
        self.assertEqual(sorted(len(b) for b in plan(['x', 'y', 'z', 'w'], {}, 2)), [2, 2])
    def test_table_covers_the_suite(self):
        table = json.load(open(DEFAULT_TABLE))
        self.assertEqual(sorted(k for k in table if not k.startswith('_')), sorted(suite_names()))

STATUS = 'RAM free %s / 31.1 GB\nslot  0  x expires in 40 min\n%d of 16 slots free\ninput lock: free\n'

class CountTests(unittest.TestCase):
    def test_ram_and_slots_limit(self):
        self.assertEqual(shards_that_fit(STATUS % ('17.0', 16), 4), 4)
        self.assertEqual(shards_that_fit(STATUS % ('17.0', 16), 3), 3)      # QA_SHARDS_MAX
        self.assertEqual(shards_that_fit(STATUS % ('9.9', 16), 4), 2)       # about 4 GB per shard
        self.assertEqual(shards_that_fit(STATUS % ('30.0', 5), 4), 2)       # two slots per shard
        self.assertEqual(shards_that_fit(STATUS % ('3.9', 16), 4), 0)
        self.assertEqual(shards_that_fit(STATUS % ('20.0', 1), 4), 0)
    def test_provider_floor_is_kept_free(self):
        self.assertEqual(shards_that_fit('RAM free 8.2 / 31.1 GB (a new slot is refused below 3.0 GB)\n10 of 16 slots free', 4), 1)
        self.assertEqual(shards_that_fit('RAM free 13.7 / 31.1 GB (a new slot is refused below 3.0 GB)\n10 of 16 slots free', 4), 2)
        self.assertEqual(shards_that_fit('RAM free 6.9 / 31.1 GB (a new slot is refused below 3.0 GB)\n10 of 16 slots free', 4), 0)
    def test_unreadable_status_waits(self):
        self.assertEqual(shards_that_fit('provider offline', 4), 0)

class SummaryTests(unittest.TestCase):
    def test_table_missing_and_durations(self):
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, 'shard1.log'), 'w').write('SHARD\t1/2\tbaseline; late join\n=== baseline\nTIME\tbaseline\t140\tPASS\nTraceback\n')
            open(os.path.join(d, 'shard2.log'), 'w').write('SHARD\t2/2\tMUTANT: x + y\nTIME\tMUTANT: x + y\t200\tFAIL\n')
            open(os.path.join(d, 'shard10.log'), 'w').write('SHARD\t10/10\t\n')
            text, rows = summary(d, 260)
            self.assertEqual(rows, [('baseline', 1, 140.0, 'PASS'), ('late join', 1, 0.0, 'NOT RUN'), ('MUTANT: x + y', 2, 200.0, 'FAIL')])
            self.assertIn('wall time: 260 s', text)
            self.assertEqual(json.load(open(os.path.join(d, 'durations.json'))), {'baseline': 140, 'MUTANT: x + y': 200})
    def test_parse_times_ignores_other_lines(self):
        self.assertEqual(parse_times('TIME\tbaseline\tabc\tPASS\nRESULT: PASS\nTIME\ta\t1\tPASS'), [('a', 1.0, 'PASS')])

if __name__ == '__main__':
    unittest.main()
