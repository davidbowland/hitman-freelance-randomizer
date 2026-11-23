#!/usr/bin/env python3
"""Self-test of build_bricks.apply_spot_moves (the full Icon's moves) on a small synthetic override brick (no game
data): python3 -I tools/landslide/test_spot_moves.py"""
import contextlib
import io
import math
import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import navp  # noqa: E402
import placement as pl  # noqa: E402

# Type hashes of the synthetic brick, and their names (as the hash list gives them).
TYPES = {
    0x10: "[assembly:/templates/x.template?/scene.entitytemplate].pc_entitytype",
    0x11: "[assembly:/templates/x.template?/helper_evergreen_cpd_territorysetuprandomizer.entitytemplate].pc_entitytype",
    0x12: "[assembly:/templates/x.template?/setpiece_evergreen_container_items_safe_unique.entitytemplate].pc_entitytype",
    0x13: "[assembly:/templates/x.template?/setpiece_evergreen_scannable_safeclue_documents_unique.entitytemplate]"
          ".pc_entitytype",
    0x14: "[assembly:/templates/x.template?/setpiece_evergreen_moneymulesetup_unique.entitytemplate].pc_entitytype",
    0x15: "[assembly:/templates/acts.template?/act_mr_sit_ground_edge.entitytemplate].pc_entitytype",
    0x16: "[assembly:/templates/acts.template?/act_mr_stand_think_100cm.entitytemplate].pc_entitytype",
    0x17: "[assembly:/templates/x.template?/approachpathpoint.entitytemplate].pc_entitytype",
    0x18: "[assembly:/templates/x.template?/setpiece_evergreen_stash_unique.entitytemplate].pc_entitytype",
}
TYPE_INDEX = {h: k for k, h in enumerate(TYPES)}
TERRITORY = 0xA1
SAFE, CLUE, MULE, SIT_ACT, STAND_ACT, POINT = 0xB1, 0xB2, 0xC1, 0xC2, 0xC3, 0xC4


def matrix(x, y, z):
    return {"XAxis": {"x": 1.0, "y": 0.0, "z": 0.0}, "YAxis": {"x": 0.0, "y": 1.0, "z": 0.0},
            "ZAxis": {"x": 0.0, "y": 0.0, "z": 1.0}, "Trans": {"x": x, "y": y, "z": z}}


def prop(name, value, typ="SEntityTemplateReference"):
    return {"nPropertyID": name, "value": {"$type": typ, "$val": value}}


def placed(x, y, z):
    return [prop("m_mTransform", matrix(x, y, z), "SMatrix43"), prop("m_eidParent", be.local_ref(0))]


def sample():
    ents = [
        (0xFFFFFFFFFFFFFFFE, "Scene", 0x10, []),
        (TERRITORY, "Territory", 0x11, [prop("availableSafe01", be.local_ref(2)),
                                        prop("availableMule01", be.local_ref(4))]),
        (SAFE, "Safe", 0x12, placed(-60.0, 90.0, 5.0) + [prop("Clue01", be.local_ref(3))]),
        (CLUE, "Clue", 0x13, placed(-58.0, 92.0, 5.0)),
        (MULE, "Mule", 0x14, [prop("m_mTransform", matrix(-85.0, 112.0, 2.0), "SMatrix43"),
                              prop("acts", [be.local_ref(5), be.local_ref(6)], "TArray<SEntityTemplateReference>")]),
        (SIT_ACT, "Act_Sit", 0x15, placed(-78.0, 122.0, 2.0) +
         [prop("m_aApproachPath", [be.local_ref(7)], "TArray<SEntityTemplateReference>")]),
        (STAND_ACT, "Act_Stand", 0x16, placed(-58.0, 121.0, 4.0)),
        (POINT, "ApproachPathPoint", 0x17, placed(-80.0, 118.0, 2.0)),
    ]
    temps, tblus = [], []
    for eid, name, typ, props in ents:
        parent = be.local_ref(0) if eid != 0xFFFFFFFFFFFFFFFE else be.local_ref(-1)
        temps.append({"logicalParent": parent, "entityTypeResourceIndex": TYPE_INDEX[typ], "propertyValues": props,
                      "postInitPropertyValues": [], "platformSpecificPropertyValues": []})
        tblus.append({"logicalParent": dict(parent), "entityTypeResourceIndex": 0, "entityId": eid, "editorOnly": False,
                      "entityName": name, "propertyAliases": [], "exposedEntities": [], "exposedInterfaces": [],
                      "entitySubsets": []})
    n = len(TYPES)
    temp = {"subType": 2, "blueprintIndexInResourceHeader": n, "rootEntityIndex": 0, "subEntities": temps,
            "propertyOverrides": [], "externalSceneTypeIndicesInResourceHeader": []}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": tblus, "externalSceneTypeIndicesInResourceHeader": [],
            "pinConnections": [], "inputPinForwardings": [], "outputPinForwardings": [], "overrideDeletes": [],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    return be.Brick("override", temp, tblu, [(0x1F, h) for h in TYPES] + [(0x1F, 0x99)], [(0x1F, 0x98)])


def base():
    """The map's brick: two acts, the anchors."""
    ents = [(0xFFFFFFFFFFFFFFFE, "Scene", []), (0xD1, "Map act 1", placed(80.0, -20.0, 12.0)),
            (0xD2, "Map act 2", placed(38.0, -60.0, 12.8))]
    temp = {"subType": 2, "blueprintIndexInResourceHeader": 0, "rootEntityIndex": 0, "propertyOverrides": [],
            "externalSceneTypeIndicesInResourceHeader": [],
            "subEntities": [{"logicalParent": be.local_ref(-1), "entityTypeResourceIndex": 0, "propertyValues": p,
                             "postInitPropertyValues": [], "platformSpecificPropertyValues": []} for _, _, p in ents]}
    tblu = {"subType": 2, "rootEntityIndex": 0, "externalSceneTypeIndicesInResourceHeader": [], "pinConnections": [],
            "inputPinForwardings": [], "outputPinForwardings": [], "overrideDeletes": [],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": [],
            "subEntities": [{"logicalParent": be.local_ref(-1), "entityTypeResourceIndex": 0, "entityId": e,
                             "editorOnly": False, "entityName": n, "propertyAliases": [], "exposedEntities": [],
                             "exposedInterfaces": [], "entitySubsets": []} for e, n, _ in ents]}
    return be.Brick("map", temp, tblu, [(0x1F, 0x50)], [(0x1F, 0x51)], False)


class Spec:
    NAME, DISPLAY, TERRITORY = "test", "The Test", TERRITORY
    NAVMESH = (0x00E920F30DEF45C1, "test.navp")

    def __init__(self, **kw):
        self.SPOT_MOVES = {
            SAFE: dict(kind="safe", to=(81.0, -21.5, 12.0), anchor=0xD1, why="x"),
            CLUE: dict(kind="clue", to=(79.0, -21.5, 12.0), anchor=0xD1, why="x"),
            MULE: dict(kind="mule", to=(38.0, -62.0, 12.8), anchor=SIT_ACT, why="x"),
            SIT_ACT: dict(kind="act", to=(38.0, -62.0, 12.8), anchor=0xD2, why="x"),
            STAND_ACT: dict(kind="act", to=(40.0, -60.0, 12.8), anchor=0xD2, why="x"),
        }
        self.RETYPE = {SIT_ACT: ("act_mr_sit_ground_edge", "act_mr_stand_think_100cm", STAND_ACT)}
        self.DELETE_APPROACH_POINTS = [POINT]
        self.RIGID = {}
        for k, v in kw.items():
            setattr(self, k, v)


def run(spec, brick=None):
    brick = brick or sample()
    m = base()
    placement = pl.Placement(None, (0.0, 0.0, 0.0), [(80.0, -20.0, 12.0), (38.0, -60.0, 12.8)])
    audit = []
    with contextlib.redirect_stderr(io.StringIO()):
        moved = bb.apply_spot_moves(spec, brick, m, placement, audit)
    return brick, moved, audit


class SpotMovesTest(unittest.TestCase):
    def setUp(self):
        bb.NAMES.clear()
        bb.NAMES.update(TYPES)

    def test_moves_retypes_and_drops_the_approach_path(self):
        b, moved, audit = run(Spec())
        self.assertEqual(moved, {SAFE, CLUE, MULE, SIT_ACT, STAND_ACT})
        self.assertEqual(b.get(SAFE, "m_mTransform")["Trans"], {"x": 81.0, "y": -21.5, "z": 12.0})
        self.assertEqual(b.get(MULE, "m_mTransform")["Trans"], {"x": 38.0, "y": -62.0, "z": be.f32(12.8)})
        self.assertEqual(bb.type_leaf(b, b.index_of(SIT_ACT)), "act_mr_stand_think_100cm.entitytemplate")
        self.assertNotIn("m_aApproachPath", bb.props(b.temp["subEntities"][b.index_of(SIT_ACT)]))
        with self.assertRaises(be.BrickError):
            b.index_of(POINT)
        self.assertTrue(any("NOT navmesh-checked" in line for line in audit))
        self.assertTrue(any("WARNING" in line for line in audit))
        b.validate()

    def test_refuses_a_safe_moved_without_its_clue(self):
        spec = Spec()
        del spec.SPOT_MOVES[CLUE]
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("but not its clue", str(e.exception))

    def test_refuses_a_mule_act_left_behind(self):
        spec = Spec()
        del spec.SPOT_MOVES[STAND_ACT]
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("but not its act", str(e.exception))

    def test_refuses_a_kind_that_is_not_the_template(self):
        spec = Spec()
        spec.SPOT_MOVES[SAFE] = dict(spec.SPOT_MOVES[SAFE], kind="stash")
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("not a stash", str(e.exception))

    def test_refuses_an_orphan_clue(self):
        spec = Spec()
        del spec.SPOT_MOVES[SAFE]
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("belongs to no moved safe or mule", str(e.exception))

    def test_refuses_a_wrong_retype(self):
        spec = Spec(RETYPE={SIT_ACT: ("act_mr_sit_bench", "act_mr_stand_think_100cm", STAND_ACT)})
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("not a act_mr_sit_bench", str(e.exception))

    def test_refuses_an_unknown_anchor(self):
        spec = Spec()
        spec.SPOT_MOVES[SAFE] = dict(spec.SPOT_MOVES[SAFE], anchor=0xEEEE)
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("anchor", str(e.exception))

    def test_refuses_a_place_the_check_rejects(self):
        spec = Spec()
        spec.SPOT_MOVES[SAFE] = dict(spec.SPOT_MOVES[SAFE], to=(0.0, 0.0, 0.0))  # far from every act
        with self.assertRaises(SystemExit) as e:
            run(spec)
        self.assertIn("rejected by the act-distance check", str(e.exception))

    def test_refuses_an_approach_point_still_used(self):
        spec = Spec()
        b = sample()
        b.temp["subEntities"][b.index_of(STAND_ACT)]["propertyValues"].append(
            prop("m_rKeep", be.local_ref(b.index_of(POINT))))
        with self.assertRaises(SystemExit) as e:
            run(spec, b)
        self.assertIn("approach points", str(e.exception))

    def test_no_moves_leave_the_brick_alone(self):
        b, moved, audit = run(Spec(SPOT_MOVES={}))
        self.assertEqual(moved, set())
        self.assertEqual(audit, [])
        self.assertEqual(b.temp, sample().temp)


STASH_A, STASH_B = 0xE1, 0xE2


def crate_brick(a=(5.0, 1.2, 0.0), yaw=180.0):
    """An override with two slots' supply crates against the wall y = 0: A at a turned yaw, B at (15, 1.2) turned
    180 (yaw 180: the open side, local -Y, toward +Y)."""
    def turned(x, y, z, deg):
        c, s_ = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        m = matrix(x, y, z)
        m["XAxis"], m["YAxis"] = {"x": c, "y": s_, "z": 0.0}, {"x": -s_, "y": c, "z": 0.0}
        return [prop("m_mTransform", m, "SMatrix43"), prop("m_eidParent", be.local_ref(0))]
    b = sample()
    for eid, name, pos, deg in ((STASH_A, "Stash_A", a, yaw), (STASH_B, "Stash_B", (15.0, 1.2, 0.0), 180.0)):
        b.temp["subEntities"].append({"logicalParent": be.local_ref(0), "entityTypeResourceIndex": TYPE_INDEX[0x18],
                                      "propertyValues": turned(*pos, deg), "postInitPropertyValues": [],
                                      "platformSpecificPropertyValues": []})
        b.tblu["subEntities"].append({"logicalParent": be.local_ref(0), "entityTypeResourceIndex": 0, "entityId": eid,
                                      "editorOnly": False, "entityName": name, "propertyAliases": [],
                                      "exposedEntities": [], "exposedInterfaces": [], "entitySubsets": []})
    terr = b.temp["subEntities"][b.index_of(TERRITORY)]["propertyValues"]
    terr.append(prop("availableStash01", be.local_ref(b.index_of(STASH_A))))
    terr.append(prop("availableStash02", be.local_ref(b.index_of(STASH_B))))
    terr.append(prop("availableStash03", be.local_ref(b.index_of(STASH_B))))
    return b


def floor(y0):
    """A room 20 m wide whose floor starts y0 m from the crates' wall (y = 0) and runs to y = 10."""
    return navp.NavMesh(navp.build([(1, [(0, y0, 0.0), (20, y0, 0.0), (20, 10, 0.0), (0, 10, 0.0)], [None] * 4)]))


class CrateSpec(Spec):
    FREELANCER_NAVMESH = (0x008980F97BE736B2, "freelancer.navp")


class CrateCheckTest(unittest.TestCase):
    """build_bricks.check_crates: every slot's supply crate, moved or not, opens toward floor on the variant's
    navmesh; one that stays is judged against Freelancer's navmesh at the same place."""

    def setUp(self):
        bb.NAMES.clear()
        bb.NAMES.update(TYPES)

    def check(self, b, variant, freelancer, spec=None):
        audit = []
        bb.check_crates(spec or CrateSpec(SPOT_MOVES={}), b, (variant, freelancer), audit)
        return audit

    def test_open_floor_passes_and_lists_every_crate_once(self):
        audit = self.check(crate_brick(), floor(0.4), floor(0.4))
        self.assertEqual(len([x for x in audit if "Stash_A" in x]), 1)
        self.assertIn("(availableStash02, availableStash03)", [x for x in audit if "Stash_B" in x][0])
        self.assertIn("2 supply crates checked", audit[-1])

    def test_a_vanilla_crate_the_variant_hems_in_stops_the_build(self):
        # The variant's dressing takes the floor in front (it starts 2.5 m out); Freelancer's has 3 m there.
        with self.assertRaises(SystemExit) as e:
            self.check(crate_brick(), floor(4.0), floor(0.4))
        msg = str(e.exception)
        self.assertIn("2 supply crate(s)", msg)
        self.assertIn("Stash_A (availableStash01)", msg)
        self.assertIn("0.00 m of The Test's navmesh in front, 3.00 m under Freelancer's", msg)

    def test_a_crate_as_cramped_under_freelancer_is_only_noted(self):
        audit = self.check(crate_brick(), floor(4.0), floor(4.0))
        self.assertIn("the navmesh cut round it as under Freelancer", [x for x in audit if "Stash_A" in x][0])

    def test_a_moved_crate_must_face_open_floor_and_stand_level(self):
        spec = CrateSpec(SPOT_MOVES={STASH_A: dict(kind="stash", to=(5.0, 1.2, 0.0), anchor=0xD1, why="x")})
        # Moved 1.2 m from the wall, facing it (yaw 0: its open side to -Y, 0.55 m of floor).
        with self.assertRaises(SystemExit) as e:
            self.check(crate_brick(yaw=0.0), floor(0.4), floor(0.4), spec)
        self.assertIn("Stash_A (availableStash01) at (5.00, 1.20, 0.00) (moved): its open side (yaw 0) has 0.55 m",
                      str(e.exception))
        self.assertNotIn("Stash_B", str(e.exception))
        audit = self.check(crate_brick(a=(5.0, 5.0, 0.0), yaw=180.0), floor(0.4), floor(0.4), spec)
        self.assertIn("(moved) opens toward", [x for x in audit if "Stash_A" in x][0])
        b = crate_brick(a=(5.0, 5.0, 0.0), yaw=180.0)
        z = b.temp["subEntities"][b.index_of(STASH_A)]["propertyValues"][0]["value"]["$val"]
        z["ZAxis"], z["YAxis"] = {"x": 0.0, "y": -0.2, "z": 0.98}, {"x": 0.0, "y": -0.98, "z": -0.2}
        with self.assertRaises(SystemExit) as e:
            self.check(b, floor(0.4), floor(0.4), spec)
        self.assertIn("tilted", str(e.exception))

    def test_no_freelancer_navmesh_no_check(self):
        self.assertIsNone(bb.crate_meshes(Spec(), None, {}, None))
        audit = []
        bb.check_crates(Spec(), crate_brick(), None, audit)
        self.assertEqual(audit, [])


if __name__ == "__main__":
    unittest.main()
