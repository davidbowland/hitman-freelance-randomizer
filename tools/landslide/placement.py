"""Where a full variant's moved Freelancer entities go (build_bricks.py, specs with SPOT_MOVES: the full Icon).

A set-only map (The Icon's movie set) has room for few of Freelancer's vanilla spots, so the spec moves the others
(each keeps its id and its randomizer slot) to a nominal place beside one of the map's own acts. Before a position is
written it is judged here, in one of two ways:

- With the map's navmesh (navp.NavMesh, read from the scene's NAVP): each nominal place is snapped to the nearest
  navmesh floor within SNAP_RADIUS in plan and SNAP_DZ in height that has at least the kind's clearance from the mesh
  edge (CLEARANCE) and no map act within ACT_GAP_M; it must be on the island of the variant's default start (islands:
  the connected components of the navmesh's areas); a clue on a table is not snapped (it is
  above the floor) but needs floor within TABLE_BELOW below it and TABLE_PLAN in plan. Entities that stay where they
  are (kept spots, exits, starts) need floor within their radius on that island. A rejected position fails the build.
- Without it (the NAVP not extracted yet): the nominal places as they are, judged by the act-distance reasoning of
  the design: each within ACT_REACH_M of one of the map's acts (3D, height counted twice, as build_bricks.Reach), a
  clue on a table within TABLE_PLAN of the map's own item spawner on that table, and the build says loudly that the
  navmesh check did not run.

A layer variant on this map (the full Bad Boy on the full Icon: badboy_spec.py) adds its own acts; when its bricks are
read, Placement's avoid lists them and the ACT_GAP_M rule (the snap and the final check) treats them as the map's own,
so a moved entity lands just past a layer NPC's act too.

The navmesh's polygons lie above the walking floor (on The Icon's navmesh 0.15 m in the median over 332 acts of
mission_copperhead, 0.09-0.19 m for most, and from 0 to 0.18 m within a few meters; acts and litter stand on the floor
itself). A place snapped onto the navmesh is put back down by the local offset (floor_z: the navmesh floor's height
above the nearest act or litter on the same floor within FLOOR_LOCAL_M, else the median over the nearest acts within
FLOOR_REF_M), so a moved safe, stash or clue lies on the floor, not 0.1-0.2 m above it (the full Bad Boy's town-hall
safe and its clues were seen floating), and never below an act or litter beside it on the same level floor
(FLOOR_BESIDE_M: the town-hall safe's documents, 3.4 cm under the basement floor with the 15 m median).

Either way: the spots themselves (safes, stashes, the supplier) are at least SPOT_GAP_M apart in plan (moved and kept
alike), and no moved entity except a clue on a table is within ACT_GAP_M (plan) of one of the map's acts at its
height, so a map NPC is not pushed off its act. A moved supplier needs more: no act of the map or a layer within
SUPPLIER_GAP_M (plan, SPOT_GAP_DZ in height), since a supplier will not deal while someone is about (the full Icon's
first supplier place, beside an act on the start-area stairs, was busy all the time: icon_spec.py).
"""
import math

CLEARANCE = {"safe": 1.0, "stash": 1.0, "supplier": 1.0, "guard": 1.0, "mule": 0.6, "act": 0.6, "clue": 0.3}
SPOT_KINDS = ("safe", "stash", "supplier")
SNAP_RADIUS = 6.0
SNAP_DZ = 1.5
TABLE_BELOW = 1.2
TABLE_PLAN = 1.5
SPOT_GAP_M = 2.0
SPOT_GAP_DZ = 2.5   # spots farther apart in height are on other floors
ACT_GAP_M = 0.8
ACT_GAP_DZ = 1.5
SUPPLIER_GAP_M = 5.0  # no map or layer act this close (plan) to a moved supplier, within SPOT_GAP_DZ in height
ACT_REACH_M = 6.0
# floor_z: the references whose navmesh offset counts (acts, and the floor_refs: litter; their navmesh floor within
# FLOOR_DZ of the place's): the nearest one within FLOOR_LOCAL_M in plan, else the median of the nearest FLOOR_ACTS
# within FLOOR_REF_M; and the offsets allowed (a larger one is an act on a step or a ramp, a negative one an act
# standing on something: Dartmoor's acts 5 cm above its navmesh, which lies on the floor there).
FLOOR_ACTS = 3
FLOOR_REF_M = 15.0
FLOOR_LOCAL_M = 2.0
FLOOR_DZ = 0.5
FLOOR_OFFSET_MIN = 0.0
FLOOR_OFFSET_MAX = 0.35
# A level floor: when the nearest FLOOR_ACTS references on the place's floor (within FLOOR_REF_M) stand within
# FLOOR_LEVEL_SPREAD of one height, and the offset puts the place within FLOOR_LEVEL_DZ of it, the place goes to that
# height (The Icon's town-hall basement: every act and litter at 12.00, its navmesh 0.15-0.25 m above, highest by the
# walls and shelves).
FLOOR_LEVEL_SPREAD = 0.01
FLOOR_LEVEL_DZ = 0.1
# ... and a place is never below a reference within FLOOR_BESIDE_M in plan (and FLOOR_BESIDE_DZ in height) that stands
# on the same level floor (the navmesh under both within FLOOR_SAME_DZ: on a slope the reference is higher by right).
FLOOR_BESIDE_M = 1.0
FLOOR_BESIDE_DZ = 0.3
FLOOR_SAME_DZ = 0.05


class PlacementError(Exception):
    pass


class Point:
    """One entity to place: id, description (for messages), nominal position, kind (CLEARANCE's keys), whether it
    lies on a table (clues), the position of the map entity whose place it borrows (for a table clue: the map's item
    spawner on that table), and the spec's reason."""

    def __init__(self, eid, desc, pos, kind, table=False, why="", anchor=None):
        if kind not in CLEARANCE:
            raise PlacementError("%s: unknown kind %r" % (desc, kind))
        if table and kind != "clue":
            raise PlacementError("%s: only a clue lies on a table" % desc)
        self.eid, self.desc, self.pos, self.kind, self.table, self.why = eid, desc, tuple(pos), kind, table, why
        self.anchor = None if anchor is None else tuple(anchor)


def reach_d(p, q):
    """build_bricks.Reach's distance: 3D with height counted twice."""
    return math.sqrt((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (2 * (p[2] - q[2])) ** 2)


def plan_d(p, q):
    return math.hypot(p[0] - q[0], p[1] - q[1])


def fmt(p):
    return "(%.2f, %.2f, %.2f)" % tuple(p)


class Placement:
    """mesh: a navp.NavMesh or None; home: the default start's position (its island is the playable one; None: only
    floor_z is used); acts: the map's act positions; avoid: more act positions (a layer's) kept ACT_GAP_M clear but
    not counted as reach; floor_refs: positions of other things lying on the floor (litter), for floor_z only."""

    def __init__(self, mesh, home, acts, avoid=(), floor_refs=()):
        self.mesh = mesh
        self.acts = list(acts)
        self.avoid = list(avoid)
        self.gap_acts = self.acts + self.avoid
        self.home_island = None
        if mesh is not None and home is not None:
            hit = mesh.snap(home, min_clear=0.0, radius=SNAP_RADIUS, up=SNAP_DZ, down=SNAP_DZ)
            if hit is None:
                raise PlacementError("no navmesh floor within %.0f m of the default start %s: the NAVP and the "
                                     "scene do not match" % (SNAP_RADIUS, fmt(home)))
            self.home_island = hit[1].island
        # (act or floor reference, its navmesh floor's height above it) for floor_z: every one with navmesh floor
        # (refs, for the same-floor rule) and those whose offset is allowed (offsets).
        self.floor_refs = list(floor_refs)
        self.refs = []
        if mesh is not None:
            for a in self.gap_acts + self.floor_refs:
                hit = mesh.area_at(a, up=FLOOR_DZ, down=FLOOR_DZ)
                if hit is not None:
                    self.refs.append((tuple(a), hit[1] - a[2]))
        self.offsets = [(a, o) for a, o in self.refs if FLOOR_OFFSET_MIN <= o <= FLOOR_OFFSET_MAX]
        self.default_offset = sorted(o for _, o in self.offsets)[len(self.offsets) // 2] if self.offsets else 0.0

    def floor_z(self, q):
        """The walking floor's height under a navmesh point q: q's height less the navmesh offset of the nearest act
        or floor reference on its floor within FLOOR_LOCAL_M, else the median offset of the nearest FLOOR_ACTS within
        FLOOR_REF_M, else of all of them; set to the height of a level floor (FLOOR_LEVEL_SPREAD) close to that;
        raised to the height of any reference within FLOOR_BESIDE_M that stands on the same level floor above it."""
        near = sorted((plan_d(q, a), a, o) for a, o in self.offsets
                      if plan_d(q, a) < FLOOR_REF_M and abs(a[2] + o - q[2]) < FLOOR_DZ)
        if near and near[0][0] < FLOOR_LOCAL_M:
            z = q[2] - near[0][2]
        elif near:
            offs = sorted(o for _, _, o in near[:FLOOR_ACTS])
            z = q[2] - offs[len(offs) // 2]
        else:
            z = q[2] - self.default_offset
        level = sorted((plan_d(q, a), a[2]) for a, o in self.refs
                       if plan_d(q, a) < FLOOR_REF_M and abs(a[2] + o - q[2]) < FLOOR_DZ)[:FLOOR_ACTS]
        if len(level) == FLOOR_ACTS:
            zs = [h for _, h in level]
            if max(zs) - min(zs) <= FLOOR_LEVEL_SPREAD and abs(z - zs[0]) < FLOOR_LEVEL_DZ:
                z = sorted(zs)[1]
        for a, o in self.refs:
            if plan_d(q, a) < FLOOR_BESIDE_M and abs(a[2] + o - q[2]) < FLOOR_SAME_DZ and \
                    0.0 < a[2] - z < FLOOR_BESIDE_DZ:
                z = a[2]
        return z

    @property
    def checked(self):
        return self.mesh is not None

    def nearest_act(self, p):
        return min(reach_d(p, q) for q in self.acts)

    def act_near(self, q):
        """A map (or layer) act closer than ACT_GAP_M in plan to q at about its height, or None."""
        for a in self.gap_acts:
            if abs(q[2] - a[2]) < ACT_GAP_DZ and plan_d(q, a) < ACT_GAP_M:
                return a
        return None

    def move(self, points, kept_spots=()):
        """{eid: final position} for the moved points, and audit lines; raises PlacementError naming every rejected
        point. kept_spots: [(description, position)] of the spots that stay (for the spot gap)."""
        problems, lines, final = [], [], {}
        for p in points:
            if self.mesh is None and p.table:
                # The map's own item spawner on that table is the evidence that it is in reach.
                if p.anchor is None or plan_d(p.pos, p.anchor) > TABLE_PLAN or abs(p.pos[2] - p.anchor[2]) > 0.5:
                    problems.append("%s %s: a clue on a table must be within %.1f m of the map's item spawner on it "
                                    "(%s)" % (p.desc, fmt(p.pos), TABLE_PLAN, "none given" if p.anchor is None else
                                              fmt(p.anchor)))
                    continue
                final[p.eid] = p.pos
                lines.append("%s -> %s (nominal; on a table, %.2f m from the map's item spawner there)" %
                             (p.desc, fmt(p.pos), plan_d(p.pos, p.anchor)))
                continue
            if self.mesh is None:
                if self.acts:
                    d = self.nearest_act(p.pos)
                    if d > ACT_REACH_M:
                        problems.append("%s %s: %.1f m from the nearest act of the map (more than %.0f m)" %
                                        (p.desc, fmt(p.pos), d, ACT_REACH_M))
                        continue
                    lines.append("%s -> %s (nominal; nearest act %.1f m)" % (p.desc, fmt(p.pos), d))
                final[p.eid] = p.pos
                continue
            if p.table:
                hit = self.mesh.snap(p.pos, min_clear=0.0, radius=TABLE_PLAN, up=0.1, down=TABLE_BELOW)
                if hit is None:
                    problems.append("%s %s: a clue on a table needs navmesh floor within %.1f m below it and %.1f m "
                                    "in plan; none" % (p.desc, fmt(p.pos), TABLE_BELOW, TABLE_PLAN))
                    continue
                if hit[1].island != self.home_island:
                    problems.append("%s %s: the floor under it is on island %d, not the start's %d" %
                                    (p.desc, fmt(p.pos), hit[1].island, self.home_island))
                    continue
                final[p.eid] = p.pos
                lines.append("%s -> %s (on a table; floor %.2f m below)" % (p.desc, fmt(p.pos), p.pos[2] - hit[0][2]))
                continue
            need = CLEARANCE[p.kind]
            # Never onto a map act (ACT_GAP_M): the snap looks past the floor beside one, so a place set next to an
            # act by the design and pushed off a wall does not end up on the act itself.
            hit = self.mesh.snap(p.pos, min_clear=need, radius=SNAP_RADIUS, up=SNAP_DZ, down=SNAP_DZ,
                                 ok=lambda q: self.act_near(q) is None)
            if hit is None:
                problems.append("%s %s: no navmesh floor within %.0f m in plan and %.1f m in height" %
                                (p.desc, fmt(p.pos), SNAP_RADIUS, SNAP_DZ))
                continue
            q, area, clear, dist = hit
            if clear < need:
                problems.append("%s %s: the best floor within %.0f m, %s, is only %.2f m from the mesh's edge (a %s "
                                "needs %.1f m)" % (p.desc, fmt(p.pos), SNAP_RADIUS, fmt(q), clear, p.kind, need))
                continue
            if area.island != self.home_island:
                problems.append("%s %s: snaps to %s on island %d, not the start's %d (unreachable)" %
                                (p.desc, fmt(p.pos), fmt(q), area.island, self.home_island))
                continue
            q = (q[0], q[1], self.floor_z(q))
            final[p.eid] = q
            lines.append("%s -> %s (snapped %.2f m, clearance %.2f m, %.2f m below the navmesh)" % (
                p.desc, fmt(q), dist, clear, hit[0][2] - q[2]))
        # The gaps, on the final positions.
        spots = [(p.desc, final[p.eid]) for p in points if p.kind in SPOT_KINDS and p.eid in final] + list(kept_spots)
        for p in points:
            if p.eid not in final:
                continue
            q = final[p.eid]
            if p.kind in SPOT_KINDS:
                for desc, r in spots:
                    if desc != p.desc and abs(q[2] - r[2]) < SPOT_GAP_DZ and plan_d(q, r) < SPOT_GAP_M:
                        problems.append("%s %s is %.2f m from the spot %s %s (at least %.1f m)" %
                                        (p.desc, fmt(q), plan_d(q, r), desc, fmt(r), SPOT_GAP_M))
            if p.kind == "supplier":
                busy = [a for a in self.gap_acts if abs(q[2] - a[2]) < SPOT_GAP_DZ and plan_d(q, a) < SUPPLIER_GAP_M]
                if busy:
                    a = min(busy, key=lambda a: plan_d(q, a))
                    problems.append("%s %s: %d act(s) of the map within %.1f m, the nearest %.2f m away at %s (a "
                                    "supplier will not deal while someone is about)" %
                                    (p.desc, fmt(q), len(busy), SUPPLIER_GAP_M, plan_d(q, a), fmt(a)))
            if not p.table:
                a = self.act_near(q)
                if a is not None:
                    problems.append("%s %s is %.2f m from a map act at %s (at least %.1f m)" %
                                    (p.desc, fmt(q), plan_d(q, a), fmt(a), ACT_GAP_M))
        if problems:
            raise PlacementError("%d of %d moved entities %s:\n  %s" % (
                len(problems), len(points), "rejected by the navmesh check" if self.mesh is not None else
                "rejected by the act-distance check", "\n  ".join(sorted(set(problems)))))
        return final, lines

    def check_kept(self, desc, pos, radius, down=SNAP_DZ):
        """An entity that stays where it is: navmesh floor within radius in plan (and SNAP_DZ in height; down below
        it, for one on high furniture) on the start's island. Returns a note, or raises PlacementError. Without a
        navmesh: None."""
        if self.mesh is None:
            return None
        hit = self.mesh.snap(pos, min_clear=0.0, radius=radius, up=SNAP_DZ, down=down)
        if hit is None:
            raise PlacementError("%s %s: no navmesh floor within %.1f m in plan" % (desc, fmt(pos), radius))
        if hit[1].island != self.home_island:
            raise PlacementError("%s %s: its floor is on island %d, not the start's %d (unreachable)" %
                                 (desc, fmt(pos), hit[1].island, self.home_island))
        return "floor %.2f m away, clearance %.2f m" % (hit[3], hit[2])


# --- a supply crate's open side ------------------------------------------------------------------------------------
#
# A Freelancer supply crate (setpiece_evergreen_stash_unique: its CollisionBox 1.60 x 0.51 x 0.86 m along its local X,
# Y, Z, about its origin) opens on its local -Y side: 109 of the 112 vanilla crates of seven maps have navmesh floor
# 0.5 to 1.1 m out from -Y, and 105 of them none behind +Y (their back to a wall); in game 47 stands 0.7 m out along
# -Y to open one (Garden Show). A moved crate keeps its vanilla rotation unless its spec gives a
# yaw, so at its new place its open side may face a wall: Garden Show's Stash09 (Dartmoor, moved 3 m west through the
# mansion's wall: its front, out of the room, now against the wall's outside) and The Icon's Fortress stash (Bad Boy,
# the crew entrance), where the player had to squeeze between the wall and the crate.
#
# crate_room is the walkable navmesh straight out from the open side, from the crate's face: the middle of three
# lines across the opening (its middle and 0.3 m to either side; a vanilla crate often stands with one end against a
# side wall or a shelf, which ends the line by that end). The navmesh stops short of walls by the walking
# radius, so the space to a wall is somewhat more; around a vanilla crate it is cut away (the crate is in the level
# the navmesh was made for), so a line's floor may start up to CRATE_CUT_M out and the gap counts as room. crate_yaw turns a crate to the quarter turn of its rotation with
# the most room in front (the half turn first: the same footprint, back to the wall it now faces).

CRATE_HALF_WIDTH = 0.80   # m, along its local X
CRATE_HALF_DEPTH = 0.26   # m, along its local Y: its face is this far out from its origin
CRATE_FRONT_M = 0.8       # m: at least this much navmesh in front of the open side (crate_problem)
CRATE_GOOD_M = 1.5        # m: this much is room enough (the vanilla rotation is kept)
CRATE_TIGHTER_M = 1.0     # m: under CRATE_GOOD_M, another side with this much more room is the side to face
CRATE_ROOM_M = 3.0        # m: the lines stop here
CRATE_STEP_M = 0.05
CRATE_CUT_M = 0.6         # m: a vanilla crate's navmesh is cut around it; its floor may start this far from its face
CRATE_ACROSS = (-0.3, 0.0, 0.3)  # m along its local X: the lines out from the opening (the middle one counts)
# A moved crate stands level: more tilt than this from upright is a problem (The Icon's Fortress stash kept 11.5
# degrees of its vanilla slope on the crew entrance's flat floor until its spec gave it a yaw).
CRATE_TILT_DEG = 3.0


def yaw_of(x_axis):
    """Degrees of a crate's local X from world X about up (its yaw, as brick_edit.set_translation takes it)."""
    return math.degrees(math.atan2(x_axis[1], x_axis[0]))


def crate_front(yaw):
    """The plan direction the open side (local -Y) faces for a crate turned yaw degrees: -(-sin, cos)."""
    r = math.radians(yaw)
    return (math.sin(r), -math.cos(r))


def crate_room(mesh, pos, yaw):
    """Meters of walkable navmesh straight out from the crate's open side (from its face, the middle of the
    CRATE_ACROSS lines' runs; at most CRATE_ROOM_M), at pos's height (a step up or down ends a line)."""
    f = crate_front(yaw)
    across = (f[1], -f[0])  # local X
    runs = []
    for a in CRATE_ACROSS:
        start = (pos[0] + f[0] * CRATE_HALF_DEPTH + across[0] * a, pos[1] + f[1] * CRATE_HALF_DEPTH + across[1] * a)
        r = 0.0
        floor = False
        while r < CRATE_ROOM_M:
            q = (start[0] + f[0] * r, start[1] + f[1] * r, pos[2] + 0.2)
            if mesh.area_at(q, up=0.6, down=0.8) is not None:
                floor = True
            elif floor or r >= CRATE_CUT_M:
                break
            r += CRATE_STEP_M
        runs.append(min(r, CRATE_ROOM_M) if floor else 0.0)
    return round(sorted(runs)[len(runs) // 2], 2)


def crate_fits(mesh, pos, yaw):
    """Whether the crate's footprint, turned yaw, lies on the navmesh (points 0.2 m apart over its 1.6 x 0.5 m): a
    quarter turn swings its 0.8 m ends where its sides were, toward a wall or a prop the navmesh stops short of."""
    f = crate_front(yaw)
    across = (f[1], -f[0])
    for k in range(-4, 5):
        for j in (-2, -1, 0, 1, 2):
            u, v = k * CRATE_HALF_WIDTH / 4, j * CRATE_HALF_DEPTH / 2
            q = (pos[0] + across[0] * u + f[0] * v, pos[1] + across[1] * u + f[1] * v, pos[2] + 0.2)
            if mesh.area_at(q, up=0.6, down=0.8) is None:
                return False
    return True


def crate_turns(mesh, pos, yaw):
    """[(yaw, room, fits)] for the crate's four quarter turns, in the order crate_yaw prefers them: kept, half turn
    (both its footprint as it stands), the quarter turns (fits: crate_fits)."""
    out = []
    for d in (0.0, 180.0, 90.0, -90.0):
        y = round((yaw + d + 180.0) % 360.0 - 180.0, 1)
        out.append((y, crate_room(mesh, pos, y), d in (0.0, 180.0) or crate_fits(mesh, pos, y)))
    return out


def crate_yaw(mesh, pos, yaw):
    """The yaw for a crate at pos turned yaw now: kept when its open side has CRATE_GOOD_M of room, else the turn
    with the most room in front among those that fit (the first in crate_turns' order on a tie). Returns (yaw, room,
    crate_turns)."""
    turns = crate_turns(mesh, pos, yaw)
    if turns[0][1] >= CRATE_GOOD_M:
        return turns[0][0], turns[0][1], turns
    best = max((t for t in turns if t[2]), key=lambda t: t[1])  # max keeps the first of equals
    return best[0], best[1], turns


def crate_problem(mesh, pos, yaw):
    """None, or why the crate's open side is cramped: under CRATE_FRONT_M of navmesh in front, or under CRATE_GOOD_M
    while another turn that fits has CRATE_TIGHTER_M more (it faces the tighter side)."""
    turns = crate_turns(mesh, pos, yaw)
    kept, room, _ = turns[0]
    best = max((t for t in turns if t[2]), key=lambda t: t[1])
    others = ", ".join("yaw %.0f: %.2f m%s" % (y, r, "" if fits else " (does not fit)") for y, r, fits in turns[1:])
    if room < CRATE_FRONT_M:
        return "its open side (yaw %.0f) has %.2f m of navmesh in front, under %.1f m (%s)" % (
            kept, room, CRATE_FRONT_M, others)
    if room < CRATE_GOOD_M and best[1] >= room + CRATE_TIGHTER_M:
        return "its open side (yaw %.0f, %.2f m of navmesh in front) faces the tighter side: yaw %.0f has %.2f m " \
               "(%s)" % (kept, room, best[0], best[1], others)
    return None


# --- a moved supplier's facing ----------------------------------------------------------------------------------------
#
# A supplier's NPC (its m_rActorProvider, a child of the spot: he turns with it) faces his local -Y, as a crate's open
# side does (IOI's acts agree: Act_FR_Stand_Clean_Window's -Y meets the window, Act_MR_Stand_Lean_Back_Wall's +Y the
# wall). Garden Show's supplier, moved out of the laundry room with his rotation kept, stood on the south terrace
# facing the mansion, 2.1 m of navmesh in front (crate_room's measure; the navmesh stops short of the wall), staring at
# it (seen in game). A moved supplier faces straight out over open floor: the full CRATE_ROOM_M.
SUPPLIER_FRONT_M = CRATE_ROOM_M


def supplier_problem(mesh, pos, yaw):
    """None, or why a supplier NPC at pos, turned yaw (his local X from world X: yaw_of), faces a wall or a prop:
    under SUPPLIER_FRONT_M of navmesh straight in front (crate_room's lines from his front). The message gives the
    other quarter turns' room."""
    room = crate_room(mesh, pos, yaw)
    if room >= SUPPLIER_FRONT_M:
        return None
    turns = [(round((yaw + d + 180.0) % 360.0 - 180.0, 1), d) for d in (180.0, 90.0, -90.0)]
    return "he faces (yaw %.0f) %.2f m of navmesh, under %.1f m: a wall or a prop in front of him (%s)" % (
        yaw, room, SUPPLIER_FRONT_M, ", ".join("turned %+.0f: %.2f m" % (d, crate_room(mesh, pos, y)) for y, d in turns))


# --- a supply crate in the way --------------------------------------------------------------------------------------
#
# A crate's open side may face the room and the crate still stand in the way: Garden Show's Stash05 (Dartmoor), moved
# out of the plant exhibition, stood just inside the greenhouse's north double door, 1.3 m in from the door's line,
# across 1.45 m of its 2 m opening ("you have to walk around it to get inside"). A crate the navmesh was
# made without (a moved one, or a vanilla one on an option's navmesh) is not cut out of it, so the navmesh says nothing
# of it: these checks put its footprint (CRATE_HALF_WIDTH x CRATE_HALF_DEPTH about its origin) against the doors and
# the navmesh round it.
#
# A door's opening: the navmesh along the door's line (its local X, a leaf's width: 64 of Dartmoor's 76 doors and
# gates have floor through along it under Sloth's navmesh, the mansion open; the other way it is often the walk
# through the door), the run of floor there that goes through to both sides, nearest its origin (a double door's
# origin is at one leaf's hinge: Dartmoor's runs 1.05-2.95 m along it, a single door's 0.75-1.25 m), widened by
# DOOR_SIDE_M either side (the navmesh stops short of the frame by the walking radius). A door the navmesh does not
# pass (shut for good, or the edge of an area an option seals off) has no opening. The doorway is the opening,
# DOOR_FRONT_M deep on either side of the door's line (which runs through the middle of the wall: about 1 m clear in
# front of the wall's face): no crate footprint in it.
# A passage: along each of the crate's two axes, the navmesh from each pair of opposite faces out to its edge (the
# narrowest of three lines); the wider of the two gaps is what is left to walk through. Under PASSAGE_M, where the
# floor was at least PASSAGE_M across before the crate (both gaps and the crate) and the wider gap's strip goes on past
# both of the crate's ends (a way through, not a nook), the crate narrows a passage; under PASSAGE_HARD_M (the open
# side's own minimum, CRATE_FRONT_M: about 1.1 m of floor, the navmesh stopping a walking radius short of a wall) it
# is in the way, between the two it is tight (a note: The Reflection's Stash10 leaves 0.80 m in its corridor, The
# Icon's PlaneArea_01 0.85 m, both placed and seen in game before this check).

DOOR_FRONT_M = 1.5     # m: the doorway's depth on either side of the door's line
DOOR_SEARCH_M = 3.0    # m: the opening is looked for this far along the door's line from its origin
DOOR_ORIGIN_M = 1.6    # m: the opening's run passes this close to the door's origin
DOOR_SIDE_M = 0.3      # m: the opening reaches this far past the navmesh's run either side
DOOR_THROUGH_M = 0.6   # m: an opening has floor this far out on both sides of the line
DOOR_DZ = 1.5          # m: a door this far above or below the crate is on another floor
PASSAGE_M = 1.0        # m: a gap a crate leaves in a passage under this is tight
PASSAGE_HARD_M = 0.8   # m: ... and under this, in the way
PASSAGE_PAST_M = 0.6   # m: a gap's strip is a way through when its floor goes on this far past both crate ends


def _floor(mesh, x, y, z):
    return mesh.area_at((x, y, z + 0.2), up=0.6, down=0.8) is not None


def _axes(yaw):
    """(local X, local Y) in plan for a turn of yaw degrees about up."""
    r = math.radians(yaw)
    return (math.cos(r), math.sin(r)), (-math.sin(r), math.cos(r))


def door_opening(mesh, pos, yaw):
    """The door's opening on the navmesh: (u0, u1, axis, normal), its floor's run along axis (a unit plan vector, the
    door's local X) from u0 to u1 meters off its origin, normal the plan direction through it; None when no run of
    floor near the origin goes through (a door the navmesh does not pass: locked for good, or not a way in)."""
    axis, normal = _axes(yaw)
    n = int(round(DOOR_SEARCH_M / CRATE_STEP_M))
    on = []
    for k in range(-n, n + 1):
        u = k * CRATE_STEP_M
        x, y = pos[0] + axis[0] * u, pos[1] + axis[1] * u
        on.append(_floor(mesh, x, y, pos[2]) and
                  _floor(mesh, x + normal[0] * DOOR_THROUGH_M, y + normal[1] * DOOR_THROUGH_M, pos[2]) and
                  _floor(mesh, x - normal[0] * DOOR_THROUGH_M, y - normal[1] * DOOR_THROUGH_M, pos[2]))
    runs, start = [], None
    for k, f in enumerate(on + [False]):
        if f and start is None:
            start = k
        elif not f and start is not None:
            runs.append(((start - n) * CRATE_STEP_M, (k - 1 - n) * CRATE_STEP_M))
            start = None
    # The run nearest the origin; one over the whole search is open floor with no wall, not a doorway.
    near = [(0.0 if u0 <= 0.0 <= u1 else min(abs(u0), abs(u1)), u0, u1) for u0, u1 in runs]
    near = [r for r in near if r[0] <= DOOR_ORIGIN_M and r[2] - r[1] < 2 * DOOR_SEARCH_M - 0.1]
    if not near:
        return None
    _, u0, u1 = min(near)
    return u0 - DOOR_SIDE_M, u1 + DOOR_SIDE_M, axis, normal


def crate_corners(pos, yaw, step=4):
    """Points over the crate's footprint (its outline and middle line), in plan."""
    ax, ay = _axes(yaw)
    out = []
    for k in range(-step, step + 1):
        for j in (-1, 0, 1):
            u, v = k * CRATE_HALF_WIDTH / step, j * CRATE_HALF_DEPTH
            out.append((pos[0] + ax[0] * u + ay[0] * v, pos[1] + ax[1] * u + ay[1] * v))
    return out


def crate_in_doorway(mesh, pos, yaw, door_pos, door_yaw):
    """None, or why the crate at pos (turned yaw) stands in the door's doorway: part of its footprint within
    DOOR_FRONT_M of the door's line, across its opening (door_opening)."""
    if abs(door_pos[2] - pos[2]) > DOOR_DZ:
        return None
    op = door_opening(mesh, door_pos, door_yaw)
    if op is None:
        return None
    u0, u1, axis, normal = op
    hits = []
    for x, y in crate_corners(pos, yaw):
        dx, dy = x - door_pos[0], y - door_pos[1]
        u, v = dx * axis[0] + dy * axis[1], dx * normal[0] + dy * normal[1]
        if u0 <= u <= u1 and abs(v) <= DOOR_FRONT_M:
            hits.append((abs(v), u))
    if not hits:
        return None
    us = [u for _, u in hits]
    return "its footprint is %.2f m from the door's line, across %.2f m of its %.2f m opening (within %.1f m in " \
           "front of a door: no crate)" % (min(v for v, _ in hits), min(max(us), u1) - max(min(us), u0) + 0.25,
                                           u1 - u0, DOOR_FRONT_M)


def _gap(mesh, pos, start, out, limit=CRATE_ROOM_M):
    """Meters of navmesh from start straight along out (a unit plan vector), at pos's height; floor that starts
    within CRATE_CUT_M of the face (the navmesh cut round a vanilla crate) counts from the face."""
    r, floor = 0.0, False
    while r < limit:
        if _floor(mesh, start[0] + out[0] * r, start[1] + out[1] * r, pos[2]):
            floor = True
        elif floor or r >= CRATE_CUT_M:
            break
        r += CRATE_STEP_M
    return min(r, limit) if floor else 0.0


def crate_passage(mesh, pos, yaw):
    """None, or (why, in the way) when the crate at pos (turned yaw) narrows a passage below PASSAGE_M; in the way:
    below PASSAGE_HARD_M (see above)."""
    ax, ay = _axes(yaw)
    for name, axis, half, across, half_across, lines in (
            ("its sides", ay, CRATE_HALF_DEPTH, ax, CRATE_HALF_WIDTH, (-0.6, 0.0, 0.6)),
            ("its ends", ax, CRATE_HALF_WIDTH, ay, CRATE_HALF_DEPTH, (-0.15, 0.0, 0.15))):
        gaps = []
        for sgn in (1.0, -1.0):
            out = (axis[0] * sgn, axis[1] * sgn)
            g = min(_gap(mesh, pos, (pos[0] + out[0] * half + across[0] * a, pos[1] + out[1] * half + across[1] * a),
                         out) for a in lines)
            gaps.append((g, out))
        (g, out), (g2, _) = max(gaps), min(gaps)
        if g >= PASSAGE_M or g + g2 + 2 * half < PASSAGE_M:
            continue
        mid = half + max(g, 0.1) / 2
        ends = [(pos[0] + out[0] * mid + across[0] * s * (half_across + PASSAGE_PAST_M),
                 pos[1] + out[1] * mid + across[1] * s * (half_across + PASSAGE_PAST_M)) for s in (1.0, -1.0)]
        if all(_floor(mesh, x, y, pos[2]) for x, y in ends):
            hard = round(g, 2) < PASSAGE_HARD_M
            return "it narrows a passage across %s to %.2f m of navmesh (%.2f m on the other side; %s %.1f m)" % (
                name, g, g2, "in the way: under" if hard else "tight: under", PASSAGE_HARD_M if hard else PASSAGE_M), \
                hard
    return None


def crate_in_the_way(mesh, pos, yaw, doors):
    """[(door or None, why, in the way)]: the doorways of doors ([(description, position, yaw, ...)]) the crate at pos
    (turned yaw) stands in (crate_in_doorway; each in the way), and the passage it narrows (crate_passage)."""
    out = []
    reach = math.hypot(CRATE_HALF_WIDTH, CRATE_HALF_DEPTH) + DOOR_SEARCH_M + DOOR_FRONT_M
    for d in doors:
        if plan_d(pos, d[1]) > reach:
            continue
        why = crate_in_doorway(mesh, pos, yaw, d[1], d[2])
        if why:
            out.append((d, "in the doorway of %s at %s: %s" % (d[0], fmt(d[1]), why), True))
    narrow = crate_passage(mesh, pos, yaw)
    if narrow:
        out.append((None,) + narrow)
    return out
