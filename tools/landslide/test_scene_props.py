#!/usr/bin/env python3
"""Self-test of build_bricks.delete_scene_props (a spec's DELETE_SCENE_PROPS: props of the variant's own bricks that
stand on a Freelancer spot, deleted by our override brick), of the Sapienza specs' tables (landslide, author, icon),
of build_bricks' world folders, and of landslide_audit.py (prop kinds, blockers, the scene's world positions across
bricks, what counts as new, the navmesh comparison, the privacy check of suppliers, safes and stashes), on small
synthetic bricks (no game data):
python3 -I tools/landslide/test_scene_props.py"""
import math
import os
import sys
import types
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import landslide_audit as la  # noqa: E402
import author_spec  # noqa: E402
import icon_spec  # noqa: E402
import landslide_spec  # noqa: E402
from test_brick_edit import LOC, OTHER, entity, prop, sample  # noqa: E402

DRESS = 0x00DD
MACHINE, SPOT, FUSE = 0x61, 0x62, 0x63


def dressing():
    """root(0); Machine(1) placed in LOC's room; Spot(2) placed on the machine and highlighting it; Fuse(3), whose pin
    switches the machine."""
    ents = [
        entity(0xFFFFFFFFFFFFFFFE, "Scene", None),
        entity(MACHINE, "Machine", 0, [prop("m_eidParent", be.ext_ref(0, 0x4D))]),
        entity(SPOT, "Spot", 0, [prop("m_eidParent", be.local_ref(1)),
                                 prop("m_aUIHighlightGeom", [be.local_ref(1)], "TArray<SEntityTemplateReference>")]),
        entity(FUSE, "Fuse", 0),
    ]
    temp = {"subType": 2, "blueprintIndexInResourceHeader": 1, "rootEntityIndex": 0,
            "subEntities": [t for t, _ in ents], "propertyOverrides": [],
            "externalSceneTypeIndicesInResourceHeader": [2]}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": [b for _, b in ents],
            "externalSceneTypeIndicesInResourceHeader": [1],
            "pinConnections": [{"fromID": 3, "toID": 1, "fromPinName": "OnTrue", "toPinName": "TurnOn"}],
            "inputPinForwardings": [], "outputPinForwardings": [], "overrideDeletes": [],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    return be.Brick("dressing", temp, tblu, [(0x1F, 0x0D10), (0x1F, 0x0D20), (0x1F, LOC)],
                    [(0x1F, 0x0D21), (0x1F, LOC)], editable=False)


def setup(items, scenes=(LOC, OTHER, DRESS)):
    ov = sample()
    ov.set_external_scenes(list(scenes))
    base = sample()
    spec = types.SimpleNamespace(OVERRIDE_SCENES=list(scenes), DELETE_SCENE_PROPS={DRESS: items})
    src = dressing()
    return spec, ov, base, (lambda h: src if h == DRESS else None)


def deleted(ov):
    return sorted((ov.scenes[r["externalSceneIndex"]], r["entityID"]) for r in ov.tblu["overrideDeletes"]
                  if r["externalSceneIndex"] >= 0 and ov.scenes[r["externalSceneIndex"]] == DRESS)


class DeleteSceneProps(unittest.TestCase):
    def test_deletes_the_prop_and_what_stands_on_it(self):
        spec, ov, base, sb = setup([(MACHINE, "Machine", "on the stash"), (SPOT, "Spot", "on the machine")])
        audit = []
        bb.delete_scene_props(spec, ov, base, sb, audit)
        self.assertEqual(deleted(ov), [(DRESS, MACHINE), (DRESS, SPOT)])
        self.assertEqual(len(audit), 2)
        self.assertIn("Machine: on the stash", audit[0])
        ov.validate()

    def test_refuses_to_leave_what_stands_on_it(self):
        spec, ov, base, sb = setup([(MACHINE, "Machine", "on the stash")])
        with self.assertRaises(SystemExit) as e:
            bb.delete_scene_props(spec, ov, base, sb, [])
        self.assertIn("Spot", str(e.exception))

    def test_refuses_another_entity(self):
        spec, ov, base, sb = setup([(MACHINE, "Vending", "x"), (SPOT, "Spot", "y")])
        with self.assertRaises(SystemExit) as e:
            bb.delete_scene_props(spec, ov, base, sb, [])
        self.assertIn("not \"Vending\"", str(e.exception))

    def test_refuses_a_scene_the_override_does_not_name(self):
        spec, ov, base, sb = setup([(MACHINE, "Machine", "x"), (SPOT, "Spot", "y")], scenes=(LOC, OTHER))
        with self.assertRaises(SystemExit) as e:
            bb.delete_scene_props(spec, ov, base, sb, [])
        self.assertIn("OVERRIDE_SCENES", str(e.exception))

    def test_refuses_a_brick_that_is_not_read(self):
        spec, ov, base, _ = setup([(MACHINE, "Machine", "x"), (SPOT, "Spot", "y")])
        with self.assertRaises(SystemExit) as e:
            bb.delete_scene_props(spec, ov, base, lambda h: None, [])
        self.assertIn("--json", str(e.exception))

    def test_refuses_a_reference_from_the_base(self):
        spec, ov, base, sb = setup([(MACHINE, "Machine", "x"), (SPOT, "Spot", "y")])
        base.set_external_scenes([LOC, OTHER, DRESS])
        base.set(0x33, "m_rOut", be.ext_ref(2, MACHINE))
        with self.assertRaises(SystemExit) as e:
            bb.delete_scene_props(spec, ov, base, sb, [])
        self.assertIn("m_rOut", str(e.exception))


SIT, STAND = 0x0011, 0x0012  # two act blueprints: SIT is in vanilla's Replicable subset, STAND never is


def two_types():
    """sample() with a second entity type (blueprint STAND, TBLU reference 1) and the root's Replicable subset
    holding Keeper(3) and Sibling(4), both SIT: the shape of override_bingo's moved acts."""
    b = sample()
    b.tblu_refs.insert(1, (0x1F, STAND))
    b.tblu["externalSceneTypeIndicesInResourceHeader"] = [2, 3]
    b.temp_refs.insert(1, (0x1F, 0x0013))
    b.temp["externalSceneTypeIndicesInResourceHeader"] = [3, 4]
    b.temp["blueprintIndexInResourceHeader"] = 2
    b.tblu["subEntities"][0]["entitySubsets"] = [["Replicable", {"entities": [3, 4]}]]
    b.tblu["subEntities"][5]["entityTypeResourceIndex"] = 1  # UnderGroup is a STAND, in no subset
    b.temp["subEntities"][5]["entityTypeResourceIndex"] = 1
    b.validate()
    return b


class SubsetTypes(unittest.TestCase):
    """build_bricks.check_subset_types: every member of an entity subset of a built brick must be of a type known to
    carry that subset, or the game crashes building the brick (the full Icon, HITMAN3.exe+0xF2DE19)."""

    def check(self, built, vanilla, roots=None):
        args = types.SimpleNamespace(json=[], bin=None, navp=None)
        old = bb.blueprint_root_subsets
        bb.blueprint_root_subsets = lambda a, h: (roots or {}).get(h)
        try:
            audit = []
            bb.check_subset_types("test", [built], [vanilla], args, audit)
            return audit
        finally:
            bb.blueprint_root_subsets = old

    def retyped_in_place(self):
        """The bug: Sibling(4) given STAND's type while it stays in Replicable."""
        b = two_types()
        b.tblu["subEntities"][4]["entityTypeResourceIndex"] = 1
        b.temp["subEntities"][4]["entityTypeResourceIndex"] = 1
        return b

    def test_a_member_retyped_to_a_type_without_the_subset_stops_the_build(self):
        with self.assertRaises(SystemExit) as e:
            self.check(self.retyped_in_place(), two_types())
        self.assertIn("Sibling is in fffffffffffffffe Scene's subset Replicable", str(e.exception))
        self.assertIn("is in no vanilla brick's Replicable subset", str(e.exception))

    def test_a_blueprint_root_without_the_subset_is_named(self):
        with self.assertRaises(SystemExit) as e:
            self.check(self.retyped_in_place(), two_types(), {STAND: []})
        self.assertIn("has no Replicable subset (its blueprint's root has none)", str(e.exception))
        problems = be.subset_type_problems(self.retyped_in_place(), set(), lambda h: {STAND: []}.get(h))
        self.assertEqual([d for _, d in problems], [False, True])  # SIT: no evidence; STAND: read, has none

    def test_a_blueprint_root_with_the_subset_passes(self):
        self.check(self.retyped_in_place(), two_types(), {STAND: ["Replicable"]})

    def test_retype_follows_the_like_entity_out_of_the_subset(self):
        b = two_types()
        b.retype(0x44, 0x55)  # Sibling like UnderGroup (STAND, in no subset): it leaves Replicable
        audit = self.check(b, two_types())
        self.assertEqual(b.tblu["subEntities"][0]["entitySubsets"], [["Replicable", {"entities": [3]}]])
        self.assertIn("1 members of 1 bricks", audit[0])

    def test_vanilla_evidence_counts_every_brick_read(self):
        b = self.retyped_in_place()
        other = two_types()
        other.tblu["subEntities"][3]["entityTypeResourceIndex"] = 1  # another vanilla brick has a STAND in it
        self.assertEqual(be.subset_type_evidence([other, None]), {("Replicable", STAND), ("Replicable", SIT)})
        args = types.SimpleNamespace(json=[], bin=None, navp=None)
        bb.check_subset_types("test", [b], [two_types(), other], args, [])


class LandslideTable(unittest.TestCase):
    def test_each_brick_is_loaded_by_the_override_and_fingerprinted(self):
        prints = {h for h, _ in landslide_spec.FINGERPRINTS}
        for h, items in landslide_spec.DELETE_SCENE_PROPS.items():
            self.assertIn(h, landslide_spec.OVERRIDE_SCENES)
            self.assertIn(h, prints)
            self.assertTrue(items)
            for eid, name, why in items:
                self.assertTrue(name and why and 0 < eid < 1 << 64)

    def test_the_vending_machine_and_its_spot(self):
        items = landslide_spec.DELETE_SCENE_PROPS[landslide_spec.DRESSING_MISSION_MAMBA[0]]
        self.assertEqual([(e, n) for e, n, _ in items], [(0x61996CFCD6565A8E, "Vending_Machine_Soda_B"),
                                                         (0xF93114CBB9EF4AB5, "Setpiece_Hips_Stand_LookAtWares")])


class SapienzaTables(unittest.TestCase):
    def test_author_deletes_the_bed(self):
        prints = {h for h, _ in author_spec.FINGERPRINTS}
        self.assertEqual(list(author_spec.DELETE_SCENE_PROPS), [author_spec.SCENARIO_EBOLA[0]])
        for h, items in author_spec.DELETE_SCENE_PROPS.items():
            self.assertIn(h, author_spec.OVERRIDE_SCENES)
            self.assertIn(h, prints)
        # The wooden crate stays since its stash moved (SPOT_MOVES).
        self.assertEqual([(e, n) for e, n, _ in author_spec.DELETE_SCENE_PROPS[author_spec.SCENARIO_EBOLA[0]]],
                         [(0xF1441CF7494072CC, "Bed_Masters_Italy_C_00")])
        self.assertEqual(len(set(author_spec.OVERRIDE_SCENES)), len(author_spec.OVERRIDE_SCENES))

    def test_author_moves_the_radio_tower_stash_out_of_the_gas_works(self):
        m = author_spec.SPOT_MOVES[0xAC7078F3BA0A6BC2]
        self.assertEqual((m["kind"], m.get("yaw")), ("stash", None))  # the vanilla rotation opens it to the door
        self.assertEqual(set(author_spec.SPOT_MOVES), {0xAC7078F3BA0A6BC2})
        # Its two slots still name it.
        self.assertEqual(author_spec.SLOT_REPOINTS["availableStash07"][1], 0xAC7078F3BA0A6BC2)
        self.assertNotIn("availableStash11", author_spec.SLOT_REPOINTS)
        # The place depends on The Author's navmesh: fingerprinted.
        self.assertIn((author_spec.NAVMESH[0], author_spec.NAVMESH[1]), author_spec.FINGERPRINTS)

    def test_the_crate_check_runs_for_every_sapienza_full_version(self):
        # build_bricks.check_crates: the variant's navmesh against Freelancer's mission01.navp.
        for spec in (landslide_spec, author_spec, icon_spec):
            self.assertEqual(spec.FREELANCER_NAVMESH, (0x008980F97BE736B2, "mission01.navp"))
            vh = (getattr(spec, "CRATE_NAVMESH", None) or spec.NAVMESH)[0]
            self.assertNotEqual(vh, spec.FREELANCER_NAVMESH[0])
        self.assertEqual(landslide_spec.CRATE_NAVMESH[0], 0x00AC273EDE6FDC08)
        self.assertEqual(author_spec.NAVMESH[0], 0x00B7A403A6517E2A)

    def test_icon_moves_the_toilet_stash_and_drops_the_road_exit(self):
        m = icon_spec.SPOT_MOVES[0xF4BFBC679E57741E]
        self.assertEqual(m["kind"], "stash")
        self.assertIn(0xD5AA959458203ABF, icon_spec.DELETE_EXITS)
        self.assertNotIn(0xD5AA959458203ABF, icon_spec.UNMOVED_EXITS)
        self.assertFalse(set(icon_spec.DELETE_EXITS) & set(icon_spec.EXIT_MOVES))

    def test_accepted_clashes_are_well_formed(self):
        for spec in (landslide_spec, author_spec, icon_spec):
            for (h, eid), why in getattr(spec, "CLASH_ACCEPTED", {}).items():
                self.assertTrue(0 < h < 1 << 56 and 0 < eid < 1 << 64 and why)

    def test_the_world_folder_keeps_its_transform(self):
        # A brick whose entity hangs off another brick's folder entity: the folder's matrix is composed.
        ents = [entity(0xFFFFFFFFFFFFFFFE, "Scene", None),
                entity(0x71, "Box", 0, [prop("m_eidParent", be.ext_ref(0, 0xF0)),
                                        prop("m_mTransform", matrix(1.0, 2.0, 0.5), "SMatrix43")])]
        temp = {"subEntities": [t for t, _ in ents], "rootEntityIndex": 0}
        moved = [[1.0, 0.0, 0.0, 34.0], [0.0, 1.0, 0.0, -35.0], [0.0, 0.0, 1.0, 12.0]]
        self.assertEqual(bb.World(temp, {0xF0}).pos(1), (1.0, 2.0, 0.5))
        self.assertEqual(bb.World(temp, {0xF0: moved}).pos(1), (35.0, -33.0, 12.5))
        self.assertIsNone(bb.World(temp).pos(1))


def matrix(x, y, z, yaw=0.0):
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    return {"XAxis": {"x": c, "y": s, "z": 0.0}, "YAxis": {"x": -s, "y": c, "z": 0.0}, "ZAxis": {"x": 0.0, "y": 0.0,
            "z": 1.0}, "Trans": {"x": x, "y": y, "z": z}}


def brick(name, ents, scenes, deletes=(), overrides=()):
    """A read-only brick: ents [(id, name, parent index or None, [props])], scenes its external scene hashes."""
    made = [entity(eid, n, par, ps) for eid, n, par, ps in ents]
    k = len(scenes)
    temp = {"subType": 2, "blueprintIndexInResourceHeader": 1, "rootEntityIndex": 0,
            "subEntities": [t for t, _ in made], "propertyOverrides": list(overrides),
            "externalSceneTypeIndicesInResourceHeader": list(range(2, 2 + k))}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": [b for _, b in made],
            "externalSceneTypeIndicesInResourceHeader": list(range(1, 1 + k)),
            "pinConnections": [], "inputPinForwardings": [], "outputPinForwardings": [],
            "overrideDeletes": list(deletes), "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    return be.Brick(name, temp, tblu, [(0x1F, 0x0E10), (0x1F, 0x0E20)] + [(0x1F, h) for h in scenes],
                    [(0x1F, 0x0E21)] + [(0x1F, h) for h in scenes], editable=False)


ROOT = 0xFFFFFFFFFFFFFFFE
LOCB, DRESSB, FLB = 0x0A01, 0x0A02, 0x0A03


def scenes():
    """location (a room at (10, 0, 5) holding a crate at (1, 0, 0) and a lamp), a variant dressing (a machine in
    location's room, a box that moves location's lamp, a delete of location's crate) and a Freelancer brick (which
    deletes the same lamp)."""
    loc = brick("location", [(ROOT, "Scene", None, []),
                             (0x10, "Room", 0, [prop("m_mTransform", matrix(10.0, 0.0, 5.0), "SMatrix43")]),
                             (0x11, "Crate", 1, [prop("m_eidParent", be.local_ref(1)),
                                                 prop("m_mTransform", matrix(1.0, 0.0, 0.0), "SMatrix43")]),
                             (0x12, "Lamp", 1, [prop("m_eidParent", be.local_ref(1)),
                                                prop("m_mTransform", matrix(0.0, 3.0, 0.0), "SMatrix43")])], [])
    move = {"propertyOwner": be.ext_ref(0, 0x12), "propertyValue": prop("m_mTransform", matrix(0.0, 4.0, 0.0),
                                                                          "SMatrix43")}
    dress = brick("dressing", [(ROOT, "Scene", None, []),
                               (0x21, "Machine", 0, [prop("m_eidParent", be.ext_ref(0, 0x10)),
                                                     prop("m_mTransform", matrix(2.0, 0.0, 0.0), "SMatrix43")])],
                  [LOCB], deletes=[be.ext_ref(0, 0x11)], overrides=[move])
    fl = brick("freelancer", [(ROOT, "Scene", None, [])], [LOCB], deletes=[be.ext_ref(0, 0x12)])
    return loc, dress, fl


class AuditScene(unittest.TestCase):
    def test_world_positions_cross_bricks_with_overrides_and_deletes(self):
        loc, dress, _ = scenes()
        sc = la.Scene({LOCB: loc, DRESSB: dress})
        self.assertEqual(sc.pos(DRESSB, 1), (12.0, 0.0, 5.0))  # placed in location's room
        self.assertEqual(sc.pos(LOCB, 3), (10.0, 4.0, 5.0))  # moved by the dressing's override
        self.assertTrue(sc.gone(LOCB, 2))  # deleted by the dressing
        self.assertFalse(sc.gone(LOCB, 3))
        self.assertTrue(la.Scene({LOCB: loc}, {(LOCB, 0x10)}).gone(LOCB, 2))  # under a deleted parent

    def test_new_content_against_freelancer(self):
        loc, dress, fl = scenes()
        names = {0x0E10: "[assembly:/_pro/environment/templates/props/x.template?/crate.entitytemplate].pc_entitytype"}
        sc, base = la.Scene({LOCB: loc, DRESSB: dress}), la.Scene({LOCB: loc, FLB: fl})
        got = {(h, sc.B[h].entity_name(i)): why for h, i, kind, _, _, why in la.new_content(sc, base, names, set())}
        # the machine is new; the lamp Freelancer deletes is back (and moved); the room and the deleted crate are not
        self.assertEqual(sorted(got), [(LOCB, "Lamp"), (DRESSB, "Machine")])
        self.assertEqual(got[(DRESSB, "Machine")], "new")
        self.assertEqual(got[(LOCB, "Lamp")], "deleted under Freelancer")
        self.assertNotIn((LOCB, "Crate"), got)

    def test_a_copy_at_the_same_place_is_no_news(self):
        loc, dress, fl = scenes()
        names = {0x0E10: "[assembly:/_pro/environment/templates/props/x.template?/crate.entitytemplate].pc_entitytype"}
        twin = brick("twin", [(ROOT, "Scene", None, []),
                              (0x31, "Room copy", 0, [prop("m_mTransform", matrix(10.0, 0.0, 5.0), "SMatrix43")])], [])
        sc, base = la.Scene({0x0A04: twin}), la.Scene({LOCB: loc})
        self.assertEqual(la.new_content(sc, base, names, set()), [])

    def test_clash_rules(self):
        loc, dress, fl = scenes()
        names = {0x0E10: "[assembly:/_pro/environment/templates/props/x.template?/crate.entitytemplate].pc_entitytype"}
        sc = la.Scene({LOCB: loc, DRESSB: dress})
        content = la.new_content(sc, la.Scene({FLB: fl}), names, set())
        machine = [c for c in content if c[0] == DRESSB]
        clashes, notes = la.audit(sc, machine, [("stash A", (12.3, 0.2, 5.0)), ("stash B", (12.9, 0.0, 5.0))])
        self.assertEqual(len(clashes), 1)
        self.assertTrue(clashes[0].startswith("stash A"))
        self.assertEqual(len(notes), 1)  # 0.9 m away: a note, unless the navmesh lost the floor there
        clashes, _ = la.audit(sc, machine, [("stash B", (12.9, 0.0, 5.0))], no_floor=["stash B"])
        self.assertEqual(len(clashes), 1)
        clashes, notes = la.audit(sc, machine, [("stash A", (12.3, 0.2, 5.0))], accepted={(DRESSB, 0x21): "by design"})
        self.assertEqual(clashes, [])
        self.assertTrue(notes[0].startswith("ACCEPTED") and notes[0].endswith("by design"))

    def test_blockers(self):
        w = bb.matrix(matrix(0.0, 0.0, 1.0, yaw=90.0))
        self.assertTrue(la.in_blocker((0.5, 0.2, 0.0), w, ("box", (1.0, 2.0, 2.0)))[0])
        self.assertTrue(la.in_blocker((0.9, 0.2, 0.0), w, ("box", (1.0, 2.0, 2.0)))[0])  # x 0.9 is the box's y
        self.assertFalse(la.in_blocker((0.2, 0.9, 0.0), w, ("box", (1.0, 2.0, 2.0)))[0])  # the box's x: 0.5 + 0.3
        self.assertFalse(la.in_blocker((0.0, 0.0, 2.5), w, ("box", (1.0, 2.0, 2.0)))[0])  # above it
        self.assertTrue(la.in_blocker((1.2, 0.0, 0.0), w, ("capsule", (1.0, 1.0)))[0])
        self.assertFalse(la.in_blocker((1.5, 0.0, 0.0), w, ("capsule", (1.0, 1.0)))[0])
        self.assertEqual(la.blocker_shape({"m_vBoxSize": {"x": 1.0, "y": 2.0, "z": 3.0}}), ("box", (1.0, 2.0, 3.0)))
        self.assertEqual(la.blocker_shape({"0x%08X" % zlib.crc32(b"m_fCapsuleRadius"): 0.5}), ("capsule", (0.5, 0.0)))

    def test_navmesh_notes(self):
        class Mesh:
            def __init__(self, floor, clear):
                self.floor, self.clear = floor, clear

            def area_at(self, q, up=0.5, down=0.5):
                return (None, q[2]) if self.floor else None

            def clearance(self, q):
                return self.clear
        places = [("p", (0.0, 0.0, 0.0))]
        self.assertEqual(la.navmesh_notes(places, Mesh(True, 1.0), Mesh(True, 0.9)), [])
        self.assertTrue(la.navmesh_notes(places, Mesh(True, 1.0), Mesh(False, 0.0))[0][1])
        self.assertFalse(la.navmesh_notes(places, Mesh(True, 1.0), Mesh(True, 0.3))[0][1])
        self.assertEqual(la.navmesh_notes(places, Mesh(False, 0.0), Mesh(False, 0.0)), [])


class AuditPrivacy(unittest.TestCase):
    def test_acts_that_keep_a_spot_busy(self):
        # A supplier at the origin whose NPC (0x40) plays the act 0x42 through a Role (0x41), with a 4 x 4 x 3 m
        # private volume centered at (8, 0, 1.5); a stash at (20, 0, 0). Every entity's type is an act here.
        at = lambda x, y, z: prop("m_mTransform", matrix(x, y, z), "SMatrix43")  # noqa: E731
        b = brick("acts", [(ROOT, "Scene", None, []),
                           (0x40, "Supplier NPC", 0, []),
                           (0x41, "Role", 0, [prop("m_rActorProvider", be.local_ref(1))]),
                           (0x42, "own act", 0, [at(1.0, 0.0, 0.0), prop("m_pActor", be.local_ref(2))]),
                           (0x43, "act 3 m away", 0, [at(3.0, 0.0, 0.2)]),
                           (0x44, "act in the volume", 0, [at(8.0, 1.5, 0.0)]),
                           (0x45, "act upstairs", 0, [at(2.0, 0.0, 5.0)]),
                           (0x46, "act by the stash", 0, [at(21.5, 0.0, 0.0)]),
                           (0x47, "act 2.5 m from the stash", 0, [at(22.5, 0.0, 0.0)]),
                           (0x48, "act past the volume", 0, [at(10.5, 0.0, 0.0)])], [])
        names = {0x0E10: "[assembly:/_pro/design/actor/acts/mr.template?/act_mr_stand_angry.entitytemplate]"
                         ".pc_entitytype"}
        h = 0x0A05
        sc = la.Scene({h: b})
        self.assertEqual(la.actor_of(sc, h, 3), (h, 1))  # through the Role to the NPC
        acts = la.scene_acts(sc, names)
        self.assertEqual(len(acts), 7)  # the root, the NPC and the Role have no place
        places = [("supplier", 0x99, "supplier S", (0.0, 0.0, 0.0), {(h, 1)}, [(bb.matrix(matrix(8.0, 0.0, 1.5)),
                                                                                (4.0, 4.0, 3.0))]),
                  ("stash", 0x98, "stash T", (20.0, 0.0, 0.0), set(), [])]
        got = la.privacy(sc, places, acts, names)
        self.assertEqual(sorted(got), [(0x98, h, 0x46), (0x99, h, 0x43), (0x99, h, 0x44)])
        self.assertIn("inside its private volume", got[(0x99, h, 0x44)])
        self.assertIn("3.00 m in plan, +0.20 m up", got[(0x99, h, 0x43)])
        # The volume's floor: an act 0.3 m below its bottom face counts, 0.5 m below does not.
        w = bb.matrix(matrix(0.0, 0.0, 1.5))
        self.assertTrue(la.in_volume((0.0, 0.0, -0.3), w, (4.0, 4.0, 3.0))[0])
        self.assertFalse(la.in_volume((0.0, 0.0, -0.5), w, (4.0, 4.0, 3.0))[0])

    def test_badboy_is_the_icon_with_its_layer(self):
        self.assertIs(la.built_spec(la.SPECS["badboy"]), icon_spec)
        self.assertEqual(la.layer_temps(la.SPECS["badboy"]), [la.badboy_spec.MISSION_CAIPIRINHA[0]])
        self.assertIs(la.built_spec(icon_spec), icon_spec)
        self.assertEqual(la.layer_temps(icon_spec), [])

    def test_icon_suppliers_are_clear_of_the_acts_that_kept_them_busy(self):
        # The first places were never free of NPCs (the first full Bad Boy session).
        self.assertNotEqual(icon_spec.SPOT_MOVES[0xFADD3FE97A972ADA]["to"], (66.71, -61.48, 15.20))
        self.assertNotEqual(icon_spec.SPOT_MOVES[0xFADD3FE97A972ADA]["anchor"], 0xC24C0B4B44E1173A)
        self.assertNotEqual(icon_spec.SPOT_MOVES[0x48ADF8D57AEB75FC]["to"], (28.00, 8.00, 11.73))


class AuditFloor(unittest.TestCase):
    class Mesh:
        """A navmesh floor at 0.15 + slope * y everywhere (area_at as navp.NavMesh's: the floor from down below the
        point to up above it)."""

        def __init__(self, slope=0.0):
            self.slope = slope

        def area_at(self, q, up=1.0, down=1.5):
            z = 0.15 + self.slope * q[1]
            return (None, z) if -up <= q[2] - z <= down else None

    def test_sunk_below_the_act_or_litter_beside_it(self):
        refs = [("litter", "litter L", (0.17, 0.0, 0.034)), ("act", "act A", (0.85, 0.0, 0.0))]
        sunk, sloped = la.sunk([("clue C", (0.0, 0.0, 0.0))], self.Mesh(), refs)
        self.assertEqual(sloped, [])
        self.assertEqual(len(sunk), 1)
        self.assertIn("0.034 m below the litter L", sunk[0])  # the nearest one
        # Within SUNK_M, or the nearest farther than SUNK_REF_M or SUNK_REF_DZ higher: nothing.
        for r in [("act", "a", (0.5, 0.0, 0.009)), ("act", "a", (2.5, 0.0, 0.1)), ("act", "a", (0.5, 0.0, 0.35))]:
            self.assertEqual(la.sunk([("clue C", (0.0, 0.0, 0.0))], self.Mesh(), [r]), ([], []))
        # A clue on a table (the navmesh 0.8 m below it) is not a floor place.
        self.assertEqual(la.sunk([("clue T", (0.0, 0.0, 0.95))], self.Mesh(), [("act", "a", (0.5, 0.0, 1.0))]),
                         ([], []))
        # Without a navmesh every place counts.
        self.assertEqual(len(la.sunk([("clue T", (0.0, 0.0, 0.95))], None, [("act", "a", (0.5, 0.0, 1.0))])[0]), 1)

    def test_an_act_up_a_slope_is_higher_by_right(self):
        # The navmesh rises 0.1 m per meter in y: an act 1 m up the street 0.1 m higher is on the same slope; 0.2 m
        # higher, the place is 0.1 m in the floor.
        sunk, sloped = la.sunk([("clue C", (0.0, 0.0, 0.0))], self.Mesh(0.1), [("act", "a", (0.0, 1.0, 0.1))])
        self.assertEqual((len(sunk), len(sloped)), (0, 1))
        self.assertIn("the navmesh rises +0.100 m", sloped[0])
        sunk, sloped = la.sunk([("clue C", (0.0, 0.0, 0.0))], self.Mesh(0.1), [("act", "a", (0.0, 1.0, 0.2))])
        self.assertEqual((len(sunk), len(sloped)), (1, 0))

    def test_floor_refs_are_acts_and_litter(self):
        b = brick("refs", [(ROOT, "Scene", None, []),
                           (0x50, "Litter_B_00_04", 0, [prop("m_mTransform", matrix(82.41, -20.03, 12.0),
                                                             "SMatrix43")])], [])
        sc = la.Scene({0x0A06: b})
        litter = {0x0E10: "[assembly:/_pro/environment/templates/props/trash/litter_b.template?/litter_b_00"
                          ".entitytemplate].pc_entitytype"}
        cup = {0x0E10: "[assembly:/_pro/environment/templates/props/food/icecream.template?/icecream_cup_stack_a"
                       ".entitytemplate].pc_entitytype"}
        got = la.scene_floor_refs(sc, litter)
        self.assertEqual([(k, p) for k, _, p in got], [("litter", (82.41, -20.03, 12.0))])
        self.assertEqual(la.scene_floor_refs(sc, cup), [])  # a small prop, but not one lying on the floor
        self.assertEqual(la.scene_floor_refs(sc, litter, skip={0x0A06}), [])  # our own bricks are no witness
        # The build's own list (build_bricks.litter_points, placement.Placement's floor_refs).
        self.assertEqual(bb.litter_points(b.temp, b.temp_refs, litter), [(82.41, -20.03, 12.0)])
        self.assertEqual(bb.litter_points(b.temp, b.temp_refs, cup), [])


class AuditKinds(unittest.TestCase):
    def test_kinds(self):
        k = la.kind_of
        self.assertEqual(k("[assembly:/_pro/environment/templates/props/street_props/street_electrical_props_a.template"
                           "?/vending_machine_soda_b.entitytemplate].pc_entitytype"), "prop")
        self.assertEqual(k("[assembly:/_pro/design/setpieces/setpieces_distractions.template?/"
                           "setpiece_distraction_power_fusebox_modern.entitytemplate].pc_entitytype"), "prop")
        self.assertEqual(k("[assembly:/_pro/environment/templates/props/wires/wires.template?/"
                           "wire_logic.entitytemplate].pc_entitytype"), "small")
        self.assertEqual(k("[assembly:/_pro/design/setpieces/setpieces_hideinplainsight.template?/"
                           "setpiece_hips_stand_lookatwares.entitytemplate].pc_entitytype"), "setpiece")
        self.assertEqual(k("[assembly:/templates/markup/actorambience.template?/waypoint.entitytemplate]"
                           ".pc_entitytype"), "logic")
        self.assertEqual(k("[modules:/zitemspawner.class].pc_entitytype"), "item")
        self.assertEqual(k("[assembly:/_pro/design/actor/acts/mr.template?/act_mr_stand_angry.entitytemplate]"
                           ".pc_entitytype"), "act")
        self.assertEqual(k("[assembly:/templates/physics/collisionvolumes.template?/collisionbox.entitytemplate]"
                           ".pc_entitytype"), "blocker")
        self.assertEqual(k("[assembly:/_pro/scenes/missions/coastaltown/templates-mission_copperhead/copperhead_props"
                           ".template?/movie_set_roadcase_b_02.entitytemplate].pc_entitytype"), "prop")
        self.assertEqual(k("[assembly:/_pro/environment/templates/props/street_props/x.template?/"
                           "extension_cord_black_a_00.entitytemplate].pc_entitytype"), "small")


if __name__ == "__main__":
    unittest.main()
