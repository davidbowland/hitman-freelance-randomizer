#!/usr/bin/env python3
"""Self-test of sealed_actors.py (the NPCs a map variant shuts in, src/core/SealedActors.inc): the sealed test on two
navmeshes, the NPCs' starts and acts on a small synthetic brick, and the table's round trip (no game data):
python3 -I tools/landslide/test_sealed_actors.py"""
import os
import sys
import tempfile

sys.dont_write_bytecode = True
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import brick_edit as be  # noqa: E402
import sealed_actors as sa  # noqa: E402
import variant_audit as va  # noqa: E402
from test_scene_props import ROOT, brick, matrix, prop  # noqa: E402

NPC_T = "[assembly:/templates/gameplay/ai2/actors.template?/npcactor.entitytemplate].pc_entitytype"
ACT_T = "[assembly:/_pro/design/acts.template?/act_stand.entitytemplate].pc_entitytype"
NAMES = {0x0E10: NPC_T, 0x0E20: ACT_T}


class Area:
    def __init__(self, island):
        self.island = island


class Mesh:
    """Floor where x < edge (island 0, the main one), and an island 1 where x > far."""

    def __init__(self, edge, far=None):
        self.edge, self.far = edge, far
        self.areas = [Area(0)] * 10 + [Area(1)]

    def snap(self, q, min_clear=0.0, radius=1.5):
        if q[0] < self.edge + radius:
            return (q, Area(0), 1.0, max(0.0, q[0] - self.edge))
        if self.far is not None and q[0] > self.far - radius:
            return (q, Area(1), 1.0, 0.0)
        return None

    def islands(self):
        return {0: 10, 1: 1}


def npc(eid, name, x, repo):
    return (eid, name, 0, [prop("m_mTransform", matrix(x, 0.0, 0.0), "SMatrix43"), prop("m_sActorName", name, "ZString"),
                           prop("m_RepositoryId", repo, "ZRepositoryID")])


def act(eid, x, who):
    return (eid, "Act", 0, [prop("m_mTransform", matrix(x, 1.0, 0.0), "SMatrix43"), prop("m_pActor", be.local_ref(who))])


def scene():
    """A (start in the sealed part), B (start open, one act sealed), C (all open), D (where Freelancer has no floor
    either), E (no repository id, sealed); a pool naming A and C."""
    b = brick("mission", [
        (ROOT, "Scene", None, []),
        npc(0x01, "Alice", 30.0, "0f504ad2-5557-989e-0000-0000000000a1"),   # 1
        npc(0x02, "Bob", 2.0, "0f504ad2-5557-989e-0000-0000000000b2"),      # 2
        npc(0x03, "Carol", 3.0, "0f504ad2-5557-989e-0000-0000000000c3"),    # 3
        npc(0x04, "Dave", 80.0, "0f504ad2-5557-989e-0000-0000000000d4"),    # 4
        (0x05, "Eve", 0, [prop("m_mTransform", matrix(31.0, 0.0, 0.0), "SMatrix43")]),  # 5
        act(0x06, 2.5, 2), act(0x07, 31.0, 2), act(0x08, 3.5, 3),            # 6-8
        (0x09, "Spawn_Targets", 0, [prop("m_aActors", [be.local_ref(1), be.local_ref(3)],
                                         "TArray<TEntityRef<ZActor>>")]),     # 9
    ], [])
    # The brick has two types (test_scene_props.brick): the NPCs keep the first; the acts, the root and the picker
    # take the second (the root and the picker name no actor, so they are no one's act).
    for i in (0, 6, 7, 8, 9):
        b.temp["subEntities"][i]["entityTypeResourceIndex"] = 1
    b.hash = 0x0C01
    return va.Scene({0x0C01: b})


class SealedTest(unittest.TestCase):
    def setUp(self):
        # Freelancer has floor up to x = 60; the option only up to x = 10, and a separate island beyond 40.
        self.sealer = sa.Sealer(Mesh(60.0), Mesh(10.0, far=40.0))

    def test_sealed_needs_floor_under_freelancer_and_none_on_the_options_main_island(self):
        self.assertFalse(self.sealer.sealed((5.0, 0.0, 0.0)))
        self.assertTrue(self.sealer.sealed((30.0, 0.0, 0.0)))   # no floor under the option
        self.assertTrue(self.sealer.sealed((50.0, 0.0, 0.0)))   # floor, but on an island of its own
        self.assertIsNone(self.sealer.sealed((80.0, 0.0, 0.0)))  # no floor under Freelancer either
        self.assertIsNone(self.sealer.sealed(None))

    def test_npcs_sealed_at_their_start_or_an_act(self):
        sc = scene()
        rows, no_repo = sa.check_npcs(sc, NAMES, self.sealer, sa.pool_actors(sc, {0x0C01}))
        self.assertEqual([r for r, _ in rows], ["0f504ad2-5557-989e-0000-0000000000a1",
                                                "0f504ad2-5557-989e-0000-0000000000b2"])
        alice, bob = rows[0][1], rows[1][1]
        self.assertTrue(alice.startswith('"Alice" mission 0000000000000001 (Freelancer\'s target pool): start'))
        self.assertIn("1 of its 2 acts sealed", bob)
        self.assertNotIn("start", bob)
        self.assertNotIn("target pool", bob)
        self.assertEqual(len(no_repo), 1)
        self.assertIn('mission 0000000000000005: start (31.0, 0.0, 0.0) sealed', no_repo[0])  # Eve, no name either

    def test_table_round_trip(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "SealedActors.inc")
            sa.write_table(path, {"garden_show": [("0f504ad2-5557-989e-0000-0000000000a1",
                                                   '"Alice" mission 0000000000000001: start (30.0, 0.0, 0.0) sealed')]},
                           {"garden_show": "Dartmoor mild, 1 sealed off"})
            with open(path) as f:
                text = f.read()
            self.assertIn('{"garden_show", "0f504ad2-5557-989e-0000-0000000000a1", "Alice"}, // mission '
                          '0000000000000001: start (30.0, 0.0, 0.0) sealed\n', text)
            old = sa.read_rows(path)
            self.assertEqual(list(old), ["garden_show"])
            # A partial run keeps another option's rows as they were.
            sa.write_table(path, dict(old), {})
            with open(path) as f:
                self.assertIn('"Alice"}, // mission', f.read())


if __name__ == "__main__":
    unittest.main()
