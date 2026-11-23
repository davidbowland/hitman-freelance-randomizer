#!/usr/bin/env python3
"""Builds the Freelancer bricks of the full map variants' packages, as ResourceLib JSON plus reference tables, from
IOI's two Freelancer bricks of the map (mild Sapienza's mission_mild_bingo and override_bingo; mild Marrakesh's
mission_mild_mancala and override_mancala): for each variant (its spec module) a copy of each, edited for that
variant's own scene. landslide_spec.py is the full Landslide (fr_mission_mild_landslide, fr_override_landslide on
_scene_mamba); author_spec.py the full Author (fr_mission_mild_author, fr_override_author on scene_ebola);
icon_spec.py the full Icon (on _scene_copperhead); marrakesh_spec.py the full Marrakesh night (fr_mission_mild_marrakesh_night,
fr_override_marrakesh_night on Marrakesh's _scene_python, in partition 25's package; no option of its own since the
plain marrakesh_night was removed: built as the full Entertainer's base). badboy_spec.py, the full Bad Boy,
is a layer on the full Icon (LAYER_OF): no bricks of its own, the vanilla mission_caipirinha appended to the Icon's,
checked against the Icon's built bricks when its JSON is there (check_layer). patientzero_spec.py is the full Patient
Zero (fr_mission_mild_patientzero, fr_override_patientzero on Hokkaido's _scene_flu, in partition 22's package): the
outbreak's story is switched off by IOI's own Contracts-mode brick, which our override loads (STORY_SWITCH), and the
checks that need the second Hokkaido read (the hotel's people, location, the normal navmesh) warn and are skipped
when its JSON or NAVP is missing. entertainer_spec.py, the full Entertainer, is a layer on the full Marrakesh night as
the Bad Boy is on the Icon: the vanilla mission_marrakesh_maitai appended to the night's bricks. gluttony_spec.py, the
full Gluttony, swaps no scene: it is a fix brick (FIX_PATH: fr_gluttony, deletes only) appended after the vanilla
scenario_azalea on the contract's own Chongqing scene, in partition 6's package (build_fix); its checks and clash audit
(gluttony_audit.py) read the bricks' binaries from the --bin folders (tools/rpkg/entity.py), never JSON.

    python3 -I tools/landslide/build_bricks.py --cache <rpkg metadata> --json <ResourceLib JSON folder>... --out <dir>
        [--variant landslide|author ...]

--json folders hold ZHMTools ResourceTool `convert` output, named <HASH>.<TYPE>.json (any prefix, such as rpkg2.py's
<package>_<HASH>.<TYPE>.json, is fine). Needed (TEMP and TBLU each): mission_mild_bingo, override_bingo, each variant's
base brick and reach sources (mission_mamba; scenario_ebola), and, for the starting-location check, every brick of a
swapped scene that holds hero spawns (agencymarkup_location) plus the bricks whose blockers count (blockedoff_mansion,
dressing_mission_mamba). A pool brick other than the base (Landslide's night ambient bricks) is optional: when there,
the pool's ids in it are checked too.

Writes to --out, per resource, <HASH>.<TYPE>.json (for ResourceTool `generate`) and the manifest package_bricks.json:
per variant its name, scene, the bricks the swap loads, its fingerprints and its resources (path, type, hash, the
vanilla source, the reference table). Stops with a message on any inconsistency: an id of a spec missing or moved, a
reference left dangling, a pool actor a loaded brick deletes, a spot the swap can spawn that is far from every act of
the variant's reach sources (or inside a blocker, where the spec asks), a Freelancer starting location left in the
scene that is far from them or inside a blocker, an own hash that collides.
scripts/build-landslide-pkg.sh runs this, then `generate`, then gen_package_inc.py.
"""
import argparse
import copy
import glob
import json
import math
import os
import struct
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import author_spec  # noqa: E402
import badboy_spec  # noqa: E402
import brick_edit as be  # noqa: E402
import entertainer_spec  # noqa: E402
import gen_package_inc as gp  # noqa: E402
import gluttony_audit  # noqa: E402
import gluttony_spec  # noqa: E402
import icon_spec  # noqa: E402
import landslide_spec  # noqa: E402
import marrakesh_spec  # noqa: E402
import navp  # noqa: E402
import patientzero_spec  # noqa: E402
import placement as pl  # noqa: E402
import spot_fix  # noqa: E402
import spotfix_spec  # noqa: E402

# The variants, in package order (the full Landslide's resources first, as before the Author existed; the Icon's
# after, so the first two stay byte for byte what they were). The Marrakesh night is in its own package (partition 25).
# The full Bad Boy is a layer on the full Icon (LAYER_OF: no bricks of its own, see build_layer), after it. The full
# Patient Zero is in Hokkaido's package (partition 22). The full Entertainer, a layer on the Marrakesh night, comes
# after them so the others keep their places, and the full Gluttony (a fix brick, in Chongqing's package, partition 6)
# last.
VARIANTS = [landslide_spec, author_spec, icon_spec, marrakesh_spec, badboy_spec, patientzero_spec, entertainer_spec,
            gluttony_spec] + spotfix_spec.SPECS

# What build_variant built, by variant name, for a layer on it (build_layer): its two bricks and the read-only bricks
# its swap loads.
BUILT = {}

# A spec's partition when it names none: Sapienza's.
SAPIENZA_PARTITION = (26, "coastaltown")


def partition(spec):
    """(index, id) of the partition whose package holds the spec's bricks (a layer's: its base's)."""
    if getattr(spec, "LAYER_OF", None):
        spec = next(s for s in VARIANTS if s.NAME == spec.LAYER_OF)
    return tuple(getattr(spec, "PARTITION", SAPIENZA_PARTITION))


def src_mission(spec):
    """The (TEMP, TBLU) of the Freelancer mission brick the spec copies (mild Sapienza's mission_mild_bingo unless the
    spec names another: SRC_MISSION)."""
    return getattr(spec, "SRC_MISSION", None) or spec.MISSION_MILD_BINGO


def src_override(spec):
    return getattr(spec, "SRC_OVERRIDE", None) or spec.OVERRIDE_BINGO


def src_names(spec):
    """The two source bricks' names, for messages."""
    return getattr(spec, "SRC_NAMES", ("mission_mild_bingo", "override_bingo"))

# An entity that stays where it is under a spec with a navmesh (a kept spot, its clues and acts, a kept target's
# acts, a start) needs navmesh floor within this in plan; an exit button (by a car door, a gate) within EXIT_FLOOR_M.
KEPT_FLOOR_M = 1.5
EXIT_FLOOR_M = 2.5

# A layer's act may stand next to a clue at least this far above it: the clue lies on a table (layer check).
TABLE_CLUE_RISE_M = 0.4

# SPOT_MOVES kinds: the template (file name start) an entity of that kind must have.
KIND_TYPES = {
    "safe": "setpiece_evergreen_container_items_safe",
    "stash": "setpiece_evergreen_stash",
    "supplier": "helper_evergreen_supplierpackage",
    "mule": "setpiece_evergreen_moneymulesetup",
    "guard": "setpiece_evergreen_safeactor",
    "clue": "setpiece_evergreen_scannable_safeclue",
    "act": "act_",
}

# A spot the swap can spawn (a randomizer slot's spot, a mule's acts, a used safe's clues and guard, an exit, a pool
# actor's acts) must be this close to one of the reach sources' acts (3D, height counted twice).
REACH_M = 15.0

MANIFEST = "package_bricks.json"


def fail(msg):
    sys.exit("build_bricks: " + msg)


def find_json(dirs, h, typ):
    hits = []
    for d in dirs:
        hits += glob.glob(os.path.join(d, "*%016X.%s.json" % (h, typ)))
    if not hits:
        return None
    with open(sorted(hits)[0]) as f:
        return json.load(f)


def need_json(dirs, h, typ, what):
    js = find_json(dirs, h, typ)
    if js is None:
        fail("%s %016X.%s.json is in none of %s (ResourceTool convert of the extracted resource)" %
             (what, h, typ, dirs))
    return js


def blueprint_root_subsets(args, h):
    """The entity subset names of the root of blueprint h (a TBLU in --json, or extracted in a --bin or --navp
    folder, read with tools/rpkg/entity.py), or None when it is in neither (or is not a TBLU: a C++ class's)."""
    u = find_json(args.json, h, "TBLU")
    if u is None:
        for d in list(getattr(args, "bin", None) or []) + list(getattr(args, "navp", None) or []):
            hits = sorted(glob.glob(os.path.join(d, "*%016X.TBLU" % h)))
            if hits:
                with open(hits[0], "rb") as f:
                    u = gluttony_audit.entity.read_tblu(f.read(), hits[0])
                break
    if u is None:
        return None
    return [n for n, _ in u["subEntities"][u["rootEntityIndex"]]["entitySubsets"]]


def check_subset_types(name, built, vanilla, args, audit):
    """Stops the build when a member of an entity subset of our built bricks has a type not known to carry that
    subset (brick_edit.subset_type_problems): the scene load crashes on it (HITMAN3.exe+0xF2DE19, seen in the full
    Icon). Known: every (subset, type) pair the vanilla bricks read for the build use, and any template whose
    blueprint's root, read from --json or --bin, has the subset."""
    known = be.subset_type_evidence(vanilla)
    problems = []
    for b in built:
        problems += be.subset_type_problems(b, known, lambda h: blueprint_root_subsets(args, h))
    if problems:
        fail("%s: entity subset members whose type may not have the subset (the game crashes building the brick; "
             "give the entity a type a vanilla brick has in that subset, or take it out of the subset):\n  %s" %
             (name, "\n  ".join(text for text, _ in problems)))
    audit.append("subsets: %d members of %d bricks, each of a type a vanilla brick has in the same subset" % (
        sum(len(b.subset_members()) for b in built), len(built)))


def props(e):
    d = {}
    for kind in ("propertyValues", "postInitPropertyValues"):
        for p in e[kind]:
            d[p["nPropertyID"]] = p["value"]["$val"]
    return d


def matrix(m):
    X, Y, Z, T = m["XAxis"], m["YAxis"], m["ZAxis"], m["Trans"]
    return [[X["x"], Y["x"], Z["x"], T["x"]], [X["y"], Y["y"], Z["y"], T["y"]], [X["z"], Y["z"], Z["z"], T["z"]]]


def smatrix(w):
    return {"XAxis": {"x": w[0][0], "y": w[1][0], "z": w[2][0]}, "YAxis": {"x": w[0][1], "y": w[1][1], "z": w[2][1]},
            "ZAxis": {"x": w[0][2], "y": w[1][2], "z": w[2][2]}, "Trans": {"x": w[0][3], "y": w[1][3], "z": w[2][3]}}


IDENT = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0) for j in range(4)]
            for i in range(3)]


class World:
    """World matrices of a TEMP's spatial entities, composing m_eidParent chains of local parents (None when the
    chain leaves the brick, unless it leaves into one of identity_parents: entity ids of other bricks taken as the
    identity, or {entity id: its world matrix} (folder_matrices))."""

    def __init__(self, temp, identity_parents=()):
        self.E = temp["subEntities"]
        self.P = [props(e) for e in self.E]
        self.root = temp["rootEntityIndex"]
        self.identity_parents = identity_parents if isinstance(identity_parents, dict) else \
            {eid: IDENT for eid in identity_parents}
        self.cache = {}

    def world(self, i, depth=0):
        if i in self.cache:
            return self.cache[i]
        if i == self.root:
            return IDENT
        if depth > 64:
            return None
        d = self.P[i]
        local = matrix(d["m_mTransform"]) if "m_mTransform" in d else IDENT
        par = d.get("m_eidParent")
        if par is None or (par["externalSceneIndex"] < 0 and par["entityIndex"] < 0):
            w = local
        elif par["externalSceneIndex"] >= 0:
            m = self.identity_parents.get(par["entityID"])
            w = mul(m, local) if m is not None else None
        else:
            pw = self.world(par["entityIndex"], depth + 1)
            w = mul(pw, local) if pw is not None else None
        self.cache[i] = w
        return w

    def pos(self, i):
        w = self.world(i)
        return None if w is None else (w[0][3], w[1][3], w[2][3])


def folder_matrices(location, folders):
    """{entity id: world matrix} of location's world folders (a spec's LOCATION_WORLD_FOLDERS): the parents other
    bricks hang world-placed entities off. A folder keeps its own transform: location's 000_Outside (a3abfc17113fa981)
    stands at (34.17, -35.76, 12.84), so blockedoff_mansion's and The Icon's blockers under it are that far from their
    local coordinates (checked against the scenes' navmeshes: each blocker's place has no floor under the
    variant's navmesh and floor under Freelancer's)."""
    w = World(location.temp)
    out = {}
    for eid in sorted(folders):
        try:
            m = w.world(location.index_of(eid))
        except be.BrickError as e:
            fail("LOCATION_WORLD_FOLDERS: %s" % e)
        if m is None:
            fail("LOCATION_WORLD_FOLDERS: %016x has no world position in %s" % (eid, location.name))
        out[eid] = m
    return out


def act_points(temp, refs, names, identity_parents=(), skip=()):
    """World positions of a TEMP's acts (entities whose type is an act template), leaving out the indexes in skip."""
    w = World(temp, identity_parents)
    points = []
    for i, e in enumerate(temp["subEntities"]):
        if i in skip:
            continue
        t = names.get(refs[e["entityTypeResourceIndex"]][1], "").lower()
        leaf = t.split("?/")[-1] if "?/" in t else ""
        if "/acts" in t or leaf.startswith("act_") or "_act_" in leaf:
            p = w.pos(i)
            if p and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                points.append(p)
    return points


def litter_points(temp, refs, names, identity_parents=()):
    """World positions of a TEMP's litter (props whose type names litter: flat, lying on the floor), for
    placement.Placement's floor_refs."""
    w = World(temp, identity_parents)
    points = []
    for i, e in enumerate(temp["subEntities"]):
        t = names.get(refs[e["entityTypeResourceIndex"]][1], "").lower()
        if "litter" in (t.split("?/")[-1] if "?/" in t else t.rsplit("/", 1)[-1]):
            p = w.pos(i)
            if p and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                points.append(p)
    return points


def floor_refs(spec, args, newest, base, names):
    """The litter of the base brick and of the spec's LOCATION brick (when its TEMP JSON is in --json): what lies on
    the floor beside the moved spots, for placement.Placement.floor_z. Returns ([positions], audit line)."""
    folders = folder_matrices(json_brick(args, newest, spec.LOCATION, "holds the world folders"),
                              spec.LOCATION_WORLD_FOLDERS) if getattr(spec, "LOCATION_WORLD_FOLDERS", None) else {}
    pts = litter_points(base.temp, base.temp_refs, names, folders)
    where = ["%d in %s" % (len(pts), base.name)]
    loc = getattr(spec, "LOCATION", None)
    t = find_json(args.json, loc, "TEMP") if loc in newest else None
    if t is not None:
        lp = litter_points(t, newest[loc]["refs"], names)
        pts += lp
        where.append("%d in %s" % (len(lp), gp_name(loc)))
    elif loc is not None:
        where.append("%s not read" % gp_name(loc))
    return pts, "floor: litter for the floor height of moved spots: %s" % ", ".join(where)


def deleted_indexes(b, h, deleted):
    """Indexes of b (brick hash h) that the loaded bricks delete ({(brick hash, entity id)}), themselves or through
    a local parent."""
    ids = [e["entityId"] for e in b.tblu["subEntities"]]
    P = [props(e) for e in b.temp["subEntities"]]
    out = set()
    for i in range(len(ids)):
        j, depth = i, 0
        while j is not None and depth < 64:
            if (h, ids[j]) in deleted:
                out.add(i)
                break
            par = P[j].get("m_eidParent")
            j = par["entityIndex"] if par and par["externalSceneIndex"] < 0 and par["entityIndex"] >= 0 else None
            depth += 1
    return out


class Reach:
    """Distance from a point to the nearest act of the variant's reach sources: [(name, temp, refs)], whose acts
    together must number at least min_acts."""

    def __init__(self, sources, names, min_acts, identity_parents=(), missing=()):
        """sources: (name, temp, refs), or (name, points) for acts read already (the spec's REACH_CROWD). missing:
        the names of optional sources that are not read (the spec's OPTIONAL_REACH): the distance is then only a
        hint, and too_far never fails a place (the navmesh and blocker checks still run)."""
        self.missing = list(missing)
        self.skipped = bool(self.missing)
        self.points = []
        self.source = []
        for src in sources:
            pts = src[1] if len(src) == 2 else act_points(src[1], src[2], names, identity_parents)
            self.points += pts
            self.source += [src[0]] * len(pts)
        self.names = [src[0] for src in sources]
        if len(self.points) < min_acts:
            fail("%s: only %d acts found (a type name list is needed: --names)" %
                 (" and ".join(self.names), len(self.points)))

    def too_far(self, d):
        """Whether a place at distance d fails the reach check (never when the check is skipped)."""
        return d > REACH_M and not self.skipped

    @staticmethod
    def _d(p, q):
        return math.sqrt((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (2 * (p[2] - q[2])) ** 2)

    def __call__(self, p):
        return min(self._d(p, q) for q in self.points)

    def nearest(self, p):
        """(distance, the source whose act it is)."""
        return min((self._d(p, q), s) for q, s in zip(self.points, self.source))

    def where(self, p):
        """", via mission_mamba's acts" when no act of the first source (the base) is within REACH_M: the spot counts
        as reachable through a later source's (the first, in the spec's order, with an act within REACH_M; else the
        nearest)."""
        if len(self.names) < 2:
            return ""
        for k, name in enumerate(self.names):
            pts = [q for q, s in zip(self.points, self.source) if s == name]
            if pts and min(self._d(p, q) for q in pts) <= REACH_M:
                return "" if k == 0 else ", via %s's acts" % name
        d, s = self.nearest(p)
        return ", via %s's acts" % s


NAMES = {}


def gp_name(h):
    p = NAMES.get(h, "")
    return p.split("]")[0].lstrip("[").rsplit("/", 1)[-1] if p else "%016X" % h


def load_names(path):
    """Resource hash -> path, from a hash list (hash.TYPE,path lines); only the types' names are needed."""
    names = {}
    if not path:
        return names
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            hh, _, rest = line.rstrip("\n").partition(",")
            h, _, _typ = hh.partition(".")
            try:
                names[int(h, 16)] = rest
            except ValueError:
                pass
    return names


def pool_brick(spec, brick):
    """A POOL_ADDITIONS / KEEP_SCENE_TARGETS brick: "base", a key of the spec's POOL_BRICKS or DAY, or a hash."""
    if brick == "base":
        return spec.BASE[0]
    if isinstance(brick, str):
        table = getattr(spec, "POOL_BRICKS", None) or getattr(spec, "DAY", {})
        if brick not in table:
            fail("%s: pool brick \"%s\" is not in the spec" % (spec.NAME, brick))
        return table[brick]
    return brick


def deleted_by(bricks):
    """{(brick hash, entity id)} that the given read-only bricks delete (overrideDeletes)."""
    out = set()
    for b in bricks:
        for r in b.tblu["overrideDeletes"]:
            out.add((b.scenes[r["externalSceneIndex"]], r["entityID"]))
    return out


def type_leaf(b, i):
    """The file name of an entity's template ("act_mr_stand_think_100cm.entitytemplate"), from the name list."""
    t = NAMES.get(b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1], "")
    return t.split("]")[0].rsplit("/", 1)[-1].lower()


def kept_floor(placement, desc, pos, radius=KEPT_FLOOR_M, down=None):
    """With a navmesh: the entity's floor (placement.check_kept; down: how far below it the floor may be, for one on
    high furniture, the spec's KEPT_RAISED), or the build stops."""
    if placement is None or not placement.checked:
        return None
    try:
        if down is not None:
            return placement.check_kept(desc, pos, radius, down=down)
        return placement.check_kept(desc, pos, radius)
    except pl.PlacementError as e:
        fail("navmesh check: %s" % e)


def apply_spot_moves(spec, b, base, placement, audit):
    """The spec's SPOT_MOVES, RETYPE, DELETE_APPROACH_POINTS and RIGID on the override brick: each moved entity keeps
    its id and gets its place (placement.Placement judges every nominal one first). Refuses a move set that is not
    whole (a moved safe without its clues, guard or guard acts, a moved mule without its acts, a clue, guard or act of
    an entity that stays), a kind that is not the entity's template, a slot spot that is not a slot's. Returns the
    moved entity ids."""
    moves = getattr(spec, "SPOT_MOVES", {})
    if not moves:
        return set()
    w = World(b.temp)
    P = w.P
    pins = b.tblu["pinConnections"]
    terr = props(b.temp["subEntities"][b.index_of(spec.TERRITORY)])
    slot_of = {}
    for k, v in terr.items():
        if k.startswith("available") and v["externalSceneIndex"] < 0:
            slot_of[b.tblu["subEntities"][v["entityIndex"]]["entityId"]] = k
    for eid, m in sorted(moves.items()):
        i = b.index_of(eid)
        if m["kind"] not in KIND_TYPES:
            fail("SPOT_MOVES %s: unknown kind %r" % (b.describe(i), m["kind"]))
        if not type_leaf(b, i).startswith(KIND_TYPES[m["kind"]]):
            fail("SPOT_MOVES %s is a %s, not a %s" % (b.describe(i), type_leaf(b, i) or "(no name)", m["kind"]))
        if m["kind"] in ("safe", "stash", "supplier", "mule") and eid not in slot_of:
            fail("SPOT_MOVES %s is no territory slot's %s" % (b.describe(i), m["kind"]))

    # Whole groups: what each moved spot brings, and the owner of each moved clue, guard and act.
    owner = {}
    for eid, m in sorted(moves.items()):
        i = b.index_of(eid)
        group = []
        if m["kind"] == "safe":
            group += [(v["entityIndex"], "clue") for k, v in P[i].items()
                      if k.startswith("Clue") and be.is_ref(v) and v["externalSceneIndex"] < 0]
            guards = [p["toID"] for p in pins if p["fromID"] == i and p["fromPinName"] == "safeEnabled"]
            group += [(g, "guard") for g in guards]
            for g in guards:  # its acts (an "Acts" group entity names the guard too, and has no place)
                group += [(j, "act") for j in range(len(P)) if "m_pActor" in P[j] and
                          P[j]["m_pActor"]["externalSceneIndex"] < 0 and P[j]["m_pActor"]["entityIndex"] == g and
                          type_leaf(b, j).startswith(KIND_TYPES["act"])]
        elif m["kind"] == "mule":
            group += [(r["entityIndex"], "act") for r in P[i].get("acts", [])]
        for j, kind in group:
            jid = b.tblu["subEntities"][j]["entityId"]
            if jid not in moves or moves[jid]["kind"] != kind:
                fail("SPOT_MOVES moves %s but not its %s %s (as a %s)" % (b.describe(i), kind, b.describe(j), kind))
            owner[jid] = eid
    for eid, m in sorted(moves.items()):
        if m["kind"] in ("clue", "guard", "act") and eid not in owner:
            fail("SPOT_MOVES %s (%s) belongs to no moved safe or mule" % (b.describe(b.index_of(eid)), m["kind"]))

    # Where each goes.
    kept_spots = []
    for eid, slot in sorted(slot_of.items(), key=lambda kv: kv[1]):
        i = b.index_of(eid)
        if eid not in moves and not slot.startswith("availableMule"):
            kept_spots.append(("%s (%s, kept)" % (b.describe(i), slot), w.pos(i)))
    # Each borrows the place of an entity of the base brick (an act, an item spawner's table) or of another moved
    # one (a guard its first act, a mule its first act).
    bw = World(base.temp)
    points = []
    for eid, m in sorted(moves.items()):
        i = b.index_of(eid)
        what = slot_of.get(eid) or "of %s" % slot_of.get(owner.get(eid), "%016x" % owner.get(eid, 0))
        if m["anchor"] in moves:
            anchor = moves[m["anchor"]]["to"]
        else:
            try:
                anchor = bw.pos(base.index_of(m["anchor"]))
            except be.BrickError:
                fail("SPOT_MOVES %s: its anchor %016x is neither %s's nor a moved entity" % (b.describe(i), m["anchor"],
                                                                                           base.name))
        points.append(pl.Point(eid, "%s (%s %s)" % (b.describe(i), m["kind"], what), m["to"], m["kind"],
                               table=m.get("table", False), anchor=anchor))
    try:
        final, lines = placement.move(points, kept_spots)
    except pl.PlacementError as e:
        fail("%s's spot moves: %s" % (spec.DISPLAY, e))
    if not placement.checked:
        audit.append("spots: WARNING: the navmesh check did not run (no %016X.NAVP, %s, in --navp): the %d moved "
                     "entities keep the design's nominal places, judged only by their distance to %s's acts" %
                     (spec.NAVMESH[0], spec.NAVMESH[1], len(points), spec.DISPLAY))
    for line in lines:
        audit.append("spots: " + line)

    # Rigid groups: the helper's extras move by its offset (the design's nominal places as a cross-check).
    rigid_ids = set()
    for helper, extras in sorted(getattr(spec, "RIGID", {}).items()):
        if helper not in moves:
            fail("RIGID %016x is not moved" % helper)
        hi = b.index_of(helper)
        old = w.pos(hi)
        nominal_delta = [moves[helper]["to"][k] - old[k] for k in range(3)]
        delta = [final[helper][k] - old[k] for k in range(3)]
        children = [b.describe(j) for j in range(len(P)) if "m_eidParent" in P[j] and
                    P[j]["m_eidParent"]["externalSceneIndex"] < 0 and P[j]["m_eidParent"]["entityIndex"] == hi]
        for xid, nominal in extras:
            xi = b.index_of(xid)
            xo = w.pos(xi)
            if math.dist([xo[k] + nominal_delta[k] for k in range(3)], nominal) > 0.05:
                fail("RIGID %s: the helper's offset puts it at (%.2f, %.2f, %.2f), not the spec's %s" %
                     (b.describe(xi), xo[0] + nominal_delta[0], xo[1] + nominal_delta[1], xo[2] + nominal_delta[2],
                      nominal))
            q = tuple(xo[k] + delta[k] for k in range(3))
            note = kept_floor(placement, b.describe(xi), q)
            b.set_translation(xid, q)
            rigid_ids.add(xid)
            audit.append("spots: %s moves with %s to (%.2f, %.2f, %.2f)%s" % (b.describe(xi), b.describe(hi), q[0],
                                                                             q[1], q[2], ": " + note if note else ""))
        audit.append("spots: %s carries %s" % (b.describe(hi), ", ".join(children) or "nothing"))

    for eid, m in sorted(moves.items()):
        b.set_translation(eid, final[eid], yaw=m.get("yaw"))

    # A moved supply crate opens toward open floor (placement.crate_problem: its open side, local -Y, with 0.8 m of
    # navmesh in front, not facing the tighter side; the spec's yaw turns it, placement.crate_yaw finds one).
    if placement.checked:
        for eid, m in sorted(moves.items()):
            if m["kind"] != "stash":
                continue
            x = b.find_property(eid, "m_mTransform")["value"]["$val"]["XAxis"]
            yaw = m["yaw"] if m.get("yaw") is not None else pl.yaw_of((x["x"], x["y"], x["z"]))
            bad = pl.crate_problem(placement.mesh, final[eid], yaw)
            if bad:
                best = pl.crate_yaw(placement.mesh, final[eid], yaw)
                fail("SPOT_MOVES %s: %s; yaw=%.1f gives %.2f m" % (b.describe(b.index_of(eid)), bad, best[0], best[1]))
            audit.append("spots: %s opens toward %.2f m of navmesh (yaw %.0f%s)" % (
                b.describe(b.index_of(eid)), pl.crate_room(placement.mesh, final[eid], yaw), yaw,
                ", the spec's" if m.get("yaw") is not None else ", vanilla"))

    # Acts retyped where a seat, rail or wall is not guaranteed.
    for eid, (old_type, new_type, like) in sorted(getattr(spec, "RETYPE", {}).items()):
        i = b.index_of(eid)
        if moves.get(eid, {}).get("kind") != "act":
            fail("RETYPE %s is not a moved act" % b.describe(i))
        if type_leaf(b, i) != old_type + ".entitytemplate":
            fail("RETYPE %s is a %s, not a %s" % (b.describe(i), type_leaf(b, i), old_type))
        if type_leaf(b, b.index_of(like)) != new_type + ".entitytemplate":
            fail("RETYPE %s: %s is a %s, not a %s" % (b.describe(i), b.describe(b.index_of(like)),
                                                     type_leaf(b, b.index_of(like)), new_type))
        b.retype(eid, like)
        audit.append("spots: %s retyped %s -> %s" % (b.describe(i), old_type, new_type))

    # Approach paths through the old town go with the moved acts' places.
    n = 0
    for eid, m in sorted(moves.items()):
        if m["kind"] == "act" and "m_aApproachPath" in props(b.temp["subEntities"][b.index_of(eid)]):
            b.remove_property(eid, "m_aApproachPath")
            n += 1
    for eid in getattr(spec, "DELETE_APPROACH_POINTS", []):
        if not type_leaf(b, b.index_of(eid)).startswith("approachpathpoint"):
            fail("DELETE_APPROACH_POINTS %s is not an ApproachPathPoint" % b.describe(b.index_of(eid)))
    try:
        gone = b.delete(list(getattr(spec, "DELETE_APPROACH_POINTS", [])), subtree=False)
    except be.BrickError as e:
        fail("deleting the approach points: %s" % e)
    audit.append("spots: %d moved acts lost their approach path; %d approach points deleted" % (n, len(gone)))
    audit.append("spots: %d entities moved (%d safes, %d stashes, %d suppliers, %d mules, %d guards, %d acts, %d clues)"
                 ", %d moved with them, %d retyped; %s" % (
                     len(moves) + len(rigid_ids), *[sum(1 for m in moves.values() if m["kind"] == k) for k in
                                                    ("safe", "stash", "supplier", "mule", "guard", "act", "clue")],
                     len(rigid_ids), len(getattr(spec, "RETYPE", {})),
                     "navmesh-checked" if placement.checked else "NOT navmesh-checked (nominal places)"))
    return set(moves) | rigid_ids


def apply_repoints(b, repoints, audit, what):
    """[(entity id, property, (old scene, old id), (new scene, new id), why)]: each new scene is added to the brick's
    external scenes first when it is not one yet."""
    for eid, prop, old, new, why in repoints:
        if new[0] not in b.scenes:
            b.add_external_scene(new[0])
        try:
            n = b.repoint(eid, prop, old, new)
        except be.BrickError as e:
            fail(str(e))
        audit.append("%s: %s.%s -> %s %016x (%d reference%s): %s" % (what, b.describe(b.index_of(eid)), prop,
                                                                      gp_name(new[0]), new[1], n, "" if n == 1 else "s",
                                                                      why))


def build_mission(spec, mmb, override_rid, base_ids, pool_ids, reach, audit, deleted, placement=None):
    b = mmb
    b.replace_scene(src_override(spec)[0], override_rid)
    pool = b.find_property(spec.SPAWN_TARGETS, "m_aActors")["value"]["$val"]
    size = getattr(spec, "POOL_SIZE", 59)
    if len(pool) != size:
        fail("%s's pool has %d slots, not %d: the game changed it" % (src_names(spec)[0], len(pool), size))
    locals_in = [b.tblu["subEntities"][r["entityIndex"]]["entityId"] for r in pool if r["externalSceneIndex"] < 0]
    if sorted(locals_in) != sorted(spec.KEEP_LOCAL_TARGETS + spec.DROP_LOCAL_TARGETS):
        fail("%s's dedicated targets differ from the spec's" % src_names(spec)[0])
    for eid in spec.KEEP_LOCAL_TARGETS + spec.DROP_LOCAL_TARGETS:
        if b.get(eid, "m_bStartEnabled") is not False:
            fail("dedicated target %016x is not start-disabled" % eid)
    keep_scene = {(pool_brick(spec, br), eid) for br, eid in spec.KEEP_SCENE_TARGETS}
    if len(keep_scene) != len(spec.KEEP_SCENE_TARGETS):
        fail("%s: a kept scene target is listed twice" % spec.NAME)

    # The slots: kept dedicated targets and kept scene actors stay where they are, the rest take the additions in
    # order.
    slots = []
    additions = list(spec.POOL_ADDITIONS)
    if len(additions) + len(spec.KEEP_LOCAL_TARGETS) + len(keep_scene) != len(pool):
        fail("%d additions + %d kept targets + %d kept scene actors != %d slots" %
             (len(additions), len(spec.KEEP_LOCAL_TARGETS), len(keep_scene), len(pool)))
    if len({a[1] for a in additions}) != len(additions):
        fail("an addition is listed twice")
    found_scene = set()
    for r in pool:
        if r["externalSceneIndex"] < 0 and b.tblu["subEntities"][r["entityIndex"]]["entityId"] in \
                spec.KEEP_LOCAL_TARGETS:
            slots.append(("local", b.tblu["subEntities"][r["entityIndex"]]["entityId"], None))
        elif r["externalSceneIndex"] >= 0 and (b.scenes[r["externalSceneIndex"]], r["entityID"]) in keep_scene:
            key = (b.scenes[r["externalSceneIndex"]], r["entityID"])
            found_scene.add(key)
            slots.append(("kept", key[0], key[1]))
        else:
            brick, eid, name = additions.pop(0)
            slots.append((pool_brick(spec, brick), eid, name))
    if found_scene != keep_scene:
        fail("kept scene targets not in mission_mild_bingo's pool: %s" %
             ["%016X:%016x" % k for k in sorted(keep_scene - found_scene)])
    b.find_property(spec.SPAWN_TARGETS, "m_aActors")["value"]["$val"] = []

    # References into bricks the swap does not load, re-pointed to a like entity of a loaded one (an act's item key).
    apply_repoints(b, getattr(spec, "MISSION_REPOINTS", []), audit, "mission")

    scenes = [override_rid if h == "OVERRIDE" else h for h in spec.MISSION_SCENES]
    for slot in slots:
        brick = slot[1] if slot[0] == "kept" else slot[0]
        if brick != "local" and brick not in scenes:
            scenes.append(brick)
    b.set_external_scenes(scenes)

    new_pool = []
    for brick, eid, name in slots:
        if brick == "local":
            new_pool.append(b.local(eid))
            continue
        if brick == "kept":
            h, eid = eid, name
            if (h, eid) in deleted:
                fail("kept pool actor %016X:%016x is deleted by a brick the swap loads" % (h, eid))
            new_pool.append(be.ext_ref(b.scene_index(h), eid))
            continue
        if eid in spec.NEVER_IN_POOL:
            fail("%s %016x is never a pool actor" % (name, eid))
        if (brick, eid) in deleted:
            fail("pool addition %s %016x is deleted by a brick the swap loads" % (name, eid))
        if brick == spec.BASE[0]:
            if base_ids.get(eid) != name:
                fail("%s has no npcactor %016x \"%s\" (found %r): the game changed it" %
                     (gp_name(brick), eid, name, base_ids.get(eid)))
        elif brick in pool_ids and pool_ids[brick].get(eid) != name:
            fail("%016X has no npcactor %016x \"%s\"" % (brick, eid, name))
        new_pool.append(be.ext_ref(b.scene_index(brick), eid))
    b.find_property(spec.SPAWN_TARGETS, "m_aActors")["value"]["$val"] = new_pool

    # Exits the override brick deletes leave "Any exit used".
    for eid, why in sorted(getattr(spec, "DELETE_EXITS", {}).items()):
        k = b.scene_index(override_rid)
        values = b.get(spec.MISSION_ANY_EXIT, "m_aValues")
        kept = [r for r in values if not (r["externalSceneIndex"] == k and r["entityID"] == eid)]
        if len(kept) != len(values) - 1:
            fail("\"Any exit used\" does not list the exit %016x once" % eid)
        b.set(spec.MISSION_ANY_EXIT, "m_aValues", kept)
        audit.append("mission: \"Any exit used\" no longer lists %016x: %s" % (eid, why))

    for eid in getattr(spec, "DELETE_EXITS", {}):
        k = b.scene_index(override_rid)
        left = [owner for owner, _, r in b.all_refs() if r["externalSceneIndex"] == k and r["entityID"] == eid]
        if left:
            fail("the mission brick still names the deleted exit %016x: %s" % (eid, left))

    # The kept dedicated targets' acts must be reachable.
    w = World(b.temp)
    P = w.P
    role_actor = {i: P[i]["m_rActorProvider"]["entityIndex"] for i in range(len(P)) if "m_rActorProvider" in P[i]}
    for eid in spec.KEEP_LOCAL_TARGETS:
        i = b.index_of(eid)
        far, via = [], ""
        for j in range(len(P)):
            if "m_pActor" in P[j] and role_actor.get(P[j]["m_pActor"]["entityIndex"]) == i:
                d = reach(w.pos(j))
                if reach.too_far(d):
                    far.append("%s %.0f m" % (b.describe(j), d))
                kept_floor(placement, "kept target %s's act %s" % (b.describe(i), b.describe(j)), w.pos(j))
                via = via or reach.where(w.pos(j))
        audit.append("pool: dedicated target %s kept%s%s" % (b.describe(i), via,
                                                             "" if not far else ", FAR: " + ", ".join(far)))
        if far:
            fail("kept target %s has acts far from %s's: %s" % (b.describe(i), spec.DISPLAY, far))
    counts = {"local": 0, "kept": 0, "base": 0, "other": 0, "checked": 0}
    for slot in slots:
        kind = slot[0] if slot[0] in ("local", "kept") else "base" if slot[0] == spec.BASE[0] else "other"
        counts[kind] += 1
        counts["checked"] += kind == "other" and slot[0] in pool_ids
    audit.append("pool: %d slots: %d dedicated targets, %d kept actors of other bricks, %d %s NPCs (checked against "
                 "it), %d NPCs of other bricks (%d checked against their brick%s)" %
                 (len(slots), counts["local"], counts["kept"], counts["base"], gp_name(spec.BASE[0]), counts["other"],
                  counts["checked"],
                  "" if counts["checked"] == counts["other"] else "; the rest only from the in-game census"))
    b.validate()
    return b


def base_override_transform(spec, base, scene, eid):
    """The base brick's m_mTransform property override on (scene, eid), as a world matrix: exactly one."""
    k = base.scene_index(scene)
    hits = [p for p in base.temp["propertyOverrides"]
            if p["propertyOwner"]["externalSceneIndex"] == k and p["propertyOwner"]["entityID"] == eid and
            p["propertyValue"]["nPropertyID"] == "m_mTransform"]
    if len(hits) != 1:
        fail("%s has %d m_mTransform overrides on %016X:%016x, not 1" % (base.name, len(hits), scene, eid))
    return matrix(hits[0]["propertyValue"]["value"]["$val"])


def ref_target(b, own, r):
    """(scene hash, entity id) a reference of brick b (whose TEMP hash is own) names."""
    if r["externalSceneIndex"] >= 0:
        return b.scenes[r["externalSceneIndex"]], r["entityID"]
    return own, b.ids()[r["entityIndex"]]


def apply_property_overrides(spec, b, scene_brick, audit):
    """The spec's PROPERTY_OVERRIDES on our override b. scene_brick(hash): the read-only brick of that hash, or None
    when it is not read. The owner's brick must be read: its name, the property's type and old value are checked;
    the new value's name is checked when its brick is read."""
    for ov in getattr(spec, "PROPERTY_OVERRIDES", []):
        scene, eid, name = ov["owner"]
        prop, typ = ov["property"], ov["type"]
        src = scene_brick(scene)
        if src is None:
            fail("PROPERTY_OVERRIDES: %016X (%s) is not read: its %016x cannot be checked" % (scene, gp_name(scene), eid))
        try:
            i = src.index_of(eid)
            old = src.find_property(eid, prop)["value"]
        except be.BrickError as e:
            fail(str(e))
        if src.entity_name(i) != name:
            fail("%s %016x is \"%s\", not \"%s\"" % (src.name, eid, src.entity_name(i), name))
        if old["$type"] != typ:
            fail("%s %s.%s is a %s, not a %s" % (src.name, name, prop, old["$type"], typ))
        if typ == "SEntityTemplateReference":
            was = ref_target(src, scene, old["$val"])
            if was != ov["was"][:2]:
                fail("%s %s.%s names %016X:%016x, not %s: the game changed it" % (src.name, name, prop, was[0], was[1],
                                                                                 ov["was"][2]))
            if was[0] == scene and src.entity_name(src.index_of(was[1])) != ov["was"][2]:
                fail("%s %016x is not \"%s\"" % (src.name, was[1], ov["was"][2]))
            vscene, vid, vname = ov["value"]
            tgt = scene_brick(vscene)
            if tgt is None:
                note = " (%s not read: its name is not checked)" % gp_name(vscene)
            else:
                if tgt.entity_name(tgt.index_of(vid)) != vname:
                    fail("%s %016x is \"%s\", not \"%s\"" % (tgt.name, vid, tgt.entity_name(tgt.index_of(vid)), vname))
                note = ""
            value, shown = (vscene, vid), "%s's %s%s" % (gp_name(vscene), vname, note)
        else:
            if old["$val"] != ov["was"]:
                fail("%s %s.%s is %r, not %r: the game changed it" % (src.name, name, prop, old["$val"], ov["was"]))
            value, shown = ov["value"], repr(ov["value"])
        try:
            b.add_property_override((scene, eid), prop, typ, value)
        except be.BrickError as e:
            fail(str(e))
        audit.append("override: %s's %s.%s (was %s) -> %s: %s" % (gp_name(scene), src.describe(i), prop,
                                                                    ov["was"][2] if typ == "SEntityTemplateReference"
                                                                    else repr(ov["was"]), shown, ov["why"]))


def build_override(spec, ovb, base, reach, audit, spawn_bricks, deleted, blockers, sources, placement=None,
                   scene_brick=None):
    b = ovb
    vanilla_pin_deletes = copy.deepcopy(ovb.tblu["pinConnectionOverrideDeletes"])
    vanilla_scenes = list(ovb.scenes)
    if getattr(spec, "DROP_SCENES_FIRST", False):
        # The overrides into the bricks the swap does not load go first (they would go with the scene list below),
        # so the entities they name can be deleted.
        early = b.drop_overrides_into([h for h in b.scenes if h not in spec.OVERRIDE_SCENES])
        for h, n in sorted(early.items()):
            audit.append("override: %d deletes, property and pin overrides into %s go (it is not loaded)" %
                         (n, gp_name(h)))
    for eid, name in spec.DELETE_OVERRIDE:
        i = b.index_of(eid)
        if b.entity_name(i) != name:
            fail("override_bingo %016x is \"%s\", not \"%s\"" % (eid, b.entity_name(i), name))
        gone = b.delete([eid])
        audit.append("override: deleted %s and %d entities under it" % (name, len(gone) - 1))

    # Entities filed (in the entity tree) under an entity of a brick the swap does not load: under our root instead.
    for eid, why in getattr(spec, "OVERRIDE_REPARENT", []):
        old = b.set_logical_parent_root(eid)
        if old["externalSceneIndex"] < 0:
            fail("OVERRIDE_REPARENT %s is filed under a local entity" % b.describe(b.index_of(eid)))
        audit.append("override: %s filed under the brick root (was under %s %016x): %s" % (
            b.describe(b.index_of(eid)), gp_name(b.scenes[old["externalSceneIndex"]]), old["entityID"], why))
    apply_repoints(b, getattr(spec, "OVERRIDE_REPOINTS", []), audit, "override")
    for scene, eid, prop, why in getattr(spec, "REMOVE_PROPERTY_OVERRIDES", []):
        try:
            b.remove_property_override(scene, eid, prop)
        except be.BrickError as e:
            fail(str(e))
        audit.append("override: no longer overrides %s %016x %s: %s" % (gp_name(scene), eid, prop, why))
    if getattr(spec, "DROP_REFS", []):
        try:
            b.drop_temp_refs([h for h, _ in spec.DROP_REFS])
        except be.BrickError as e:
            fail(str(e))
        for h, why in spec.DROP_REFS:
            audit.append("override: no longer references %016X %s: %s" % (h, gp_name(h), why))

    # Randomizer slots.
    for slot, (old, new, why) in sorted(spec.SLOT_REPOINTS.items()):
        p = b.find_property(spec.TERRITORY, slot)["value"]["$val"]
        if p["externalSceneIndex"] >= 0 or b.tblu["subEntities"][p["entityIndex"]]["entityId"] != old:
            fail("%s does not name %016x: the game changed override_bingo" % (slot, old))
        p["entityIndex"] = b.index_of(new)
        audit.append("override: %s -> %s: %s" % (slot, b.describe(b.index_of(new)), why))

    # Spots moved into a set-only map (the full Icon).
    moved = apply_spot_moves(spec, b, base, placement, audit)

    # Every spot a slot can spawn must be reachable; a safe's clues and guard and a mule's acts too.
    w = World(b.temp)
    P = w.P
    pins = b.tblu["pinConnections"]
    terr = props(b.temp["subEntities"][b.index_of(spec.TERRITORY)])
    used = sorted({v["entityIndex"] for k, v in terr.items() if k.startswith("available")})
    for i in used:
        check = [i]
        d = P[i]
        for k, v in d.items():
            if k.startswith("Clue") and isinstance(v, dict) and be.is_ref(v) and v["externalSceneIndex"] < 0:
                check.append(v["entityIndex"])
            if k == "acts":
                check += [r["entityIndex"] for r in v]
        check += [p["toID"] for p in pins if p["fromID"] == i and p["fromPinName"] == "safeEnabled"]
        far, via = [], ""
        for j in sorted(set(check)):
            pos = w.pos(j)
            if pos is None:
                fail("%s has no world position" % b.describe(j))
            dist = reach(pos)
            eid = b.tblu["subEntities"][j]["entityId"]
            if dist > REACH_M and eid in spec.REACH_EXCEPTIONS:
                audit.append("override: %s is %.0f m away, accepted: %s" % (b.describe(j), dist,
                                                                            spec.REACH_EXCEPTIONS[eid]))
            elif reach.too_far(dist):
                far.append("%s (%.0f, %.0f, %.0f) %.0f m" % (b.describe(j), pos[0], pos[1], pos[2], dist))
            else:
                via = via or reach.where(pos)
            if getattr(spec, "SPOT_BLOCKER_CHECK", False):
                for name, m, size in blockers:
                    if any(inside_box((pos[0], pos[1], pos[2] + up), m, size, 0.25) for up in (0.1, 0.9)):
                        fail("the spot %s (%.0f, %.0f, %.0f) is inside the blocker %s" %
                             (b.describe(j), pos[0], pos[1], pos[2], name))
            raised = getattr(spec, "KEPT_RAISED", {}).get(eid)
            if eid not in moved and raised:
                note = kept_floor(placement, "the kept %s" % b.describe(j), pos, down=raised[0])
                if note:
                    audit.append("override: %s on high furniture: %s (accepted up to %.1f m below: %s)" %
                                 (b.describe(j), note, raised[0], raised[1]))
            elif eid not in moved:
                kept_floor(placement, "the kept %s" % b.describe(j), pos)
        if far:
            fail("a spot the randomizer can spawn is far from %s's acts: %s" % (spec.DISPLAY, "; ".join(far)))
        audit.append("override: spawnable %s and %d linked entities: reachable%s" % (b.describe(i), len(check) - 1,
                                                                                    via))

    # Exits: move onto the variant's exits.
    for eid, move in sorted(spec.EXIT_MOVES.items()):
        i = b.index_of(eid)
        par = P[i].get("m_eidParent")
        if not par or par["externalSceneIndex"] >= 0 or par["entityIndex"] != b.temp["rootEntityIndex"]:
            fail("%s does not hang off the brick root" % b.describe(i))
        kind, place_scene, place = move["place"]
        seq_scene, seq, start, end = move["new"]
        if kind == "entity":
            if place_scene != base_hash(base):
                fail("an exit's place must be an entity of %s" % base.name)
            pw = World(base.temp)
            mw_i = base.index_of(place)
            m = pw.world(mw_i)
            if m is None:
                fail("%s %s has no world position" % (base.name, base.describe(mw_i)))
        elif kind == "override":
            if (place_scene, place) in deleted:
                fail("the exit place %016X:%016x is deleted by a brick the swap loads" % (place_scene, place))
            m = base_override_transform(spec, base, place_scene, place)
            # The override is a local transform; its parent in location is not extracted. Another scene's own copy of
            # that exit, placed in world coordinates, must stand where it puts it (the parent is the identity).
            c_scene, c_eid = move["confirm"]
            if c_scene not in sources:
                fail("the exit place's confirming brick %016X is not a reach source" % c_scene)
            cb = sources[c_scene]
            cp = World(cb.temp).pos(cb.index_of(c_eid))
            off = math.dist(cp, (m[0][3], m[1][3], m[2][3]))
            if off > 1.0:
                fail("%s's override puts %016x at (%.1f, %.1f, %.1f), %.1f m from %s's %s: its parent is not the "
                     "identity" % (base.name, place, m[0][3], m[1][3], m[2][3], off, cb.name,
                                   cb.describe(cb.index_of(c_eid))))
            audit.append("override: exit place %016x at (%.0f, %.0f, %.0f): %.2f m from %s's %s" %
                         (place, m[0][3], m[1][3], m[2][3], off, cb.name, cb.describe(cb.index_of(c_eid))))
        else:
            fail("unknown exit place %r" % (move["place"],))
        b.set(eid, "m_mTransform", smatrix(m))
        for prop in move["drop"]:
            b.remove_property(eid, prop)
        if move["wired_by"] == "brick":
            if kind != "entity":
                fail("an exit wired in the base brick needs a place there")
            # The place's own exit is wired to the sequence in the base brick: Exit -> start, end -> Trigger.
            si = base.index_of(seq)
            if seq_scene != base_hash(base) or \
                    not any(p["fromID"] == mw_i and p["toID"] == si and p["toPinName"] == start for p in base.pins()) or \
                    not any(p["fromID"] == si and p["toID"] == mw_i and p["fromPinName"] == end for p in base.pins()):
                fail("%s's %s is not wired to %s.%s/%s" % (base.name, base.describe(mw_i), base.describe(si), start,
                                                           end))
            # Other pins of the place's exit that the button takes over (an event transmitter).
            for pin, (x_scene, x_eid), x_pin in move.get("extra", []):
                xi = base.index_of(x_eid)
                if x_scene != base_hash(base) or not any(p["fromID"] == mw_i and p["fromPinName"] == pin and
                                                         p["toID"] == xi and p["toPinName"] == x_pin
                                                         for p in base.pins()):
                    fail("%s's %s.%s is not wired to %s.%s" % (base.name, base.describe(mw_i), pin, base.describe(xi),
                                                               x_pin))
        elif move["wired_by"] == "vanilla":
            # override_bingo deletes the place's own wiring: the sequence's end -> the place's Trigger.
            if (seq_scene, seq) in deleted:
                fail("the exit sequence %016X:%016x is deleted by a brick the swap loads" % (seq_scene, seq))

            def is_ext(r, h, e):
                return r["externalSceneIndex"] >= 0 and vanilla_scenes[r["externalSceneIndex"]] == h and \
                    r["entityID"] == e
            if not any(is_ext(p["fromEntity"], seq_scene, seq) and p["fromPinName"] == end and
                       is_ext(p["toEntity"], place_scene, place) and p["toPinName"] == "Trigger"
                       for p in vanilla_pin_deletes):
                fail("override_bingo does not delete the wiring %016x.%s -> %016x.Trigger" % (seq, end, place))
        else:
            fail("unknown exit wiring %r" % move["wired_by"])
        audit.append("override: exit %s moved: %s" % (b.describe(i), move["why"]))
    root = b.temp["subEntities"][b.temp["rootEntityIndex"]]
    if any(p["nPropertyID"] == "m_mTransform" for p in root["propertyValues"]):
        fail("override_bingo's root has a transform; exit positions would be off")
    for eid in sorted(set(spec.EXIT_MOVES) | set(spec.UNMOVED_EXITS)):
        i = b.index_of(eid)
        pos = World(b.temp).pos(i)
        dist = reach(pos)
        audit.append("override: exit %s at (%.0f, %.0f, %.0f), %.1f m from %s's acts%s" %
                     (b.describe(i), pos[0], pos[1], pos[2], dist, spec.DISPLAY, reach.where(pos)))
        if dist > REACH_M and eid in spec.REACH_EXCEPTIONS:
            audit.append("override: exit %s accepted: %s" % (b.describe(i), spec.REACH_EXCEPTIONS[eid]))
        elif reach.too_far(dist):
            fail("exit %s at (%.0f, %.0f, %.0f) is %.1f m from %s's acts" % (b.describe(i), pos[0], pos[1], pos[2],
                                                                           dist, spec.DISPLAY))
        note = kept_floor(placement, "exit %s" % b.describe(i), pos, EXIT_FLOOR_M)
        if note:
            audit.append("override: exit %s: navmesh %s" % (b.describe(i), note))
    left = set(spec.EXIT_MOVES) | set(spec.UNMOVED_EXITS) | set(getattr(spec, "DELETE_EXITS", {}))
    buttons = {e["entityId"] for i, e in enumerate(b.tblu["subEntities"])
               if any(t in NAMES.get(b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1], "")
                      for t in ("exit_evergreen_pushbutton", "exit_evergreen_usekey"))}
    if buttons != left:
        fail("%s's exits %s are not the spec's %s" %
             (src_names(spec)[1], sorted("%016x" % x for x in buttons), sorted("%016x" % x for x in left)))
    # Deleted exits go before the scene list changes: their pin overrides and references may name a dropped scene.
    for eid, why in sorted(getattr(spec, "DELETE_EXITS", {}).items()):
        n = b.remove_pin_overrides_of(eid)
        gone = b.delete([eid], subtree=False)
        audit.append("override: deleted the exit %s (and its %d pin overrides): %s" % (
            ", ".join("%016x" % x for x in gone), n, why))

    dropped = b.set_external_scenes(list(spec.OVERRIDE_SCENES))
    for h, n in sorted(dropped.items()):
        audit.append("override: %d deletes and pin overrides into %s went with it" % (n, gp_name(h)))
    for eid, move in sorted(spec.EXIT_MOVES.items()):
        sc, seq, start, end = move["old"]
        b.remove_pin_override(eid, "Exit", (sc, seq), start)
        b.remove_pin_override((sc, seq), end, eid, "Trigger")
        sc, seq, start, end = move["new"]
        b.add_pin_override(eid, "Exit", (sc, seq), start)
        b.add_pin_override((sc, seq), end, eid, "Trigger")
        for pin, target, x_pin in move.get("extra", []):
            b.add_pin_override(eid, pin, target, x_pin)
    for mid, name in spec.DELETE_BASE_EXITS:
        i = base.index_of(mid)
        if base.entity_name(i) != name:
            fail("%s %016x is \"%s\", not \"%s\"" % (base.name, mid, base.entity_name(i), name))
        b.add_override_delete(base_hash(base), mid)
        audit.append("override: deletes %s's %s" % (gp_name(base_hash(base)), base.describe(i)))
    apply_property_overrides(spec, b, scene_brick or (lambda h: base if h == base_hash(base) else None), audit)
    for eid, move in sorted(spec.EXIT_MOVES.items()):
        kind, place_scene, place = move["place"]
        if kind == "override" and not any(
                b.scenes[r["externalSceneIndex"]] == place_scene and r["entityID"] == place
                for r in b.tblu["overrideDeletes"]):
            fail("our override does not delete %016X:%016x, the exit whose place the button takes" %
                 (place_scene, place))

    # Starting locations inside the closed area (check_hero_spawns checks the rest).
    for h, items in sorted(spec.DELETE_HERO_SPAWNS.items()):
        src = spawn_bricks[h]
        for eid, name, why in items:
            i = src.index_of(eid)
            if src.entity_name(i) != name:
                fail("%s %016x is \"%s\", not \"%s\"" % (src.name, eid, src.entity_name(i), name))
            n = b.remove_property_overrides(h, eid)
            b.add_override_delete(h, eid)
            audit.append("override: deletes %s's %s%s: %s" % (src.name, src.describe(i),
                                                                " (and its %d property overrides)" % n if n else "",
                                                                why))

    delete_scene_props(spec, b, base, scene_brick, audit)

    # What our copy still says about entities a loaded brick deletes (deletes, overrides: inert there).
    inert = sum(1 for owner, _, r in b.all_refs() if r["externalSceneIndex"] >= 0 and
                (b.scenes[r["externalSceneIndex"]], r["entityID"]) in deleted)
    if inert:
        audit.append("override: %d deletes, property and pin overrides name entities that %s's bricks delete "
                     "(inert there, as in the vanilla bricks)" % (inert, spec.DISPLAY))
    b.validate()
    return b


def delete_scene_props(spec, b, base, scene_brick, audit):
    """The spec's DELETE_SCENE_PROPS ({brick TEMP hash: [(entity id, name, why)]}): props of the variant's own bricks
    that stand where a Freelancer spot spawns (the clash audit, landslide_audit.py), deleted by our override brick. Each
    brick must be one of OVERRIDE_SCENES and read (--json). Refuses an entity that is not the one named, one that
    anything of the brick, of the base or of our override still names (other than a pin of the brick itself: a pin
    into a deleted entity goes with it), or one that another entity of the brick is placed on (m_eidParent) without
    being deleted too: that one would lose its place."""
    for h, items in sorted(getattr(spec, "DELETE_SCENE_PROPS", {}).items()):
        if h not in spec.OVERRIDE_SCENES:
            fail("DELETE_SCENE_PROPS: %s is not one of the override brick's scenes (OVERRIDE_SCENES)" % gp_name(h))
        src = scene_brick(h) if scene_brick else None
        if src is None:
            fail("DELETE_SCENE_PROPS: %s (%016X) is not in --json: its deletes cannot be checked" % (gp_name(h), h))
        gone = {}
        for eid, name, why in items:
            try:
                i = src.index_of(eid)
            except be.BrickError as e:
                fail("DELETE_SCENE_PROPS: %s" % e)
            if src.entity_name(i) != name:
                fail("%s %016x is \"%s\", not \"%s\"" % (src.name, eid, src.entity_name(i), name))
            gone[i] = eid
        ids = set(gone.values())
        for j, e in enumerate(src.temp["subEntities"]):
            par = props(e).get("m_eidParent")
            if par and par["externalSceneIndex"] < 0 and par["entityIndex"] in gone and j not in gone:
                fail("DELETE_SCENE_PROPS: %s stands on %s's %s, which is deleted; delete it too" %
                     (src.describe(j), src.name, src.describe(par["entityIndex"])))
        gone_owners = {src.describe(k) for k in gone}
        for owner, where, r in src.all_refs():
            if r["externalSceneIndex"] < 0 and r["entityIndex"] in gone and owner not in gone_owners:
                fail("DELETE_SCENE_PROPS: %s %s %s names %s, which is deleted" % (src.name, owner, where,
                                                                                  src.describe(r["entityIndex"])))
        for other in (base, b):
            for owner, where, r in other.all_refs():
                if r["externalSceneIndex"] >= 0 and other.scenes[r["externalSceneIndex"]] == h and \
                        r["entityID"] in ids and owner != "overrideDeletes":
                    fail("DELETE_SCENE_PROPS: %s %s %s names %s %016x, which is deleted" % (
                        other.name, owner, where, src.name, r["entityID"]))
        for i, eid in sorted(gone.items()):
            b.add_override_delete(h, eid)
            why = [w for x, _, w in items if x == eid][0]
            audit.append("override: deletes %s's %s: %s" % (src.name, src.describe(i), why))


def base_hash(base):
    return base.temp_hash


def scene_closure(newest, names, start):
    """TEMP hashes of every brick and entity that loads with the start ones, through the rpkg reference tables (a
    brick's external scenes are its TEMP references named *.brick / *.entity)."""
    seen, todo = set(), list(start)
    while todo:
        h = todo.pop()
        if h in seen:
            continue
        if h not in newest:
            fail("%016X (%s), loaded by the swap, is in no mounted package" % (h, gp_name(h)))
        seen.add(h)
        for _, r in newest[h]["refs"]:
            n = names.get(r, "")
            if newest.get(r, {}).get("type") == "TEMP" and \
                    (n.endswith(".brick].pc_entitytype") or n.endswith(".entity].pc_entitytype")):
                todo.append(r)
    return seen


def blocker_boxes(b, names, kind, world_folders, audit):
    """The blockers of a brick: (description, world matrix, full size). kind "boxes": collision boxes and pathfinder
    boxes, each with a world position; "solid": collision boxes and pathfinder obstacles, skipping (and logging) one
    hung off another brick's entity."""
    w = World(b.temp, world_folders)
    out = []
    skipped = []
    for i, e in enumerate(b.temp["subEntities"]):
        d = w.P[i]
        t = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")
        if "collisionbox.entitytemplate" in t and "m_vBoxSize" in d:
            size = d["m_vBoxSize"]
        elif kind == "boxes" and str(d.get("m_eType", "")).startswith("PFBT_") and "m_vGlobalSize" in d:
            size = d["m_vGlobalSize"]
        elif kind == "solid" and "zpfobstacleentity" in t and "m_vGlobalSize" in d:
            size = d["m_vGlobalSize"]
        else:
            continue
        m = w.world(i)
        if m is None:
            if kind == "solid":
                skipped.append(b.describe(i))
                continue
            fail("%s's blocker %s has no world position" % (b.name, b.describe(i)))
        out.append(("%s %s" % (b.name, b.describe(i)), m, (size["x"], size["y"], size["z"])))
    if skipped:
        audit.append("blockers: %s: %d hung off another brick's entity, not placed: %s" %
                     (b.name, len(skipped), ", ".join(skipped)))
    return out


def inside_box(p, m, size, margin):
    v = [p[k] - m[k][3] for k in range(3)]
    return all(abs(sum(m[r][c] * v[r] for r in range(3))) <= size[c] / 2 + margin for c in range(3))


def crowd_reach(spec, args, newest, base, names, audit):
    """The spec's REACH_CROWD (name, {key: TEMP hash}), optional reads: when every brick's TEMP and TBLU JSON are
    there, (name, points) of their acts that the loaded bricks (the base and the blocker bricks) do not delete,
    themselves or through a parent; otherwise None and an audit note. The kept scene actors' names (the spec's
    KEEP_SCENE_NAMES) are checked against those bricks' npcactors too."""
    name, bricks = spec.REACH_CROWD
    missing, read = [], []
    for key, h in sorted(bricks.items()):
        tb = [r for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "TBLU"] if h in newest else []
        if h not in newest or len(tb) != 1 or find_json(args.json, h, "TEMP") is None or \
                find_json(args.json, tb[0], "TBLU") is None:
            missing.append(key)
        else:
            read.append((key, h))
    if missing:
        audit.append("reach: NOTE: %s not read (%d of %d bricks not extracted: %s): spots are judged by the other "
                     "sources' acts only, and the kept scene actors' names are not checked" %
                     (name, len(missing), len(bricks), ", ".join(missing)))
        return None
    deleted = deleted_by([base] + [json_brick(args, newest, pair[0], "has blockers")
                                   for pair, _ in spec.BLOCKER_BRICKS])
    points, counts = [], []
    keep = {}
    for br, eid in spec.KEEP_SCENE_TARGETS:
        keep.setdefault(pool_brick(spec, br), []).append(eid)
    expect = getattr(spec, "KEEP_SCENE_NAMES", {})
    if set(expect) != {eid for _, eid in spec.KEEP_SCENE_TARGETS}:
        fail("KEEP_SCENE_NAMES does not name exactly the kept scene actors")
    for key, h in read:
        b = json_brick(args, newest, h, "is a crowd brick")
        gone = deleted_indexes(b, h, deleted)
        pts = act_points(b.temp, b.temp_refs, names, skip=gone)
        every = act_points(b.temp, b.temp_refs, names)
        points += pts
        counts.append("%s %d/%d" % (key, len(pts), len(every)))
        for eid in keep.get(h, []):
            try:
                i = b.index_of(eid)
            except be.BrickError:
                fail("kept scene actor %016x is not in %s" % (eid, b.name))
            e = b.temp["subEntities"][i]
            if "npcactor" not in names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "").lower():
                fail("kept scene actor %s is not an npcactor" % b.describe(i))
            actor = props(e).get("m_sActorName")
            if actor != expect[eid]:
                fail("kept scene actor %s is \"%s\", not \"%s\"" % (b.describe(i), actor, expect[eid]))
            if i in gone:
                fail("kept scene actor %s is deleted by a brick the swap loads" % b.describe(i))
            audit.append("pool: kept scene actor %s \"%s\" (%s): an npcactor, not deleted" % (b.describe(i), actor,
                                                                                               key))
    audit.append("reach: %s: %d acts the swap keeps (of %d), per brick kept/all: %s" % (
        name, len(points), sum(int(c.split("/")[1]) for c in counts), ", ".join(counts)))
    return (name, points)


def json_brick(args, newest, h, what):
    tb = [r for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "TBLU"]
    t = find_json(args.json, h, "TEMP")
    u = find_json(args.json, tb[0], "TBLU") if len(tb) == 1 else None
    if t is None or u is None:
        fail("%016X (%s) %s, but it is not extracted: extract its TEMP and TBLU (%s) into --raw" %
             (h, gp_name(h), what, ", ".join("%016X" % x for x in tb)))
    return be.Brick(gp_name(h), t, u, newest[h]["refs"], newest[tb[0]]["refs"], editable=False)


def check_hero_spawns(spec, args, newest, names, reach, mission, override, own, blockers, base_deleted, audit,
                      placement=None, base=None, parents=None):
    """Freelancer's starting locations under the swap: every hero spawn of every brick the swapped scene loads (the
    spec's Starting locations). Each one left must be near the variant's acts and outside its blockers, and the spec's
    default must be the one default (the base brick's m_bDefaultHeroSpawn overrides count); each one our override
    deletes must be unreachable. A start that hangs off another brick's entity is placed through parents (Parents:
    the spec's PARENT_BRICKS); one whose parent brick is not read is not checked. Returns those: [description]."""
    default_set = {}
    if base is not None:
        for p in base.temp["propertyOverrides"]:
            o = p["propertyOwner"]
            if o["externalSceneIndex"] >= 0 and p["propertyValue"]["nPropertyID"] == "m_bDefaultHeroSpawn":
                default_set[(base.scenes[o["externalSceneIndex"]], o["entityID"])] = p["propertyValue"]["value"]["$val"]
    unplaced = []
    if names.get(spec.HERO_SPAWN_TYPE, "").find("/herospawn.entitytemplate].pc_entitytype") < 0:
        fail("%016X is not herospawn.entitytemplate in the name list" % spec.HERO_SPAWN_TYPE)
    for b in (mission, override):
        if any(r == spec.HERO_SPAWN_TYPE for _, r in b.temp_refs):
            fail("%s has its own hero spawns, which the spec does not expect" % b.name)
    closure = scene_closure(newest, names, [spec.SCENE] +
                            [h for h in mission.scenes + override.scenes if h not in own])
    for h in spec.HERO_SPAWN_REQUIRED:
        if h not in closure:
            fail("%016X (%s) is not among the %d bricks the swap loads: the name list (--names) lacks brick names, or "
                 "the game changed" % (h, gp_name(h), len(closure)))
    for h in spec.HERO_SPAWN_FORBIDDEN:
        if h in closure:
            fail("%s loads under the swap: its starts in the closed mansion are back" % gp_name(h))
    audit.append("hero spawns: the swap loads %d bricks%s" % (len(closure), "; %s %s not among them" % (
        ", ".join(gp_name(h).rsplit(".", 1)[0] for h in spec.HERO_SPAWN_FORBIDDEN),
        "is" if len(spec.HERO_SPAWN_FORBIDDEN) == 1 else "are") if spec.HERO_SPAWN_FORBIDDEN else ""))

    deleted = {(override.scenes[r["externalSceneIndex"]], r["entityID"]) for r in override.tblu["overrideDeletes"]}
    defaults, kept = [], 0
    for h in sorted(closure):
        if not any(r == spec.HERO_SPAWN_TYPE for _, r in newest[h]["refs"]):
            continue
        b = json_brick(args, newest, h, "holds hero spawns")
        w = World(b.temp)
        for i, e in enumerate(b.temp["subEntities"]):
            if b.temp_refs[e["entityTypeResourceIndex"]][1] != spec.HERO_SPAWN_TYPE:
                continue
            eid = b.tblu["subEntities"][i]["entityId"]
            pos = w.pos(i)
            if pos is None and parents is not None and (h, eid) not in deleted and (h, eid) not in base_deleted:
                pos, missing = parents.pos(b, i)
                if pos is None and missing:
                    unplaced.append("%s %s" % (b.name, b.describe(i)))
                    print("build_bricks: %s: WARNING: the start %s %s was not checked: it hangs off an entity of %s, "
                          "which is not in --json (the second Deck read)" % (spec.NAME, b.name, b.describe(i),
                                                                             missing), file=sys.stderr)
                    audit.append("hero spawns: %s %s: NOT CHECKED (%s not read)" % (b.name, b.describe(i), missing))
                    continue
            if pos is None and ((h, eid) in deleted or (h, eid) in base_deleted):
                audit.append("hero spawns: %s %s: deleted (no place of its own)" % (b.name, b.describe(i)))
                continue
            if pos is None:
                fail("hero spawn %s %s has no world position" % (b.name, b.describe(i)))
            dist = reach(pos)
            where = "%s %s at (%.0f, %.0f, %.0f), %.0f m from %s's acts%s" % (
                b.name, b.describe(i), pos[0], pos[1], pos[2], dist, spec.DISPLAY, reach.where(pos))
            if (h, eid) in deleted:
                if dist <= REACH_M:
                    # Near the acts but off the navmesh (an upstairs room the map does not reach) may go too.
                    off_mesh = None
                    if placement is not None and placement.checked:
                        try:
                            placement.check_kept("the start", pos, KEPT_FLOOR_M)
                        except pl.PlacementError as e:
                            off_mesh = str(e)
                    if off_mesh is None:
                        fail("our override deletes the reachable start %s" % where)
                    where += " (navmesh: %s)" % off_mesh.split(": ", 1)[-1]
                audit.append("hero spawns: %s: deleted" % where)
                continue
            if (h, eid) in base_deleted:
                audit.append("hero spawns: %s: deleted by %s" % (where, spec.DISPLAY))
                continue
            if reach.too_far(dist) and eid not in spec.HERO_SPAWN_EXCEPTIONS:
                fail("Freelancer can start 47 at %s, far from %s's acts (inside the closed area): delete it "
                     "(DELETE_HERO_SPAWNS) or, if it is reachable after all, list it in HERO_SPAWN_EXCEPTIONS" %
                     (where, spec.DISPLAY))
            for name, m, size in blockers:
                if any(inside_box((pos[0], pos[1], pos[2] + up), m, size, 0.5) for up in (0.1, 0.9)):
                    fail("Freelancer can start 47 at %s, inside %s's blocker %s" % (where, spec.DISPLAY, name))
            note = kept_floor(placement, "the start %s" % where, pos)
            if note:
                where += " (navmesh %s)" % note
            if default_set.get((h, eid), props(e).get("m_bDefaultHeroSpawn")) is True:
                defaults.append(eid)
            kept += 1
            audit.append("hero spawns: %s: %s" % (where, "accepted: " + spec.HERO_SPAWN_EXCEPTIONS[eid]
                                                   if dist > REACH_M and eid in spec.HERO_SPAWN_EXCEPTIONS else
                                                   "on the navmesh, outside every blocker (reach NOT CHECKED)"
                                                   if dist > REACH_M else "reachable, outside every blocker"))
    if defaults != [spec.DEFAULT_HERO_SPAWN]:
        fail("the swapped scene's default hero spawns are %s, not just %016x" %
             (["%016x" % x for x in defaults], spec.DEFAULT_HERO_SPAWN))
    missing = [(h, eid) for h, items in spec.DELETE_HERO_SPAWNS.items() for eid, _, _ in items
               if (h, eid) not in deleted]
    if missing:
        fail("DELETE_HERO_SPAWNS not applied: %s" % missing)
    audit.append("hero spawns: %d left, checked against %d %s blockers; default %016x%s" %
                 (kept, len(blockers), spec.DISPLAY, spec.DEFAULT_HERO_SPAWN,
                  "; %d NOT CHECKED (parent brick not read)" % len(unplaced) if unplaced else ""))
    return unplaced


class Parents:
    """World positions of entities that hang off another brick's entity (m_eidParent into an external scene), for
    the bricks the spec's PARENT_BRICKS names [(name, (TEMP, TBLU))] whose JSON is in --json: each read once, on first
    use."""

    def __init__(self, spec, args, newest):
        self.spec, self.args, self.newest = spec, args, newest
        self.known = {pair[0]: (name, pair) for name, pair in getattr(spec, "PARENT_BRICKS", [])}
        self.bricks = {}
        self.worlds = {}

    def _brick(self, h):
        if h not in self.bricks:
            name, pair = self.known[h]
            t, u = find_json(self.args.json, pair[0], "TEMP"), find_json(self.args.json, pair[1], "TBLU")
            if t is None or u is None:
                self.bricks[h] = None
            else:
                b = be.Brick(name, t, u, self.newest[pair[0]]["refs"], self.newest[pair[1]]["refs"], editable=False)
                self.bricks[h] = (b, World(b.temp), {e: i for i, e in enumerate(b.ids())})
        return self.bricks[h]

    def world(self, b, i, depth=0):
        """(world matrix or None, the name of a parent brick that is not read, or None)."""
        if depth > 16:
            return None, None
        w = World(b.temp) if id(b) not in self.worlds else self.worlds[id(b)]
        self.worlds[id(b)] = w
        m = w.world(i)
        if m is not None:
            return m, None
        # Walk up the local chain to the entity whose parent is in another brick.
        chain, j = [], i
        while True:
            d = w.P[j]
            chain.append(j)
            par = d.get("m_eidParent")
            if par is None or (par["externalSceneIndex"] < 0 and par["entityIndex"] < 0):
                return None, None
            if par["externalSceneIndex"] >= 0:
                break
            j = par["entityIndex"]
            if len(chain) > 64:
                return None, None
        h = b.scenes[par["externalSceneIndex"]]
        if h not in self.known:
            return None, None
        hit = self._brick(h)
        if hit is None:
            return None, self.known[h][0]
        pb, pw, ids = hit
        if par["entityID"] not in ids:
            return None, None
        pm, missing = self.world(pb, ids[par["entityID"]], depth + 1)
        if pm is None:
            return None, missing
        for k in reversed(chain):
            d = w.P[k]
            pm = mul(pm, matrix(d["m_mTransform"]) if "m_mTransform" in d else IDENT)
        return pm, None

    def pos(self, b, i):
        m, missing = self.world(b, i)
        return (None if m is None else (m[0][3], m[1][3], m[2][3])), missing


def story_switch(spec, args, newest, base, audit):
    """The spec's STORY_SWITCH (IOI's Contracts-mode brick, loaded by our override): it must be one of OVERRIDE_SCENES,
    delete exactly the spec's entities of the base brick (by name) and nothing else, and override no property of the
    base or our sources' bricks. Returns {(base hash, entity id)} it deletes, everything logically under them
    included (for the pool check)."""
    pair, expect = spec.STORY_SWITCH
    if pair[0] not in spec.OVERRIDE_SCENES:
        fail("%s: the story switch %016X is not one of OVERRIDE_SCENES (our override must load it)" % (spec.NAME,
                                                                                                     pair[0]))
    t, u = need_json(args.json, pair[0], "TEMP", "the story switch"), need_json(args.json, pair[1], "TBLU",
                                                                                "the story switch")
    sw = be.Brick(gp_name(pair[0]), t, u, newest[pair[0]]["refs"], newest[pair[1]]["refs"], editable=False)
    if sw.blueprint() != pair[1]:
        fail("%s's blueprint is %016X, not %016X" % (sw.name, sw.blueprint(), pair[1]))
    got = {}
    for r in sw.tblu["overrideDeletes"]:
        got[(sw.scenes[r["externalSceneIndex"]], r["entityID"])] = True
    want = {(base_hash(base), eid) for eid in expect}
    if set(got) != want:
        fail("%s deletes %s, not the spec's %s: the game changed it" % (
            sw.name, sorted("%016X:%016x" % k for k in got), sorted("%016X:%016x" % k for k in want)))
    out = set()
    for eid, name in sorted(expect.items()):
        i = base.index_of(eid)
        if base.entity_name(i) != name:
            fail("%s %016x is \"%s\", not \"%s\"" % (base.name, eid, base.entity_name(i), name))
        under = base.subtree(i)
        out |= {(base_hash(base), base.ids()[j]) for j in under}
        audit.append("story: %s deletes %s's %s and the %d entities under it" % (sw.name, base.name, base.describe(i),
                                                                                 len(under) - 1))
    for p in sw.temp["propertyOverrides"]:
        o = p["propertyOwner"]
        fail("%s overrides %s of %s: the spec does not expect it" % (
            sw.name, p["propertyValue"]["nPropertyID"],
            "a local entity" if o["externalSceneIndex"] < 0 else gp_name(sw.scenes[o["externalSceneIndex"]])))
    loads = [gp_name(h) for h in sw.scenes]
    audit.append("story: %s also loads %s; it adds %s" % (sw.name, ", ".join(loads), ", ".join(
        sw.describe(i) for i in range(len(sw.temp["subEntities"])) if i != sw.temp["rootEntityIndex"] and
        sw.entity_name(i) not in ("Scene",))))
    return out


def check_kept_scene(spec, args, newest, names, deleted, audit):
    """The kept scene actors (KEEP_SCENE_TARGETS, names in KEEP_SCENE_NAMES) of a spec without REACH_CROWD: when
    their brick's JSON is there, each must be an npcactor of that name that no loaded brick deletes (itself or
    through a parent); otherwise only the direct deletes are checked (in build_mission) and the build warns. Returns
    "checked" or "not checked"."""
    expect = spec.KEEP_SCENE_NAMES
    if set(expect) != {eid for _, eid in spec.KEEP_SCENE_TARGETS}:
        fail("KEEP_SCENE_NAMES does not name exactly the kept scene actors")
    by_brick = {}
    for br, eid in spec.KEEP_SCENE_TARGETS:
        by_brick.setdefault(pool_brick(spec, br), []).append(eid)
    result = "checked"
    for h, eids in sorted(by_brick.items()):
        tb = [r for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "TBLU"] if h in newest else []
        t = find_json(args.json, h, "TEMP")
        u = find_json(args.json, tb[0], "TBLU") if len(tb) == 1 else None
        if t is None or u is None:
            result = "not checked"
            print("build_bricks: %s: WARNING: the kept pool actors of %s (%d) were not checked against it (its TEMP "
                  "and TBLU are not in --json): only that no loaded brick deletes them directly. Extract it on the "
                  "Deck (the second read) and build again before shipping." % (spec.NAME, gp_name(h), len(eids)),
                  file=sys.stderr)
            audit.append("pool: kept actors of %s: NOT CHECKED against it (not read)" % gp_name(h))
            continue
        b = be.Brick(gp_name(h), t, u, newest[h]["refs"], newest[tb[0]]["refs"], editable=False)
        gone = deleted_indexes(b, h, deleted)
        for eid in eids:
            try:
                i = b.index_of(eid)
            except be.BrickError:
                fail("kept scene actor %016x is not in %s" % (eid, b.name))
            e = b.temp["subEntities"][i]
            if "npcactor" not in names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "").lower():
                fail("kept scene actor %s is not an npcactor" % b.describe(i))
            actor = props(e).get("m_sActorName")
            if actor != expect[eid]:
                fail("kept scene actor %s is \"%s\", not \"%s\"" % (b.describe(i), actor, expect[eid]))
            if i in gone:
                fail("kept scene actor %s is deleted by a brick the swap loads" % b.describe(i))
            if props(e).get("m_bStartEnabled") is False:
                fail("kept scene actor %s is start-disabled" % b.describe(i))
            audit.append("pool: kept scene actor %s \"%s\": an npcactor of %s, not deleted" % (b.describe(i), actor,
                                                                                              b.name))
    return result


def keycard_doors(spec, args, newest, override, parents, audit):
    """The spec's KEYCARD_DOORS: the Freelancer spots of our override within KEYCARD_NEAR_M (plan) of one, on its
    floor, listed in the audit (which side of the door a spot is on is an in-game check). Returns "" or, when a
    door's place needs a brick that is not read, a note."""
    spots = spot_points(override)
    missing = []
    for h, eid, desc in spec.KEYCARD_DOORS:
        b = json_brick(args, newest, h, "holds a keycard door")
        i = b.index_of(eid)
        p = World(b.temp).pos(i)
        if p is None:
            p, miss = parents.pos(b, i)
            if p is None:
                missing.append(desc)
                print("build_bricks: %s: WARNING: the keycard door %s was not placed (%s is not in --json): the spots "
                      "near it are not listed" % (spec.NAME, desc, miss or "its parent"), file=sys.stderr)
                continue
        near = [(math.hypot(p[0] - q[0], p[1] - q[1]), sd, q) for sd, q in spots
                if abs(p[2] - q[2]) < 2.5 and math.hypot(p[0] - q[0], p[1] - q[1]) < spec.KEYCARD_NEAR_M]
        audit.append("keycard door %s at (%.1f, %.1f, %.1f): %s" % (desc, p[0], p[1], p[2], "; ".join(
            "%s (%.0f, %.0f, %.0f) %.1f m" % (sd, q[0], q[1], q[2], d) for d, sd, q in sorted(near)) or
            "no Freelancer spot within %.0f m" % spec.KEYCARD_NEAR_M))
    return "not placed: %s" % ", ".join(missing) if missing else ""


def compare_navmesh(spec, args, newest, override, audit):
    """With the spec's COMPARE_NAVMESH (the normal level's) in --navp: which of our override's spots and exits stand
    on its walkable floor too (information for the in-game checks). Returns "checked" or "not checked"."""
    h, name = spec.COMPARE_NAVMESH
    path = find_navp(navp_dirs(args), h)
    if path is None:
        print("build_bricks: %s: WARNING: the comparison with the normal level's navmesh did not run: %016X.NAVP "
              "(%s) is not in --navp (the second Deck read). It is information only; every place is still checked "
              "on the outbreak's navmesh." % (spec.NAME, h, name), file=sys.stderr)
        audit.append("navmesh compare: NOT RUN (%s not read)" % name)
        return "not checked"
    with open(path, "rb") as f:
        data = f.read()
    if h in newest and len(data) != newest[h]["final"]:
        fail("%s is %d bytes; %s says %d (extract the newest copy)" % (path, len(data), newest[h]["package"],
                                                                     newest[h]["final"]))
    mesh = navp.NavMesh(data)
    both, only = 0, []
    for desc, q in spot_points(override):
        if mesh.snap(q, min_clear=0.0, radius=KEPT_FLOOR_M, up=pl.SNAP_DZ, down=pl.SNAP_DZ) is not None:
            both += 1
        else:
            only.append("%s (%.0f, %.0f, %.0f)" % (desc, q[0], q[1], q[2]))
    audit.append("navmesh compare: %d spots and exits on both navmeshes; %d only on the outbreak's%s" % (
        both, len(only), ": " + "; ".join(only) if only else ""))
    return "checked"


def find_navp(dirs, h):
    hits = []
    for d in dirs or []:
        hits += glob.glob(os.path.join(d, "*%016X.NAVP" % h))
    return sorted(hits)[0] if hits else None


def navp_dirs(args):
    """The folders a navmesh is looked for in: --navp (every --raw of scripts/build-landslide-pkg.sh), then --bin."""
    return list(args.navp or []) + list(args.bin or [])


def read_navmesh(spec, newest, dirs, h, name, why):
    """A navmesh the build cannot do without (why: what needs it), the newest copy's size; else the build stops."""
    if h not in newest or newest[h]["type"] != "NAVP":
        fail("%s's navmesh %016X (%s) is not a NAVP in a mounted package" % (spec.NAME, h, name))
    path = find_navp(dirs, h)
    if path is None:
        fail("%s: %s needs %016X.NAVP (%s, %d bytes in %s) in a --navp or --bin folder (%s); extract it "
             "(landslide_audit.py --missing; build-landslide/audit/sapienza holds Sapienza's) and pass that folder" %
             (spec.NAME, why, h, name, newest[h]["final"], newest[h]["package"], ", ".join(dirs) or "none given"))
    with open(path, "rb") as f:
        data = f.read()
    if len(data) != newest[h]["final"]:
        fail("%s is %d bytes; %s says %d (extract the newest copy)" % (path, len(data), newest[h]["package"],
                                                                     newest[h]["final"]))
    try:
        return navp.NavMesh(data)
    except (ValueError, struct.error) as e:
        fail("%s: not readable as a navmesh (navp.py): %s" % (path, e))


def crate_meshes(spec, args, newest, placement):
    """(variant's navmesh, Freelancer's) for check_crates: spec.CRATE_NAVMESH (else spec.NAVMESH, the placement's
    when it was read) and spec.FREELANCER_NAVMESH, both required; None for a spec without FREELANCER_NAVMESH."""
    if not getattr(spec, "FREELANCER_NAVMESH", None):
        return None
    dirs = navp_dirs(args)
    vh, vname = getattr(spec, "CRATE_NAVMESH", None) or spec.NAVMESH
    if placement is not None and placement.checked and getattr(spec, "NAVMESH", (None,))[0] == vh:
        vmesh = placement.mesh
    else:
        vmesh = read_navmesh(spec, newest, dirs, vh, vname, "the crate check")
    fh, fname = spec.FREELANCER_NAVMESH
    return vmesh, read_navmesh(spec, newest, dirs, fh, fname, "the crate check (Freelancer's navmesh)")


def check_crates(spec, b, meshes, audit):
    """Every supply crate a territory slot of our override b names must open toward open floor on the variant's
    navmesh (meshes: crate_meshes'; None: no check), as landslide_audit.crate_faces asks after the build: a moved one
    (SPOT_MOVES) placement.crate_problem passes, standing level; one that stays where Freelancer has it, at least
    placement.CRATE_FRONT_M of navmesh in front of its open side, unless Freelancer's own navmesh has less there too
    (the navmesh cut round it: vanilla Freelancer plays with it). A variant's dressing can hem in a crate it never
    moves (The Author's smoke machine against InsideRadioTowerFence's, Patient Zero's morgue table against
    Spawnable_15's): the build stops on it."""
    if meshes is None:
        return
    vmesh, fmesh = meshes
    vname = (getattr(spec, "CRATE_NAVMESH", None) or spec.NAVMESH)[1]
    fname = spec.FREELANCER_NAVMESH[1]
    moves = getattr(spec, "SPOT_MOVES", {})
    w = World(b.temp)
    terr = props(b.temp["subEntities"][b.index_of(spec.TERRITORY)])
    slots = {}
    for k, v in sorted(terr.items()):
        if k.startswith("available") and be.is_ref(v) and v["externalSceneIndex"] < 0:
            slots.setdefault(v["entityIndex"], []).append(k)
    problems, n = [], 0
    for i, ks in sorted(slots.items()):
        if not type_leaf(b, i).startswith(KIND_TYPES["stash"]):
            continue
        m = w.world(i)
        if m is None:
            fail("%s has no world position" % b.describe(i))
        q = (m[0][3], m[1][3], m[2][3])
        yaw = pl.yaw_of((m[0][0], m[1][0], m[2][0]))
        tilt = math.degrees(math.acos(max(-1.0, min(1.0, m[2][2]))))
        desc = "%s (%s) at %s" % (b.describe(i), ", ".join(ks), pl.fmt(q))
        room = pl.crate_room(vmesh, q, yaw)
        n += 1
        if b.tblu["subEntities"][i]["entityId"] in moves:
            bad = pl.crate_problem(vmesh, q, yaw)
            if bad:
                best = pl.crate_yaw(vmesh, q, yaw)
                problems.append("%s (moved): %s; yaw=%.1f gives %.2f m" % (desc, bad, best[0], best[1]))
            if tilt > pl.CRATE_TILT_DEG:
                problems.append("%s (moved): tilted %.1f degrees (a yaw levels it)" % (desc, tilt))
            audit.append("crates: %s (moved) opens toward %.2f m of navmesh (yaw %.0f)" % (desc, room, yaw))
            continue
        before = pl.crate_room(fmesh, q, yaw)
        if room < pl.CRATE_FRONT_M and before >= pl.CRATE_FRONT_M:
            problems.append("%s: its open side (yaw %.0f) has %.2f m of %s's navmesh in front, %.2f m under "
                            "Freelancer's: %s's dressing hems it in; move it (SPOT_MOVES) or re-point its slots "
                            "(SLOT_REPOINTS)" % (desc, yaw, room, spec.DISPLAY, before, spec.DISPLAY))
            continue
        audit.append("crates: %s opens toward %.2f m of navmesh (yaw %.0f; %.2f m under Freelancer's%s)" % (
            desc, room, yaw, before, ", the navmesh cut round it as under Freelancer" if room < pl.CRATE_FRONT_M
            else ""))
    if problems:
        fail("%s: %d supply crate(s) open toward too little floor (placement.CRATE_FRONT_M %.1f m):\n  %s" % (
            spec.DISPLAY, len(problems), pl.CRATE_FRONT_M, "\n  ".join(problems)))
    audit.append("crates: %d supply crates checked on %s and Freelancer's %s" % (n, vname, fname))


def make_placement(spec, args, newest, base, reach, avoid=(), floor=()):
    """The spec's placement judge: with its navmesh when the NAVP is in --navp (it must be the newest copy's size),
    without (and a loud warning) otherwise. The playable island is that of the default start. avoid: more act
    positions (layer_acts) that moved entities keep clear of; floor: litter positions (floor_refs) for the floor
    height."""
    h, name = spec.NAVMESH
    if h not in newest or newest[h]["type"] != "NAVP":
        fail("%s's navmesh %016X (%s) is not a NAVP in a mounted package" % (spec.NAME, h, name))
    home = World(base.temp).pos(base.index_of(spec.DEFAULT_HERO_SPAWN))
    path = find_navp(navp_dirs(args), h)
    mesh = None
    if path is None:
        print("build_bricks: %s: WARNING: the navmesh check did not run: %016X.NAVP (%s, %d bytes in %s) is not in "
              "--navp %s; the moved spots keep the design's nominal places, checked only by their distance to %s's "
              "acts. Extract it on the Deck (scripts/build-landslide-pkg.sh --help) and build again before shipping." %
              (spec.NAME, h, name, newest[h]["final"], newest[h]["package"], navp_dirs(args) or "(none)", spec.DISPLAY),
              file=sys.stderr)
    else:
        with open(path, "rb") as f:
            data = f.read()
        if len(data) != newest[h]["final"]:
            fail("%s is %d bytes; %s says %d (extract the newest copy)" % (path, len(data), newest[h]["package"],
                                                                         newest[h]["final"]))
        try:
            mesh = navp.NavMesh(data)
        except (ValueError, struct.error) as e:
            fail("%s: not readable as a navmesh (navp.py): %s" % (path, e))
        print("build_bricks: %s: navmesh %s: %d areas, %d boundary edges, %d islands" %
              (spec.NAME, os.path.basename(path), len(mesh.areas), len(mesh.boundary), len(mesh.islands())))
    try:
        return pl.Placement(mesh, home, reach.points, avoid, floor)
    except pl.PlacementError as e:
        fail("%s: %s" % (spec.NAME, e))


def build_variant(spec, args, newest, every, names, out):
    """One variant's two bricks; returns its manifest entry (the JSON files are written by main)."""
    def refs(h, what):
        if h not in newest:
            fail("%s %016X is in no mounted package" % (what, h))
        return [(f, x) for f, x in newest[h]["refs"]]

    def brick(name, pair, editable=True):
        t = need_json(args.json, pair[0], "TEMP", name)
        u = need_json(args.json, pair[1], "TBLU", name)
        b = be.Brick(name, t, u, refs(pair[0], name), refs(pair[1], name + " blueprint"), editable)
        b.temp_hash = pair[0]
        if b.blueprint() != pair[1]:
            fail("%s's blueprint is %016X, not %016X" % (name, b.blueprint(), pair[1]))
        return b

    mmb = brick(src_names(spec)[0], src_mission(spec))
    ovb = brick(src_names(spec)[1], src_override(spec))
    sources = {}
    unread = []  # optional reach sources (the spec's OPTIONAL_REACH) not extracted
    for name, pair in spec.REACH_SOURCES:
        if pair[0] in getattr(spec, "OPTIONAL_REACH", ()) and \
                (find_json(args.json, pair[0], "TEMP") is None or find_json(args.json, pair[1], "TBLU") is None):
            unread.append(name)
            print("build_bricks: %s: WARNING: the reach check did not run: %s (%016X.TEMP, %016X.TBLU) is not in "
                  "--json; places are judged by the navmesh and blocker checks only. Extract it on the Deck ("
                  "scripts/build-landslide-pkg.sh --help: Patient Zero, the second read) and build again before shipping." %
                  (spec.NAME, name, pair[0], pair[1]), file=sys.stderr)
            continue
        sources[pair[0]] = brick(name, pair, editable=False)
    if spec.BASE[0] not in sources:
        sources[spec.BASE[0]] = brick(gp_name(spec.BASE[0]).rsplit(".", 1)[0], spec.BASE, editable=False)
    base = sources[spec.BASE[0]]
    reach_sources = [(name, sources[pair[0]].temp, sources[pair[0]].temp_refs) for name, pair in spec.REACH_SOURCES
                     if name not in unread]
    crowd_audit = []
    if getattr(spec, "REACH_CROWD", None):
        crowd = crowd_reach(spec, args, newest, base, names, crowd_audit)
        if crowd is not None:
            reach_sources.insert(1, crowd)
    reach = Reach(reach_sources, names, spec.REACH_MIN_ACTS, missing=unread)
    if unread:
        crowd_audit.append("reach: WARNING: not checked (%s not read): the distances below are to %s's acts only, "
                           "and no place fails on them" % (", ".join(unread), ", ".join(n for n, _, _ in reach_sources)))
    for name, pair in getattr(spec, "CONFIRM_SOURCES", []):
        sources[pair[0]] = brick(name, pair, editable=False)
    placement = None
    if getattr(spec, "NAVMESH", None):
        avoid, avoid_lines = layer_acts(spec, args, newest, names)
        crowd_audit += avoid_lines
        floor, floor_line = floor_refs(spec, args, newest, base, names)
        crowd_audit.append(floor_line)
        placement = make_placement(spec, args, newest, base, reach, avoid, floor)

    def npc_ids(b):
        out = {}
        for i, e in enumerate(b.temp["subEntities"]):
            if "npcactor" in names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "").lower():
                out[b.tblu["subEntities"][i]["entityId"]] = props(e).get("m_sActorName")
        return out

    base_ids = npc_ids(base)
    pool_ids = {}
    loaded = {spec.BASE[0]: base}  # read-only bricks the swap loads, for a layer's check (BUILT)
    for h in sorted({pool_brick(spec, a[0]) for a in spec.POOL_ADDITIONS} - {spec.BASE[0]}):
        if h not in newest:
            fail("pool brick %016X is in no mounted package" % h)
        t = find_json(args.json, h, "TEMP")
        if t is not None:
            tb = [r for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "TBLU"]
            u = find_json(args.json, tb[0], "TBLU") if len(tb) == 1 else None
            if u is not None:
                loaded[h] = be.Brick("%016X" % h, t, u, refs(h, "pool brick"), refs(tb[0], "pool brick blueprint"),
                                     editable=False)
                pool_ids[h] = npc_ids(loaded[h])

    rid = gp.rid
    hashes = {}
    for path, suffix, typ in ((spec.MISSION_PATH, "pc_entitytype", "TEMP"),
                              (spec.MISSION_PATH, "pc_entityblueprint", "TBLU"),
                              (spec.OVERRIDE_PATH, "pc_entitytype", "TEMP"),
                              (spec.OVERRIDE_PATH, "pc_entityblueprint", "TBLU")):
        h = rid("[%s].%s" % (path, suffix))
        if h in every or h in names:
            fail("%016X ([%s].%s) is already a known hash" % (h, path, suffix))
        hashes[(path, typ)] = h

    audit = list(crowd_audit)
    blockers = []
    folders = folder_matrices(json_brick(args, newest, spec.LOCATION, "holds the world folders"),
                              spec.LOCATION_WORLD_FOLDERS) if spec.LOCATION_WORLD_FOLDERS else {}
    for pair, kind in spec.BLOCKER_BRICKS:
        blockers += blocker_boxes(json_brick(args, newest, pair[0], "has blockers"), names, kind, folders, audit)
    # What the loaded vanilla bricks delete: the base and the blocker bricks (each loaded by the swapped scene).
    loaded_ro = [base] + [json_brick(args, newest, pair[0], "has blockers") for pair, _ in spec.BLOCKER_BRICKS]
    deleted = deleted_by(loaded_ro)
    checks = {}  # the manifest's notes on checks that did not run (a Deck read missing)
    if getattr(spec, "STORY_SWITCH", None):
        deleted |= story_switch(spec, args, newest, base, audit)
    if getattr(spec, "KEEP_SCENE_NAMES", None) and not getattr(spec, "REACH_CROWD", None):
        if check_kept_scene(spec, args, newest, names, deleted, audit) != "checked":
            checks["keptActors"] = "not checked"
    if unread:
        checks["reach"] = "not checked"

    mission = build_mission(spec, mmb, hashes[(spec.OVERRIDE_PATH, "TEMP")], base_ids, pool_ids, reach, audit,
                            deleted, placement)
    spawn_bricks = {spec.AGENCYMARKUP_LOCATION: brick("agencymarkup_location", (spec.AGENCYMARKUP_LOCATION,
                                                                            spec.AGENCYMARKUP_LOCATION_TBLU),
                                                      editable=False)}
    read_scenes = {}

    def scene_brick(h):
        """A loaded vanilla brick by TEMP hash (read-only), or None when its JSON is not in --json."""
        if h in sources:
            return sources[h]
        if h not in read_scenes:
            tb = [r for _, r in newest.get(h, {}).get("refs", []) if newest.get(r, {}).get("type") == "TBLU"]
            t = find_json(args.json, h, "TEMP")
            u = find_json(args.json, tb[0], "TBLU") if len(tb) == 1 else None
            read_scenes[h] = None if t is None or u is None else be.Brick(
                gp_name(h), t, u, newest[h]["refs"], newest[tb[0]]["refs"], editable=False)
        return read_scenes[h]

    override = build_override(spec, ovb, base, reach, audit, spawn_bricks, deleted, blockers, sources, placement,
                              scene_brick)
    check_crates(spec, override, crate_meshes(spec, args, newest, placement), audit)
    parents = Parents(spec, args, newest)
    unplaced = check_hero_spawns(spec, args, newest, names, reach, mission, override, set(hashes.values()), blockers,
                                 deleted_by([base]), audit, placement, base, parents)
    if unplaced:
        checks["heroSpawns"] = "not checked: %s" % ", ".join(unplaced)
    if getattr(spec, "KEYCARD_DOORS", None):
        doors = keycard_doors(spec, args, newest, override, parents, audit)
        if doors:
            checks["keycardDoors"] = doors
    if getattr(spec, "COMPARE_NAVMESH", None):
        if compare_navmesh(spec, args, newest, override, audit) != "checked":
            checks["navmeshCompare"] = "information only"
    check_subset_types(spec.NAME, [mission, override],
                       [brick(src_names(spec)[0], src_mission(spec), editable=False),
                        brick(src_names(spec)[1], src_override(spec), editable=False)] +
                       list(sources.values()) + list(loaded.values()) + list(spawn_bricks.values()) + loaded_ro,
                       args, audit)
    mission.set_blueprint(hashes[(spec.MISSION_PATH, "TBLU")])
    override.set_blueprint(hashes[(spec.OVERRIDE_PATH, "TBLU")])
    BUILT[spec.NAME] = {"spec": spec, "mission": mission, "override": override, "loaded": loaded,
                        "mission_rid": hashes[(spec.MISSION_PATH, "TEMP")],
                        "override_rid": hashes[(spec.OVERRIDE_PATH, "TEMP")]}

    if rid("[%s].pc_entitytemplate" % spec.SCENE_PATH) != spec.SCENE:
        fail("%s's scene path %s is not %016X" % (spec.NAME, spec.SCENE_PATH, spec.SCENE))
    entry = {"name": spec.NAME, "display": spec.DISPLAY, "scene": spec.SCENE_PATH, "sceneRid": "%016X" % spec.SCENE,
             "partition": list(partition(spec)), "swapBricks": [spec.MISSION_PATH],
             "fingerprints": [["%016X" % h, n] for h, n in spec.FINGERPRINTS], "resources": []}
    if checks:
        entry["checks"] = checks
    if getattr(spec, "STORY_SWITCH", None):
        entry["storySwitch"] = "%016X" % spec.STORY_SWITCH[0][0]
    if placement is not None:
        entry["navmesh"] = "checked" if placement.checked else "not checked"
        if spec.NAVMESH[0] not in {h for h, _ in spec.FINGERPRINTS}:
            fail("%s's navmesh %016X is not one of its fingerprints" % (spec.NAME, spec.NAVMESH[0]))
    for b, path, source in ((mission, spec.MISSION_PATH, src_mission(spec)),
                            (override, spec.OVERRIDE_PATH, src_override(spec))):
        for typ, js, table, src in (("TEMP", b.temp, b.temp_refs, source[0]), ("TBLU", b.tblu, b.tblu_refs, source[1])):
            h = hashes[(path, typ)]
            name = "%016X.%s.json" % (h, typ)
            with open(os.path.join(out, name + ".tmp"), "w") as f:
                json.dump(js, f, indent=1)
            os.replace(os.path.join(out, name + ".tmp"), os.path.join(out, name))
            entry["resources"].append({"path": path, "type": typ, "rid": "%016X" % h, "source": "%016X" % src,
                                       "json": name, "refs": [[f, "%016X" % r] for f, r in table],
                                       "entities": len(b.temp["subEntities"])})
    if args.audit:
        print("\n".join("%s %s" % (spec.NAME + ":", line) for line in audit))
    return entry


def spot_points(b):
    """(description, world position) of every Freelancer spot of a built override brick that the swap can spawn or
    use: safes, stashes, the supplier, mules and their acts, safe guards and clues (build_bricks.KIND_TYPES), and the
    exit buttons. Entities without a world position (parented into another brick) are left out."""
    w = World(b.temp)
    prefixes = tuple(KIND_TYPES.values())
    out = []
    for i in range(len(b.temp["subEntities"])):
        leaf = type_leaf(b, i)
        if not (leaf.startswith(prefixes) or "pushbutton" in leaf):
            continue
        p = w.pos(i)
        if p and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
            out.append((b.describe(i), p))
    return out


def layer_things(b, names):
    """A layer brick's acts and props with a world position: ([(kind "act" or "prop", description, position)],
    the number of others, parented into other bricks)."""
    w = World(b.temp)
    unplaced = 0
    things = []
    for i, e in enumerate(b.temp["subEntities"]):
        t = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "").lower()
        leaf = t.split("]")[0].rsplit("/", 1)[-1]
        kind = "act" if ("/acts" in t or leaf.startswith("act_")) else \
            "prop" if ("/props/" in t or "/vehicles/" in t) else None
        if kind is None:
            continue
        p = w.pos(i)
        if p is None:
            unplaced += 1
            continue
        things.append((kind, b.describe(i), p))
    return things, unplaced


def layer_acts(spec, args, newest, names):
    """The acts of the layers on spec (layer specs whose LAYER_OF is spec.NAME: their PLACEMENT_BRICKS, else
    LAYER_BRICKS) whose TEMP and TBLU JSON are in --json, for the placement to step past (placement.Placement's avoid),
    so a moved spot does not land on a layer NPC's act (check_layer's ACT_GAP_M rule). Optional: a brick not read
    adds nothing. Returns ([positions], [audit lines])."""
    acts, lines = [], []
    for ls in VARIANTS:
        if getattr(ls, "LAYER_OF", None) != spec.NAME:
            continue
        for path, temp, tblu in getattr(ls, "PLACEMENT_BRICKS", ls.LAYER_BRICKS):
            name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
            t, u = find_json(args.json, temp, "TEMP"), find_json(args.json, tblu, "TBLU")
            if t is None or u is None or temp not in newest or tblu not in newest:
                lines.append("spots: %s's %s is not read: its acts are not avoided" % (ls.DISPLAY, name))
                continue
            b = be.Brick(name, t, u, newest[temp]["refs"], newest[tblu]["refs"], editable=False)
            pts = [p for kind, _, p in layer_things(b, names)[0] if kind == "act"]
            acts += pts
            lines.append("spots: avoiding %d acts of %s's %s (%.1f m in plan)" % (len(pts), ls.DISPLAY, name,
                                                                                pl.ACT_GAP_M))
    return acts, lines


def check_layer(spec, layer, built, names, audit):
    """The layer check of a vanilla brick a layer spec appends (spec.LAYER_BRICKS) on the full variant built[...]:
    layer is that brick (read only), built the base variant's BUILT entry. Stops the build when the layer deletes or
    switches off one of the base mission brick's pool actors, deletes an entity either built brick points at, or puts
    one of its props (furniture, vehicles: within spec.PROP_GAP_M) or acts (placement.ACT_GAP_M) on a Freelancer spot
    or exit. Lists its NPCs (with the spec's NEVER_TARGETS found among them), exits, starts and overrides in the
    audit. Returns {npc entity id: actor name}."""
    base_spec, mission, override = built["spec"], built["mission"], built["override"]
    problems = []

    # What the layer deletes, with everything under a deleted entity in the read-only bricks we have.
    deleted = deleted_by([layer])
    for h, b in built["loaded"].items():
        ids = b.ids()
        deleted |= {(h, ids[i]) for i in deleted_indexes(b, h, deleted)}
    per_scene = {}
    for h, _ in deleted_by([layer]):
        per_scene[h] = per_scene.get(h, 0) + 1
    audit.append("layer: %s deletes %s" % (layer.name, ", ".join("%d in %s" % (n, gp_name(h)) for h, n in
                                                                  sorted(per_scene.items())) or "nothing"))

    pool = []
    for r in mission.find_property(base_spec.SPAWN_TARGETS, "m_aActors")["value"]["$val"]:
        if r["externalSceneIndex"] >= 0:
            pool.append((mission.scenes[r["externalSceneIndex"]], r["entityID"]))
    for key in pool:
        if key in deleted:
            problems.append("it deletes the pool actor %016X:%016x (a slot of %s's target pool)" %
                            (key[0], key[1], gp_name(built["mission_rid"])))
    pool_set = set(pool)
    for b in (mission, override):
        seen = set()
        for owner, where, r in b.all_refs():
            if r["externalSceneIndex"] < 0 or owner == "overrideDeletes":
                continue
            key = (b.scenes[r["externalSceneIndex"]], r["entityID"])
            if key in deleted and key not in pool_set and key not in seen:
                seen.add(key)
                problems.append("it deletes %016X:%016x, which %s's %s %s names" % (key[0], key[1], b.name, owner,
                                                                                    where))

    # Its property overrides: on a pool actor, only noted, unless it switches the actor off.
    overridden = {}
    for p in layer.temp["propertyOverrides"]:
        o = p["propertyOwner"]
        if o["externalSceneIndex"] < 0:
            continue
        key = (layer.scenes[o["externalSceneIndex"]], o["entityID"])
        overridden[key[0]] = overridden.get(key[0], 0) + 1
        prop, value = p["propertyValue"]["nPropertyID"], p["propertyValue"]["value"]["$val"]
        if key in pool_set:
            if prop == "m_bStartEnabled" and value is False:
                problems.append("it switches the pool actor %016X:%016x off (m_bStartEnabled false)" % key)
            else:
                audit.append("layer: %s sets %s of the pool actor %016X:%016x" % (layer.name, prop, key[0], key[1]))
    audit.append("layer: %s overrides %s" % (layer.name, ", ".join("%d properties in %s" % (n, gp_name(h)) for h, n in
                                                                    sorted(overridden.items())) or "nothing"))

    # Its acts and props against the Freelancer spots and exits.
    spots = spot_points(override)
    things, unplaced = layer_things(layer, names)
    w = World(layer.temp)
    for kind, desc, p in things:
        gap, dz = (pl.ACT_GAP_M, pl.ACT_GAP_DZ) if kind == "act" else (spec.PROP_GAP_M, spec.PROP_GAP_DZ)
        for sdesc, q in spots:
            d = math.hypot(p[0] - q[0], p[1] - q[1])
            # A clue on a table may stand next to an act, as placement allows (placement.py: the ACT_GAP_M rule
            # spares a clue on a table): someone standing at the table does not hide or block it.
            if kind == "act" and "safeclue" in sdesc.lower() and q[2] - p[2] > TABLE_CLUE_RISE_M:
                if d < gap:
                    audit.append("layer: its act %s is %.1f m from the clue on a table %s (allowed)" % (desc, d, sdesc))
                continue
            if d < gap and abs(p[2] - q[2]) < dz:
                problems.append("its %s %s at (%.1f, %.1f, %.1f) is %.1f m from the spot %s at (%.1f, %.1f, %.1f)" %
                                (kind, desc, p[0], p[1], p[2], d, sdesc, q[0], q[1], q[2]))
    audit.append("layer: %s: %d acts and %d props placed, %d more parented into other bricks (not checked), against "
                 "%d Freelancer spots and exits" % (layer.name, sum(k == "act" for k, _, _ in things),
                                                   sum(k == "prop" for k, _, _ in things), unplaced, len(spots)))

    # Its NPCs, exits and starts.
    npcs = {}
    for i, e in enumerate(layer.temp["subEntities"]):
        t = names.get(layer.temp_refs[e["entityTypeResourceIndex"]][1], "").lower()
        leaf = t.split("]")[0].rsplit("/", 1)[-1]
        p = w.pos(i)
        where = "" if p is None else " at (%.1f, %.1f, %.1f)" % p
        if "npcactor" in leaf:
            name = props(e).get("m_sActorName")
            npcs[layer.ids()[i]] = name
            audit.append("layer: NPC %s \"%s\"%s" % (layer.describe(i), name, where))
        elif "exit" in leaf or "herospawn" in leaf:
            audit.append("layer: %s %s%s (%s)" % ("start" if "herospawn" in leaf else "exit", layer.describe(i), where,
                                                  leaf))
    for name, repo, eid in spec.NEVER_TARGETS:
        if eid:
            if eid not in npcs:
                problems.append("its never-target %s %016x is not one of its NPCs" % (name, eid))
            continue
        last = name.split()[-1].lower()
        hits = [(k, v) for k, v in npcs.items() if v and last in v.lower()]
        if hits:
            audit.append("layer: never-target %s (%s) is %s: put the entity id in %s's NEVER_TARGETS and "
                         "src/core/LandslideFull.cpp" % (name, repo, ", ".join("%016x \"%s\"" % h for h in hits),
                                                         spec.NAME))
        else:
            print("build_bricks: %s: WARNING: the never-target %s (%s) is not found by name among %s's %d NPCs" %
                  (spec.NAME, name, repo, layer.name, len(npcs)), file=sys.stderr)

    if problems:
        fail("%s: layer check of %s on %s: %s" % (spec.NAME, layer.name, base_spec.DISPLAY, "; ".join(problems)))
    audit.append("layer: %s on %s: no pool actor, spot or exit is touched" % (layer.name, base_spec.DISPLAY))
    return npcs


def build_layer(spec, args, newest, names):
    """A layer spec's manifest entry: its base variant's scene and swap bricks with the vanilla LAYER_BRICKS appended,
    and its own fingerprints (gen_package_inc.py adds the base's resources, fingerprints and dependencies). Runs the
    layer check when each layer brick's TEMP and TBLU JSON is in --json; warns loudly otherwise."""
    built = BUILT.get(spec.LAYER_OF)
    if built is None:
        fail("%s is a layer on %s, which this run did not build (--variant %s too)" % (spec.NAME, spec.LAYER_OF,
                                                                                       spec.LAYER_OF))
    base_spec = built["spec"]
    if (spec.SCENE, spec.SCENE_PATH) != (base_spec.SCENE, base_spec.SCENE_PATH):
        fail("%s: its scene is not %s's" % (spec.NAME, base_spec.NAME))
    for h, fname in spec.FINGERPRINTS:
        if h not in newest:
            fail("%s's fingerprint %s %016X is in no mounted package" % (spec.NAME, fname, h))
    audit = []
    checked = True
    layer_bricks = []
    for path, temp, tblu in spec.LAYER_BRICKS:
        if gp.rid("[%s].pc_entitytype" % path) != temp:
            fail("%s: %s is not %016X" % (spec.NAME, path, temp))
        if newest.get(temp, {}).get("type") != "TEMP" or tblu not in {r for _, r in newest[temp]["refs"]}:
            fail("%s: %016X is not a TEMP in a mounted package whose blueprint is %016X" % (spec.NAME, temp, tblu))
        layer_bricks.append({"path": path, "rid": "%016X" % temp})
        name = path.rsplit("/", 1)[-1].rsplit(".", 1)[0]
        t, u = find_json(args.json, temp, "TEMP"), find_json(args.json, tblu, "TBLU")
        if t is None or u is None:
            checked = False
            print("build_bricks: %s: WARNING: the layer check did not run: %016X.TEMP and %016X.TBLU (%s, %d and %d "
                  "bytes in %s) are not in --json; nothing says it leaves %s's pool, spots and exits alone. Extract "
                  "them on the Deck (scripts/build-landslide-pkg.sh --help: Bad Boy) and build again before shipping." %
                  (spec.NAME, temp, tblu, name, newest[temp]["final"], newest.get(tblu, {}).get("final", 0),
                   newest[temp]["package"], base_spec.DISPLAY), file=sys.stderr)
            continue
        layer = be.Brick(name, t, u, newest[temp]["refs"], newest[tblu]["refs"], editable=False)
        check_layer(spec, layer, built, names, audit)
    entry = {"name": spec.NAME, "display": spec.DISPLAY, "scene": spec.SCENE_PATH, "sceneRid": "%016X" % spec.SCENE,
             "partition": list(partition(spec)), "layerOf": spec.LAYER_OF, "layerBricks": layer_bricks,
             "swapBricks": [base_spec.MISSION_PATH] + [b["path"] for b in layer_bricks],
             "fingerprints": [["%016X" % h, n] for h, n in spec.FINGERPRINTS], "resources": [],
             "layerCheck": "checked" if checked else "not checked"}
    if args.audit:
        print("\n".join("%s %s" % (spec.NAME + ":", line) for line in audit))
    return entry


# Bricks a spot fix's scenes load that the build may leave unread: chunk0's global UI bricks (nothing placed in a
# level; the Deck's chunk0 is copied in part, and these lie outside the parts: variant_audit's "Not read").
SPOT_UNREAD_OK = {0x0036D64700F55065: "ui_hitman_s3.brick", 0x0064FE1090FB2775: "ui_hitman_s2.brick",
                  0x000135F67D700B4F: "gameessentials.brick"}


def fix_source(spec, args, newest):
    """(TEMP, TBLU) of a fix brick's source as ResourceLib JSON: the --json conversion, else its binaries in a --bin
    folder read with tools/rpkg/entity.py (the brick is stripped to its root, which the reader reads whole; build_fix
    refuses a value it did not decode)."""
    out = []
    for h, typ in zip(spec.FIX_SOURCE, ("TEMP", "TBLU")):
        js = find_json(args.json, h, typ)
        if js is None:
            hits = sorted(x for d in (args.bin or []) for x in glob.glob(os.path.join(d, "*%016X.%s" % (h, typ))))
            if not hits:
                fail("%s: the fix brick's source %s %016X.%s is in no --json folder (ResourceTool convert) nor "
                     "--bin folder (tools/rpkg/rpkg.py extract)" % (spec.NAME, spec.FIX_SOURCE_NAME, h, typ))
            with open(hits[0], "rb") as f:
                data = f.read()
            if len(data) != newest[h]["final"]:
                fail("%s is %d bytes; %s says %d (extract the newest copy)" % (hits[0], len(data),
                                                                             newest[h]["package"], newest[h]["final"]))
            read = gluttony_audit.entity.read_temp if typ == "TEMP" else gluttony_audit.entity.read_tblu
            js = read(data, hits[0])
        out.append(js)
    return out


def spot_fix_scenes(spec, args, newest, names):
    """[(label, {hash: Brick} in load order, Freelancer's closure, the option's own hashes, (navmesh hash, the
    navmesh, Freelancer's navmesh))] of a spot fix's
    scenes (spec.FREELANCER_SCENES, each with the option's APPEND_BRICKS), every brick read from --json or --bin
    (landslide_audit.read_brick), in the load order variant_audit uses."""
    import landslide_audit as la
    out = []
    opt = [t for _, t, _ in spec.APPEND_BRICKS]
    for label, start in spec.FREELANCER_SCENES:
        fl = scene_closure(newest, names, start)
        var = fl | scene_closure(newest, names, opt)
        order = list(dict.fromkeys(list(start) + sorted(fl) + opt + sorted(var)))
        bricks, missing = {}, []
        for h in order:
            b = la.read_brick(h, newest, args.json, list(args.bin or []))
            if b is not None:
                bricks[h] = b
            elif h not in SPOT_UNREAD_OK:
                missing.append("%s (%016X)" % (gp_name(h), h))
        if missing:
            fail("%s %s: %d bricks of the scene are in no --bin folder (extract them: variant_audit.py --deck "
                 "--bin build-landslide/audit/<map> does): %s" % (spec.NAME, label, len(missing), ", ".join(missing)))

        def read_navp(h):
            path = find_navp(list(args.bin or []) + list(args.navp or []), h)
            if path is None:
                return None
            with open(path, "rb") as f:
                return navp.NavMesh(f.read())
        try:
            nh, mesh, fmesh = spot_fix.navmesh_of(newest, fl, var, read_navp)
        except spot_fix.SpotFixError as e:
            fail("%s %s: %s (in no --bin or --navp folder?)" % (spec.NAME, label, e))
        out.append((label, bricks, fl, var - fl, (nh, mesh, fmesh)))
    return out


def build_fix(spec, args, newest, every, names, out):
    """A fix spec's brick (FIX_PATH: a copy of FIX_SOURCE stripped to its root) and its manifest entry: no scene
    swap, the contract's bricks are kept, and the swap appends the vanilla APPEND_BRICKS and then the fix brick.

    The full Gluttony's fr_gluttony holds only the spec's DELETES; the build stops when a delete is not the entity the
    spec names, when a Freelancer brick (or anything IOI's Gluttony scene does not delete itself) names one, or when
    the clash audit (gluttony_audit.audit, with the deletes) finds a clash left. The bricks it checks are read in
    binary from the --bin folders (gluttony_spec.READS, all required).

    A spot fix (SPOT_FIX: spotfix_spec.py) holds property overrides on Freelancer's override brick that move or
    re-point territory spots (spot_fix.apply), and stops the build when spot_fix.check finds a problem on one of its
    scenes (FREELANCER_SCENES with the option's bricks, every brick read from --bin). Its source's own external
    scenes go.

    The source is edited as JSON (--json), or read from its binaries in --bin when it has no JSON (fix_source)."""
    spot = getattr(spec, "SPOT_FIX", False)
    if spot:
        import landslide_audit as la  # it imports this module (main() registers it under its name)
        audit = []
        scenes = spot_fix_scenes(spec, args, newest, names)
    else:
        try:
            read = gluttony_audit.read_all(list(args.bin or []) + list(args.navp or []), newest)
        except gluttony_audit.AuditError as e:
            fail("%s: %s" % (spec.NAME, e))
        missing = ["%s (%016X.TEMP, %016X.TBLU)" % (n, pair[0], pair[1]) for n, pair in spec.READS if n not in read]
        if missing:
            fail("%s: not in --bin (extract them with tools/rpkg/rpkg.py, see tools/rpkg/README.md): %s" %
                 (spec.NAME, ", ".join(missing)))
        audit, problems = gluttony_audit.check_deletes(read, spec.DELETES)
        if problems:
            fail("%s: the fix brick's deletes: %s" % (spec.NAME, "; ".join(problems)))
        lines, clashes = gluttony_audit.audit(read, names, {(h, eid) for h, eid, _, _ in spec.DELETES})
        audit += ["audit: " + line for line in lines]
        if clashes:
            fail("%s: the clash audit finds %d clashes left with the fix brick: %s" % (
                spec.NAME, len(clashes), "; ".join("%s: %s" % (scene, text) for scene, _, text in clashes[:6])))
        _, before = gluttony_audit.audit(read, names)
        audit.append("audit: without the fix brick %d clashes in its %d scenes; with it none" % (
            len(before), len(spec.FREELANCER_SCENES)))

    rid = gp.rid
    for suffix, h in zip(("pc_entitytype", "pc_entityblueprint"), spec.FIX_IDS):
        if rid("[%s].%s" % (spec.FIX_PATH, suffix)) != h:
            fail("%s: [%s].%s is not %016X" % (spec.NAME, spec.FIX_PATH, suffix, h))
        if h in every or h in names:
            fail("%016X ([%s].%s) is already a known hash" % (h, spec.FIX_PATH, suffix))
    for path, temp, tblu in spec.APPEND_BRICKS:
        if rid("[%s].pc_entitytype" % path) != temp:
            fail("%s: %s is not %016X" % (spec.NAME, path, temp))
        if newest.get(temp, {}).get("type") != "TEMP" or tblu not in {r for _, r in newest[temp]["refs"]}:
            fail("%s: %016X is not a TEMP in a mounted package whose blueprint is %016X" % (spec.NAME, temp, tblu))
    for h, fname in spec.FINGERPRINTS:
        if h not in newest:
            fail("%s's fingerprint %s %016X is in no mounted package" % (spec.NAME, fname, h))
    if rid("[%s].pc_entitytemplate" % spec.SCENE_PATH) != spec.SCENE or spec.SCENE not in newest:
        fail("%s's scene path %s is not %016X in a mounted package" % (spec.NAME, spec.SCENE_PATH, spec.SCENE))

    # The fix brick: the source's root and its external scenes (a spot fix: none of them), plus the bricks of the
    # deleted entities, and the deletes (a spot fix: and its property overrides).
    src = spec.FIX_SOURCE
    for h in src:
        if h not in newest:
            fail("%s: the fix brick's source %016X is in no mounted package" % (spec.NAME, h))
    src_temp, src_tblu = fix_source(spec, args, newest)
    b = be.Brick(spec.FIX_SOURCE_NAME, src_temp, src_tblu, newest[src[0]]["refs"], newest[src[1]]["refs"])
    if b.blueprint() != src[1]:
        fail("%s's blueprint is %016X, not %016X" % (spec.FIX_SOURCE_NAME, b.blueprint(), src[1]))
    for k in ("overrideDeletes", "pinConnectionOverrides", "pinConnectionOverrideDeletes"):
        if b.tblu[k]:
            fail("%s: %s has %d %s; the fix brick's source must have none" % (spec.NAME, spec.FIX_SOURCE_NAME,
                                                                               len(b.tblu[k]), k))
    try:
        dropped = len(b.temp["propertyOverrides"])
        b.temp["propertyOverrides"] = []
        root = b.temp["rootEntityIndex"]
        kids = b.delete([b.ids()[i] for i in b.children(root) if i != root])
        if len(b.ids()) != 1:
            fail("%s: %s keeps %d entities, not only its root" % (spec.NAME, spec.FIX_SOURCE_NAME, len(b.ids())))
        if spot:
            b.set_external_scenes([])
        tail = len(b.temp_refs) - len(b.temp["externalSceneTypeIndicesInResourceHeader"])
        used = {e["entityTypeResourceIndex"] for e in b.temp["subEntities"]}
        used.add(b.temp["blueprintIndexInResourceHeader"])
        b.drop_temp_refs([h for k, (_, h) in enumerate(b.temp_refs[:tail]) if k not in used])
        b.set_blueprint(spec.FIX_IDS[1])
        if spot:
            mild = scenes[0][1]
            lines = spot_fix.apply(spec, b, mild[spec.OVERRIDE[0]], la.Scene(mild), names)
            audit += ["fix: " + line for line in lines]
        else:
            for h in dict.fromkeys(h for h, _, _, _ in spec.DELETES):
                if h not in b.scenes:
                    b.add_external_scene(h)
            for h, eid, _, _ in spec.DELETES:
                b.add_override_delete(h, eid)
        b.validate()
    except (be.BrickError, spot_fix.SpotFixError) as e:
        fail("%s: %s" % (spec.NAME, e))
    raw = []
    _find_raw([b.temp, b.tblu], raw)
    if raw:
        fail("%s: the fix brick keeps a value the binary reader did not decode (%s): convert %s with ResourceTool "
             "(--json)" % (spec.NAME, raw[0], spec.FIX_SOURCE_NAME))
    if spot:
        b.hash = spec.FIX_IDS[0]
        problems = []
        for label, bricks, fl, own, (nh, mesh, fmesh) in scenes:
            loaded = dict(bricks)
            loaded[spec.FIX_IDS[0]] = b
            scene = la.Scene(loaded)
            freelancer = la.Scene({h: x for h, x in bricks.items() if h in fl})
            lines, found = spot_fix.check(spec, label, scene, freelancer, own, mesh, la, names, fmesh)
            audit.append("check %s: %d bricks read, navmesh %016X (%d areas)" % (label, len(bricks), nh,
                                                                                 len(mesh.areas)))
            audit += ["check " + line for line in lines]
            problems += found
        if problems:
            fail("%s: the spot fix's checks:\n  %s" % (spec.NAME, "\n  ".join(problems)))
        audit.append("check: %d moved, %d re-pointed, %d deleted; every check passes on %s" % (
            len(spec.MOVES), len(spec.REPOINTS), len(spec.DELETES), ", ".join(x[0] for x in scenes)))
    check_subset_types(spec.NAME, [b], [be.Brick(spec.FIX_SOURCE_NAME, src_temp, src_tblu, newest[src[0]]["refs"],
                                                 newest[src[1]]["refs"], editable=False)], args, audit)
    audit.append("fix brick: %s from %s: its root kept, %d entities and %d property overrides dropped, external "
                 "scenes %s, %d deletes, %d property overrides" % (
                     spec.FIX_PATH.rsplit("/", 1)[-1], spec.FIX_SOURCE_NAME, len(kids), dropped,
                     ", ".join(gp_name(h) for h in b.scenes), len(b.tblu["overrideDeletes"]),
                     len(b.temp["propertyOverrides"])))

    entry = {"name": spec.NAME, "display": spec.DISPLAY, "scene": spec.SCENE_PATH, "sceneRid": "%016X" % spec.SCENE,
             "partition": list(partition(spec)), "appends": True,
             "appendBricks": [{"path": path, "rid": "%016X" % temp} for path, temp, _ in spec.APPEND_BRICKS],
             "swapBricks": [path for path, _, _ in spec.APPEND_BRICKS] + [spec.FIX_PATH],
             "fingerprints": [["%016X" % h, n] for h, n in spec.FINGERPRINTS], "resources": []}
    for typ, js, table, source, h in (("TEMP", b.temp, b.temp_refs, src[0], spec.FIX_IDS[0]),
                                      ("TBLU", b.tblu, b.tblu_refs, src[1], spec.FIX_IDS[1])):
        name = "%016X.%s.json" % (h, typ)
        with open(os.path.join(out, name + ".tmp"), "w") as f:
            json.dump(js, f, indent=1)
        os.replace(os.path.join(out, name + ".tmp"), os.path.join(out, name))
        entry["resources"].append({"path": spec.FIX_PATH, "type": typ, "rid": "%016X" % h, "source": "%016X" % source,
                                   "json": name, "refs": [[f, "%016X" % r] for f, r in table],
                                   "entities": len(b.temp["subEntities"])})
    if args.audit:
        print("\n".join("%s %s" % (spec.NAME + ":", line) for line in audit))
    return entry


def _find_raw(v, out):
    """Every value the binary reader left undecoded ({"$type", "$raw"}) inside v."""
    if isinstance(v, dict):
        if "$raw" in v:
            out.append(v.get("$type", "?"))
            return
        for x in v.values():
            _find_raw(x, out)
    elif isinstance(v, list):
        for x in v:
            _find_raw(x, out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", required=True, help="folder of rpkg metadata (rpkg2.py cache)")
    ap.add_argument("--json", required=True, action="append", help="folder of ResourceLib JSON (repeatable)")
    ap.add_argument("--names", required=True, help="hash list (hash.TYPE,path lines) for the types' names")
    ap.add_argument("--patch-level", type=int, default=4)
    ap.add_argument("--out", required=True)
    ap.add_argument("--variant", action="append", choices=[s.NAME for s in VARIANTS],
                    help="build only these variants (repeatable; default: all)")
    ap.add_argument("--audit", action="store_true", help="print the spot and pool audit")
    ap.add_argument("--bin", action="append",
                    help="folder of extracted resources read in binary only (tools/rpkg/entity.py; repeatable): the "
                         "full Gluttony's bricks (gluttony_spec.READS), never converted to JSON; navmeshes are looked "
                         "for here too (build-landslide/audit/sapienza: the Sapienza crate check's)")
    ap.add_argument("--navp", action="append",
                    help="folder searched for a spec's navmesh, *<HASH>.NAVP as extracted (repeatable; the full "
                         "Icon's; without it its spots are not navmesh-checked, with a warning; the crate check's "
                         "navmeshes, CRATE_NAVMESH or NAVMESH and FREELANCER_NAVMESH, are required, here or in --bin)")
    args = ap.parse_args()
    # landslide_audit (a spot fix's checks) imports this module by name: let it find this one, not a second copy.
    sys.modules.setdefault("build_bricks", sys.modules[__name__])

    # What the game mounts with each variant's map: chunk0 and that partition's chunk (its patches up to the level).
    newest_of = {}
    every = set()
    for spec in VARIANTS:
        part = partition(spec)[0]
        if part not in newest_of:
            newest_of[part], seen = gp.load_newest(args.cache, args.patch_level, (0, part))
            every |= seen
    names = load_names(args.names)
    NAMES.update(names)
    os.makedirs(args.out, exist_ok=True)

    manifest = {"variants": []}
    paths = set()
    for spec in VARIANTS:
        if args.variant and spec.NAME not in args.variant:
            continue
        if getattr(spec, "LAYER_OF", None):
            entry = build_layer(spec, args, newest_of[partition(spec)[0]], names)
            manifest["variants"].append(entry)
            print("build_bricks: %s: a layer on %s: %s (layer check: %s)" % (
                spec.NAME, spec.LAYER_OF, ", ".join(b["path"].rsplit("/", 1)[-1] for b in entry["layerBricks"]),
                entry["layerCheck"]))
            continue
        if getattr(spec, "FIX_PATH", None):
            entry = build_fix(spec, args, newest_of[partition(spec)[0]], every, names, args.out)
            manifest["variants"].append(entry)
            print("build_bricks: %s: wrote %s (appended after %s)" % (spec.NAME, ", ".join(
                "%s %s %s" % (r["rid"], r["type"], r["path"].rsplit("/", 1)[-1]) for r in entry["resources"]),
                ", ".join(b["path"].rsplit("/", 1)[-1] for b in entry["appendBricks"])))
            continue
        if {spec.MISSION_PATH, spec.OVERRIDE_PATH} & paths:
            fail("%s's brick paths are another variant's" % spec.NAME)
        paths |= {spec.MISSION_PATH, spec.OVERRIDE_PATH}
        entry = build_variant(spec, args, newest_of[partition(spec)[0]], every, names, args.out)
        manifest["variants"].append(entry)
        print("build_bricks: %s: wrote %s" % (spec.NAME, ", ".join(
            "%s %s %s (%d entities)" % (r["rid"], r["type"], r["path"].rsplit("/", 1)[-1], r["entities"])
            for r in entry["resources"])))
    with open(os.path.join(args.out, MANIFEST), "w") as f:
        json.dump(manifest, f, indent=1)


if __name__ == "__main__":
    main()
