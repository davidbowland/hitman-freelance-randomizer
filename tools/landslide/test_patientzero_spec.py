#!/usr/bin/env python3
"""Self-test of the full Patient Zero's spec (tools/landslide/patientzero_spec.py) without game data: its resource ids
against the path formula, its never-targets (names, repository and entity ids) against the mod's
(src/core/LandslideFull.cpp), its pool helper against src/core/SceneKinds.cpp's, its tables'
shape (the 42 slots, the story switch loaded by our override, the starts, the exits):
python3 -I tools/landslide/test_patientzero_spec.py (build_bricks.py checks the tables against the bricks)"""
import importlib.util
import os
import re
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import build_bricks as bb  # noqa: E402
import gen_package_inc as gp  # noqa: E402
import patientzero_spec as spec  # noqa: E402


def read(rel):
    with open(os.path.join(REPO, rel), encoding="utf-8") as f:
        return f.read()


class PatientZeroSpec(unittest.TestCase):
    def test_new_ids_follow_the_path_formula(self):
        self.assertEqual(gp.rid("[%s].pc_entitytype" % spec.MISSION_PATH), spec.MISSION_IDS[0])
        self.assertEqual(gp.rid("[%s].pc_entityblueprint" % spec.MISSION_PATH), spec.MISSION_IDS[1])
        self.assertEqual(gp.rid("[%s].pc_entitytype" % spec.OVERRIDE_PATH), spec.OVERRIDE_IDS[0])
        self.assertEqual(gp.rid("[%s].pc_entityblueprint" % spec.OVERRIDE_PATH), spec.OVERRIDE_IDS[1])

    def test_vanilla_ids_follow_the_path_formula(self):
        self.assertEqual(gp.rid("[%s].pc_entitytemplate" % spec.SCENE_PATH), spec.SCENE)
        for (temp, tblu), name in zip((spec.SRC_MISSION, spec.SRC_OVERRIDE), spec.SRC_NAMES):
            self.assertEqual(gp.rid("[%s%s.brick].pc_entitytype" % (spec.HOKKAIDO, name)), temp)
            self.assertEqual(gp.rid("[%s%s.brick].pc_entityblueprint" % (spec.HOKKAIDO, name)), tblu)
        self.assertEqual(gp.rid("[%smission_flu.brick].pc_entitytype" % spec.HOKKAIDO), spec.MISSION_FLU[0])
        self.assertEqual(gp.rid("[%s_scene_flu.navp].pc_navp" % spec.HOKKAIDO), spec.NAVMESH[0])

    def test_partition_and_mod_entry(self):
        self.assertEqual(spec.PARTITION, (22, "hokkaido"))
        cpp = read("src/core/LandslideFull.cpp")
        entry = cpp[cpp.index("constexpr FullVariant kPatientZeroFull"):]
        entry = entry[:entry.index("};")]
        self.assertIn('"%s"' % spec.NAME, entry)
        self.assertRegex(entry, r'\b%d,\s*"%s"' % spec.PARTITION)
        header = read("src/core/LandslideFull.h")
        self.assertIn('kPatientZeroScene = "%s"' % spec.SCENE_PATH, header)
        self.assertIn('"%smission_mild_shogi.brick"' % spec.HOKKAIDO, header)

    def test_never_targets_match_the_mod(self):
        cpp = read("src/core/LandslideFull.cpp")
        block = cpp[cpp.index("kPatientZeroNeverTargets {{"):]
        block = block[:block.index("}};")]
        mod = re.findall(r'\{"([^"]+)", "([0-9a-f-]{36})", 0x([0-9A-F]{16})ull\}', block)
        self.assertEqual([(name, repo, "%016X" % eid) for eid, repo, name in spec.NEVER_TARGETS], mod)

    def test_pool_helper_matches_the_known_helpers(self):
        scenes = read("src/core/SceneKinds.cpp")
        for brick in (spec.SRC_MISSION[0], spec.MISSION_IDS[0]):
            m = re.search(r"\{0x%016X, 0x([0-9a-f]{16})\}" % brick, scenes)
            self.assertIsNotNone(m, "%016X not in kKnownHelpers" % brick)
            self.assertEqual(int(m.group(1), 16), spec.SPAWN_TARGETS)

    def test_built(self):
        # In build_bricks' variants, in Hokkaido's package, with every table the bricks' decode filled in.
        self.assertIn(spec, bb.VARIANTS)
        self.assertEqual(bb.partition(spec), (22, "hokkaido"))
        self.assertIsNotNone(spec.TERRITORY)
        self.assertIsNotNone(spec.DEFAULT_HERO_SPAWN)
        self.assertIsNotNone(spec.MISSION_ANY_EXIT)
        self.assertTrue(all(entity for entity, _, _ in spec.NEVER_TARGETS))
        self.assertEqual(sorted(spec.NEVER_IN_POOL), sorted(e for e, _, _ in spec.NEVER_TARGETS))

    def test_pool_shape(self):
        # The 42 slots: the dedicated targets, the kept hotel actors and the outbreak's people; at least a third the
        # outbreak's own; none listed twice, none a never-target.
        kept = len(spec.KEEP_LOCAL_TARGETS) + len(spec.KEEP_SCENE_TARGETS)
        self.assertEqual(kept + len(spec.POOL_ADDITIONS), spec.POOL_SIZE)
        outbreak = [a for a in spec.POOL_ADDITIONS if a[0] == "base"]
        self.assertGreaterEqual(len(outbreak) * 3, spec.POOL_SIZE)
        ids = spec.KEEP_LOCAL_TARGETS + [e for _, e in spec.KEEP_SCENE_TARGETS] + [a[1] for a in spec.POOL_ADDITIONS]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertFalse(set(ids) & set(spec.NEVER_IN_POOL))
        self.assertEqual(set(spec.KEEP_SCENE_NAMES), {e for _, e in spec.KEEP_SCENE_TARGETS})
        for key, _ in spec.KEEP_SCENE_TARGETS:
            self.assertIn(key, spec.POOL_BRICKS)

    def test_story_switch(self):
        # IOI's Contracts-mode switch is loaded by our override, not appended to the contract: the swap stays one
        # brick (the package's tables and the mod's swap expect that).
        pair, deletes = spec.STORY_SWITCH
        self.assertEqual(pair, spec.CONTRACTCREATION_FLU)
        self.assertIn(pair[0], spec.OVERRIDE_SCENES)
        self.assertNotIn(pair[0], spec.MISSION_SCENES)
        self.assertEqual(sorted(deletes.values()), ["Vaccine", "Virus"])
        self.assertEqual(spec.APPEND_BRICKS, [])
        prints = {h for h, _ in spec.FINGERPRINTS}
        self.assertIn(spec.CONTRACTCREATION_FLU[0], prints)
        self.assertIn(spec.OVERRIDE_CONSTANTJEFF[0], prints)
        self.assertEqual(gp.rid("[%scontractcreation_flu.brick].pc_entitytype" % spec.HOKKAIDO), pair[0])

    def test_starts_and_exits(self):
        # Nothing deleted or moved: every Freelancer place stays (the decode found them all on the outbreak's map).
        self.assertEqual(spec.EXIT_MOVES, {})
        self.assertEqual(spec.DELETE_EXITS, {})
        self.assertEqual(len(spec.UNMOVED_EXITS), 5)
        self.assertEqual(spec.SLOT_REPOINTS, {})
        self.assertEqual(spec.DELETE_HERO_SPAWNS, {})
        self.assertIn(spec.MISSION_FLU[0], spec.HERO_SPAWN_REQUIRED)
        self.assertTrue(spec.SPOT_BLOCKER_CHECK)
        for _, (below, why) in spec.KEPT_RAISED.items():
            self.assertLessEqual(below, 2.0)
            self.assertTrue(why)

    def test_suit_not_globally_illegal(self):
        # mission_flu's Hero_IsFluStartingOutfit (any 47 suit) is re-pointed at location's ninja suit, the same
        # profession location's Hero_IsNinja checks: both bricks are our override's external scenes.
        self.assertEqual(len(spec.PROPERTY_OVERRIDES), 1)
        ov = spec.PROPERTY_OVERRIDES[0]
        self.assertEqual(ov["owner"], (spec.MISSION_FLU[0], 0x581AB55C286067A1, "Hero_IsFluStartingOutfit"))
        self.assertEqual((ov["property"], ov["type"]), ("m_rRequirements", "SEntityTemplateReference"))
        self.assertEqual(ov["was"], (spec.MISSION_FLU[0], 0x37EBC9610259A196, "Prof_Agent47"))
        self.assertEqual(ov["value"], (spec.LOCATION, 0x2253E821FAD8F621, "Prof_Hero_NinjaSuit"))
        self.assertTrue(ov["why"])
        for scene in (ov["owner"][0], ov["value"][0]):
            self.assertIn(scene, spec.OVERRIDE_SCENES)
        self.assertIn(spec.LOCATION, [pair[0] for _, pair in spec.PARENT_BRICKS])

    def test_apply_property_overrides(self):
        # On a synthetic owner brick: the old value is checked, the override lands on our copy.
        import brick_edit as be
        import test_brick_edit as tbe
        own, ours, loc = 0x00BB, tbe.sample(), 0x00AA
        src = tbe.sample()  # its Keeper (0x33) m_rTarget names its local Sibling (0x44)

        class Spec:
            PROPERTY_OVERRIDES = [{"owner": (own, 0x33, "Keeper"), "property": "m_rTarget",
                                   "type": "SEntityTemplateReference", "was": (own, 0x44, "Sibling"),
                                   "value": (loc, 0x76, "Ninja"), "why": "test"}]
        audit = []
        bb.apply_property_overrides(Spec, ours, lambda h: src if h == own else None, audit)
        p = ours.temp["propertyOverrides"][-1]
        self.assertEqual(p["propertyOwner"], be.ext_ref(ours.scene_index(own), 0x33))
        self.assertEqual(p["propertyValue"]["value"]["$val"], be.ext_ref(ours.scene_index(loc), 0x76))
        self.assertIn("not checked", audit[-1])
        Spec.PROPERTY_OVERRIDES[0]["was"] = (own, 0x22, "Leaf")
        with self.assertRaises(SystemExit):
            bb.apply_property_overrides(Spec, tbe.sample(), lambda h: src if h == own else None, [])

    def test_fingerprints(self):
        hashes = [h for h, _ in spec.FINGERPRINTS]
        self.assertEqual(len(hashes), len(set(hashes)))
        self.assertNotIn(spec.MISSION_IDS[0], hashes)

    def test_the_morgue_stash_moves_and_the_crate_check_runs(self):
        # Spawnable_15 faced ea_flu's morgue table (build_bricks.check_crates): it moves 2.5 m south.
        self.assertEqual(set(spec.SPOT_MOVES), {0x9A5FCFBB55E00D63})
        m = spec.SPOT_MOVES[0x9A5FCFBB55E00D63]
        self.assertEqual((m["kind"], m.get("yaw")), ("stash", None))
        # Freelancer's Hokkaido navmesh is the normal level's; the outbreak's is fingerprinted (the place needs it).
        self.assertEqual(spec.FREELANCER_NAVMESH, spec.COMPARE_NAVMESH)
        self.assertIn(spec.NAVMESH[0], [h for h, _ in spec.FINGERPRINTS])


if __name__ == "__main__":
    unittest.main()
