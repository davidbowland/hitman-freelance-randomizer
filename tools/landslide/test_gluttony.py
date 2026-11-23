#!/usr/bin/env python3
"""Self-test of the full Gluttony: gluttony_spec.py's tables, gluttony_audit.py's clash audit and delete checks, and
build_bricks.build_fix (the fix brick), on small synthetic bricks (no game data):
python3 -I tools/landslide/test_gluttony.py"""
import argparse
import contextlib
import io
import json
import os
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import gen_package_inc as gp  # noqa: E402
import gluttony_audit as ga  # noqa: E402
import gluttony_spec as spec  # noqa: E402

ROOT = 0xFFFFFFFFFFFFFFFE
RAT, AZALEA = spec.SCENARIO_RAT[0], spec.SCENARIO_AZALEA[0]
TRUCK, CAN, TABLE, PROPS = 0x9249724A74D21096, 0x0C01, 0x0C02, 0x0C03
TENT, CRATE_09, CRATE_12, PLATE = 0x0A01, 0x9AE30CE99EBB8658, 0xEFA0B03F512AA326, 0x0A04
CLUE, ACT = 0x0B01, 0x0B02
TYPES = {
    0x10: "[modules:/zentity.class].pc_entitytype",
    0x11: "[assembly:/_pro/vehicles/templates/vehicles_a.template?/old_truck_a.entitytemplate].pc_entitytype",
    0x12: "[assembly:/_pro/environment/templates/props/containers/c.template?/military_can_plastic_a_00"
          ".entitytemplate].pc_entitytype",
    0x13: "[assembly:/_pro/environment/templates/props/street_props/s.template?/canopy_tent_b.entitytemplate]"
          ".pc_entitytype",
    0x14: "[assembly:/_pro/environment/templates/props/containers/k.template?/container_cardboard_box_b_beer_00"
          ".entitytemplate].pc_entitytype",
    0x15: "[assembly:/_pro/environment/templates/props/food/f.template?/food_dirty_plate_a.entitytemplate]"
          ".pc_entitytype",
    0x16: "[assembly:/_pro/design/setpieces/evergreen.template?/setpiece_evergreen_scannable_safeclue_tablet_unique"
          ".entitytemplate].pc_entitytype",
    0x17: "[assembly:/_pro/design/actor/acts/mr.template?/act_mr_stand_neutral.entitytemplate].pc_entitytype",
    0x18: "[assembly:/_pro/environment/templates/props/furniture/t.template?/table_sofa_modern_a.entitytemplate]"
          ".pc_entitytype",
}
TYPE_INDEX = {h: k for k, h in enumerate(TYPES)}


def matrix(x, y, z):
    return {"XAxis": {"x": 1.0, "y": 0.0, "z": 0.0}, "YAxis": {"x": 0.0, "y": 1.0, "z": 0.0},
            "ZAxis": {"x": 0.0, "y": 0.0, "z": 1.0}, "Trans": {"x": x, "y": y, "z": z}}


def prop(name, value, typ="SEntityTemplateReference"):
    return {"nPropertyID": name, "value": {"$type": typ, "$val": value}}


def at(x, y, z, parent=None):
    """An entity's place: world coordinates under the brick root, or local ones under a local parent index."""
    return [prop("m_mTransform", matrix(x, y, z), "SMatrix43"),
            prop("m_eidParent", be.local_ref(0 if parent is None else parent))]


def brick(name, h, ents, scenes=(), deletes=(), overrides=()):
    """An audit Brick: ents are (id, name, type hash, properties, logical parent index or None for the root's)."""
    ents = [(ROOT, "Scene", 0x10, [], -1)] + list(ents)
    temps, tblus = [], []
    for eid, ename, typ, props, parent in ents:
        lp = be.local_ref(-1 if parent == -1 else (0 if parent is None else parent))
        temps.append({"logicalParent": lp, "entityTypeResourceIndex": TYPE_INDEX[typ], "propertyValues": props,
                      "postInitPropertyValues": [], "platformSpecificPropertyValues": []})
        tblus.append({"logicalParent": dict(lp), "entityTypeResourceIndex": 0, "entityId": eid, "editorOnly": False,
                      "entityName": ename, "propertyAliases": [], "exposedEntities": [], "exposedInterfaces": [],
                      "entitySubsets": []})
    n = len(TYPES)
    temp = {"subType": 2, "blueprintIndexInResourceHeader": n, "rootEntityIndex": 0, "subEntities": temps,
            "propertyOverrides": list(overrides),
            "externalSceneTypeIndicesInResourceHeader": list(range(n + 1, n + 1 + len(scenes)))}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": tblus,
            "externalSceneTypeIndicesInResourceHeader": list(range(1, 1 + len(scenes))),
            "pinConnections": [], "inputPinForwardings": [], "outputPinForwardings": [],
            "overrideDeletes": [be.ext_ref(list(scenes).index(s), e) for s, e in deletes],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    temp_refs = [(0x1F, t) for t in TYPES] + [(0x1F, h + 1)] + [(0x1F, s) for s in scenes]
    tblu_refs = [(0x1F, 0x99)] + [(0x1F, s) for s in scenes]
    return ga.Brick(name, h, temp, tblu, temp_refs, tblu_refs)


def bricks(clue_names_truck=False):
    """The spec's READS as tiny stand-ins: the truck (with a can on it) under the set's tent, the set's crate on
    Freelancer's clue, a plate (a small item) on Freelancer's act."""
    rat = brick("scenario_rat", RAT, [
        (PROPS, "Props", 0x10, [], None),
        (TRUCK, "Old_Truck_A_01", 0x11, at(-85.0, 70.0, 96.9), 1),
        (CAN, "Military_Can_Plastic_A_00", 0x12, at(0.2, 2.0, 0.8, 2), 2),
        (TABLE, "table_sofa_modern_a_04", 0x18, at(-96.3, 49.7, 101.0), 1),
    ])
    azalea = brick("scenario_azalea", AZALEA, [
        (TENT, "canopy_tent_b_02", 0x13, at(-85.3, 69.9, 96.9), None),
        (CRATE_09, "Container_Cardboard_Box_B_Beer_00_09", 0x14, at(-96.0, 49.24, 101.0), None),
        (CRATE_12, "Container_Cardboard_Box_B_Beer_00_12", 0x14, at(-96.26, 49.06, 101.48), None),
        (PLATE, "food_dirty_plate_a", 0x15, at(-40.0, 9.3, 97.0), None),
    ], scenes=[RAT])
    commentary = brick("commentary_rat", spec.COMMENTARY_RAT[0], [], scenes=[RAT])
    scene = brick("scene_wet_azalea", spec.SCENE_WET_AZALEA[0], [],
                  scenes=[RAT, spec.COMMENTARY_RAT[0], AZALEA], deletes=[(RAT, TRUCK)])
    clue_props = at(-96.1, 49.2, 101.5)
    if clue_names_truck:
        clue_props.append(prop("m_rHelper", be.ext_ref(0, CAN)))
    override = brick("override_mahjong", spec.OVERRIDE_MAHJONG[0], [
        (CLUE, "Setpiece_Evergreen_Scannable_SafeClue_Tablet_Unique_20", 0x16, clue_props, None),
        (ACT, "Act_MR_Stand_Inspect_100cm", 0x17, at(-40.0, 9.3, 97.0), None),
    ], scenes=[RAT])
    out = {"scenario_rat": rat, "scenario_azalea": azalea, "commentary_rat": commentary, "scene_wet_azalea": scene,
           "override_mahjong": override}
    for name, pair in spec.READS:
        if name not in out:
            out[name] = brick(name, pair[0], [], scenes=[RAT])
    return out


NAMES = {h: t for h, t in TYPES.items()}
DELETES = {(RAT, TRUCK), (AZALEA, CRATE_12)}


class SpecTest(unittest.TestCase):
    def test_ids(self):
        self.assertEqual(gp.rid("[%s].pc_entitytype" % spec.FIX_PATH), spec.FIX_IDS[0])
        self.assertEqual(gp.rid("[%s].pc_entityblueprint" % spec.FIX_PATH), spec.FIX_IDS[1])
        self.assertEqual(gp.rid("[%s].pc_entitytemplate" % spec.SCENE_PATH), spec.SCENE)
        for path, temp, _ in spec.APPEND_BRICKS:
            self.assertEqual(gp.rid("[%s].pc_entitytype" % path), temp)
        self.assertEqual(spec.PARTITION, (6, "wet"))
        self.assertTrue(spec.FIX_PATH.rsplit("/", 1)[-1].startswith("fr_"))

    def test_tables(self):
        names = [n for n, _ in spec.READS]
        self.assertEqual(len(set(names)), len(names))
        for n in spec.OFFICIAL_SCENE + spec.FREELANCER_BRICKS + [spec.BASE_NAME, spec.LAYER_NAME, spec.FIX_SOURCE_NAME]:
            self.assertIn(n, names)
        for _, bricks_ in spec.FREELANCER_SCENES:
            self.assertEqual(bricks_[0], spec.BASE_NAME)
            self.assertNotIn(spec.LAYER_NAME, bricks_)
        ids = [(h, e) for h, e, _, _ in spec.DELETES]
        self.assertEqual(len(set(ids)), len(ids))
        self.assertTrue(all(h in (RAT, AZALEA) for h, _ in ids))
        prints = {h for h, _ in spec.FINGERPRINTS}
        for _, pair in spec.READS:
            self.assertIn(pair[0], prints)  # every brick the checks read is fingerprinted
        self.assertIn(spec.FIX_SOURCE[0], prints)
        # The truck and the crate column (seven crates: the one on the clue and the six on top of it).
        self.assertIn((RAT, TRUCK), ids)
        self.assertEqual(sum(1 for h, _ in ids if h == AZALEA), 7)
        self.assertIn((AZALEA, CRATE_12), ids)
        self.assertNotIn((AZALEA, CRATE_09), ids)  # the crate under the clue stays


class AuditTest(unittest.TestCase):
    def test_clashes_without_the_fix(self):
        lines, clashes = ga.audit(bricks(), NAMES)
        kinds = {}
        for scene, kind, text in clashes:
            kinds.setdefault(scene, []).append((kind, text))
        self.assertEqual(sorted(kinds), sorted(n for n, _ in spec.FREELANCER_SCENES))
        for scene, found in kinds.items():
            texts = " | ".join(t for _, t in found)
            # The truck and the can on it, both deleted by IOI's Gluttony scene and left in place by Freelancer's.
            self.assertEqual(sum(1 for k, _ in found if k == "left in place"), 2, texts)
            self.assertIn("Old_Truck_A_01", texts)
            self.assertIn("Military_Can_Plastic_A_00", texts)
            self.assertIn("deleted by the Gluttony scene", texts)
            # Freelancer's clue inside the set's crate (0.2 m in plan, same height).
            self.assertEqual([t for k, t in found if k == "Freelancer's prop"][0].count("Tablet_Unique_20"), 1)
            self.assertIn("Container_Cardboard_Box_B_Beer_00_12", texts)
            self.assertEqual(len(found), 3, texts)
        # The plate on the act is a small item: no clash, not even a note (only solid props count).
        self.assertFalse(any("food_dirty_plate" in line for line in lines))

    def test_no_clash_with_the_fix(self):
        lines, clashes = ga.audit(bricks(), NAMES, DELETES)
        self.assertEqual(clashes, [])
        self.assertTrue(any("0 left in place, 0 Freelancer props on the set" in line for line in lines))

    def test_a_clue_on_a_crate_is_not_in_it(self):
        # With only the crate on the clue deleted, the clue lies on the one under it (0.5 m above its origin).
        _, clashes = ga.audit(bricks(), NAMES, {(RAT, TRUCK), (AZALEA, CRATE_12)})
        self.assertFalse(any("Beer_00_09" in text for _, _, text in clashes))

    def test_scene_positions(self):
        read = bricks()
        s = ga.Scene([read["scenario_rat"]])
        rat = read["scenario_rat"]
        p = s.pos(rat, rat.ids().index(CAN))  # local to the truck
        self.assertAlmostEqual(p[0], -84.8)
        self.assertAlmostEqual(p[1], 72.0)
        self.assertAlmostEqual(p[2], 97.7)
        official = ga.Scene([read[n] for n in spec.OFFICIAL_SCENE])
        self.assertTrue(official.is_gone(rat, rat.ids().index(CAN)))  # under the deleted truck
        self.assertFalse(official.is_gone(rat, rat.ids().index(TABLE)))


class DeletesTest(unittest.TestCase):
    def test_spec_like_deletes(self):
        deletes = [(RAT, TRUCK, "Old_Truck_A_01", "tent"), (AZALEA, CRATE_12, "Container_Cardboard_Box_B_Beer_00_12",
                                                                    "clue")]
        lines, problems = ga.check_deletes(bricks(), deletes)
        self.assertEqual(problems, [])
        self.assertTrue(any("Old_Truck_A_01" in line and "IOI's Gluttony scene deletes it too" in line
                            and "2 entities with it" in line for line in lines))

    def test_a_wrong_or_missing_entity(self):
        _, problems = ga.check_deletes(bricks(), [(RAT, TRUCK, "Old_Truck_B", "x"), (RAT, 0x777, "Gone", "x")])
        self.assertEqual(len(problems), 2)
        self.assertIn("not \"Old_Truck_B\"", problems[0])
        self.assertIn("not an entity of a read brick", problems[1])

    def test_named_by_freelancer(self):
        # A Freelancer brick naming anything under a delete stops the build.
        _, problems = ga.check_deletes(bricks(clue_names_truck=True), [(RAT, TRUCK, "Old_Truck_A_01", "x")])
        self.assertEqual(len(problems), 1)
        self.assertIn("override_mahjong", problems[0])
        self.assertIn("under the deleted \"Old_Truck_A_01\"", problems[0])

    def test_named_inside_its_own_brick(self):
        # A set entity the set itself names may go only when IOI's Gluttony scene deletes it too.
        read = bricks()
        az = read["scenario_azalea"]
        az.temp["subEntities"][1]["propertyValues"].append(prop("m_rHelper", be.local_ref(az.ids().index(CRATE_12))))
        _, problems = ga.check_deletes(read, [(AZALEA, CRATE_12, "Container_Cardboard_Box_B_Beer_00_12", "x")])
        self.assertEqual(len(problems), 1)
        self.assertIn("IOI's Gluttony scene does not delete it", problems[0])


class BuildFixTest(unittest.TestCase):
    """build_bricks.build_fix on the stand-ins: the fix brick is the source's root with the deletes."""

    def test_build(self):
        read = bricks()
        src = read[spec.FIX_SOURCE_NAME]
        # The source as ResourceTool JSON: a root with three children, three property overrides.
        source = brick(spec.FIX_SOURCE_NAME, spec.FIX_SOURCE[0], [
            (0x51, "Difficulty_Easy_Rat", 0x10, [], None), (0x52, "Disguises", 0x10, [], 1),
            (0x53, "Items", 0x10, [], 1)], scenes=[RAT],
            overrides=[{"propertyOwner": be.ext_ref(0, TABLE),
                        "propertyValue": prop("m_aEntities", [], "TArray<SEntityTemplateReference>")}] * 3)
        source.temp["blueprintIndexInResourceHeader"] = len(TYPES)
        temp_refs = [(0x1F, t) for t in TYPES] + [(0x1F, spec.FIX_SOURCE[1]), (0x1F, RAT)]
        tblu_refs = [(0x1F, 0x99), (0x1F, RAT)]
        newest = {h: {"type": "TEMP", "refs": [(0x1F, 0x42)]} for h, _ in spec.FINGERPRINTS}
        newest[spec.FIX_SOURCE[0]] = {"type": "TEMP", "refs": temp_refs}
        newest[spec.FIX_SOURCE[1]] = {"type": "TBLU", "refs": tblu_refs}
        for _, temp, tblu in spec.APPEND_BRICKS:
            newest[temp] = {"type": "TEMP", "refs": [(0x1F, tblu)]}
        newest[spec.SCENE] = {"type": "TEMP", "refs": []}
        with tempfile.TemporaryDirectory() as tmp:
            js = os.path.join(tmp, "json")
            out = os.path.join(tmp, "out")
            os.makedirs(js)
            os.makedirs(out)
            for h, typ, body in ((spec.FIX_SOURCE[0], "TEMP", source.temp), (spec.FIX_SOURCE[1], "TBLU", source.tblu)):
                with open(os.path.join(js, "chunk6_%016X.%s.json" % (h, typ)), "w") as f:
                    json.dump(body, f)
            # The audit's stand-ins hold every spec delete (the seven crates as copies of the one).
            az = read["scenario_azalea"]
            for h, eid, name, _ in spec.DELETES:
                if h == AZALEA and eid not in az.ids():
                    az.temp["subEntities"].append(dict(az.temp["subEntities"][3]))
                    az.tblu["subEntities"].append(dict(az.tblu["subEntities"][3], entityId=eid, entityName=name))
                    az._ids.append(eid)
                    az.P.append(ga.props(az.temp["subEntities"][-1]))
            args = argparse.Namespace(bin=[], navp=[], json=[js], audit=False)
            real = ga.read_all
            ga.read_all = lambda dirs, newest_: read
            try:
                entry = bb.build_fix(spec, args, newest, set(), NAMES, out)
            finally:
                ga.read_all = real
            with open(os.path.join(out, "%016X.TEMP.json" % spec.FIX_IDS[0])) as f:
                temp = json.load(f)
            with open(os.path.join(out, "%016X.TBLU.json" % spec.FIX_IDS[1])) as f:
                tblu = json.load(f)
        del src
        self.assertEqual(len(temp["subEntities"]), 1)
        self.assertEqual(temp["propertyOverrides"], [])
        self.assertEqual([e["entityName"] for e in tblu["subEntities"]], ["Scene"])
        # The source's scenario_rat, then scenario_azalea for the crates; the deletes into each.
        res = {r["type"]: r for r in entry["resources"]}
        temp_scenes = [int(res["TEMP"]["refs"][i][1], 16) for i in temp["externalSceneTypeIndicesInResourceHeader"]]
        self.assertEqual(temp_scenes, [RAT, AZALEA])
        tblu_scenes = [int(res["TBLU"]["refs"][i][1], 16) for i in tblu["externalSceneTypeIndicesInResourceHeader"]]
        self.assertEqual(tblu_scenes, [RAT, AZALEA])
        got = sorted((tblu_scenes[r["externalSceneIndex"]], r["entityID"]) for r in tblu["overrideDeletes"])
        self.assertEqual(got, sorted((h, e) for h, e, _, _ in spec.DELETES))
        # The blueprint is the built TBLU; the unused zentity type is dropped.
        self.assertEqual(int(res["TEMP"]["refs"][temp["blueprintIndexInResourceHeader"]][1], 16), spec.FIX_IDS[1])
        self.assertEqual(len(res["TEMP"]["refs"]), 4)
        self.assertEqual(entry["swapBricks"], [p for p, _, _ in spec.APPEND_BRICKS] + [spec.FIX_PATH])
        self.assertTrue(entry["appends"])
        self.assertEqual(entry["partition"], [6, "wet"])

    def test_a_clash_left_stops_the_build(self):
        read = bricks()
        args = argparse.Namespace(bin=[], navp=[], json=[], audit=False)
        real, real_deletes = ga.read_all, spec.DELETES
        ga.read_all = lambda dirs, newest_: read
        spec.DELETES = [(RAT, TRUCK, "Old_Truck_A_01", "tent")]  # the crate on the clue stays
        try:
            with self.assertRaises(SystemExit) as e, contextlib.redirect_stderr(io.StringIO()):
                bb.build_fix(spec, args, {}, set(), NAMES, tempfile.gettempdir())
        finally:
            ga.read_all, spec.DELETES = real, real_deletes
        self.assertIn("clashes left with the fix brick", str(e.exception))
        self.assertIn("Tablet_Unique_20", str(e.exception))


if __name__ == "__main__":
    unittest.main()
