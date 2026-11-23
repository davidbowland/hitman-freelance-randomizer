#!/usr/bin/env python3
"""Self-test of variant_audit.py (the clash audit of every map variant): reading Variants.cpp and LandslideFull.cpp,
and its checks beyond landslide_audit's (left in place, on the set, Freelancer's references and exits into what an
option deletes), on small synthetic bricks (no game data):
python3 -I tools/landslide/test_variant_audit.py"""
import os
import sys

sys.dont_write_bytecode = True
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import brick_edit as be  # noqa: E402
import landslide_audit as la  # noqa: E402
import variant_audit as va  # noqa: E402
from test_scene_props import ROOT, brick, matrix, prop  # noqa: E402

LOCB, OPTB, FLB, OFFB = 0x0B01, 0x0B02, 0x0B03, 0x0B04
PROP = {0x0E10: "[assembly:/_pro/environment/templates/props/x.template?/crate.entitytemplate].pc_entitytype"}
EXITS = {0x0E30: "[assembly:/_pro/design/setpieces/evergreen.template?/exit_evergreen_usekey.entitytemplate]"
                 ".pc_entitytype"}


def location():
    """A room at (10, 0, 5) with a truck at (0, 0, 0) in it, a car at (5, 0, 0) and an exit sequence whose track
    names the car."""
    return brick("location", [
        (ROOT, "Scene", None, []),
        (0x10, "Room", 0, [prop("m_mTransform", matrix(10.0, 0.0, 5.0), "SMatrix43")]),
        (0x11, "Truck", 1, [prop("m_eidParent", be.local_ref(1)), prop("m_mTransform", matrix(0, 0, 0), "SMatrix43")]),
        (0x12, "Car", 1, [prop("m_eidParent", be.local_ref(1)), prop("m_mTransform", matrix(5, 0, 0), "SMatrix43")]),
        (0x13, "Car_Exit", 0, []),
        (0x14, "EntityTrack_Car", 4, [prop("m_targetEntity", be.local_ref(3))]),
    ], [])


def option(deletes=()):
    """A tent 0.3 m from location's truck; it deletes what it is given."""
    return brick("option", [
        (ROOT, "Scene", None, []),
        (0x21, "Tent", 0, [prop("m_eidParent", be.ext_ref(0, 0x10)),
                           prop("m_mTransform", matrix(0.3, 0.0, 0.0), "SMatrix43")]),
    ], [LOCB], deletes=[be.ext_ref(0, e) for e in deletes])


def freelancer():
    """Freelancer's brick: a clue at the tent's place, a button starting location's Car_Exit, a reference to the car
    and a pool (m_aActors) naming it too."""
    b = brick("freelancer", [
        (ROOT, "Scene", None, []),
        (0x31, "Exit_Evergreen_PushButton_03", 0, [prop("m_mTransform", matrix(14.0, 1.0, 5.0), "SMatrix43")]),
        (0x32, "Setpiece_Clue", 0, [prop("m_eidParent", be.ext_ref(0, 0x10)),
                                    prop("m_mTransform", matrix(0.35, 0.0, 0.0), "SMatrix43")]),
        (0x33, "Watcher", 0, [prop("m_rTarget", be.ext_ref(0, 0x12))]),
        (0x34, "Pool", 0, [prop("m_aActors", [be.ext_ref(0, 0x12)], "TArray<SEntityTemplateReference>")]),
    ], [LOCB])
    b.tblu["pinConnectionOverrides"] = [{"fromEntity": be.local_ref(1), "toEntity": be.ext_ref(0, 0x13),
                                         "fromPinName": "Exit", "toPinName": "StartExitSequence"}]
    return b


class ParseVariants(unittest.TestCase):
    def test_every_map_option_and_full_version(self):
        maps, options, full = va.parse_variants(os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "core"))
        self.assertEqual(len(maps), 17)
        self.assertEqual(set(va.CHUNKS), set(maps))
        for m in maps.values():
            for _, opts in m.values():
                for o in opts:
                    self.assertTrue(options[o]["bricks"], o)
        self.assertEqual(full["gluttony"], "gluttony")
        self.assertEqual(full["lighting_copperhead"], "icon")
        # Every spec is some option's full version, but the full Marrakesh night's: no option of its own (the plain
        # marrakesh_night was removed), only the full Entertainer's base, audited through it.
        self.assertNotIn("marrakesh", set(full.values()))
        self.assertNotIn("marrakesh_night", options)
        self.assertEqual(full["the_entertainer"], "entertainer")
        self.assertEqual(set(full.values()) | {"marrakesh"}, set(va.FULL_SPECS))
        # a parenthesis after the contract in its comment
        self.assertEqual(options["the_fugitive"]["contract"], "HOKKAIDO/ELUSIVE/THEFUGITIVE")
        self.assertNotIn("the_vector", options)  # removed: it loaded the Sniper Assassin mode
        self.assertNotIn("the_vector", maps["Colorado"]["mild"][1])
        self.assertEqual(options["the_rage"]["contract"], "CHONGQING/ELUSIVE/THERAGE")
        self.assertIsNone(options["gluttony"]["contract"])
        self.assertEqual([b[0] for b in options["the_splitter"]["bricks"]], ["scenario_lambic", "sound_lambic"])
        self.assertIn("the_guru", maps["Sapienza"]["hot"][1])
        self.assertNotIn("landslide", maps["Sapienza"]["hot"][1])


class Checks(unittest.TestCase):
    def test_left_in_place(self):
        loc, opt, fl = location(), option(), freelancer()
        ours = la.Scene({LOCB: loc, FLB: fl, OPTB: opt})
        official = la.Scene({LOCB: loc, OPTB: opt, OFFB: brick("official", [(ROOT, "Scene", None, [])], [LOCB],
                                                                  deletes=[be.ext_ref(0, 0x11)])})
        props = va.option_props(ours, PROP, {OPTB})
        clashes, notes = va.left_in_place(ours, official, PROP, props, {OPTB})
        self.assertEqual(len(clashes), 1)
        self.assertIn("Truck", clashes[0])
        self.assertIn("deleted by the official scene", clashes[0])
        # what the official scene keeps is no finding
        clashes, notes = va.left_in_place(ours, la.Scene({LOCB: loc, OPTB: opt}), PROP, props, {OPTB})
        self.assertEqual(clashes + notes, [])

    def test_on_the_set(self):
        loc, opt, fl = location(), option(), freelancer()
        sc = la.Scene({LOCB: loc, FLB: fl, OPTB: opt})
        names = dict(PROP)
        lines = va.on_the_set(sc, names, va.option_props(sc, names, {OPTB}), {FLB})
        self.assertTrue(any("Setpiece_Clue" in x and "Tent" in x for x in lines))

    def test_exit_whose_car_the_option_deletes(self):
        loc, fl = location(), freelancer()
        base = la.Scene({LOCB: loc, FLB: fl})
        sc = la.Scene({LOCB: loc, FLB: fl, OPTB: option(deletes=[0x12])})
        exits = va.broken_exits(sc, base, PROP, {FLB})
        self.assertEqual(len(exits), 1)
        self.assertIn("Exit_Evergreen_PushButton_03 starts", exits[0])
        self.assertIn("EntityTrack_Car m_targetEntity", exits[0])
        self.assertEqual(va.broken_exits(la.Scene({LOCB: loc, FLB: fl, OPTB: option()}), base, PROP, {FLB}), [])
        named = va.dangling(sc, base, PROP, {FLB})
        self.assertEqual(len(named), 1)
        self.assertIn("Watcher m_rTarget", named[0])
        self.assertNotIn("m_aActors", named[0])  # the pool: the mod's variant check handles it

    def test_exit_the_option_deletes(self):
        def fl_with(button):
            # a key exit under a group of its own brick, logically under location's room too (another brick's
            # entity, as override_meyer's exits under scenario_fox's Exit locations); the button only when asked
            ents = [(ROOT, "Scene", None, []), (0x36, "Exits", 0, [])] + ([(0x31, "Exit_Evergreen_PushButton_03", 0, [
                prop("m_mTransform", matrix(14.0, 1.0, 5.0), "SMatrix43")])] if button else []) + [
                (0x35, "Exit_Evergreen_UseKey", 1, [prop("m_eidParent", be.ext_ref(0, 0x10)),
                                                    prop("m_mTransform", matrix(1.0, 0.0, 0.0), "SMatrix43")])]
            b = brick("freelancer", ents, [LOCB])
            b.temp_refs.append((0x1F, 0x0E30))  # both exits are exit_evergreen_* instances, whatever their names
            for e in b.temp["subEntities"][2:]:
                e["entityTypeResourceIndex"] = len(b.temp_refs) - 1
            return b, len(ents) - 1

        def deleting(*refs):
            return brick("option", [(ROOT, "Scene", None, [])], [LOCB, FLB], deletes=list(refs))
        loc = location()
        for button, left in ((True, "1 of Freelancer's 2 exits left)"), (False, "NO Freelancer exit")):
            fl, key = fl_with(button)
            fl.temp["subEntities"][key]["logicalParent"] = be.ext_ref(0, 0x10)
            base = va.Scene({LOCB: loc, FLB: fl})
            self.assertEqual(va.deleted_exits(va.Scene({LOCB: loc, FLB: fl, OPTB: option()}), base, {FLB}, EXITS),
                             [])
            # its logical parent in another brick deleted: the exit stays (as Lust's eight in game)
            self.assertEqual(va.deleted_exits(va.Scene({LOCB: loc, FLB: fl, OPTB: option(deletes=[0x10])}), base,
                                              {FLB}, EXITS), [])
            # the exit itself deleted
            lines = va.deleted_exits(va.Scene({LOCB: loc, FLB: fl, OPTB: deleting(be.ext_ref(1, 0x35))}), base,
                                     {FLB}, EXITS)
            self.assertEqual(len(lines), 1)
            self.assertIn("Exit_Evergreen_UseKey", lines[0])
            self.assertIn(left, lines[0])
            # its logical parent in its own brick deleted: it goes with it
            fl, key = fl_with(button)
            base = va.Scene({LOCB: loc, FLB: fl})
            lines = va.deleted_exits(va.Scene({LOCB: loc, FLB: fl, OPTB: deleting(be.ext_ref(1, 0x36))}), base,
                                     {FLB}, EXITS)
            self.assertEqual(len(lines), 1)
            self.assertIn("Exit_Evergreen_UseKey", lines[0])

    def test_exit_whose_sequence_the_option_deletes(self):
        loc, fl = location(), freelancer()
        fl.temp_refs.append((0x1F, 0x0E30))
        fl.temp["subEntities"][1]["entityTypeResourceIndex"] = len(fl.temp_refs) - 1
        base = va.Scene({LOCB: loc, FLB: fl})
        self.assertEqual(va.gone_sequences(va.Scene({LOCB: loc, FLB: fl, OPTB: option()}), base, {FLB}, EXITS), [])
        lines = va.gone_sequences(va.Scene({LOCB: loc, FLB: fl, OPTB: option(deletes=[0x13])}), base, {FLB}, EXITS)
        self.assertEqual(len(lines), 1)
        self.assertIn("Exit_Evergreen_PushButton_03 starts", lines[0])
        self.assertIn("Car_Exit", lines[0])
        self.assertIn("StartExitSequence", lines[0])

    def test_territory_by_name(self):
        b = brick("override", [(ROOT, "Scene", None, []), (0x41, "Other", 0, []),
                               (0x42, la.TERRITORY_NAME, 0, [prop("availableStash01", be.local_ref(1))])], [])
        self.assertEqual(la.territory_index(b), 2)

    def test_territory_by_numbered_name_or_type(self):
        # Isle of Sgail's override_whist names it Helper_Evergreen_CPD_TerritorySetupRandomizer_01
        b = brick("override", [(ROOT, "Scene", None, []), (0x41, "Other", 0, []),
                               (0x42, la.TERRITORY_NAME + "_01", 0, [])], [])
        self.assertEqual(la.territory_index(b), 2)
        # by its template, whatever its name
        b = brick("override", [(ROOT, "Scene", None, []), (0x41, "Other", 0, []), (0x42, "Renamed", 0, [])], [])
        b.temp_refs.append((0x1F, la.TERRITORY_TYPE))
        b.temp["subEntities"][2]["entityTypeResourceIndex"] = len(b.temp_refs) - 1
        self.assertEqual(la.territory_index(b), 2)
        with self.assertRaises(be.BrickError):
            la.territory_index(brick("override", [(ROOT, "Scene", None, []), (0x41, "Other", 0, [])], []))


class Sealed(unittest.TestCase):
    def test_start_the_option_cuts_off(self):
        import navp
        sq = lambda x, y: [(x, y, 0), (x + 4, y, 0), (x + 4, y + 4, 0), (x, y + 4, 0)]  # noqa: E731
        # Freelancer's: three squares in a row, one island
        fl = navp.NavMesh(navp.build([(1, sq(0, 0), [None, 1, None, None]), (1, sq(4, 0), [None, 2, None, 0]),
                                      (1, sq(8, 0), [None, None, None, 1])]))
        # the option's: the first two, a gap, then the third on its own (walled off), and nothing at x 12+
        opt = navp.NavMesh(navp.build([(1, sq(0, 0), [None, 1, None, None]), (1, sq(4, 0), [None, None, None, 0]),
                                       (2, sq(8, 0), [None] * 4)]))
        places = [("start a", (2, 2, 0)), ("start b", (10, 2, 0)), ("exit c", (40, 2, 0)), ("stash d", (10, 2, 0)),
                  ("clue e (of safe f)", (11, 2, 0))]
        lines = va.sealed(places, fl, opt)
        self.assertEqual(len(lines), 1)
        self.assertIn("start b at (10.0, 2.0, 0.0): on the option's navmesh island 2", lines[0])
        gone = navp.NavMesh(navp.build([(1, sq(0, 0), [None] * 4)]))
        lines = va.sealed(places, fl, gone)
        self.assertEqual(len(lines), 1)
        self.assertIn("start b at (10.0, 2.0, 0.0): floor", lines[0])
        # a stash, safe, supplier or mule spot, or a safe's clue, in the walled-off part: unreachable
        lines = va.sealed(places, fl, opt, va.SEALED_SPOTS)
        self.assertEqual(len(lines), 2)
        self.assertIn("stash d at (10.0, 2.0, 0.0): on the option's navmesh island 2", lines[0])
        self.assertIn("clue e (of safe f) at (11.0, 2.0, 0.0): on the option's navmesh island 2", lines[1])


class DeletedStarts(unittest.TestCase):
    def test_where_the_start_fix_puts_47(self):
        def spawns(b, idx):
            b.temp_refs.append((0x1F, la.HERO_SPAWN_TYPE))
            for i in idx:
                b.temp["subEntities"][i]["entityTypeResourceIndex"] = len(b.temp_refs) - 1
            return b
        fl = spawns(brick("freelancer", [
            (ROOT, "Scene", None, []),
            (0x51, "Start_Bar", 0, [prop("m_mTransform", matrix(0, 0, 0), "SMatrix43")]),
            (0x52, "Start_Roof", 0, [prop("m_mTransform", matrix(50, 0, 0), "SMatrix43")])], []), [1, 2])
        opt = spawns(brick("option", [
            (ROOT, "Scene", None, []),
            (0x61, "Opt_Bar", 0, [prop("m_mTransform", matrix(1, 0, 0), "SMatrix43")]),
            (0x62, "Opt_Default", 0, [prop("m_mTransform", matrix(100, 0, 0), "SMatrix43"),
                                      prop("m_bDefaultHeroSpawn", True, "bool")])], [FLB],
            deletes=[be.ext_ref(0, 0x51), be.ext_ref(0, 0x52)]), [1, 2])
        sc = la.Scene({FLB: fl, OPTB: opt})
        # not in the mod's KnownDeletedSpawns: the option's default spawn, wherever it is
        lines = va.deleted_starts(sc, {FLB}, {OPTB})
        self.assertEqual(len(lines), 2)
        self.assertIn("Start_Bar -> ", lines[0])
        self.assertIn("Opt_Default (default)", lines[0])
        self.assertIn("NOT where", lines[0])
        # a known deleted spawn: the kept spawn on its spot
        known = va.KNOWN_DELETED_SPAWNS
        va.KNOWN_DELETED_SPAWNS = {0x51}
        try:
            lines = va.deleted_starts(sc, {FLB}, {OPTB})
        finally:
            va.KNOWN_DELETED_SPAWNS = known
        self.assertIn("Opt_Bar, 1.0 m away (same spot)", lines[0])
        self.assertIn("Start_Roof -> ", lines[1])
        self.assertIn("Opt_Default", lines[1])


RULE_T, ZONE_T, BOX_T, NPC_T, ACT_T = 0x0E40, 0x0E41, 0x0E42, 0x0E43, 0x0E44
KINDS = dict(PROP)
KINDS.update({
    RULE_T: "[modules:/ztrespassingruleentity.class].pc_entitytype",
    ZONE_T: "[assembly:/templates/gameplay/disguise.template?/disguisezone.entitytemplate].pc_entitytype",
    BOX_T: "[assembly:/templates/gameplay/disguise.template?/disguisezonevolume.entitytemplate].pc_entitytype",
    NPC_T: "[assembly:/templates/gameplay/ai2/actors.template?/npcactor.entitytemplate].pc_entitytype",
    ACT_T: "[assembly:/_pro/design/actor/acts/mr.template?/act_mr_stand_pace_calm.entitytemplate].pc_entitytype"})


def typed(b, types):
    """Give b's entities {index: type hash} their types."""
    for i, t in types.items():
        b.temp_refs.append((0x1F, t))
        b.temp["subEntities"][i]["entityTypeResourceIndex"] = len(b.temp_refs) - 1
    return b


def rule_brick(name, deep=True, enabled=True, at=(0.0, 0.0, 1.5)):
    """A trespassing rule over a zone of one 4 x 4 x 3 m box centered at `at` (its floor at at's z - 1.5)."""
    return typed(brick(name, [
        (ROOT, "Scene", None, []),
        (0x71, "Rule", 0, [prop("m_bDeepTrespassing", deep, "bool"),
                           prop("m_Zones", [be.local_ref(2)], "TArray<SEntityTemplateReference>")]),
        (0x72, "Bedroom zone", 0, [prop("m_Boxes", [be.local_ref(3)], "TArray<SEntityTemplateReference>")] +
         ([] if enabled else [prop("m_bEnabled", False, "bool")])),
        (0x73, "Bedroom box", 0, [prop("m_mTransform", matrix(*at), "SMatrix43"),
                                  prop("m_vGlobalSize", {"x": 4.0, "y": 4.0, "z": 3.0}, "SVector3")])], []),
        {1: RULE_T, 2: ZONE_T, 3: BOX_T})


class Trespassing(unittest.TestCase):
    PLACES = [("start fl House", (0.5, 0.5, 0.0)), ("start fl Garden", (10.0, 0.0, 0.0)),
              ("safe fl Safe08", (-1.0, 1.0, 0.0)), ("clue fl Documents", (0.0, 0.0, 0.5))]

    def test_start_in_the_options_hostile_area(self):
        opt = rule_brick("option")
        sc = va.Scene({OPTB: opt})
        clashes, notes = va.zone_findings(self.PLACES, va.trespass_zones(sc, KINDS, {OPTB}), [])
        self.assertEqual(len(clashes), 1)
        self.assertIn("start fl House at (0.50, 0.50, 0.00): inside a HOSTILE (deep trespassing) area "
                      "\"Bedroom zone\" (Bedroom box)", clashes[0])
        self.assertIn("under Freelancer: no trespassing area", clashes[0])
        # a safe (stash, supplier, mule) in it: a note; a clue: nothing
        self.assertEqual(len(notes), 1)
        self.assertIn("safe fl Safe08", notes[0])

    def test_trespassing_off_at_load_and_freelancers_own(self):
        sc = va.Scene({OPTB: rule_brick("option", deep=False)})
        base = va.Scene({FLB: rule_brick("freelancer", deep=False)})
        clashes, _ = va.zone_findings(self.PLACES, va.trespass_zones(sc, KINDS, {OPTB}),
                                      va.trespass_zones(base, KINDS, {FLB}))
        self.assertEqual(len(clashes), 1)
        self.assertIn("inside a trespassing area", clashes[0])
        self.assertIn("under Freelancer: Bedroom zone", clashes[0])
        # off at load: a note, not a clash
        sc = va.Scene({OPTB: rule_brick("option", enabled=False)})
        clashes, notes = va.zone_findings(self.PLACES, va.trespass_zones(sc, KINDS, {OPTB}), [])
        self.assertEqual(clashes, [])
        self.assertTrue(any("start fl House" in x and "(off at load)" in x for x in notes))
        # the rule's area elsewhere: nothing
        sc = va.Scene({OPTB: rule_brick("option", at=(30.0, 0.0, 1.5))})
        self.assertEqual(va.zone_findings(self.PLACES, va.trespass_zones(sc, KINDS, {OPTB}), []), ([], []))


class Company(unittest.TestCase):
    def test_start_by_the_options_npc_or_act(self):
        opt = typed(brick("option", [
            (ROOT, "Scene", None, []),
            (0x81, "CHAR_Target", 0, [prop("m_mTransform", matrix(0.91, 0.0, 0.0), "SMatrix43")]),
            (0x82, "Act_Pace", 0, [prop("m_mTransform", matrix(2.5, 0.0, 0.0), "SMatrix43")]),
            (0x83, "Act_Upstairs", 0, [prop("m_mTransform", matrix(0.5, 0.0, 3.0), "SMatrix43")]),
            (0x84, "Act_Far", 0, [prop("m_mTransform", matrix(3.5, 0.0, 0.0), "SMatrix43")])], []),
            {1: NPC_T, 2: ACT_T, 3: ACT_T, 4: ACT_T})
        sc = va.Scene({OPTB: opt})
        places = [("start fl House", (0.0, 0.0, 0.0)), ("start fl Garden", (20.0, 0.0, 0.0)),
                  ("safe fl Safe", (0.0, 0.0, 0.0))]
        lines = va.start_company(sc, KINDS, {OPTB}, places)
        self.assertEqual(len(lines), 1)
        self.assertIn("start fl House at (0.00, 0.00, 0.00): 1 NPCs and 1 acts of the option within 3.0 m", lines[0])
        self.assertIn("NPC 0000000000000B02 0000000000000081 \"CHAR_Target\" 0.91 m; act", lines[0])
        self.assertIn("Act_Pace", lines[0])
        self.assertNotIn("Act_Upstairs", lines[0])  # another floor
        self.assertNotIn("Act_Far", lines[0])
        # Freelancer's own NPCs do not count
        self.assertEqual(va.start_company(sc, KINDS, set(), places), [])


class Extent(unittest.TestCase):
    def content(self, yaw=0.0):
        """A fridge whose model runs 0 to 1.0 m along its x and 0 to 0.7 m along its y from its origin (a corner), and
        a safe 0.91 m from the origin, inside it; a start 1.4 m off, outside it."""
        opt = brick("option", [(ROOT, "Scene", None, []),
                               (0x91, "Kitchen_Fridge", 0, [prop("m_mTransform", matrix(0.0, 0.0, 0.0, yaw),
                                                                 "SMatrix43")])], [])
        sc = va.Scene({OPTB: opt})
        return sc, [(OPTB, 1, "prop", PROP[0x0E10], (0.0, 0.0, 0.0), "new")]

    PLACES = [("safe fl Safe08", (0.87, 0.26, 0.0)), ("start fl House", (1.4, 0.0, 0.0)),
              ("clue fl Documents", (0.65, 0.3, 0.0))]
    BOX = ((0.0, 0.0, 0.0), (1.0, 0.7, 1.8))

    def test_place_inside_the_props_model(self):
        sc, content = self.content()
        # origin only (no extent): 0.91 m is no clash
        clashes, notes = la.audit(sc, content, self.PLACES)
        self.assertEqual(clashes, [])
        clashes, notes = la.audit(sc, content, self.PLACES, extent=lambda h, i: self.BOX)
        self.assertEqual(len(clashes), 1)
        self.assertIn("safe fl Safe08", clashes[0])
        self.assertIn("inside its model's bounds (1.00 x 0.70 x 1.80 m", clashes[0])
        # a tree's crown (more than EXTENT_MAX_M across): its box is mostly air, only its origin counts
        tree = ((-4.0, -4.0, 0.0), (4.0, 4.0, 9.0))
        clashes, _ = la.audit(sc, content, self.PLACES, extent=lambda h, i: tree)
        self.assertEqual(clashes, [])
        # turned 180 degrees the model runs the other way: the safe is outside it
        sc, content = self.content(180.0)
        clashes, _ = la.audit(sc, content, self.PLACES, extent=lambda h, i: self.BOX)
        self.assertEqual(clashes, [])

    def test_unknown_extent_widens_the_radius(self):
        sc, content = self.content()
        clashes, notes = la.audit(sc, content, self.PLACES, extent=lambda h, i: None)
        self.assertEqual(len(clashes), 1)
        self.assertIn("safe fl Safe08", clashes[0])
        self.assertIn("extent unknown", clashes[0])
        self.assertFalse(any("start fl House" in x for x in clashes))  # 1.4 m: a note
        self.assertFalse(any("clue " in x for x in clashes))  # clues keep the origin check

    def test_prim_bounds_and_inverse(self):
        import struct
        data = struct.pack("<I", 8) + b"\0" * 4 + b"\0" * 20 + struct.pack("<6f", -0.5, -0.4, 0.0, 0.5, 0.4, 1.8)
        lo, hi = va.prim_bounds(data)
        for got, want in zip(lo + hi, (-0.5, -0.4, 0.0, 0.5, 0.4, 1.8)):
            self.assertAlmostEqual(got, want, places=5)
        self.assertIsNone(va.prim_bounds(b"\0\0"))
        w = va.bb.matrix(matrix(3.0, 4.0, 0.0, 90.0))
        w = [[w[r][c] * (2.0 if c < 3 else 1.0) for c in range(4)] for r in range(3)]  # scaled 2x
        inside, loc = la.in_extent((3.0, 5.0, 0.5), w, ((-0.5, -0.5, 0.0), (0.5, 0.5, 1.0)))
        self.assertTrue(inside)
        self.assertAlmostEqual(loc[0], 0.5)
        self.assertAlmostEqual(loc[1], 0.0)
        self.assertAlmostEqual(loc[2], 0.25)


class FreelancerStart(unittest.TestCase):
    def test_contract_bricks_join_the_scene(self):
        import json
        import tempfile
        scene = "assembly:/_pro/scenes/missions/hokkaido/scene_snowcrane_mild_shogi.entity"
        mission = "assembly:/_pro/scenes/missions/hokkaido/mission_mild_shogi.brick"
        sh, mh = va.rid("[%s].pc_entitytemplate" % scene), va.rid("[%s].pc_entitytype" % mission)
        newest = {sh: {}, mh: {}}
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, "SNUG", "MILD"))
            with open(os.path.join(d, "SNUG", "MILD", "SHOGI.json"), "w") as f:
                json.dump({"Metadata": {"ScenePath": scene}, "Data": {"Bricks": [mission]}}, f)
            self.assertEqual(va.freelancer_start(newest, d, scene), [sh, mh])
        # no contractdata: the scene folder's mission brick, when mounted
        self.assertEqual(va.freelancer_start(newest, None, scene), [sh, mh])
        self.assertEqual(va.freelancer_start({sh: {}}, None, scene), [sh])
        ours, ov = va.freelancer_bricks(newest, {sh, mh}, scene, [sh, mh])
        self.assertIn(mh, ours)


if __name__ == "__main__":
    unittest.main()
