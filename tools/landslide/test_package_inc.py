#!/usr/bin/env python3
"""Self-test of gen_package_inc.py's scene rules for the full variants (scene_problem): scene swaps own their scene,
layers share their base's, and several appending variants may share a contract scene (no game data):
python3 -I tools/landslide/test_package_inc.py"""
import json
import os
import sys

sys.dont_write_bytecode = True
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_package_inc as gp  # noqa: E402

M = "assembly:/_pro/scenes/missions/"
SANTA_FORTUNA = M + "colombia/scene_hippo_mild_trompo.entity"
DARTMOOR = M + "ancestral/scene_ancestral_mild_bridge.entity"
MIAMI = M + "miami/scene_flamingo_mild_pinochle.entity"
HOKKAIDO_MILD = M + "hokkaido/scene_snowcrane_mild_shogi.entity"
FLU = M + "hokkaido/_scene_flu.entity"
COPPERHEAD = M + "coastaltown/_scene_copperhead.entity"
PYTHON = M + "marrakesh/_scene_python.entity"


def swap(name, scene):
    return {"name": name, "scene": scene}


def layer(name, base, scene):
    return {"name": name, "scene": scene, "layerOf": base}


def append(name, scene):
    return {"name": name, "scene": scene, "appends": True}


# The package's variants as the manifest has them today.
TODAY = [
    swap("landslide", M + "coastaltown/_scene_mamba.entity"), swap("author", M + "coastaltown/scene_ebola.entity"),
    swap("icon", COPPERHEAD), swap("marrakesh", PYTHON), layer("badboy", "icon", COPPERHEAD),
    swap("patientzero", FLU), layer("entertainer", "marrakesh", PYTHON),
    append("gluttony", M + "wet/scene_wet_mild_mahjong.entity"),
    append("revolutionary", SANTA_FORTUNA), append("serpent", SANTA_FORTUNA), append("reflection", HOKKAIDO_MILD),
    append("gardenshow", DARTMOOR), append("sloth", DARTMOOR), append("illusions", M + "mumbai/scene_mongoose_mild_ludo.entity"),
    append("undying", MIAMI), append("undyingreturns", MIAMI), append("banker", M + "paris/scene_peacock_mild_craps.entity"),
]


class SceneRules(unittest.TestCase):
    def test_todays_variants(self):
        self.assertIsNone(gp.scene_problem(TODAY))

    def test_appending_variants_share_a_contract_scene(self):
        # The two Santa Fortuna fixes, the two Dartmoor ones and the two Miami ones: one option per load.
        self.assertIsNone(gp.scene_problem([append("revolutionary", SANTA_FORTUNA), append("serpent", SANTA_FORTUNA),
                                            append("third", SANTA_FORTUNA.upper())]))

    def test_an_append_beside_a_swap_on_its_map(self):
        # The Reflection appends to mild Hokkaido; Patient Zero swaps in the outbreak's own scene.
        self.assertIsNone(gp.scene_problem([swap("patientzero", FLU), append("reflection", HOKKAIDO_MILD)]))

    def test_two_swaps_of_one_scene(self):
        problem = gp.scene_problem([swap("patientzero", FLU), swap("other", FLU.upper())])
        self.assertIn("other: patientzero swaps in", problem)

    def test_a_layer_shares_its_base_scene(self):
        self.assertIsNone(gp.scene_problem([swap("icon", COPPERHEAD), layer("badboy", "icon", COPPERHEAD)]))

    def test_an_append_to_a_swapped_scene(self):
        # The scene would name two variants (FullVariantOfScene answers the swap).
        for order in ([swap("patientzero", FLU), append("x", FLU)], [append("x", FLU), swap("patientzero", FLU)]):
            self.assertIn("x: appends to %s, which patientzero swaps in" % FLU, gp.scene_problem(order))

    def test_names_are_unique(self):
        self.assertIn("serpent: two variants have this name",
                      gp.scene_problem([append("serpent", SANTA_FORTUNA), append("serpent", DARTMOOR)]))

    def test_a_layer_does_not_append(self):
        bad = dict(layer("badboy", "icon", COPPERHEAD), appends=True)
        self.assertIn("a layer cannot append", gp.scene_problem([swap("icon", COPPERHEAD), bad]))

    def test_the_built_manifest(self):
        # The last build's manifest, when there is one (build-landslide/ is local and gitignored).
        path = os.path.join(gp.REPO, "build-landslide", "built", "package_bricks.json")
        if not os.path.exists(path):
            self.skipTest("no build-landslide/built/package_bricks.json")
        with open(path) as f:
            self.assertIsNone(gp.scene_problem(json.load(f)["variants"]))


if __name__ == "__main__":
    unittest.main()
