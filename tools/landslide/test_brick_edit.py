#!/usr/bin/env python3
"""Self-test of brick_edit.py on a small synthetic brick (no game data): python3 -I tools/landslide/test_brick_edit.py"""
import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import brick_edit as be  # noqa: E402

LOC, OTHER, NEW = 0x00AA, 0x00BB, 0x00CC


def entity(eid, name, parent, props=None):
    temp = {"logicalParent": be.local_ref(parent) if parent is not None else be.local_ref(-1),
            "entityTypeResourceIndex": 0, "propertyValues": props or [], "postInitPropertyValues": [],
            "platformSpecificPropertyValues": []}
    tblu = {"logicalParent": dict(temp["logicalParent"]), "entityTypeResourceIndex": 0, "entityId": eid,
            "editorOnly": False, "entityName": name, "propertyAliases": [], "exposedEntities": [],
            "exposedInterfaces": [], "entitySubsets": []}
    return temp, tblu


def prop(name, value, typ="SEntityTemplateReference"):
    return {"nPropertyID": name, "value": {"$type": typ, "$val": value}}


def sample():
    """root(0) <- group(1) <- leaf(2); keeper(3) refers to leaf's sibling(4) and into OTHER; 5 under group."""
    ents = [
        entity(0xFFFFFFFFFFFFFFFE, "Scene", None),
        entity(0x11, "Group", 0),
        entity(0x22, "Leaf", 1, [prop("m_rNext", be.local_ref(5))]),
        entity(0x33, "Keeper", 0, [prop("m_rTarget", be.local_ref(4)), prop("m_rOut", be.ext_ref(1, 0x99)),
                                   prop("m_aList", [be.local_ref(4), be.ext_ref(0, 0x77)], "TArray<SEntityTemplateReference>")]),
        entity(0x44, "Sibling", 0),
        entity(0x55, "UnderGroup", 1),
    ]
    temp = {"subType": 2, "blueprintIndexInResourceHeader": 1, "rootEntityIndex": 0,
            "subEntities": [t for t, _ in ents], "propertyOverrides": [],
            "externalSceneTypeIndicesInResourceHeader": [2, 3]}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": [b for _, b in ents],
            "externalSceneTypeIndicesInResourceHeader": [1, 2],
            "pinConnections": [{"fromID": 2, "toID": 5, "fromPinName": "a", "toPinName": "b"},
                               {"fromID": 3, "toID": 4, "fromPinName": "c", "toPinName": "d"}],
            "inputPinForwardings": [], "outputPinForwardings": [], "overrideDeletes": [be.ext_ref(1, 0x98)],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    temp_refs = [(0x1F, 0x0010), (0x1F, 0x0020), (0x1F, LOC), (0x1F, OTHER)]
    tblu_refs = [(0x1F, 0x0011), (0x1F, LOC), (0x1F, OTHER)]
    return be.Brick("sample", temp, tblu, temp_refs, tblu_refs)


class BrickEditTest(unittest.TestCase):
    def test_delete_subtree_reindexes(self):
        b = sample()
        gone = b.delete([0x11])
        self.assertEqual(sorted(gone), [0x11, 0x22, 0x55])
        self.assertEqual(b.ids(), [0xFFFFFFFFFFFFFFFE, 0x33, 0x44])
        self.assertEqual(b.get(0x33, "m_rTarget")["entityIndex"], 2)
        self.assertEqual(b.tblu["pinConnections"], [{"fromID": 1, "toID": 2, "fromPinName": "c", "toPinName": "d"}])
        b.validate()

    def test_delete_refuses_a_dangling_reference(self):
        b = sample()
        with self.assertRaises(be.BrickError) as e:
            b.delete([0x44])
        self.assertIn("Keeper", str(e.exception))

    def test_delete_refuses_a_pin_from_a_kept_entity(self):
        b = sample()
        with self.assertRaises(be.BrickError):
            b.delete([0x55], subtree=False)

    def test_scenes_remap_and_drop(self):
        b = sample()
        b.set(0x33, "m_aList", [be.local_ref(4)])
        dropped = b.set_external_scenes([OTHER, NEW])
        self.assertEqual(dropped, {})
        self.assertEqual(b.get(0x33, "m_rOut")["externalSceneIndex"], 0)
        self.assertEqual(b.temp_refs[-2:], [(0x1F, OTHER), (0x1F, NEW)])
        self.assertEqual(b.temp["externalSceneTypeIndicesInResourceHeader"], [2, 3])
        self.assertEqual(b.tblu_refs[-2:], [(0x1F, OTHER), (0x1F, NEW)])
        b.validate()

    def test_a_reference_into_a_dropped_scene_is_an_error_but_its_deletes_go(self):
        b = sample()
        with self.assertRaises(be.BrickError):
            b.set_external_scenes([OTHER])
        b = sample()
        b.set(0x33, "m_rOut", be.ext_ref(0, 0x99))
        dropped = b.set_external_scenes([LOC])
        self.assertEqual(dropped, {OTHER: 1})
        self.assertEqual(b.tblu["overrideDeletes"], [])

    def test_missing_or_moved_ids_fail(self):
        b = sample()
        with self.assertRaises(be.BrickError):
            b.index_of(0x1234)
        with self.assertRaises(be.BrickError):
            b.set(0x33, "m_rTarget", be.local_ref(2), expect=be.local_ref(3))

    def test_pin_overrides(self):
        b = sample()
        b.add_pin_override(0x33, "Exit", (OTHER, 0x5150), "Start")
        b.remove_pin_override(0x33, "Exit", (OTHER, 0x5150), "Start")
        with self.assertRaises(be.BrickError):
            b.remove_pin_override(0x33, "Exit", (OTHER, 0x5150), "Start")

    def test_remove_pin_overrides_of_then_delete(self):
        b = sample()
        b.add_pin_override(0x55, "Exit", (OTHER, 0x5150), "Start")
        b.add_pin_override((OTHER, 0x5150), "Over", 0x55, "Trigger")
        b.add_pin_override(0x33, "Exit", (OTHER, 0x5151), "Start")
        with self.assertRaises(be.BrickError):
            b.delete([0x55], subtree=False)  # its pin overrides still name it (and a pin connection)
        self.assertEqual(b.remove_pin_overrides_of(0x55), 2)
        self.assertEqual(len(b.tblu["pinConnectionOverrides"]), 1)
        self.assertEqual(b.remove_pin_overrides_of(0x55), 0)

    def test_override_delete_and_its_property_overrides(self):
        b = sample()
        b.temp["propertyOverrides"] = [
            {"propertyOwner": be.ext_ref(1, 0x97), "propertyValue": prop("m_bFlag", True, "bool")},
            {"propertyOwner": be.ext_ref(0, 0x97), "propertyValue": prop("m_bFlag", True, "bool")},
            {"propertyOwner": be.ext_ref(1, 0x96), "propertyValue": prop("m_bFlag", True, "bool")}]
        self.assertEqual(b.remove_property_overrides(OTHER, 0x97), 1)
        self.assertEqual([p["propertyOwner"]["entityID"] for p in b.temp["propertyOverrides"]], [0x97, 0x96])
        b.add_override_delete(OTHER, 0x97)
        with self.assertRaises(be.BrickError):
            b.add_override_delete(OTHER, 0x97)
        b.validate()

    def test_add_property_override(self):
        b = sample()
        p = b.add_property_override((OTHER, 0x97), "m_rRequirements", "SEntityTemplateReference", (LOC, 0x76))
        self.assertEqual(p["propertyOwner"], be.ext_ref(1, 0x97))
        self.assertEqual(p["propertyValue"], prop("m_rRequirements", be.ext_ref(0, 0x76)))
        b.add_property_override((OTHER, 0x97), "m_bFlag", "bool", True)
        self.assertEqual(b.temp["propertyOverrides"][-1]["propertyValue"], prop("m_bFlag", True, "bool"))
        with self.assertRaises(be.BrickError):  # the same property twice
            b.add_property_override((OTHER, 0x97), "m_bFlag", "bool", False)
        with self.assertRaises(be.BrickError):  # not an external scene
            b.add_property_override((NEW, 0x97), "m_bFlag", "bool", True)
        b.validate()
        # It moves with the scene list like IOI's own overrides.
        b.set_external_scenes([OTHER, LOC])
        self.assertEqual(b.temp["propertyOverrides"][0]["propertyOwner"]["externalSceneIndex"], 0)
        self.assertEqual(b.temp["propertyOverrides"][0]["propertyValue"]["value"]["$val"]["externalSceneIndex"], 1)

    def test_set_translation_keeps_the_rotation(self):
        b = sample()
        rot = {"XAxis": {"x": 0.0, "y": 1.0, "z": 0.0}, "YAxis": {"x": -1.0, "y": 0.0, "z": 0.0},
               "ZAxis": {"x": 0.0, "y": 0.0, "z": 1.0}, "Trans": {"x": 1.0, "y": 2.0, "z": 3.0}}
        b.temp["subEntities"][4]["propertyValues"] += [prop("m_mTransform", rot, "SMatrix43"),
                                                       prop("m_eidParent", be.local_ref(0))]
        old = b.set_translation(0x44, (10.0, -20.5, 12.25))
        self.assertEqual(old, (1.0, 2.0, 3.0))
        m = b.get(0x44, "m_mTransform")
        self.assertEqual(m["Trans"], {"x": 10.0, "y": -20.5, "z": 12.25})
        # Stored as the 32-bit floats the binary holds, so ResourceTool's round trip gives the same JSON.
        b.set_translation(0x44, (81.46, -22.61, 12.01))
        m = b.get(0x44, "m_mTransform")
        self.assertEqual(m["Trans"]["x"], be.f32(81.46))
        self.assertNotEqual(m["Trans"]["x"], 81.46)
        self.assertEqual(m["XAxis"], {"x": 0.0, "y": 1.0, "z": 0.0})
        b.validate()

    def test_set_translation_with_a_yaw(self):
        b = sample()
        b.temp["subEntities"][4]["propertyValues"].append(prop("m_mTransform", {
            "XAxis": {"x": 1.0, "y": 0.0, "z": 0.0}, "YAxis": {"x": 0.0, "y": 1.0, "z": 0.0},
            "ZAxis": {"x": 0.0, "y": 0.0, "z": 1.0}, "Trans": {"x": 0.0, "y": 0.0, "z": 0.0}}, "SMatrix43"))
        b.set_translation(0x44, (1.0, 1.0, 1.0), yaw=90.0)
        m = b.get(0x44, "m_mTransform")
        self.assertAlmostEqual(m["XAxis"]["x"], 0.0)
        self.assertAlmostEqual(m["XAxis"]["y"], 1.0)
        self.assertAlmostEqual(m["YAxis"]["x"], -1.0)
        self.assertEqual(m["ZAxis"], {"x": 0.0, "y": 0.0, "z": 1.0})

    def test_set_translation_refuses_a_parented_or_unplaced_entity(self):
        b = sample()
        with self.assertRaises(be.BrickError):  # no m_mTransform
            b.set_translation(0x44, (0.0, 0.0, 0.0))
        b.temp["subEntities"][4]["propertyValues"] += [
            prop("m_mTransform", {"XAxis": {"x": 1.0, "y": 0.0, "z": 0.0}, "YAxis": {"x": 0.0, "y": 1.0, "z": 0.0},
                                  "ZAxis": {"x": 0.0, "y": 0.0, "z": 1.0}, "Trans": {"x": 0.0, "y": 0.0, "z": 0.0}},
                 "SMatrix43"),
            prop("m_eidParent", be.local_ref(1))]
        with self.assertRaises(be.BrickError) as e:  # hangs off Group, not the root: its transform is not world
            b.set_translation(0x44, (0.0, 0.0, 0.0))
        self.assertIn("Group", str(e.exception))
        b.set_translation(0x44, (5.0, 0.0, 0.0), parents=[0x11])  # a parent that moves with it (a rigid group)
        self.assertEqual(b.get(0x44, "m_mTransform")["Trans"]["x"], 5.0)

    def test_retype_copies_the_type_of_a_like_entity(self):
        b = sample()
        b.temp["subEntities"][3]["entityTypeResourceIndex"] = 1
        b.tblu["subEntities"][3]["entityTypeResourceIndex"] = 2
        old = b.retype(0x44, 0x33)
        self.assertEqual(old, (0, 0))
        self.assertEqual(b.temp["subEntities"][4]["entityTypeResourceIndex"], 1)
        self.assertEqual(b.tblu["subEntities"][4]["entityTypeResourceIndex"], 2)
        with self.assertRaises(be.BrickError):
            b.retype(0x44, 0x44)
        with self.assertRaises(be.BrickError):
            b.retype(0x44, 0x1234)

    def test_retype_takes_the_like_entitys_subsets(self):
        # The full Icon's crash: an act in the root's Replicable subset retyped like an act that is not in it.
        b = sample()
        b.tblu["subEntities"][0]["entitySubsets"] = [["Replicable", {"entities": [4, 5]}],
                                                     ["AudioEmitters", {"entities": [3]}]]
        b.retype(0x44, 0x33)  # Sibling(4), Replicable, like Keeper(3), AudioEmitters
        self.assertEqual(b.tblu["subEntities"][0]["entitySubsets"],
                         [["Replicable", {"entities": [5]}], ["AudioEmitters", {"entities": [3, 4]}]])
        self.assertEqual(sorted((o, n, m) for o, n, m, _ in b.subset_members()),
                         [(0, "AudioEmitters", 3), (0, "AudioEmitters", 4), (0, "Replicable", 5)])

    def test_replace_scene_keeps_the_index(self):
        b = sample()
        b.replace_scene(OTHER, NEW)
        self.assertEqual(b.scenes, [LOC, NEW])
        self.assertEqual(b.get(0x33, "m_rOut")["externalSceneIndex"], 1)


    def test_repoint_into_an_added_scene(self):
        b = sample()
        b.add_external_scene(NEW)
        self.assertEqual(b.scenes, [LOC, OTHER, NEW])
        self.assertEqual(b.repoint(0x33, "m_rOut", (OTHER, 0x99), (NEW, 0x66)), 1)
        self.assertEqual(b.get(0x33, "m_rOut"), be.ext_ref(2, 0x66))
        with self.assertRaises(be.BrickError):
            b.repoint(0x33, "m_rOut", (OTHER, 0x99), (NEW, 0x66))  # no longer names it
        with self.assertRaises(be.BrickError):
            b.add_external_scene(NEW)
        b.validate()

    def test_set_logical_parent_root(self):
        b = sample()
        old = b.set_logical_parent_root(0x22)
        self.assertEqual(old["entityIndex"], 1)
        for js in (b.temp, b.tblu):
            self.assertEqual(js["subEntities"][2]["logicalParent"], be.local_ref(0))

    def test_drop_overrides_into_a_scene(self):
        b = sample()
        b.temp["propertyOverrides"].append({"propertyOwner": be.ext_ref(1, 0x98),
                                            "propertyValue": prop("m_bVisible", False, "bool")})
        b.temp["propertyOverrides"].append({"propertyOwner": be.ext_ref(0, 0x97),
                                            "propertyValue": prop("m_bVisible", True, "bool")})
        self.assertEqual(b.drop_overrides_into([OTHER]), {OTHER: 2})
        self.assertEqual(b.tblu["overrideDeletes"], [])
        self.assertEqual(len(b.temp["propertyOverrides"]), 1)
        self.assertEqual(b.scenes, [LOC, OTHER])  # the scene itself stays
        self.assertEqual(b.remove_property_override(LOC, 0x97, "m_bVisible"), True)
        self.assertEqual(b.temp["propertyOverrides"], [])
        with self.assertRaises(be.BrickError):
            b.remove_property_override(LOC, 0x97, "m_bVisible")

    def test_drop_temp_refs_shifts_the_indices_after_it(self):
        b = sample()
        # [type 0x10, navmesh 0x30, grid 0x40, blueprint 0x20, LOC, OTHER]; a resource id names the grid.
        b.temp_refs = [(0x1F, 0x0010), (0x5F, 0x0030), (0x5F, 0x0040), (0x1F, 0x0020), (0x1F, LOC), (0x1F, OTHER)]
        b.temp["blueprintIndexInResourceHeader"] = 3
        b.temp["externalSceneTypeIndicesInResourceHeader"] = [4, 5]
        b.temp["subEntities"][4]["propertyValues"].append(
            prop("m_pGrid", {"m_IDHigh": 0, "m_IDLow": 2}, "ZRuntimeResourceID"))
        b.temp["propertyOverrides"].append({"propertyOwner": be.ext_ref(0, 0x97), "propertyValue": prop(
            "m_aRes", [{"m_IDHigh": 0, "m_IDLow": 3}, {"m_IDHigh": 4294967295, "m_IDLow": 4294967295}],
            "TArray<ZRuntimeResourceID>")})
        with self.assertRaises(be.BrickError):
            b.drop_temp_refs([0x0040])  # still used
        b.drop_temp_refs([0x0030])
        self.assertEqual(b.temp_refs, [(0x1F, 0x0010), (0x5F, 0x0040), (0x1F, 0x0020), (0x1F, LOC), (0x1F, OTHER)])
        self.assertEqual(b.temp["blueprintIndexInResourceHeader"], 2)
        self.assertEqual(b.temp["externalSceneTypeIndicesInResourceHeader"], [3, 4])
        self.assertEqual(b.get(0x44, "m_pGrid"), {"m_IDHigh": 0, "m_IDLow": 1})
        self.assertEqual(b.temp["propertyOverrides"][0]["propertyValue"]["value"]["$val"],
                         [{"m_IDHigh": 0, "m_IDLow": 2}, {"m_IDHigh": 4294967295, "m_IDLow": 4294967295}])
        self.assertEqual(b.blueprint(), 0x0020)
        with self.assertRaises(be.BrickError):
            b.drop_temp_refs([0x0030])  # gone already
        with self.assertRaises(be.BrickError):
            b.drop_temp_refs([LOC])  # an external scene
        b.validate()


if __name__ == "__main__":
    unittest.main()
