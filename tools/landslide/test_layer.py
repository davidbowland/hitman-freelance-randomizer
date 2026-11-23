#!/usr/bin/env python3
"""Self-test of build_bricks.check_layer (the full Bad Boy's check of mission_caipirinha on the full Icon) on small
synthetic bricks (no game data): python3 -I tools/landslide/test_layer.py"""
import contextlib
import io
import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402

TYPES = {
    0x10: "[assembly:/templates/x.template?/zentity.entitytemplate].pc_entitytype",
    0x11: "[assembly:/templates/gameplay/ai2/actors.template?/npcactor.entitytemplate].pc_entitytype",
    0x12: "[assembly:/templates/x.template?/setpiece_evergreen_container_items_safe_unique.entitytemplate]"
          ".pc_entitytype",
    0x13: "[assembly:/_pro/design/setpieces/setpieces_evergreen.template?/exit_evergreen_pushbutton.entitytemplate]"
          ".pc_entitytype",
    0x14: "[assembly:/_pro/design/actor/acts/mr.template?/act_mr_stand_neutral.entitytemplate].pc_entitytype",
    0x15: "[assembly:/_pro/environment/templates/props/furniture/x.template?/old_chair.entitytemplate].pc_entitytype",
    0x16: "[assembly:/_pro/design/support/markup.template?/exit_point.entitytemplate].pc_entitytype",
}
TYPE_INDEX = {h: k for k, h in enumerate(TYPES)}
ROOT = 0xFFFFFFFFFFFFFFFE
MAP = 0x50               # the base brick (mission_copperhead's stand-in)
POOL_NPC, OTHER_NPC, SEQUENCE, GROUP, GROUPED_NPC = 0xD1, 0xD2, 0xD3, 0xD4, 0xD5
SPAWN_TARGETS, SAFE, EXIT = 0xA1, 0xB1, 0xB2
ARGUS, ARGUS_ACT, CHAIR, ESCAPE = 0xE1, 0xE2, 0xE3, 0xE4


def matrix(x, y, z):
    return {"XAxis": {"x": 1.0, "y": 0.0, "z": 0.0}, "YAxis": {"x": 0.0, "y": 1.0, "z": 0.0},
            "ZAxis": {"x": 0.0, "y": 0.0, "z": 1.0}, "Trans": {"x": x, "y": y, "z": z}}


def prop(name, value, typ="SEntityTemplateReference"):
    return {"nPropertyID": name, "value": {"$type": typ, "$val": value}}


def placed(x, y, z):
    return [prop("m_mTransform", matrix(x, y, z), "SMatrix43"), prop("m_eidParent", be.local_ref(0))]


def make(name, ents, scenes, editable=True):
    """A brick: ents are (id, name, type hash, properties, logical parent index or None for the root's child)."""
    temps, tblus = [], []
    for eid, ename, typ, props, parent in ents:
        lp = be.local_ref(-1) if eid == ROOT else be.local_ref(0 if parent is None else parent)
        temps.append({"logicalParent": lp, "entityTypeResourceIndex": TYPE_INDEX[typ], "propertyValues": props,
                      "postInitPropertyValues": [], "platformSpecificPropertyValues": []})
        tblus.append({"logicalParent": dict(lp), "entityTypeResourceIndex": 0, "entityId": eid, "editorOnly": False,
                      "entityName": ename, "propertyAliases": [], "exposedEntities": [], "exposedInterfaces": [],
                      "entitySubsets": []})
    n = len(TYPES)
    temp = {"subType": 2, "blueprintIndexInResourceHeader": n, "rootEntityIndex": 0, "subEntities": temps,
            "propertyOverrides": [], "externalSceneTypeIndicesInResourceHeader": list(range(n + 1, n + 1 + len(scenes)))}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": tblus,
            "externalSceneTypeIndicesInResourceHeader": list(range(1, 1 + len(scenes))),
            "pinConnections": [], "inputPinForwardings": [], "outputPinForwardings": [], "overrideDeletes": [],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    temp_refs = [(0x1F, h) for h in TYPES] + [(0x1F, 0x99)] + [(0x1F, h) for h in scenes]
    tblu_refs = [(0x1F, 0x98)] + [(0x1F, h) for h in scenes]
    return be.Brick(name, temp, tblu, temp_refs, tblu_refs, editable)


def base_map():
    return make("map", [
        (ROOT, "Scene", 0x10, [], None),
        (POOL_NPC, "NPC pool", 0x11, placed(1.0, 1.0, 0.0) + [prop("m_sActorName", "Pool One", "ZString")], None),
        (OTHER_NPC, "NPC other", 0x11, placed(2.0, 2.0, 0.0) + [prop("m_sActorName", "Other", "ZString")], None),
        (SEQUENCE, "Exit sequence", 0x10, [], None),
        (GROUP, "Group", 0x10, [], None),
        (GROUPED_NPC, "NPC grouped", 0x11, [prop("m_eidParent", be.local_ref(4)), prop("m_sActorName", "Grouped", "ZString")],
         4),
    ], [], editable=False)


def mission():
    return make("fr_mission_mild_test", [
        (ROOT, "Scene", 0x10, [], None),
        (SPAWN_TARGETS, "Helper_Evergreen_Spawn_Targets", 0x10,
         [prop("m_aActors", [be.ext_ref(0, POOL_NPC), be.ext_ref(0, GROUPED_NPC)],
               "TArray<SEntityTemplateReference>")], None),
    ], [MAP])


def override():
    return make("fr_override_test", [
        (ROOT, "Scene", 0x10, [], None),
        (SAFE, "Safe", 0x12, placed(10.0, 10.0, 0.0), None),
        (EXIT, "Exit_Evergreen_PushButton", 0x13, placed(30.0, 30.0, 0.0) + [prop("m_rSequence",
                                                                                be.ext_ref(0, SEQUENCE))], None),
    ], [MAP])


def layer(chair=(60.0, 60.0, 0.0), act=(50.5, 50.0, 0.0)):
    return make("mission_test_layer", [
        (ROOT, "Scene", 0x10, [], None),
        (ARGUS, "Argus", 0x11, placed(50.0, 50.0, 0.0) + [prop("m_sActorName", "Bartholomew Argus", "ZString")],
         None),
        (ARGUS_ACT, "Act", 0x14, placed(*act), None),
        (CHAIR, "Chair", 0x15, placed(*chair), None),
        (ESCAPE, "Exit_Point", 0x16, placed(70.0, 70.0, 0.0), None),
    ], [MAP])


class BaseSpec:
    NAME, DISPLAY, SPAWN_TARGETS = "base", "The Base", SPAWN_TARGETS


class Spec:
    NAME, DISPLAY = "layer", "The Layer"
    PROP_GAP_M, PROP_GAP_DZ = 1.5, 2.5

    def __init__(self, never=0):
        self.NEVER_TARGETS = [("Bartholomew Argus", "ab3fe438-acd0-49a4-a0f4-690c86a10324", never)]


def run(lay, spec=None):
    built = {"spec": BaseSpec, "mission": mission(), "override": override(), "loaded": {MAP: base_map()},
             "mission_rid": 0x60}
    audit = []
    with contextlib.redirect_stderr(io.StringIO()):
        npcs = bb.check_layer(spec or Spec(), lay, built, TYPES, audit)
    return npcs, audit


class LayerCheckTest(unittest.TestCase):
    def setUp(self):
        bb.NAMES.clear()
        bb.NAMES.update(TYPES)

    def fails(self, lay, text, spec=None):
        with self.assertRaises(SystemExit) as e:
            run(lay, spec)
        self.assertIn(text, str(e.exception))

    def test_a_clean_layer_passes_and_names_its_never_target(self):
        npcs, audit = run(layer())
        self.assertEqual(npcs, {ARGUS: "Bartholomew Argus"})
        self.assertTrue(any("never-target Bartholomew Argus" in line and "%016x" % ARGUS in line for line in audit))
        self.assertTrue(any(line.startswith("layer: exit ") and "exit_point" in line for line in audit))
        self.assertTrue(any("1 acts and 1 props placed" in line and "2 Freelancer spots" in line for line in audit))
        self.assertTrue(audit[-1].endswith("no pool actor, spot or exit is touched"))

    def test_deleting_a_pool_actor_fails(self):
        lay = layer()
        lay.add_override_delete(MAP, POOL_NPC)
        self.fails(lay, "deletes the pool actor 0000000000000050:00000000000000d1")

    def test_deleting_a_pool_actors_group_fails(self):
        lay = layer()
        lay.add_override_delete(MAP, GROUP)
        self.fails(lay, "deletes the pool actor 0000000000000050:00000000000000d5")

    def test_deleting_what_our_brick_names_fails(self):
        lay = layer()
        lay.add_override_delete(MAP, SEQUENCE)
        self.fails(lay, "fr_override_test's")

    def test_deleting_something_else_passes(self):
        lay = layer()
        lay.add_override_delete(MAP, OTHER_NPC)
        _, audit = run(lay)
        self.assertIn("layer: mission_test_layer deletes 1 in 0000000000000050", audit)

    def test_switching_a_pool_actor_off_fails(self):
        lay = layer()
        lay.temp["propertyOverrides"].append({"propertyOwner": be.ext_ref(0, POOL_NPC),
                                              "propertyValue": prop("m_bStartEnabled", False, "bool")})
        self.fails(lay, "switches the pool actor 0000000000000050:00000000000000d1 off")

    def test_another_override_on_a_pool_actor_is_noted(self):
        lay = layer()
        lay.temp["propertyOverrides"].append({"propertyOwner": be.ext_ref(0, POOL_NPC),
                                              "propertyValue": prop("m_sActorName", "Renamed", "ZString")})
        _, audit = run(lay)
        self.assertTrue(any("sets m_sActorName of the pool actor" in line for line in audit))

    def test_a_prop_on_a_spot_fails(self):
        self.fails(layer(chair=(10.5, 10.5, 0.0)), "its prop")

    def test_an_act_on_an_exit_fails(self):
        self.fails(layer(act=(30.3, 30.0, 0.0)), "its act")

    def test_a_prop_on_another_floor_passes(self):
        run(layer(chair=(10.5, 10.5, 5.0)))

    def test_a_listed_never_target_must_be_one_of_its_npcs(self):
        run(layer(), Spec(never=ARGUS))
        self.fails(layer(), "is not one of its NPCs", Spec(never=0xF0))


if __name__ == "__main__":
    unittest.main()
