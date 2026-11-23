#!/usr/bin/env python3
"""Self-test of the spot fixes of appended map variants: spot_fix.py's fix brick (moves and re-points as property
overrides on Freelancer's override brick) and its checks, landslide_audit's reading of re-pointed slots, and the
specs of spotfix_spec.py, on small synthetic bricks (no game data):
python3 -I tools/landslide/test_spot_fix.py"""
import math
import os
import sys

sys.dont_write_bytecode = True
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import landslide_audit as la  # noqa: E402
import navp  # noqa: E402
import spot_fix  # noqa: E402
import spotfix_spec  # noqa: E402
import variant_audit as va  # noqa: E402
from test_scene_props import ROOT, brick, matrix, prop  # noqa: E402

OVB, OPTB = 0x00C0C0C0C0C0C001, 0x00C0C0C0C0C0C002
STASH_A, STASH_B, STASH_C, SUPPLIER, VOLUME, NPC, TERRITORY = 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x50
SAFE, CLUE_A, CLUE_B, GUARD, GUARD_ACT = 0x57, 0x58, 0x59, 0x5A, 0x5B
T = "[assembly:/_pro/design/setpieces/setpieces_evergreen.template?/%s.entitytemplate].pc_entitytype"
NAMES = {
    0x0F01: T % "setpiece_evergreen_stash_unique_spawnable",
    0x0F02: T % "helper_evergreen_supplierpackage",
    0x0F03: "[assembly:/_pro/design/actor/acts/mr.template?/act_mr_stand_neutral.entitytemplate].pc_entitytype",
    0x0F04: "[modules:/zboxvolumeentity.class].pc_entitytype",
    0x0F05: "[assembly:/_pro/environment/templates/props/x.template?/crate.entitytemplate].pc_entitytype",
    0x0F06: T % "setpiece_evergreen_container_items_safe_unique",
    0x0F07: T % "setpiece_evergreen_scannable_safeclue_documents_unique",
    0x0F08: T % "setpiece_evergreen_safeactor_unique",
    0x0F09: "[assembly:/_pro/environment/templates/doors/x.template?/door_double_test_a.entitytemplate].pc_entitytype",
    0x0F0A: "[assembly:/_pro/environment/templates/props/x.template?/kitchen_cabinet_door_a.entitytemplate]"
            ".pc_entitytype",
    la.TERRITORY_TYPE: T % "helper_evergreen_cpd_territorysetuprandomizer",
}


def typed(b, types):
    """Gives the brick's entities (by index) their types (hashes appended to the TEMP's reference table, before its
    external scenes)."""
    tail = len(b.temp["externalSceneTypeIndicesInResourceHeader"])
    head = b.temp_refs[:len(b.temp_refs) - tail]
    scenes = b.temp_refs[len(b.temp_refs) - tail:]
    for i, h in types.items():
        if (0x1F, h) not in head:
            head.append((0x1F, h))
        b.temp["subEntities"][i]["entityTypeResourceIndex"] = head.index((0x1F, h))
    b.temp_refs = head + scenes
    b.temp["externalSceneTypeIndicesInResourceHeader"] = list(range(len(head), len(head) + tail))
    return b


def override(safe=False):
    """Freelancer's override brick: three stashes (0, 10, 20 on the x axis) and a supplier at 30 with his private
    volume (2 x 2 x 2 m, 1 m off him) and his NPC, all in the territory randomizer's slots; with safe, a safe in a
    slot too, at 75 (past the end of the option's navmesh, mesh()), with two clues (one on the floor at 78, one on a
    table at 72), its guard (a safeEnabled pin) and his act at (76, 2)."""
    slots = [prop("availableStash01", be.local_ref(2, "VBB_Spawn")), prop("availableStash02", be.local_ref(3, "VBB_Spawn")),
             prop("availableStash03", be.local_ref(4, "VBB_Spawn")), prop("availableSupplier01", be.local_ref(5))]
    if safe:
        slots.append(prop("availableSafe01", be.local_ref(8, "VBB_Spawn")))
    ents = [
        (ROOT, "Scene", None, []),
        (TERRITORY, "Helper_Evergreen_CPD_TerritorySetupRandomizer", 0, slots),
        (STASH_A, "Stash_A", 0, [prop("m_mTransform", matrix(0.0, 0.0, 0.0), "SMatrix43")]),
        (STASH_B, "Stash_B", 0, [prop("m_mTransform", matrix(10.0, 0.0, 0.0), "SMatrix43")]),
        (STASH_C, "Stash_C", 0, [prop("m_mTransform", matrix(20.0, 0.0, 0.0), "SMatrix43")]),
        (SUPPLIER, "Helper_Evergreen_SupplierPackage", 0, [
            prop("m_mTransform", matrix(30.0, 0.0, 0.0, yaw=90.0), "SMatrix43"),
            prop("privateVolume", [be.local_ref(6)], "TArray<SEntityTemplateReference>"),
            prop("m_rActorProvider", be.local_ref(7)), prop("exclusionList", [be.local_ref(7)],
                                                            "TArray<SEntityTemplateReference>")]),
        (VOLUME, "Crashsite", 5, [prop("m_eidParent", be.local_ref(5)),
                                  prop("m_mTransform", matrix(1.0, 0.0, 0.0), "SMatrix43"),
                                  prop("m_vGlobalSize", {"x": 2.0, "y": 2.0, "z": 2.0}, "SVector3")]),
        (NPC, "Supplier", 5, [prop("m_eidParent", be.local_ref(5)), prop("m_mTransform", matrix(0, 0, 0), "SMatrix43")]),
    ]
    types = {1: la.TERRITORY_TYPE, 2: 0x0F01, 3: 0x0F01, 4: 0x0F01, 5: 0x0F02, 6: 0x0F04}
    if safe:
        ents += [
            (SAFE, "Safe_Unique", 0, [prop("m_mTransform", matrix(75.0, 0.0, 0.0), "SMatrix43"),
                                      prop("Clue01", be.local_ref(9, "VBB_Spawn")),
                                      prop("Clue02", be.local_ref(10, "VBB_Spawn"))]),
            (CLUE_A, "SafeClue_A", 0, [prop("m_mTransform", matrix(78.0, 0.0, 0.0), "SMatrix43")]),
            (CLUE_B, "SafeClue_B", 0, [prop("m_mTransform", matrix(72.0, 0.0, 1.0), "SMatrix43")]),
            (GUARD, "SafeActor", 0, [prop("m_mTransform", matrix(76.0, 2.0, 0.0), "SMatrix43")]),
            (GUARD_ACT, "Act_MR_Guard", 0, [prop("m_mTransform", matrix(76.0, 2.0, 0.0), "SMatrix43"),
                                            prop("m_pActor", be.local_ref(11, "NPC Actor"))])]
        types.update({8: 0x0F06, 9: 0x0F07, 10: 0x0F07, 11: 0x0F08, 12: 0x0F03})
    b = typed(brick("override_test", ents, []), types)
    if safe:
        b.tblu["pinConnections"].append({"fromID": 8, "toID": 11, "fromPinName": "safeEnabled", "toPinName": "Enable",
                                         "constantPinValue": {"$type": "void", "$val": None}})
    b.hash = OVB
    return b


def option(act_x=30.5):
    """The option: an act at the supplier's table (act_x) and a crate on Stash_B."""
    ents = [(ROOT, "Scene", None, []),
            (0x61, "Act_MR_Stand", 0, [prop("m_mTransform", matrix(act_x, 0.3, 0.0), "SMatrix43")]),
            (0x62, "Crate", 0, [prop("m_mTransform", matrix(10.3, 0.0, 0.0), "SMatrix43")]),
            (0x63, "Act_MR_Far", 0, [prop("m_mTransform", matrix(44.0, 0.0, 0.0), "SMatrix43")])]
    b = typed(brick("option_test", ents, []), {1: 0x0F03, 2: 0x0F05, 3: 0x0F03})
    b.hash = OPTB
    return b


def mesh(x1=65.0):
    """A floor from x -5 to x1, 10 m wide, whose navmesh lies 0.15 m above it (the acts stand at z 0). mesh(85.0):
    Freelancer's, whose floor the option's (mesh()) cuts at 65 (a closed room: the safe's)."""
    sq = [(-5.0, -5.0, 0.15), (x1, -5.0, 0.15), (x1, 5.0, 0.15), (-5.0, 5.0, 0.15)]
    return navp.NavMesh(navp.build([(1, sq, [None] * 4)]))


class Spec(spotfix_spec.SpotFix):
    NAME = "test"
    OVERRIDE = (OVB, 0x00C0C0C0C0C0C011)
    FIX_PATH = "assembly:/_pro/scenes/missions/test/fr_test.brick"
    FIX_IDS = (0x00C0C0C0C0C0C021, 0x00C0C0C0C0C0C022)
    MOVES = {SUPPLIER: dict(kind="supplier", to=(50.0, 0.0, 0.0), why="the option's act at his table")}
    REPOINTS = {"availableStash02": (STASH_B, STASH_C, "the option's crate on it")}
    BROKEN = {SUPPLIER: "an act at his table", STASH_B: "a crate on it"}


def spec(**changes):
    return type("Spec", (Spec,), changes)


# The safe's group moved out of its closed room onto the option's floor: the safe at 56, its clues 4.2 m from it, its
# guard at his act 1.8 m from it (his own: the privacy check leaves it).
SAFE_MOVES = {
    SAFE: dict(kind="safe", to=(56.0, 0.0, 0.0), why="sealed off"),
    CLUE_A: dict(kind="clue", to=(59.0, 3.0, 0.0), why="its clue"),
    CLUE_B: dict(kind="clue", to=(53.0, -3.0, 0.0), why="its clue"),
    GUARD: dict(kind="guard", to=(57.0, -1.5, 0.0), why="its guard"),
    GUARD_ACT: dict(kind="act", to=(57.0, -1.5, 0.0), why="its guard's act"),
}


def safe_spec(**moves):
    """Spec with the safe's group moved (SAFE_MOVES, changed by moves: entity=None leaves one out)."""
    m = dict(Spec.MOVES)
    m.update(SAFE_MOVES)
    for k, v in moves.items():
        eid = globals()[k]
        if v is None:
            del m[eid]
        else:
            m[eid] = dict(m[eid], to=v)
    return spec(MOVES=m)


def scenes(s, act_x=30.5, safe=False):
    """(the option's scene with s's fix brick, Freelancer's scene, the fix brick)."""
    ov, opt = override(safe), option(act_x)
    fl = la.Scene({OVB: override(safe)})
    bricks = {OVB: ov, OPTB: opt}
    fix = spot_fix.audit_brick(s, bricks, NAMES, la.Scene)
    loaded = dict(bricks)
    loaded.update(fix)
    return la.Scene(loaded), fl, fix[s.FIX_IDS[0]]


class FixBrick(unittest.TestCase):
    def test_moves_and_repoints_as_overrides_on_the_override_brick(self):
        sc, _, fix = scenes(Spec)
        self.assertEqual(fix.scenes, [OVB])
        self.assertEqual(len(fix.temp["propertyOverrides"]), 2)
        # The supplier moves, his rotation kept, his volume and NPC with him.
        self.assertEqual(sc.pos(OVB, 5), (50.0, 0.0, 0.0))
        w = sc.world(OVB, 5)
        self.assertAlmostEqual(w[1][0], 1.0)  # yaw 90: his x axis along y
        v = sc.pos(OVB, 6)
        self.assertAlmostEqual(v[0], 50.0)
        self.assertAlmostEqual(v[1], 1.0)
        self.assertEqual(sc.pos(OVB, 7), (50.0, 0.0, 0.0))
        # The slot names Stash_C now, its exposed entity kept; Stash_B is no slot's.
        slot = [o for o in fix.temp["propertyOverrides"] if o["propertyValue"]["nPropertyID"] == "availableStash02"][0]
        self.assertEqual(slot["propertyValue"]["value"]["$val"]["exposedEntity"], "VBB_Spawn")
        self.assertEqual(slot["propertyValue"]["value"]["$val"]["entityID"], STASH_C)
        self.assertEqual(la.slot_spots(override(), sc), {2: ["availableStash01"], 4: ["availableStash02",
                                                                                      "availableStash03"],
                                                          5: ["availableSupplier01"]})
        self.assertEqual(la.slot_spots(override()), {2: ["availableStash01"], 3: ["availableStash02"],
                                                     4: ["availableStash03"], 5: ["availableSupplier01"]})
        places = [d for d, _ in la.spots(sc.B[OVB], NAMES, sc)]
        self.assertFalse(any("Stash_B" in d for d in places))

    def test_a_spec_that_does_not_match_the_bricks_fails(self):
        for bad, text in ((spec(MOVES={STASH_A: dict(kind="supplier", to=(0, 0, 0), why="")}), "not a supplier"),
                          (spec(REPOINTS={"availableStash02": (STASH_A, STASH_C, "")}), "names"),
                          (spec(REPOINTS={"availableStash02": (STASH_B, SUPPLIER, "")}), "re-points a stash"),
                          (spec(REPOINTS={"availableStash09": (STASH_B, STASH_C, "")}), "not a slot")):
            with self.assertRaises(spot_fix.SpotFixError) as e:
                scenes(bad)
            self.assertIn(text, str(e.exception))


class SafeGroups(unittest.TestCase):
    def test_a_moved_safe_brings_its_clues_guard_and_acts(self):
        sc, _, fix = scenes(safe_spec(), safe=True)
        self.assertEqual(len(fix.temp["propertyOverrides"]), 2 + len(SAFE_MOVES))
        for eid, m in SAFE_MOVES.items():
            self.assertEqual(sc.pos(OVB, sc.B[OVB].index_of(eid)), m["to"])
        self.assertEqual(sorted(k for _, k in spot_fix.safe_members(override(True), 8, NAMES)),
                         ["act", "clue", "clue", "guard"])

    def test_a_group_that_is_not_whole_fails(self):
        for bad, text in ((safe_spec(CLUE_B=None), "but not its clue"),
                          (safe_spec(GUARD_ACT=None), "but not its act"),
                          (spec(MOVES=dict(list(Spec.MOVES.items()) + [(CLUE_A, SAFE_MOVES[CLUE_A])])), "belongs to no moved safe")):
            with self.assertRaises(spot_fix.SpotFixError) as e:
                scenes(bad, safe=True)
            self.assertIn(text, str(e.exception))


class Checks(unittest.TestCase):
    def problems(self, s, act_x=30.5, safe=False, fl_mesh=None):
        sc, fl, _ = scenes(s, act_x, safe)
        return spot_fix.check(s, "mild", sc, fl, {OPTB}, mesh(), la, NAMES, fl_mesh)

    def test_a_good_fix_passes(self):
        lines, problems = self.problems(Spec)
        self.assertEqual(problems, [])
        self.assertTrue(any("supplier" in x and "0.00 m from the walking floor" in x for x in lines), lines)

    def test_a_supplier_near_an_act(self):
        _, problems = self.problems(spec(MOVES={SUPPLIER: dict(kind="supplier", to=(41.0, 0.0, 0.0), why="")}))
        self.assertTrue(any("will not deal" in p for p in problems), problems)

    def test_an_act_in_the_moved_private_volume(self):
        # The option's act moved to where his volume goes (1 m off him): inside it (and within 5 m).
        _, problems = self.problems(spec(MOVES={SUPPLIER: dict(kind="supplier", to=(50.0, 0.0, 0.0), why="")}),
                                    act_x=50.0)
        self.assertTrue(any("inside its private volume" in p for p in problems), problems)

    def test_a_supplier_facing_a_wall(self):
        # At 63, his NPC facing +X (the spot's yaw 90, the NPC's -Y): the floor ends at 65, under 3 m in front of him.
        _, problems = self.problems(spec(MOVES={SUPPLIER: dict(kind="supplier", to=(63.0, 0.0, 0.0), why="")}))
        self.assertTrue(any("a wall or a prop in front of him" in p and "turned +180: 3.00 m" in p for p in problems),
                        problems)
        # Turned half round (the spot's yaw -90) he faces the floor back along x.
        lines, problems = self.problems(spec(MOVES={SUPPLIER: dict(kind="supplier", to=(63.0, 0.0, 0.0), yaw=-90.0,
                                                                   why="")}))
        self.assertEqual(problems, [])
        self.assertTrue(any("its NPC faces (yaw -90) 3.00 m of navmesh" in x for x in lines), lines)

    def test_a_spot_at_the_navmesh_height_floats(self):
        _, problems = self.problems(spec(MOVES={SUPPLIER: dict(kind="supplier", to=(50.0, 0.0, 0.15), why="")}))
        self.assertTrue(any("0.15 m above the floor" in p for p in problems), problems)

    def test_off_the_navmesh(self):
        _, problems = self.problems(spec(MOVES={SUPPLIER: dict(kind="supplier", to=(80.0, 0.0, 0.0), why="")}))
        self.assertTrue(any("no navmesh floor" in p for p in problems), problems)

    def test_a_spoiled_spot_left_in_a_slot(self):
        _, problems = self.problems(spec(REPOINTS={}))
        self.assertTrue(any("availableStash02 still names" in p for p in problems), problems)

    def test_a_sealed_safe_and_its_clues_fail(self):
        # The safe and both clues (the one on a table too) have floor under Freelancer's navmesh, none on the option's.
        _, problems = self.problems(Spec, safe=True, fl_mesh=mesh(85.0))
        self.assertEqual(len(problems), 3, problems)
        self.assertTrue(all("sealed off" in p for p in problems), problems)
        self.assertTrue(any(p.startswith("mild: safe ") and "(availableSafe01)" in p for p in problems), problems)
        self.assertEqual(sum(1 for p in problems if p.startswith("mild: clue ")), 2, problems)
        # Not checked without Freelancer's navmesh, or when the option walks on Freelancer's own.
        self.assertEqual(self.problems(Spec, safe=True)[1], [])

    def test_a_moved_safe_group_passes(self):
        lines, problems = self.problems(safe_spec(), safe=True, fl_mesh=mesh(85.0))
        self.assertEqual(problems, [])
        # Stash_A, Stash_C (Stash_B's slot names it), the supplier, the safe and its two clues.
        self.assertTrue(any("sealed check: 6 " in x for x in lines), lines)
        self.assertTrue(any("safe" in x and "nearest act 1.8 m (its own NPC's)" in x for x in lines), lines)

    def test_a_moved_safe_with_someone_at_it(self):
        # 1.5 m from the option's act at 44: past the 0.8 m act gap, inside the privacy check's 2 m.
        _, problems = self.problems(safe_spec(SAFE=(44.0, 1.5, 0.0), GUARD=(46.0, -1.5, 0.0),
                                              GUARD_ACT=(46.0, -1.5, 0.0)), safe=True)
        self.assertTrue(any("someone stands at the safe" in p for p in problems), problems)

    def test_a_guard_act_by_a_supplier(self):
        # 4.1 m from the moved supplier at 50: he will not deal while the guard stands there.
        _, problems = self.problems(safe_spec(GUARD=(54.0, -1.5, 0.0), GUARD_ACT=(54.0, -1.5, 0.0)), safe=True)
        self.assertTrue(any("keeps a spot busy: supplier" in p for p in problems), problems)

    def test_stacked_clues(self):
        _, problems = self.problems(safe_spec(CLUE_B=(59.5, 3.0, 0.0)), safe=True)
        self.assertTrue(any("not stacked" in p for p in problems), problems)

    def test_a_guard_act_on_another_act(self):
        _, problems = self.problems(safe_spec(GUARD=(44.0, 0.5, 0.0), GUARD_ACT=(44.0, 0.5, 0.0)), safe=True)
        self.assertTrue(any("act" in p and "an act 0.50 m away" in p for p in problems), problems)

    def test_a_repoint_onto_a_spoiled_spot(self):
        # Re-pointing the slot onto Stash_B itself (under the option's crate) leaves it naming a spoiled spot.
        _, problems = self.problems(spec(REPOINTS={"availableStash02": (STASH_B, STASH_B, "")}))
        self.assertTrue(any("availableStash02 still names" in p for p in problems), problems)
        self.assertTrue(any("Crate" in p for p in problems), problems)

    def test_a_moved_crate_must_open_toward_the_room(self):
        # Stash_A moved 1 m from the floor's edge at y = -5, its rotation kept (yaw 0: it opens on -Y, toward it).
        moves = dict(Spec.MOVES)
        moves[STASH_A] = dict(kind="stash", to=(40.0, -4.0, 0.0), why="test")
        _, problems = self.problems(spec(MOVES=moves))
        self.assertTrue(any("stash" in p and "under 0.8 m" in p for p in problems), problems)
        # Turned half round by the spec's yaw: it opens on the room, its axes level and turned.
        moves[STASH_A] = dict(kind="stash", to=(40.0, -4.0, 0.0), yaw=180.0, why="test")
        lines, problems = self.problems(spec(MOVES=moves))
        self.assertEqual(problems, [])
        self.assertTrue(any("open side (yaw 180) 3.00 m of navmesh in front" in x for x in lines), lines)
        sc, _, fix = scenes(spec(MOVES=moves))
        w = sc.world(OVB, 2)
        self.assertAlmostEqual(w[0][0], -1.0)  # its x axis along -x
        self.assertAlmostEqual(w[1][1], -1.0)  # its y axis along -y: the open side (-Y) faces +y
        self.assertAlmostEqual(w[2][2], 1.0)
        self.assertEqual(sc.pos(OVB, 2), (40.0, -4.0, 0.0))

    def test_a_repointed_crate_must_open_toward_the_room(self):
        # The option's navmesh stops 0.5 m in front of the crates (y -0.5); Freelancer's gives them room. The slot
        # re-pointed onto Stash_C names a crate the option hems in.
        sq = [(-5.0, -0.5, 0.15), (65.0, -0.5, 0.15), (65.0, 5.0, 0.15), (-5.0, 5.0, 0.15)]
        hemmed = navp.NavMesh(navp.build([(1, sq, [None] * 4)]))
        sc, fl, _ = scenes(Spec)
        _, problems = spot_fix.check(Spec, "mild", sc, fl, {OPTB}, hemmed, la, NAMES, mesh())
        self.assertTrue(any("(availableStash02)" in p and "under Freelancer's navmesh" in p for p in problems),
                        problems)
        # Hemmed in under Freelancer's navmesh too: as under Freelancer, no problem.
        lines, problems = spot_fix.check(Spec, "mild", sc, fl, {OPTB}, hemmed, la, NAMES, hemmed)
        self.assertFalse(any("open side" in p for p in problems), problems)
        self.assertTrue(any("availableStash02 ->" in x and "(as under Freelancer)" in x for x in lines), lines)
        # With room (the good fix): the line gives it.
        lines, problems = self.problems(Spec)
        self.assertTrue(any("availableStash02 ->" in x and "3.00 m of navmesh in front" in x for x in lines), lines)

    def test_a_turned_crate_under_a_turned_parent(self):
        # The yaw is a world one: under a parent turned 90 degrees, the local axes are turned back by it.
        b = override()
        i = b.index_of(STASH_A)
        b.temp["subEntities"][i]["propertyValues"].append(prop("m_eidParent", be.local_ref(5)))
        sc = la.Scene({OVB: b})
        fix = spot_fix.skeleton(Spec)
        moves = {STASH_A: dict(kind="stash", to=(40.0, -4.0, 0.0), yaw=180.0, why="test")}
        s = spec(MOVES=moves, REPOINTS={}, BROKEN={})
        spot_fix.apply(s, fix, b, sc, NAMES)
        fix.hash = Spec.FIX_IDS[0]
        after = la.Scene({OVB: b, fix.hash: fix})
        w = after.world(OVB, i)
        self.assertAlmostEqual(w[0][0], -1.0, places=5)
        self.assertAlmostEqual(w[1][1], -1.0, places=5)
        self.assertAlmostEqual(after.pos(OVB, i)[0], 40.0, places=4)
        self.assertAlmostEqual(after.pos(OVB, i)[1], -4.0, places=4)


class CrateFaces(unittest.TestCase):
    """landslide_audit.crate_faces, as variant_audit runs it on each option (and run_variant on the full ones)."""

    def test_vanilla_crates_with_room_pass(self):
        fl = la.Scene({OVB: override()})
        self.assertEqual(la.crate_faces(fl, fl.B[OVB], NAMES, mesh(), fl, fl.B[OVB], mesh()), ([], []))

    def test_a_moved_crate_facing_the_edge(self):
        moves = dict(Spec.MOVES)
        moves[STASH_A] = dict(kind="stash", to=(40.0, -4.0, 0.0), why="test")
        sc, fl, _ = scenes(spec(MOVES=moves))
        problems, notes = la.crate_faces(sc, sc.B[OVB], NAMES, mesh(), fl, fl.B[OVB], mesh())
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("Stash_A", problems[0])
        self.assertIn("(moved)", problems[0])
        self.assertIn("yaw=-180.0 gives 3.00 m", problems[0])
        moves[STASH_A] = dict(moves[STASH_A], yaw=180.0)
        sc, fl, _ = scenes(spec(MOVES=moves))
        self.assertEqual(la.crate_faces(sc, sc.B[OVB], NAMES, mesh(), fl, fl.B[OVB], mesh()), ([], []))

    def test_a_vanilla_crate_the_option_hems_in(self):
        # The option's navmesh stops 0.5 m in front of the crates (y -0.5): under Freelancer's they had room.
        sq = [(-5.0, -0.5, 0.15), (65.0, -0.5, 0.15), (65.0, 5.0, 0.15), (-5.0, 5.0, 0.15)]
        hemmed = navp.NavMesh(navp.build([(1, sq, [None] * 4)]))
        fl = la.Scene({OVB: override()})
        problems, notes = la.crate_faces(fl, fl.B[OVB], NAMES, hemmed, fl, fl.B[OVB], mesh())
        self.assertEqual(len(problems), 3, problems)  # Stash_A, B and C
        self.assertTrue(all("under Freelancer's navmesh" in p for p in problems), problems)
        # Hemmed in under Freelancer's navmesh too: a note (the navmesh is cut round some vanilla crates).
        problems, notes = la.crate_faces(fl, fl.B[OVB], NAMES, hemmed, fl, fl.B[OVB], hemmed)
        self.assertEqual(problems, [])
        self.assertEqual(len(notes), 3, notes)

    def test_a_crate_in_a_doorway(self):
        # A wall along y 1-1.6 with two 2 m doorways (x 19-21 and 39-41), a door in each (their origins at the west
        # end of the opening). Stash_C (vanilla, at 20, 0; the slot re-pointed from Stash_B names it) stands 1.04 m
        # in front of the first; Stash_A moved to
        # (40, 0.2), 0.84 m in front of the second (Garden Show's Stash05 in the greenhouse's north door).
        walled = navp.NavMesh(navp.build([
            (1, [(-5.0, -5.0, 0.15), (65.0, -5.0, 0.15), (65.0, 1.0, 0.15), (-5.0, 1.0, 0.15)], [None] * 4),
            (1, [(19.0, 1.0, 0.15), (21.0, 1.0, 0.15), (21.0, 1.6, 0.15), (19.0, 1.6, 0.15)], [None] * 4),
            (1, [(39.0, 1.0, 0.15), (41.0, 1.0, 0.15), (41.0, 1.6, 0.15), (39.0, 1.6, 0.15)], [None] * 4),
            (1, [(-5.0, 1.6, 0.15), (65.0, 1.6, 0.15), (65.0, 5.0, 0.15), (-5.0, 5.0, 0.15)], [None] * 4)]))

        def doors(*xs):
            ents = [(ROOT, "Scene", None, [])] + [
                (0x70 + k, "Door_%d" % k, 0, [prop("m_mTransform", matrix(x, 1.3, 0.0), "SMatrix43")])
                for k, x in enumerate(xs)]
            b = typed(brick("doors_test", ents, []), {k + 1: 0x0F09 for k in range(len(xs))})
            b.hash = 0x00C0C0C0C0C0C003
            return b
        moves = dict(Spec.MOVES)
        moves[STASH_A] = dict(kind="stash", to=(40.0, 0.2, 0.0), yaw=180.0, why="test")
        sc, fl, _ = scenes(spec(MOVES=moves))
        # The doors in the option's bricks only: both crates are in the way.
        opt = la.Scene(dict(list(sc.B.items()) + [(0x00C0C0C0C0C0C003, doors(19.0, 39.0))]))
        problems, notes = la.crate_faces(opt, opt.B[OVB], NAMES, walled, fl, fl.B[OVB], walled)
        self.assertEqual(len(problems), 2, problems)
        self.assertTrue(any("Stash_A" in p and "(moved): in the doorway of door" in p for p in problems), problems)
        self.assertTrue(any("Stash_C" in p and "in the doorway of door" in p for p in problems), problems)
        # The vanilla crate's door in Freelancer's scene too: as under Freelancer, a note; the moved crate stays a
        # problem wherever its door comes from.
        fld = la.Scene(dict(list(fl.B.items()) + [(0x00C0C0C0C0C0C003, doors(19.0, 39.0))]))
        problems, notes = la.crate_faces(opt, opt.B[OVB], NAMES, walled, fld, fld.B[OVB], walled)
        self.assertEqual(len(problems), 1, problems)
        self.assertIn("Stash_A", problems[0])
        self.assertTrue(any("Stash_C" in x and "(as under Freelancer)" in x for x in notes), notes)
        # The spot fix's check refuses the moved crate.
        _, found = spot_fix.check(spec(MOVES=moves), "mild", opt, fl, {OPTB}, walled, la, NAMES, walled)
        self.assertTrue(any("Stash_A" in p and "in the doorway of door" in p for p in found), found)

    def test_what_counts_as_a_door(self):
        self.assertTrue(la.is_door(NAMES[0x0F09]))
        self.assertTrue(la.is_door("[assembly:/_pro/environment/templates/x.template?/gate_italy_mansion_a_dynamic"
                                   ".entitytemplate].pc_entitytype"))
        self.assertFalse(la.is_door(NAMES[0x0F0A]))  # furniture with a door
        self.assertFalse(la.is_door("[assembly:/_pro/environment/templates/x.template?/a_ext_door_3x7_frame_a.prim]"
                                    ".pc_entitytype"))
        self.assertFalse(la.is_door("[assembly:/_pro/design/sound/x.template?/ambience_sound_gate_transition_static"
                                    ".entitytemplate].pc_entitytype"))  # logic: a sound gate

    def test_a_moved_crate_keeps_no_tilt(self):
        b = override()
        m = b.find_property(STASH_A, "m_mTransform")["value"]["$val"]
        t = math.radians(11.5)  # The Icon's Fortress stash: 11.5 degrees about its x axis
        m["YAxis"] = {"x": 0.0, "y": math.cos(t), "z": math.sin(t)}
        m["ZAxis"] = {"x": 0.0, "y": -math.sin(t), "z": math.cos(t)}
        m["Trans"] = {"x": 40.0, "y": 3.0, "z": 0.0}
        sc = la.Scene({OVB: b})
        problems, _ = la.crate_faces(sc, b, NAMES, mesh(), la.Scene({OVB: override()}), override(), mesh())
        self.assertTrue(any("tilted 11.5 degrees" in p for p in problems), problems)


class Specs(unittest.TestCase):
    def test_every_spec_is_well_formed(self):
        maps, options, full = va.parse_variants(os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "core"))
        names = set()
        for s in spotfix_spec.SPECS:
            self.assertNotIn(s.NAME, names)
            names.add(s.NAME)
            self.assertEqual(full.get(s.OPTION), s.NAME, s.NAME)  # its FullVariant in LandslideFull.cpp
            self.assertIn(s, bb.VARIANTS)
            self.assertIs(va.FIX_SPECS[s.OPTION], s)
            for suffix, h in zip(("pc_entitytype", "pc_entityblueprint"), s.FIX_IDS):
                self.assertEqual(bb.gp.rid("[%s].%s" % (s.FIX_PATH, suffix)), h)
            self.assertEqual(bb.gp.rid("[%s].pc_entitytemplate" % s.SCENE_PATH), s.SCENE)
            self.assertEqual(s.FREELANCER_SCENES[0][1][0], s.SCENE)
            for path, temp, _ in s.APPEND_BRICKS:
                self.assertEqual(bb.gp.rid("[%s].pc_entitytype" % path), temp)
                self.assertIn(path.rsplit("/", 1)[-1][:-6], [b[0] for b in options[s.OPTION]["bricks"]])
            prints = {h for h, _ in s.FINGERPRINTS}
            self.assertTrue({s.OVERRIDE[0], s.FIX_SOURCE[0]} | {t for _, t, _ in s.APPEND_BRICKS} <= prints)
            # Every spoiled spot is moved or left by every slot; no slot is re-pointed onto one.
            olds = {old for old, _, _ in s.REPOINTS.values()}
            spots = {eid for eid, m in s.MOVES.items() if m["kind"] in spot_fix.KINDS}
            self.assertEqual(set(s.BROKEN), spots | olds, s.NAME)
            for old, new, why in s.REPOINTS.values():
                self.assertNotIn(new, s.BROKEN)
                self.assertNotEqual(old, new)
                self.assertTrue(why)
            for m in s.MOVES.values():
                self.assertIn(m["kind"], set(spot_fix.KINDS) | set(spot_fix.MEMBER_KINDS))
                self.assertTrue(m["why"])
                if m.get("yaw") is not None:  # a turn: a crate's open side (placement.crate_yaw), a supplier's NPC
                    self.assertIn(m["kind"], ("stash", "supplier"))
                    self.assertTrue(-180.0 <= m["yaw"] <= 180.0)
            # The partition is the map's (variant_audit.CHUNKS).
            self.assertIn(s.PARTITION[0], va.CHUNKS.values())


if __name__ == "__main__":
    unittest.main()
