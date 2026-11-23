#!/usr/bin/env python3
"""Self-test of gen_package_inc.py's recipes (make_recipes and its checks) on synthetic bytes (no game data):
python3 -I tools/landslide/test_recipes.py"""
import os
import random
import sys

sys.dont_write_bytecode = True
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_package_inc as gp  # noqa: E402

W = gp.WINDOW


def noise(seed, size):
    return random.Random(seed).randbytes(size)


def rebuild(kept, literals, recipe, sources):
    mine, ops, _, _ = recipe
    return gp.replay(ops, [sources[kept[k]] for k in mine], literals)


class Fnv(unittest.TestCase):
    def test_pinned_with_the_cpp_one(self):
        # tests/test_rng.cpp pins the same value for fr::Fnv1a64.
        self.assertEqual(gp.fnv1a64(b"freelancer"), 0xEC246B4D3C3EC404)
        self.assertEqual(gp.fnv1a64(b""), gp.FNV_BASIS)


class Recipes(unittest.TestCase):
    def setUp(self):
        self.sources = {"a": noise(1, 4000), "b": noise(2, 3000), "c": noise(3, 2000)}

    def test_round_trip(self):
        a, b = self.sources["a"], self.sources["b"]
        target = a[100:600] + b"edit" + b[50:450] + noise(4, 20) + a[1000:1040] + b[2900:]
        kept, literals, recipes, pads = gp.make_recipes([(target, [["a", "b"], []], 1)], self.sources)
        self.assertEqual(rebuild(kept, literals, recipes[0], self.sources), target)
        self.assertEqual(kept, ["a", "b"])
        mine, ops, copied, literal = recipes[0]
        self.assertEqual(copied, 500 + 400 + 40 + 100)
        self.assertEqual(literal, 24)
        self.assertEqual((len(literals), pads), (24, 0))
        # Adjacent literal bytes are one op; every copy is at least a window long.
        self.assertEqual(sum(1 for op in ops if op[0] == gp.LITERAL), 2)
        self.assertTrue(all(op[3] >= W for op in ops if op[0] == gp.COPY))

    def test_unused_candidates_are_not_kept(self):
        target = self.sources["b"][:300]
        kept, _, recipes, _ = gp.make_recipes([(target, [["a", "b", "c"], []], 1)], self.sources)
        self.assertEqual(kept, ["b"])
        self.assertEqual(recipes[0][0], [0])

    def test_short_shared_runs_stay_literal(self):
        target = noise(5, 50) + self.sources["a"][:W - 1] + noise(6, 50)
        kept, literals, recipes, _ = gp.make_recipes([(target, [["a"], []], 1)], self.sources)
        self.assertEqual(kept, [])
        self.assertEqual(literals, target)
        self.assertEqual(rebuild(kept, literals, recipes[0], self.sources), target)

    def test_a_planted_source_run_fails_the_check(self):
        c = self.sources["c"]
        planted = noise(7, 40) + c[500:500 + W] + noise(8, 40)
        found = gp.source_runs(planted, self.sources)
        self.assertEqual(found[40], ("c", 500))
        self.assertEqual(len(found), 1)
        self.assertEqual(gp.source_runs(planted[:40 + W - 1] + planted[40 + W:], self.sources), {})

    def test_a_run_across_two_literal_runs_fails_the_check(self):
        c = self.sources["c"]
        first, second = noise(9, 30) + c[0:16], c[16:W] + noise(10, 30)
        self.assertEqual(gp.source_runs(first, self.sources), {})
        self.assertEqual(gp.source_runs(second, self.sources), {})
        self.assertEqual(gp.source_runs(first + second, self.sources), {30: ("c", 0)})

    def test_a_partition_candidate_is_copied_when_no_own_one_matches(self):
        # Resource 0 holds a run of c, resource 1's candidate in the same package: it copies it from there.
        a, c = self.sources["a"], self.sources["c"]
        one = a[:200] + noise(11, 40) + c[700:700 + 3 * W] + noise(12, 40)
        resources = [(one, [["a"], ["c"]], 1), (c[:300], [["c"], ["a"]], 1)]
        kept, literals, recipes, pads = gp.make_recipes(resources, self.sources)
        self.assertEqual(rebuild(kept, literals, recipes[0], self.sources), one)
        mine, ops, copied, literal = recipes[0]
        self.assertEqual([kept[k] for k in mine], ["a", "c"])
        self.assertIn((gp.COPY, 1, 700, 3 * W), ops)
        self.assertEqual((copied, literal, pads), (200 + 3 * W, 80, 0))
        # An earlier tier's match wins over a later tier's.
        both = c[:100]
        resources = [(both, [["c"], ["b"]], 1), (both, [["b"], ["c"]], 1)]
        kept, _, recipes, _ = gp.make_recipes(resources, dict(self.sources, b=c))
        self.assertEqual([kept[k] for k in recipes[0][0]], ["c"])
        self.assertEqual([kept[k] for k in recipes[1][0]], ["b"])

    def test_a_package_candidate_run_inside_one_literal_run_fails(self):
        # c is a candidate of the package that resource 0 may not copy from: no pad may hide the run.
        c = self.sources["c"]
        one = noise(11, 40) + c[700:700 + W] + noise(12, 40)
        with self.assertRaisesRegex(gp.RecipeError, "resource 0's literal bytes at 40 hold c's bytes at 700"):
            gp.make_recipes([(one, [["a"], []], 1), (c[:300], [["c"], []], 1)], self.sources)

    def test_a_run_of_another_packages_candidate_is_allowed(self):
        # c is a candidate of package 2 only: package 1's literals may hold its runs, and package 1 never copies it.
        a, c = self.sources["a"], self.sources["c"]
        one = noise(11, 40) + c[700:700 + 3 * W] + noise(12, 40)
        resources = [(one, [["a"], []], 1), (c[:300], [["c"], []], 2), (noise(16, 30) + a[:100], [["a"], []], 1)]
        kept, literals, recipes, pads = gp.make_recipes(resources, self.sources)
        self.assertEqual((kept, pads), (["c", "a"], 0))
        self.assertEqual(recipes[0][1], [(gp.LITERAL, 0, 0, len(one))])
        for (data, _, _), recipe in zip(resources, recipes):
            self.assertEqual(rebuild(kept, literals, recipe, self.sources), data)
        # Each package's literal runs are stored together.
        self.assertEqual(literals, one + noise(16, 30))

    def test_a_run_across_two_resources_literals_gets_a_pad(self):
        # Resource 0 ends with c[100:120] and resource 1 (package 2, whose candidate c is) starts with c[120:140]:
        # stored one after the other they hold c[100:140], so a pad no op reads separates them.
        c = self.sources["c"]
        one = noise(11, 40) + c[100:120]
        two = c[120:140] + noise(13, 60) + c[1500:1800]
        resources = [(one, [["a"], []], 1), (two, [["c"], []], 2)]
        kept, literals, recipes, pads = gp.make_recipes(resources, self.sources)
        self.assertEqual(pads, 1)
        self.assertEqual(gp.source_runs(literals, self.sources), {})
        for (data, _, _), recipe in zip(resources, recipes):
            self.assertEqual(rebuild(kept, literals, recipe, self.sources), data)
        self.assertEqual(sum(r[3] for r in recipes) + pads, len(literals))
        self.assertEqual(kept, ["c"])
        # The pad sits between the two resources' runs, never inside one resource's contiguous literals.
        self.assertEqual(recipes[1][1][0], (gp.LITERAL, 0, 61, 80))

    def test_runs_separated_by_a_copy_get_a_pad(self):
        # One resource's two literal runs with a copy between them are stored side by side: a pad may separate them.
        a, c = self.sources["a"], self.sources["c"]
        one = c[0:16] + a[0:100] + c[16:W] + noise(15, 20)
        kept, literals, recipes, pads = gp.make_recipes([(one, [["a"], []], 1), (c[:10], [["b"], []], 1)],
                                                         dict(self.sources, b=c))
        self.assertEqual(pads, 1)
        self.assertEqual(rebuild(kept, literals, recipes[0], self.sources), one)

    def test_deterministic(self):
        a, b, c = self.sources["a"], self.sources["b"], self.sources["c"]
        resources = [(a[:900] + b"x" * 5 + c[:200] + b[1000:1300], [["a", "b"], ["c"]], 1),
                     (c[300:800] + noise(14, 70) + a[10:400], [["c", "a"], []], 1)]
        first = gp.make_recipes(resources, self.sources)
        again = gp.make_recipes(resources, dict(reversed(list(self.sources.items()))))
        self.assertEqual(first, again)

    def test_a_missing_candidate_fails(self):
        with self.assertRaises(gp.RecipeError):
            gp.make_recipes([(b"x" * 100, [["a", "nowhere"], []], 1)], self.sources)


if __name__ == "__main__":
    unittest.main()
