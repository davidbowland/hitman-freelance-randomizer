#!/usr/bin/env python3
"""Self-test of navp.py and placement.py on small synthetic navmeshes (no game data):
python3 -I tools/landslide/test_placement.py"""
import math
import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import navp  # noqa: E402
import placement as pl  # noqa: E402


def square(x0, y0, size, z):
    return [(x0, y0, z), (x0 + size, y0, z), (x0 + size, y0 + size, z), (x0, y0 + size, z)]


def mesh():
    """Island 1: a 20 x 10 m floor at z 0 (two 10 m squares sharing x = 10) and a table-height-free gap; island 2: a
    10 m square at (40, 0), z 0 (unreachable)."""
    return navp.NavMesh(navp.build([
        (1, square(0, 0, 10, 0.0), [None, 1, None, None]),
        (1, square(10, 0, 10, 0.0), [None, None, None, 0]),
        (2, square(40, 0, 10, 0.0), [None] * 4),
    ]))


HOME = (5.0, 5.0, 0.0)
ACTS = [(15.0, 5.0, 0.0), (45.0, 5.0, 0.0)]


class NavpTest(unittest.TestCase):
    def test_selftest(self):
        navp.selftest()

    def test_clearance_ignores_shared_edges_and_far_edges(self):
        m = mesh()
        self.assertAlmostEqual(m.clearance((10.0, 5.0, 0.0)), 5.0)  # the shared edge at x = 10 is not a wall
        self.assertAlmostEqual(m.clearance((1.0, 5.0, 0.0)), 1.0)
        self.assertAlmostEqual(m.clearance((1.0, 5.0, 9.0)), 8.0)  # no edge at that height: the search limit

    def test_a_bad_layout_fails_loudly(self):
        data = bytearray(navp.build([(1, square(0, 0, 10, 0.0), [None] * 4)]))
        data[navp.HDR + 8] += 1  # the header's area bytes no longer match the areas
        with self.assertRaises(ValueError):
            navp.NavMesh(bytes(data))


class PlacementWithNavmeshTest(unittest.TestCase):
    def test_snaps_to_clearance_on_the_start_island(self):
        p = pl.Placement(mesh(), HOME, ACTS)
        self.assertTrue(p.checked)
        final, lines = p.move([pl.Point(1, "safe", (0.2, 5.0, 0.4), "safe")])
        q = final[1]
        self.assertGreaterEqual(q[0], 1.0 - 1e-6)  # pushed off the wall to 1 m clearance
        self.assertAlmostEqual(q[2], 0.0)  # onto the floor
        self.assertIn("snapped", lines[0])

    def test_a_snapped_place_goes_down_to_the_floor_the_acts_stand_on(self):
        # The navmesh lies 0.15 m above the floor here: the acts stand at -0.15 (and one far act at -0.05 is not
        # among the nearest three).
        acts = [(12.0, 5.0, -0.15), (14.0, 5.0, -0.15), (16.0, 5.0, -0.12), (19.0, 9.0, -0.05)]
        p = pl.Placement(mesh(), HOME, acts)
        final, lines = p.move([pl.Point(1, "safe", (5.0, 5.0, 0.0), "safe")])
        self.assertAlmostEqual(final[1][2], -0.15)
        self.assertIn("0.15 m below the navmesh", lines[0])
        # Two acts on that floor (0.1 and 0.2 m under the navmesh; the one at z 9 has no floor): their median.
        p = pl.Placement(mesh(), HOME, [(15.0, 5.0, -0.2), (15.0, 6.0, -0.1), (15.0, 7.0, 9.0)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.0)), -0.2)
        # A place on no act's floor: the map's median offset.
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 3.0)), 3.0 - p.default_offset)
        # An act on a step (its offset beyond FLOOR_OFFSET_MAX) does not count; without a navmesh nothing moves down.
        p = pl.Placement(mesh(), HOME, [(15.0, 5.0, -0.6)])
        self.assertEqual(p.floor_z((5.0, 5.0, 0.0)), 0.0)
        final, _ = pl.Placement(None, HOME, ACTS).move([pl.Point(1, "safe", (16.0, 5.0, 0.0), "safe")])
        self.assertEqual(final[1], (16.0, 5.0, 0.0))

    def test_the_nearest_act_or_litter_sets_the_offset(self):
        # Acts 10 m off stand 0.2 m under the navmesh; a litter 0.5 m from the place 0.15 m under it: the litter's
        # offset counts (the town-hall basement: its documents went 3.4 cm under the floor with the 15 m median).
        far = [(15.0, 5.0, -0.2), (15.0, 6.0, -0.2), (16.0, 4.0, -0.2)]
        p = pl.Placement(mesh(), HOME, far, floor_refs=[(5.5, 5.0, -0.15)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.0)), -0.15)
        self.assertAlmostEqual(pl.Placement(mesh(), HOME, far).floor_z((5.0, 5.0, 0.0)), -0.2)
        # Beyond FLOOR_LOCAL_M it is one of the nearest three again (their median).
        p = pl.Placement(mesh(), HOME, far, floor_refs=[(7.5, 5.0, -0.15)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.0)), -0.2)
        # A litter does not count for reach or the act gap.
        self.assertEqual(p.gap_acts, far)

    def test_never_below_an_act_beside_it_on_a_level_floor(self):
        # The nearest reference (a litter 0.3 m off, 0.2 m under the navmesh) would put the place at -0.2; an act
        # 0.8 m off on the same level navmesh stands at -0.15: the place goes up to it.
        p = pl.Placement(mesh(), HOME, [(5.8, 5.0, -0.15)], floor_refs=[(5.3, 5.0, -0.2)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.0)), -0.15)
        # 1 m or more away it does not count.
        p = pl.Placement(mesh(), HOME, [(6.2, 5.0, -0.15)], floor_refs=[(5.3, 5.0, -0.2)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.0)), -0.2)
        # An act on something (above the navmesh, an offset not used) still counts beside the place.
        p = pl.Placement(mesh(), HOME, [(5.8, 5.0, 0.02), (14.0, 5.0, -0.1)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.0)), 0.02)
        # A snapped place on the navmesh ends on it.
        final, _ = pl.Placement(mesh(), HOME, [(5.8, 5.0, -0.15)], floor_refs=[(5.3, 5.0, -0.2)]).move(
            [pl.Point(1, "clue", (5.0, 5.0, 0.0), "clue")])
        self.assertAlmostEqual(final[1][2], -0.15)

    def test_on_a_slope_an_act_up_the_slope_is_higher_by_right(self):
        # The floor rises 0.1 m per meter in y; the navmesh lies 0.1 m above it. An act 0.9 m up the slope stands
        # 0.09 m higher than the place's floor: the place stays on its own floor.
        slope = navp.NavMesh(navp.build([(1, [(0, 0, 0.0), (10, 0, 0.0), (10, 10, 1.0), (0, 10, 1.0)], [None] * 4)]))
        p = pl.Placement(slope, (5.0, 5.0, 0.5), [(5.0, 5.9, 0.49)])
        self.assertAlmostEqual(p.floor_z((5.0, 5.0, 0.5)), 0.4)

    def test_a_level_floor(self):
        # The place's navmesh (z 0.05, x < 10) is 5 cm above that of the room's acts (z 0, x > 10), which all stand
        # at -0.2: the offset would put the place at -0.15, the level floor puts it at -0.2.
        bumpy = navp.NavMesh(navp.build([
            (1, square(0, 0, 10, 0.05), [None, 1, None, None]),
            (1, square(10, 0, 10, 0.0), [None, None, None, 0]),
        ]))
        acts = [(13.0, 5.0, -0.2), (14.0, 5.0, -0.2), (15.0, 5.0, -0.2)]
        self.assertAlmostEqual(pl.Placement(bumpy, HOME, acts).floor_z((5.0, 5.0, 0.05)), -0.2)
        # Not level (2 cm apart): the offset.
        acts = [(13.0, 5.0, -0.2), (14.0, 5.0, -0.2), (15.0, 5.0, -0.18)]
        self.assertAlmostEqual(pl.Placement(bumpy, HOME, acts).floor_z((5.0, 5.0, 0.05)), -0.15)

    def test_rejects_another_island(self):
        p = pl.Placement(mesh(), HOME, ACTS)
        with self.assertRaises(pl.PlacementError) as e:
            p.move([pl.Point(1, "stash", (43.0, 3.0, 0.0), "stash")])
        self.assertIn("island 2", str(e.exception))

    def test_rejects_no_floor(self):
        p = pl.Placement(mesh(), HOME, ACTS)
        with self.assertRaises(pl.PlacementError) as e:
            p.move([pl.Point(1, "act", (30.0, 5.0, 0.0), "act"), pl.Point(2, "safe", (5.0, 5.0, 6.0), "safe")])
        self.assertIn("2 of 2", str(e.exception))
        self.assertIn("no navmesh floor", str(e.exception))

    def test_rejects_too_little_clearance(self):
        narrow = navp.NavMesh(navp.build([(1, [(0, 0, 0), (1.5, 0, 0), (1.5, 20, 0), (0, 20, 0)], [None] * 4)]))
        p = pl.Placement(narrow, (0.75, 10.0, 0.0), [])
        with self.assertRaises(pl.PlacementError) as e:
            p.move([pl.Point(1, "safe", (0.75, 10.0, 0.0), "safe")])
        self.assertIn("only 0.75 m from the mesh's edge", str(e.exception))
        final, _ = p.move([pl.Point(2, "clue", (0.75, 10.0, 0.0), "clue")])  # a floor clue needs 0.3 m
        self.assertIn(2, final)

    def test_a_table_clue_keeps_its_height_over_floor(self):
        p = pl.Placement(mesh(), HOME, ACTS)
        final, _ = p.move([pl.Point(1, "clue", (3.0, 3.0, 0.9), "clue", table=True)])
        self.assertEqual(final[1], (3.0, 3.0, 0.9))
        with self.assertRaises(pl.PlacementError):  # nothing within 1.2 m below
            p.move([pl.Point(2, "clue", (3.0, 3.0, 2.5), "clue", table=True)])

    def test_spot_and_act_gaps(self):
        p = pl.Placement(mesh(), HOME, ACTS)
        with self.assertRaises(pl.PlacementError) as e:
            p.move([pl.Point(1, "safe A", (5.0, 5.0, 0.0), "safe")], kept_spots=[("kept stash", (6.0, 5.0, 0.0))])
        self.assertIn("from the spot kept stash", str(e.exception))
        # Another floor (2.5 m or more above) does not count.
        p.move([pl.Point(1, "safe A", (5.0, 5.0, 0.0), "safe")], kept_spots=[("upstairs", (6.0, 5.0, 3.0))])
        # Next to a map act, the snap steps off it (the gap is checked again on the final position).
        final, _ = p.move([pl.Point(2, "act", (15.5, 5.0, 0.0), "act")])
        self.assertGreaterEqual(pl.plan_d(final[2], (15.0, 5.0, 0.0)), pl.ACT_GAP_M - 1e-6)
        self.assertLess(pl.plan_d(final[2], (15.5, 5.0, 0.0)), 0.6)

    def test_the_snap_steps_past_a_map_act(self):
        # Pushed off the wall at x = 0 to 1 m clearance it would land on the act at (1.2, 5): it goes past it instead.
        p = pl.Placement(mesh(), HOME, ACTS + [(1.2, 5.0, 0.0)])
        final, _ = p.move([pl.Point(1, "safe", (0.2, 5.0, 0.4), "safe")])
        self.assertGreaterEqual(pl.plan_d(final[1], (1.2, 5.0, 0.0)), pl.ACT_GAP_M - 1e-6)
        self.assertGreaterEqual(final[1][0], 1.0 - 1e-6)

    def test_the_snap_steps_past_a_layer_act_too(self):
        # A layer's act (avoid) is kept ACT_GAP_M clear like the map's own, without counting for reach.
        p = pl.Placement(mesh(), HOME, ACTS, avoid=[(1.2, 5.0, 0.0)])
        final, _ = p.move([pl.Point(1, "safe", (0.2, 5.0, 0.4), "safe")])
        self.assertGreaterEqual(pl.plan_d(final[1], (1.2, 5.0, 0.0)), pl.ACT_GAP_M - 1e-6)
        self.assertEqual(p.acts, ACTS)
        # Without it the same safe lands on that spot.
        final, _ = pl.Placement(mesh(), HOME, ACTS).move([pl.Point(1, "safe", (0.2, 5.0, 0.4), "safe")])
        self.assertLess(pl.plan_d(final[1], (1.2, 5.0, 0.0)), pl.ACT_GAP_M)

    def test_a_supplier_needs_no_act_about(self):
        # The act at (15, 5) is 3 m from a supplier at (12, 5): busy; a safe there is fine (only ACT_GAP_M applies).
        p = pl.Placement(mesh(), HOME, ACTS)
        with self.assertRaises(pl.PlacementError) as e:
            p.move([pl.Point(1, "supplier", (12.0, 5.0, 0.0), "supplier")])
        self.assertIn("within %.1f m" % pl.SUPPLIER_GAP_M, str(e.exception))
        p.move([pl.Point(1, "safe", (12.0, 5.0, 0.0), "safe")])
        # A layer's act counts too; an act on another floor does not.
        with self.assertRaises(pl.PlacementError):
            pl.Placement(mesh(), HOME, ACTS, avoid=[(4.0, 5.0, 0.0)]).move(
                [pl.Point(1, "supplier", (6.0, 5.0, 0.0), "supplier")])
        final, _ = pl.Placement(mesh(), HOME, ACTS, avoid=[(4.0, 5.0, 3.0)]).move(
            [pl.Point(1, "supplier", (6.0, 5.0, 0.0), "supplier")])
        self.assertEqual(final[1], (6.0, 5.0, 0.0))

    def test_islands_are_the_connected_areas(self):
        m = mesh()
        self.assertEqual(m.islands(), {1: 2, 2: 1})
        self.assertIs(m.areas[0].adj[1], m.areas[1])

    def test_kept_entities(self):
        p = pl.Placement(mesh(), HOME, ACTS)
        self.assertIn("floor", p.check_kept("exit", (10.0, 11.0, 0.5), 2.5))
        with self.assertRaises(pl.PlacementError):
            p.check_kept("exit", (10.0, 14.0, 0.5), 2.5)
        with self.assertRaises(pl.PlacementError):
            p.check_kept("start", (45.0, 5.0, 0.0), 1.5)  # island 2

    def test_a_start_off_the_mesh_is_an_error(self):
        with self.assertRaises(pl.PlacementError):
            pl.Placement(mesh(), (100.0, 100.0, 0.0), ACTS)


class PlacementWithoutNavmeshTest(unittest.TestCase):
    def test_nominal_positions_by_act_distance(self):
        p = pl.Placement(None, HOME, ACTS)
        self.assertFalse(p.checked)
        final, lines = p.move([pl.Point(1, "safe", (12.0, 5.0, 0.0), "safe")])
        self.assertEqual(final[1], (12.0, 5.0, 0.0))
        self.assertIn("nominal", lines[0])
        self.assertIsNone(p.check_kept("exit", (99.0, 99.0, 0.0), 2.5))

    def test_far_from_every_act_is_rejected(self):
        p = pl.Placement(None, HOME, ACTS)
        with self.assertRaises(pl.PlacementError) as e:
            p.move([pl.Point(1, "safe", (25.0, 5.0, 0.0), "safe")])
        self.assertIn("act-distance", str(e.exception))
        with self.assertRaises(pl.PlacementError):  # height counts twice: 4 m up is 8 m away
            p.move([pl.Point(2, "safe", (13.0, 5.0, 4.0), "safe")])

    def test_a_table_clue_by_its_item_spawner(self):
        p = pl.Placement(None, HOME, ACTS)
        final, lines = p.move([pl.Point(1, "clue", (30.35, 5.0, 0.9), "clue", table=True, anchor=(30.0, 5.0, 0.9))])
        self.assertEqual(final[1], (30.35, 5.0, 0.9))  # 15 m from every act, but on the map's own table
        self.assertIn("item spawner", lines[0])
        with self.assertRaises(pl.PlacementError):
            p.move([pl.Point(2, "clue", (30.35, 5.0, 0.9), "clue", table=True)])
        with self.assertRaises(pl.PlacementError):
            p.move([pl.Point(3, "clue", (33.0, 5.0, 0.9), "clue", table=True, anchor=(30.0, 5.0, 0.9))])

    def test_gaps_apply_too(self):
        p = pl.Placement(None, HOME, ACTS)
        with self.assertRaises(pl.PlacementError):
            p.move([pl.Point(1, "safe", (12.0, 5.0, 0.0), "safe"), pl.Point(2, "stash", (13.0, 5.0, 0.0), "stash")])

    def test_unknown_kind(self):
        with self.assertRaises(pl.PlacementError):
            pl.Point(1, "x", (0, 0, 0), "chair")
        with self.assertRaises(pl.PlacementError):
            pl.Point(1, "x", (0, 0, 0), "safe", table=True)


class CrateFaceTest(unittest.TestCase):
    """A supply crate opens on its local -Y side; on mesh() (floor 0-20 x 0-10 m, walls all round)."""

    def test_the_open_side_is_local_minus_y(self):
        for yaw in (0.0, 90.0, -135.0):
            f = pl.crate_front(yaw)
            r = math.radians(yaw)
            self.assertAlmostEqual(f[0], math.sin(r))  # -(local Y) = -(-sin, cos)
            self.assertAlmostEqual(f[1], -math.cos(r))
            self.assertAlmostEqual(pl.yaw_of((math.cos(r), math.sin(r), 0.0)), yaw)

    def test_room_in_front_of_the_face(self):
        m = mesh()
        # 1.2 m from the wall at y = 0, opening toward it: 1.2 - 0.26 m of floor in front of its face.
        self.assertAlmostEqual(pl.crate_room(m, (5.0, 1.2, 0.0), 0.0), 0.95, delta=0.06)
        # Opening away from it: the lines stop at CRATE_ROOM_M.
        self.assertEqual(pl.crate_room(m, (5.0, 1.2, 0.0), 180.0), pl.CRATE_ROOM_M)
        # One end against a side wall ends one of the three lines only: the middle one counts.
        self.assertEqual(pl.crate_room(m, (19.5, 5.0, 0.0), 180.0), pl.CRATE_ROOM_M)

    def test_a_gap_by_the_face_counts_as_room_up_to_the_cut(self):
        # The navmesh cut round a vanilla crate: floor from y = 1.5 (0.24 m out from the face) counts from the face;
        # from y = 2.0 (0.74 m out) it is too far to be the crate's own cut.
        near = navp.NavMesh(navp.build([(1, [(0, 1.5, 0), (20, 1.5, 0), (20, 10, 0), (0, 10, 0)], [None] * 4)]))
        self.assertEqual(pl.crate_room(near, (5.0, 1.0, 0.0), 180.0), pl.CRATE_ROOM_M)
        far = navp.NavMesh(navp.build([(1, [(0, 2.0, 0), (20, 2.0, 0), (20, 10, 0), (0, 10, 0)], [None] * 4)]))
        self.assertEqual(pl.crate_room(far, (5.0, 1.0, 0.0), 180.0), 0.0)

    def test_a_crate_facing_the_wall_is_turned_half_round(self):
        m = mesh()
        q = (5.0, 1.2, 0.0)
        bad = pl.crate_problem(m, q, 0.0)
        self.assertIsNotNone(bad)
        self.assertIn("faces the tighter side", bad)
        yaw, room, turns = pl.crate_yaw(m, q, 0.0)
        self.assertAlmostEqual(yaw, -180.0)  # yaws in [-180, 180)
        self.assertEqual(room, pl.CRATE_ROOM_M)
        self.assertEqual([t[0] for t in turns], [0.0, -180.0, 90.0, -90.0])
        self.assertIsNone(pl.crate_problem(m, q, yaw))
        # Under CRATE_FRONT_M of navmesh in front.
        self.assertIn("under 0.8 m", pl.crate_problem(m, (5.0, 0.9, 0.0), 0.0))
        # Room enough: kept.
        self.assertAlmostEqual(pl.crate_yaw(m, (5.0, 5.0, 0.0), 0.0)[0], 0.0)
        self.assertIsNone(pl.crate_problem(m, (5.0, 5.0, 0.0), 0.0))

    def test_a_quarter_turn_must_fit(self):
        m = mesh()
        # 1 m from the wall at x = 20, opening toward it (yaw 90: open side +X); its half turn opens on the room.
        q = (19.0, 5.0, 0.0)
        self.assertLess(pl.crate_room(m, q, 90.0), pl.CRATE_FRONT_M)
        self.assertAlmostEqual(pl.crate_yaw(m, q, 90.0)[0], -90.0)
        # 0.5 m from that wall, a quarter turn would put an end 0.3 m into it: it does not fit, and is not picked
        # even with more room in front.
        self.assertTrue(pl.crate_fits(m, (19.0, 5.0, 0.0), 0.0))
        self.assertFalse(pl.crate_fits(m, (19.5, 5.0, 0.0), 0.0))
        turns = pl.crate_turns(m, (19.5, 5.0, 0.0), 90.0)
        self.assertEqual([t[2] for t in turns], [True, True, False, False])

    def test_a_supplier_facing_a_wall(self):
        m = mesh()
        # 2 m from the wall at y = 0, facing it (local -Y toward -Y: yaw 0): 1.74 m in front, under 3 m.
        bad = pl.supplier_problem(m, (5.0, 2.0, 0.0), 0.0)
        self.assertIsNotNone(bad)
        self.assertIn("under 3.0 m", bad)
        self.assertIn("turned +180: 3.00 m", bad)
        # Half round he faces the room.
        self.assertIsNone(pl.supplier_problem(m, (5.0, 2.0, 0.0), 180.0))


def rect(x0, y0, x1, y1, z=0.0):
    return [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]


def rooms():
    """Two 10 x 5 m rooms at z 0 (y 0-5 and 5.6-10.6), a 0.6 m wall between them with a 2 m doorway (x 4-6), and a
    room on the floor above (z 4)."""
    return navp.NavMesh(navp.build([
        (1, rect(0, 0, 10, 5), [None] * 4),
        (1, rect(4, 5, 6, 5.6), [None] * 4),
        (1, rect(0, 5.6, 10, 10.6), [None] * 4),
        (1, rect(0, 0, 10, 5, 4.0), [None] * 4),
    ]))


DOOR = ("door", (4.0, 5.3, 0.0), 0.0)  # its origin at one end of the opening, its local X along the wall


class CrateInTheWayTest(unittest.TestCase):
    """placement.crate_in_the_way: not in a door's doorway, not narrowing a passage (Garden Show's greenhouse door)."""

    def test_the_opening_is_the_floor_through_the_door_line(self):
        u0, u1, axis, normal = pl.door_opening(rooms(), DOOR[1], DOOR[2])
        self.assertAlmostEqual(u0, -pl.DOOR_SIDE_M, delta=0.06)
        self.assertAlmostEqual(u1, 2.0 + pl.DOOR_SIDE_M, delta=0.06)
        self.assertAlmostEqual(axis[0], 1.0)
        # A door in the outer wall (no floor beyond it) has no opening.
        self.assertIsNone(pl.door_opening(rooms(), (4.0, 0.0, 0.0), 0.0))

    def test_a_crate_in_front_of_the_doorway(self):
        m = rooms()
        # 0.84 m from the door's line, across its opening (as Garden Show's Stash05: 1.26 m in front, 1.45 m across).
        found = pl.crate_in_the_way(m, (5.0, 4.2, 0.0), 0.0, [DOOR])
        self.assertEqual(len(found), 1)
        self.assertIn("in the doorway of door", found[0][1])
        # On the other side too; beside the opening, farther out than DOOR_FRONT_M, or on the floor above: not.
        self.assertTrue(pl.crate_in_the_way(m, (5.0, 6.6, 0.0), 180.0, [DOOR]))
        self.assertEqual(pl.crate_in_the_way(m, (8.0, 4.2, 0.0), 0.0, [DOOR]), [])
        self.assertTrue(pl.crate_in_the_way(m, (7.0, 4.2, 0.0), 0.0, [DOOR]))  # 0.2 m past the floor's run
        self.assertEqual(pl.crate_in_the_way(m, (5.0, 2.0, 0.0), 0.0, [DOOR]), [])
        self.assertEqual(pl.crate_in_the_way(m, (5.0, 4.2, 4.0), 0.0, [DOOR]), [])

    def test_a_crate_narrowing_a_passage(self):
        hall = navp.NavMesh(navp.build([(1, rect(0, 0, 10, 2), [None] * 4)]))
        # In the middle of a 2 m corridor: 0.74 m either side, in the way.
        why, hard = pl.crate_passage(hall, (5.0, 1.0, 0.0), 0.0)
        self.assertIn("narrows a passage across its sides", why)
        self.assertTrue(hard)
        self.assertEqual(pl.crate_in_the_way(hall, (5.0, 1.0, 0.0), 0.0, [])[0][2], True)
        # 0.9 m on one side, 0.58 on the other: tight, not in the way.
        why, hard = pl.crate_passage(hall, (5.0, 0.84, 0.0), 0.0)
        self.assertIn("tight", why)
        self.assertFalse(hard)
        # Against its wall: 1.44 m left.
        self.assertIsNone(pl.crate_passage(hall, (5.0, 0.3, 0.0), 0.0))
        # Across the corridor (turned a quarter): 0.2 m past each end.
        self.assertIn("across its ends", pl.crate_passage(hall, (5.0, 1.0, 0.0), 90.0)[0])
        # In a room, 0.64 m off its wall: the room is open on the other side.
        self.assertIsNone(pl.crate_passage(mesh(), (5.0, 0.9, 0.0), 0.0))

    def test_a_crate_in_a_nook_narrows_nothing(self):
        # A 1.8 x 1.2 m alcove off a room: 0.1 m past either end, but nothing beyond its back.
        nook = navp.NavMesh(navp.build([(1, rect(0, 0, 10, 10), [None] * 4), (1, rect(4, 10, 5.8, 11.2), [None] * 4)]))
        self.assertIsNone(pl.crate_passage(nook, (4.9, 10.6, 0.0), 0.0))
        self.assertEqual(pl.crate_in_the_way(nook, (4.9, 10.6, 0.0), 0.0, []), [])


if __name__ == "__main__":
    unittest.main()
