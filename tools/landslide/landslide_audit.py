#!/usr/bin/env python3
"""The clash audit of Sapienza's full variants (the full Landslide, the full Author, the full Icon, and the full Bad Boy:
the Icon with its layer, mission_caipirinha): everything the variant's scene places that Freelancer's mild Sapienza
does not, against every place Freelancer spawns something or
puts 47, from the package build's bricks (build-landslide/built) and the vanilla bricks of both scenes, read as
ResourceTool JSON (--json) or straight from the extracted binaries (--bin, tools/rpkg/entity.py).

Freelancer's places (our copies of mission_mild_bingo and override_bingo) were made for normal Sapienza: the bricks
scene_costaltown_mild_bingo loads. A full variant loads its own scene instead (Landslide's _scene_mamba, The Author's
scene_ebola, The Icon's _scene_copperhead) with our two bricks. Its content is checked against the Freelancer scene,
entity by entity: a placed entity of the variant's scene counts as new when its brick is not loaded by Freelancer's
scene, or when Freelancer's scene deletes it (a vanilla Freelancer brick such as mission01 deletes it and the variant
does not), or when it stands somewhere else (a variant brick moves it: a transform or parent override). Everything
else is what Freelancer's places were made with.

The places: every spot a territory slot of our override names (stashes, safes, the supplier, money mules), the clues
of those safes, every hero spawn the variant's scene keeps (not deleted, not hidden), and Freelancer's exit buttons
(Exit_Evergreen_PushButton*) in our two bricks. A place clashes when something new and solid is on it:
- a prop (the environment's props, vehicles, distraction set pieces, a mission's own prop templates) within CLASH_M
  in plan and CLASH_DZ_LOW..CLASH_DZ_HIGH in height of the place's origin (props stand on their origins; a supply
  crate is about 0.9 x 0.5 x 0.5 m);
- a collision box or capsule (m_bCollideHitman not false) that holds the place (BOX_MARGIN in plan);
- with the scenes' navmeshes (their NAVP in a --bin folder): a prop within NAV_CLASH_M of a place that has floor under
  Freelancer's navmesh and none under the variant's (IOI bake solid props into the navmesh, so a big one, such as a
  bed, cuts the floor at a place its origin is more than CLASH_M from). Every place whose floor the variant's
  navmesh lost, or whose clearance dropped, is listed too.
Within NEAR_M anything new that is placed (a prop, a set piece, an item spawner) is listed as a note. A spec's
CLASH_ACCEPTED ({(brick, entity id): why}) lists clashes looked at and left (an exit button by its car's door).

The privacy check: a supplier will not deal while someone is about, and a safe or a stash (a supply crate) with an NPC
standing on it is no use. So every act of every brick the variant's scene loads (our bricks and a layer's included)
that stands within SUPPLIER_PRIVACY_M (plan; SUPPLIER_PRIVACY_DZ in height) of a supplier spot, inside the supplier's
private volume (privateVolume: its m_vGlobalSize box, VOLUME_DZ_LOW of slack below for an act on the floor under it),
or within SPOT_PRIVACY_M (plan; SPOT_PRIVACY_DZ) of a safe or stash spot is listed as BUSY; the spot's own NPC's acts
(the supplier's, a safe's guard's) do not count. A finding that Freelancer's own scene has too (the same act at the
same place by a spot at the same place) is a note, not a finding: vanilla Freelancer plays with it.

World positions chain m_eidParent across every read brick of the scene (location included, its folders with their
own transforms), with every loaded brick's m_mTransform and m_eidParent property overrides applied (later bricks in
hash order win; no two read bricks override the same entity's place differently on Sapienza); an entity is gone when a
loaded brick deletes it or one of its logical parents. Bricks with neither JSON nor binary are listed as not read.

    python3 -I tools/landslide/landslide_audit.py --json build-landslide/json --bin <extracted folder> ... \\
        --built build-landslide/built --cache build-landslide/cache --names third_party/zhmtools/hash_list.txt \\
        [--variant landslide --variant author --variant icon --variant badboy]

The --bin folders hold what has no JSON: the bricks only one scene loads and the four navmeshes, extracted with
tools/rpkg/rpkg.py extract; --missing prints their hashes by chunk.

Per variant it prints two runs: our override brick as built, and with the spec's DELETE_SCENE_PROPS added (the same
once the package is rebuilt with them); the privacy check runs on the first (as built).
"""
import argparse
import glob
import json
import math
import os
import re
import sys
import zlib

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "rpkg"))

import author_spec  # noqa: E402
import badboy_spec  # noqa: E402
import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import entity  # noqa: E402
import icon_spec  # noqa: E402
import navp  # noqa: E402
import placement as pl  # noqa: E402
import landslide_spec as spec  # noqa: E402  (the default variant)

# The full Bad Boy is a layer on the full Icon (its LAYER_OF): the Icon's built bricks and scene, plus LAYER_BRICKS.
SPECS = {"landslide": spec, "author": author_spec, "icon": icon_spec, "badboy": badboy_spec}

CLASH_M = 0.6
CLASH_DZ_LOW = -0.3
CLASH_DZ_HIGH = 1.2
NEAR_M = 1.5
NEAR_DZ = 2.5
SAME_M = 0.05  # a shared entity this close to its place under Freelancer has not moved (or is a copy)
NAV_DROP_M = 0.25  # navmesh_notes: a clearance drop this large, to under NAV_LOW_M, is listed
NAV_LOW_M = 0.6
NAV_CLASH_M = 1.0  # a solid prop this close to a place whose navmesh floor the variant lost is a clash
# A prop's real extent (audit's extent): a place (not a clue) inside the bounding box of its model (its PRIMs' bounds,
# in its own frame) with EXTENT_MARGIN in plan, from EXTENT_DZ_BELOW under the box's bottom (the crate or 47 standing
# under it) to EXTENT_TOP_M under its top (a place on top of it is on it, not in it), clashes, wherever its origin is:
# A Bitter Pill's Kitchen_Fridge_C_00 held Whittleton's Safe_Unique_08 with its origin 0.91 m away.
EXTENT_MARGIN = 0.1
EXTENT_DZ_BELOW = 0.5
EXTENT_TOP_M = 0.1
# ... only for a model up to this across in plan: a bigger one's box is mostly air (a tree's crown, a tent, an arch;
# Embrace of the Serpent's jungle trees "held" Santa Fortuna's Supplier02 7 m from their trunks): only its origin
# counts (CLASH_M), as before.
EXTENT_MAX_M = 3.0
# A prop whose model is not readable (not in the copied packages): its origin within this in plan (CLASH_DZ_LOW ..
# CLASH_DZ_HIGH in height) clashes, "extent unknown": a fridge's or a wardrobe's origin is at a corner or the back,
# about 1 m from the far side of its footprint (the fridge above).
UNKNOWN_EXTENT_M = 1.0
# The privacy check: no NPC act this close to a supplier (plan; height) or inside its private volume (VOLUME_DZ_LOW of
# slack below the box: an act stands on the floor its bottom face is about level with), nor this close to a safe or
# a stash (plan; height). The supplier's distance is the one build_bricks' placement keeps a moved supplier to.
SUPPLIER_PRIVACY_M = pl.SUPPLIER_GAP_M
SUPPLIER_PRIVACY_DZ = pl.SPOT_GAP_DZ
SPOT_PRIVACY_M = 2.0
SPOT_PRIVACY_DZ = pl.ACT_GAP_DZ
VOLUME_DZ_LOW = 0.3
FREELANCER_SCENE = 0x004B6755A8F753FA  # scene_costaltown_mild_bingo.entity, the contract's scene
HERO_SPAWN_TYPE = 0x00F3EA8B2DB49189  # herospawn.entitytemplate

SPOT_KINDS = (("setpiece_evergreen_stash_unique", "stash"), ("setpiece_evergreen_container_items_safe", "safe"),
              ("helper_evergreen_supplierpackage", "supplier"), ("setpiece_evergreen_moneymulesetup", "mule"))


def leaf(path):
    return path.split("?/")[-1].split("/")[-1].split("]")[0].lower() if path else ""


# Small loose things (tableware, books, food, litter, cables on the floor, stickers): nothing solid; notes only.
SMALL = ("/props/utensils/", "/props/wires/", "/props/food/", "/props/trash/", "/props/accessories/", "book", "can_soda", "litter",
         "glass_", "bottle", "decal", "extension_cord", "wirebundle", "sticker", "napkin", "carpet_", "rug_")
# Physics volumes: solid although invisible (a mission's blocker round a stack of props, an invisible wall).
BLOCKERS = ("collisionvolumes.template?/collisionbox.entitytemplate", "collisionvolumes.template?/collisioncapsule")
BOX_MARGIN = 0.3  # a place this close to a collision box counts as in it (a supply crate is about 0.9 x 0.5 m)
CLUE_BOX_MARGIN = 0.0  # a safe clue (a tablet, a laptop: scanned from where 47 stands) only when the box holds it


def kind_of(path):
    """"prop" for placed geometry (the environment's props, vehicles, distraction set pieces such as a fuse box, a
    mission's own prop templates such as The Icon's copperhead_props), "small" for loose small props, "blocker" for a
    collision box or capsule, "item" for an item spawner, "act", "setpiece" (other set pieces), or "logic" (classes,
    markup, sound, volumes, blend-in points, activators)."""
    t = (path or "").lower()
    lf = leaf(t)
    if "zitemspawner" in t:
        return "item"
    if lf.startswith("act_") or "/acts" in t or lf.startswith("crowdactivity"):
        return "act"
    if any(s in t for s in BLOCKERS):
        return "blocker"
    if "/environment/" in t or "/vehicles/" in t or ".prim]" in t or "setpieces_distractions" in t or \
            "_props.template?/" in t:
        return "small" if any(s in t for s in SMALL) else "prop"
    if lf.startswith("setpiece_"):
        return "setpiece"  # a blend-in point, an activator socket: nothing solid, but it has a prompt or a use
    return "logic"


def prop_value(d, name):
    """A property by name, also when the binary reader only had its crc (entity.py names a few)."""
    if name in d:
        return d[name]
    return d.get("0x%08X" % zlib.crc32(name.encode()))


def in_box(q, w, size, margin=BOX_MARGIN):
    """(inside, place in the box's frame): a place q (a crate standing on it: CLASH_DZ_LOW..CLASH_DZ_HIGH) against a
    box of full size (x, y, z) centered on the world matrix w, with margin (BOX_MARGIN) in plan."""
    v = [q[k] - w[k][3] for k in range(3)]
    loc = [sum(w[r][c] * v[r] for r in range(3)) for c in range(3)]
    inside = abs(loc[0]) <= size[0] / 2 + margin and abs(loc[1]) <= size[1] / 2 + margin and \
        loc[2] + CLASH_DZ_LOW < size[2] / 2 and loc[2] + CLASH_DZ_HIGH > -size[2] / 2
    return inside, loc


def blocker_shape(d):
    """("box", full size) of a collision box (m_vBoxSize), ("capsule", (radius, length)) of a collision capsule
    (m_fCapsuleRadius, m_vCapsuleLength: taken along its local z), or None."""
    s = prop_value(d, "m_vBoxSize")
    if isinstance(s, dict) and "x" in s:
        return "box", (s["x"], s["y"], s["z"])
    r, ln = prop_value(d, "m_fCapsuleRadius"), prop_value(d, "m_vCapsuleLength")
    if isinstance(r, (int, float)):
        return "capsule", (r, ln if isinstance(ln, (int, float)) else 0.0)
    return None


def in_blocker(q, w, shape, margin=BOX_MARGIN):
    """(inside, place in the blocker's frame, what): in_box for a box; for a capsule, whether the crate's vertical
    span at q comes within its radius (plus margin) of its axis segment."""
    what, dims = shape
    if what == "box":
        inside, loc = in_box(q, w, dims, margin)
        return inside, loc, "size %.2f x %.2f x %.2f" % dims
    v = [q[k] - w[k][3] for k in range(3)]
    loc = [sum(w[r][c] * v[r] for r in range(3)) for c in range(3)]
    r, ln = dims
    up = [w[2][c] for c in range(3)]  # the world's up in the capsule's frame
    best = None
    for k in range(13):  # the crate's vertical span, sampled
        dz = CLASH_DZ_LOW + (CLASH_DZ_HIGH - CLASH_DZ_LOW) * k / 12
        pt = [loc[c] + up[c] * dz for c in range(3)]
        t = max(-ln / 2, min(ln / 2, pt[2]))
        d = math.sqrt(pt[0] ** 2 + pt[1] ** 2 + (pt[2] - t) ** 2)
        best = d if best is None else min(best, d)
    return best <= r + margin, loc, "radius %.2f, length %.2f, %.2f m from its axis" % (r, ln, best)


def inverse3(w):
    """The inverse of a world matrix's 3x3 part (rotation and scale), or None when it is singular."""
    a = [[w[r][c] for c in range(3)] for r in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
           a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    if abs(det) < 1e-9:
        return None
    inv = [[0.0] * 3 for _ in range(3)]
    for r in range(3):
        for c in range(3):
            m = [[a[i][j] for j in range(3) if j != r] for i in range(3) if i != c]
            inv[r][c] = ((-1) ** (r + c)) * (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det
    return inv


def in_extent(q, w, box):
    """(inside, place in the prop's frame): a place q against a prop's model bounds box ((min x, y, z), (max x, y, z))
    in its own frame, the prop at world matrix w (scale included): inside with EXTENT_MARGIN in plan, from
    EXTENT_DZ_BELOW under its bottom to EXTENT_TOP_M under its top."""
    inv = inverse3(w)
    if inv is None:
        return False, None
    v = [q[k] - w[k][3] for k in range(3)]
    loc = [sum(inv[r][c] * v[c] for c in range(3)) for r in range(3)]
    lo, hi = box
    inside = lo[0] - EXTENT_MARGIN <= loc[0] <= hi[0] + EXTENT_MARGIN and \
        lo[1] - EXTENT_MARGIN <= loc[1] <= hi[1] + EXTENT_MARGIN and \
        lo[2] - EXTENT_DZ_BELOW <= loc[2] <= hi[2] - EXTENT_TOP_M
    return inside, loc


def extent_reach(w, box):
    """How far in plan the box's corners reach from the prop's origin (world matrix w, scale included)."""
    lo, hi = box
    best = 0.0
    for x in (lo[0], hi[0]):
        for y in (lo[1], hi[1]):
            best = max(best, math.hypot(w[0][0] * x + w[0][1] * y, w[1][0] * x + w[1][1] * y))
    return best


def find_bin(dirs, h, typ):
    hits = []
    for d in dirs:
        hits += glob.glob(os.path.join(d, "*%016X.%s" % (h, typ)))
    return sorted(hits)[0] if hits else None


def read_brick(h, newest, json_dirs, bin_dirs):
    """The vanilla brick of TEMP hash h (read only) from --json, else from the binaries in --bin; None when either
    file is missing."""
    tb = [r for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "TBLU"]
    if len(tb) != 1:
        return None
    t = bb.find_json(json_dirs, h, "TEMP")
    u = bb.find_json(json_dirs, tb[0], "TBLU")
    for typ, reader in (("TEMP", entity.read_temp), ("TBLU", entity.read_tblu)):
        if (t if typ == "TEMP" else u) is not None:
            continue
        path = find_bin(bin_dirs, h if typ == "TEMP" else tb[0], typ)
        if path is None:
            return None
        with open(path, "rb") as f:
            js = reader(f.read(), path)
        if typ == "TEMP":
            t = js
        else:
            u = js
    b = be.Brick(bb.gp_name(h), t, u, newest[h]["refs"], newest[tb[0]]["refs"], editable=False)
    b.hash = h
    return b


class Scene:
    """Bricks loaded together ({TEMP hash: Brick}, in load order): world positions across bricks, with every loaded
    brick's m_mTransform and m_eidParent property overrides applied (later bricks win), and every delete (an entity is
    gone when it or one of its logical parents is deleted)."""

    def __init__(self, bricks, extra_deletes=()):
        """extra_deletes: more (brick hash, entity id) deletes, as a brick loaded after these would make them."""
        self.B = bricks
        self.P = {h: [bb.props(e) for e in b.temp["subEntities"]] for h, b in bricks.items()}
        self.idx = {h: {e["entityId"]: i for i, e in enumerate(b.tblu["subEntities"])} for h, b in bricks.items()}
        self.ov = {}  # (brick, index) -> {property: (value, the brick whose scenes its references index)}
        self.conflicts = []  # (brick, index), property, two bricks that place it differently (the load order decides)
        self.deletes = set(extra_deletes)
        for h, b in bricks.items():
            for r in b.tblu["overrideDeletes"]:
                if r["externalSceneIndex"] >= 0:
                    self.deletes.add((b.scenes[r["externalSceneIndex"]], r["entityID"]))
            for o in b.temp["propertyOverrides"]:
                key = self.resolve(h, o["propertyOwner"])
                if key is not None:
                    pv = o["propertyValue"]
                    had = self.ov.setdefault(key, {}).get(pv["nPropertyID"])
                    if had is not None and had[1] != h and had[0] != pv["value"]["$val"] and \
                            pv["nPropertyID"] in ("m_mTransform", "m_eidParent"):
                        self.conflicts.append((key, pv["nPropertyID"], had[1], h))
                    self.ov[key][pv["nPropertyID"]] = (pv["value"]["$val"], h)
        self.dels = {(h, self.idx[h][eid]) for h, eid in self.deletes if h in bricks and eid in self.idx[h]}
        self.cache, self.gone_cache = {}, {}
        self.unplaced = {}  # (brick, brick it hangs off) -> count

    def resolve(self, ctx, ref):
        """(brick, index) an entity reference of brick ctx names, or None (an unread brick, a missing id)."""
        if ref is None:
            return None
        if ref["externalSceneIndex"] >= 0:
            sc = self.B[ctx].scenes[ref["externalSceneIndex"]]
            i = self.idx.get(sc, {}).get(ref["entityID"])
            return None if i is None else (sc, i)
        return (ctx, ref["entityIndex"]) if ref["entityIndex"] >= 0 else None

    def prop(self, h, i, name):
        """(value, context brick) of a property, the last override first."""
        o = self.ov.get((h, i), {})
        if name in o:
            return o[name]
        return self.P[h][i].get(name), h

    def gone(self, h, i, depth=0):
        k = (h, i)
        if k in self.gone_cache:
            return self.gone_cache[k]
        g = k in self.dels
        if not g and depth < 200:
            lp = self.B[h].temp["subEntities"][i]["logicalParent"]
            p = self.resolve(h, lp)
            g = p is not None and p != k and self.gone(p[0], p[1], depth + 1)
        self.gone_cache[k] = g
        return g

    def world(self, h, i, depth=0):
        k = (h, i)
        if k in self.cache:
            return self.cache[k]
        b = self.B[h]
        if i == b.temp["rootEntityIndex"] or depth > 96:
            return bb.IDENT
        m, _ = self.prop(h, i, "m_mTransform")
        local = bb.matrix(m) if isinstance(m, dict) and "Trans" in m else bb.IDENT
        par, ctx = self.prop(h, i, "m_eidParent")
        w = None
        if not be.is_ref(par) or (par["externalSceneIndex"] < 0 and par["entityIndex"] < 0):
            w = local
        else:
            p = self.resolve(ctx, par)
            if p is None:
                if par["externalSceneIndex"] >= 0:
                    sc = self.B[ctx].scenes[par["externalSceneIndex"]]
                    self.unplaced[(h, sc)] = self.unplaced.get((h, sc), 0) + 1
            else:
                pw = self.world(p[0], p[1], depth + 1)
                w = bb.mul(pw, local) if pw is not None else None
        self.cache[k] = w
        return w

    def pos(self, h, i):
        w = self.world(h, i)
        return None if w is None else (w[0][3], w[1][3], w[2][3])


TERRITORY_NAME = "Helper_Evergreen_CPD_TerritorySetupRandomizer"
TERRITORY_TYPE = bb.gp.rid("[assembly:/_pro/design/setpieces/setpieces_evergreen.template?/"
                           "helper_evergreen_cpd_territorysetuprandomizer.entitytemplate].pc_entitytype")


def territory_index(ov):
    """The index of the override brick's territory randomizer (the entity whose available* slots name every stash,
    safe, supplier and money mule spot): the one entity of its template, else the one named TERRITORY_NAME (or
    TERRITORY_NAME_<n>: Isle of Sgail's override_whist calls it ..._01)."""
    hits = [i for i, e in enumerate(ov.temp["subEntities"])
            if ov.temp_refs[e["entityTypeResourceIndex"]][1] == TERRITORY_TYPE]
    if len(hits) != 1:
        hits = [i for i, e in enumerate(ov.tblu["subEntities"])
                if re.fullmatch(re.escape(TERRITORY_NAME) + r"(_\d+)?", e["entityName"])]
    if len(hits) != 1:
        raise be.BrickError("%s has %d territory randomizers (%s)" % (ov.name, len(hits), TERRITORY_NAME))
    return hits[0]


def slot_spots(ov, scene=None):
    """{entity index: [slot properties]} of the spots the territory slots of our override brick name (local entities
    only). With a scene, a slot a later brick overrides (a spot fix's re-point: spot_fix.py) names what the override
    names, when that is an entity of ov."""
    ti = territory_index(ov)
    h = getattr(ov, "hash", None)
    slots = {}
    for k, v in sorted(bb.props(ov.temp["subEntities"][ti]).items()):
        if not (k.startswith("available") and isinstance(v, dict) and v.get("externalSceneIndex", 0) < 0):
            continue
        i = v["entityIndex"]
        if scene is not None and h is not None and h in scene.B:
            sv, ctx = scene.prop(h, ti, k)
            r = scene.resolve(ctx, sv) if be.is_ref(sv) else None
            if r is None or r[0] != h:
                continue
            i = r[1]
        slots.setdefault(i, []).append(k)
    return slots


def spots(ov, names, scene=None):
    """[(description, position)] of every place a territory slot of our override brick can spawn, and its safes'
    clues. Positions from the scene (cross-brick parents and overrides) when given, else from the brick alone."""
    w = bb.World(ov.temp)
    h = getattr(ov, "hash", None)

    def pos(i):
        return scene.pos(h, i) if scene is not None else w.pos(i)
    slots = slot_spots(ov, scene)
    out = []
    for i, ks in sorted(slots.items()):
        t = leaf(names.get(ov.temp_refs[ov.temp["subEntities"][i]["entityTypeResourceIndex"]][1], ""))
        kind = next((n for p, n in SPOT_KINDS if t.startswith(p)), "spot")
        out.append(("%s %s (%s)" % (kind, ov.describe(i), ", ".join(ks)), pos(i)))
        if kind == "safe":
            for k, v in sorted(w.P[i].items()):
                if k.startswith("Clue") and be.is_ref(v) and v["externalSceneIndex"] < 0 and v["entityIndex"] >= 0:
                    out.append(("clue %s (of %s)" % (ov.describe(v["entityIndex"]), ov.describe(i)),
                                pos(v["entityIndex"])))
    return out


def starts_and_exits(scene, ours, start_bricks=None, exit_prefix="Exit_Evergreen_PushButton"):
    """[(description, position)] of every hero spawn of the scene that is not gone or hidden (m_bHide), and of
    Freelancer's exit buttons in our bricks (ours: their hashes; entities named exit_prefix*, none when it is None).
    start_bricks: when given, only these bricks' hero spawns count (an appended layer's own starts are not
    Freelancer's)."""
    out = []
    for h, b in scene.B.items():
        for i, e in enumerate(b.temp["subEntities"]):
            t = b.temp_refs[e["entityTypeResourceIndex"]][1]
            name = b.entity_name(i)
            if t == HERO_SPAWN_TYPE and (start_bricks is None or h in start_bricks):
                what = "start"
            elif h in ours and exit_prefix is not None and name.startswith(exit_prefix):
                what = "exit"
            else:
                continue
            if scene.gone(h, i) or scene.prop(h, i, "m_bHide")[0] is True:
                continue
            p = scene.pos(h, i)
            if p is not None:
                out.append(("%s %s %s" % (what, bb.gp_name(h) if h not in ours else b.name, b.describe(i)), p))
    return out


def placed_index(scene, names, skip=()):
    """{(type hash, grid cell): [world position]} of every placed entity of the scene that is not gone (the copy
    check of new_content)."""
    idx = {}
    for h, b in scene.B.items():
        if h in skip:
            continue
        for i, e in enumerate(b.temp["subEntities"]):
            t = b.temp_refs[e["entityTypeResourceIndex"]][1]
            if kind_of(names.get(t, "")) in ("logic", "act") or scene.gone(h, i):
                continue
            p = scene.pos(h, i)
            if p is not None:
                idx.setdefault((t, round(p[0]), round(p[1]), round(p[2])), []).append(p)
    return idx


def is_copy(idx, t, p):
    """Whether an entity of type t stands at p (within SAME_M) in the indexed scene."""
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for q in idx.get((t, round(p[0]) + dx, round(p[1]) + dy, round(p[2]) + dz), ()):
                    if math.dist(p, q) < SAME_M:
                        return True
    return False


def new_content(scene, base, names, skip, base_index=None):
    """[(brick, index, kind, path, position, why)] of what the scene places that base (Freelancer's scene) does not:
    entities of bricks base does not load, entities base deletes, and entities that stand elsewhere in base; an
    entity of the same type at the same place in base (a variant brick's copy of one it deletes, such as
    scenario_ebola's copies of location's church collision boxes) is no news. skip: bricks left out (ours: they are
    Freelancer's own). base_index: placed_index(base, names), when already made."""
    if base_index is None:
        base_index = placed_index(base, names)
    out = []
    for h in sorted(scene.B):
        if h in skip:
            continue
        b = scene.B[h]
        shared = h in base.B
        for i in range(len(b.temp["subEntities"])):
            t = b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1]
            path = names.get(t, "")
            kind = kind_of(path)
            if kind in ("logic", "act") or i == b.temp["rootEntityIndex"] or scene.gone(h, i):
                continue
            p = scene.pos(h, i)
            if p is None:
                continue
            why = "new"
            if shared:
                if base.gone(h, i):
                    why = "deleted under Freelancer"
                else:
                    q = base.pos(h, i)
                    if q is not None and math.dist(p, q) < SAME_M:
                        continue
                    why = "moved (under Freelancer at %s)" % ("(%.2f, %.2f, %.2f)" % q if q else "no place")
            if is_copy(base_index, t, p):
                continue
            out.append((h, i, kind, path, p, why))
    return out


def audit(scene, content, places, accepted=None, no_floor=(), extent=None):
    """(clash lines, note lines) for the new content against the places. accepted: {(brick, entity id): why} of
    clashes looked at and left (the spec's CLASH_ACCEPTED), listed first among the notes. no_floor: descriptions of
    places whose navmesh floor the variant lost (navmesh_notes): there a solid prop within NAV_CLASH_M clashes too (a
    big one, such as a bed, whose origin is farther than CLASH_M from what it covers). extent: a function (brick,
    index) -> the prop's model bounds in its frame ((min x, y, z), (max x, y, z)), or None when they are not readable;
    with it a prop also clashes with a place (not a clue: clues lie on tables) inside its bounds (in_extent), and one
    whose bounds are unknown with a place within UNKNOWN_EXTENT_M of its origin ("extent unknown"); a model more than
    EXTENT_MAX_M across in plan keeps the origin check only."""
    accepted = accepted or {}
    no_floor = set(no_floor)
    clashes, notes = [], []
    for h, i, kind, path, p, why in content:
        if scene.gone(h, i):
            continue
        b = scene.B[h]
        shape = blocker_shape(scene.P[h][i]) if kind == "blocker" else None
        reach = (max(shape[1]) / 2 if shape[0] == "box" else shape[1][0] + shape[1][1] / 2) if shape else 0.0
        box = w = None
        if kind == "prop" and extent is not None:
            box = extent(h, i)
            w = scene.world(h, i)
            if box is not None and max(box[1][k] - box[0][k] for k in range(2)) > EXTENT_MAX_M:
                box = w = None  # too big for its box to mean anything: the origin check only
            if box is not None and w is not None:
                reach = max(0.0, extent_reach(w, box) + EXTENT_MARGIN - NEAR_M)
        for desc, q in places:
            if q is None:
                continue
            dp, dz = math.hypot(p[0] - q[0], p[1] - q[1]), p[2] - q[2]
            if dp > NEAR_M + reach or abs(dz) > NEAR_DZ + reach:
                continue
            line = "%s: %s %016x \"%s\" (%s, %s; %s) at (%.2f, %.2f, %.2f): %.2f m in plan, %+.2f m up" % (
                desc, bb.gp_name(h), b.tblu["subEntities"][i]["entityId"], b.entity_name(i), kind, leaf(path), why,
                p[0], p[1], p[2], dp, dz)
            clash = kind == "prop" and dp < CLASH_M and CLASH_DZ_LOW <= dz <= CLASH_DZ_HIGH
            if kind == "prop" and not clash and desc in no_floor and dp < NAV_CLASH_M and \
                    CLASH_DZ_LOW <= dz <= CLASH_DZ_HIGH:
                clash = True
                line += "; the variant's navmesh has no floor at the place"
            if kind == "prop" and extent is not None and not desc.startswith("clue ") and w is not None:
                if box is not None:
                    inside, loc = in_extent(q, w, box)
                    if inside:
                        clash = True
                        line += "; inside its model's bounds (%.2f x %.2f x %.2f m; the place at (%.2f, %.2f, " \
                                "%.2f) in its frame)" % (tuple(box[1][k] - box[0][k] for k in range(3)) + tuple(loc))
                    elif not clash and (dp > NEAR_M or abs(dz) > NEAR_DZ):
                        continue  # only within its bounds' reach
                elif dp < UNKNOWN_EXTENT_M and CLASH_DZ_LOW <= dz <= CLASH_DZ_HIGH:
                    if not clash:
                        line += "; extent unknown (its model is not readable here): origin within %.1f m" % \
                            UNKNOWN_EXTENT_M
                    clash = True
            if kind == "blocker":
                if shape is None:
                    line += ", size not read"
                else:
                    clash, loc, what = in_blocker(q, scene.world(h, i), shape,
                                                  CLUE_BOX_MARGIN if desc.startswith("clue ") else BOX_MARGIN)
                    if not clash and dp > NEAR_M:
                        continue
                    line += ", %s, the place at (%.2f, %.2f, %.2f) in its frame" % ((what,) + tuple(loc))
                    if scene.prop(h, i, "m_bCollideHitman")[0] is False:
                        clash = False  # it stops NPCs only
                        line += ", m_bCollideHitman false"
            eid = b.tblu["subEntities"][i]["entityId"]
            if clash and (h, eid) in accepted:
                clash = False
                line = "ACCEPTED " + line + ": " + accepted[(h, eid)]
            (clashes if clash else notes).append(line)
    return clashes, notes


def actor_of(scene, h, i):
    """(brick, index) of the NPC an act plays for: its m_pActor, through a Role's m_rActorProvider; or None."""
    v, ctx = scene.prop(h, i, "m_pActor")
    if not be.is_ref(v):
        return None
    k = scene.resolve(ctx, v)
    if k is None:
        return None
    p, c2 = scene.prop(k[0], k[1], "m_rActorProvider")
    if be.is_ref(p):
        return scene.resolve(c2, p) or k
    return k


def privacy_places(ov, names, scene):
    """[(kind, entity id, description, position, own NPCs {(brick, index)}, private volumes [(world matrix, size)])]
    of every supplier, safe and stash spot a territory slot of our override brick (ov, its hash in ov.hash) names.
    A supplier's own NPCs: its m_rActorProvider and exclusionList; a safe's: its guards (safeEnabled pins)."""
    h = ov.hash
    slots = slot_spots(ov, scene)
    out = []
    for i, ks in sorted(slots.items()):
        t = leaf(names.get(ov.temp_refs[ov.temp["subEntities"][i]["entityTypeResourceIndex"]][1], ""))
        kind = next((n for p, n in SPOT_KINDS if t.startswith(p)), None)
        if kind not in ("supplier", "safe", "stash"):
            continue
        q = scene.pos(h, i)
        if q is None:
            continue
        own, vols = set(), []
        if kind == "supplier":
            refs = [scene.prop(h, i, "m_rActorProvider")] + [(r, scene.prop(h, i, "exclusionList")[1]) for r in
                                                             scene.prop(h, i, "exclusionList")[0] or []]
            for r, ctx in refs:
                k = scene.resolve(ctx, r) if be.is_ref(r) else None
                if k is not None:
                    own.add(k)
            vr, ctx = scene.prop(h, i, "privateVolume")
            for r in vr or []:
                k = scene.resolve(ctx, r) if be.is_ref(r) else None
                size = scene.prop(k[0], k[1], "m_vGlobalSize")[0] if k else None
                w = scene.world(k[0], k[1]) if k else None
                if isinstance(size, dict) and w is not None:
                    vols.append((w, (size["x"], size["y"], size["z"])))
        elif kind == "safe":
            own |= {(h, p["toID"]) for p in ov.tblu["pinConnections"]
                    if p["fromID"] == i and p["fromPinName"] == "safeEnabled"}
        out.append((kind, ov.tblu["subEntities"][i]["entityId"], "%s %s (%s)" % (kind, ov.describe(i), ", ".join(ks)),
                    q, own, vols))
    return out


def scene_acts(scene, names):
    """[(brick, index, world position)] of every act of the scene's read bricks that is not gone and has a place."""
    out = []
    for h, b in scene.B.items():
        for i, e in enumerate(b.temp["subEntities"]):
            if kind_of(names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")) != "act" or scene.gone(h, i):
                continue
            p = scene.pos(h, i)
            if p is not None and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                out.append((h, i, p))
    return out


def in_volume(p, w, size):
    """(inside, place in the volume's frame): an act at p (its feet) in a box of full size centered on w, with
    VOLUME_DZ_LOW of slack below."""
    v = [p[k] - w[k][3] for k in range(3)]
    loc = [sum(w[r][c] * v[r] for r in range(3)) for c in range(3)]
    inside = abs(loc[0]) <= size[0] / 2 and abs(loc[1]) <= size[1] / 2 and \
        -size[2] / 2 - VOLUME_DZ_LOW <= loc[2] <= size[2] / 2
    return inside, loc


def privacy(scene, places, acts, names):
    """{(spot entity id, act brick, act entity id): line} of the acts that keep a spot busy (the module's privacy
    check): within SUPPLIER_PRIVACY_M of a supplier or inside its private volume, within SPOT_PRIVACY_M of a safe or a
    stash; the spot's own NPCs' acts left out."""
    out = {}
    for kind, sid, desc, q, own, vols in places:
        gap, gdz = (SUPPLIER_PRIVACY_M, SUPPLIER_PRIVACY_DZ) if kind == "supplier" else (SPOT_PRIVACY_M, SPOT_PRIVACY_DZ)
        for h, i, p in acts:
            dp, dz = math.hypot(p[0] - q[0], p[1] - q[1]), p[2] - q[2]
            why = []
            if dp < gap and abs(dz) < gdz:
                why.append("%.2f m in plan, %+.2f m up" % (dp, dz))
            for w, size in vols:
                inside, loc = in_volume(p, w, size)
                if inside:
                    why.append("inside its private volume (%.1f x %.1f x %.1f m; at (%.2f, %.2f, %.2f) in its frame)"
                               % (size + tuple(loc)))
            if not why or actor_of(scene, h, i) in own:
                continue
            b = scene.B[h]
            eid = b.tblu["subEntities"][i]["entityId"]
            out[(sid, h, eid)] = "%s at (%.2f, %.2f, %.2f): %s %016x \"%s\" (%s) at (%.2f, %.2f, %.2f): %s" % (
                desc, q[0], q[1], q[2], bb.gp_name(h), eid, b.entity_name(i),
                leaf(names.get(b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1], "")),
                p[0], p[1], p[2], "; ".join(why))
    return out


def privacy_report(scene, ov, base, base_ov, names):
    """(busy lines, lines Freelancer's own scene has too): privacy() on the variant's scene, with each finding that
    Freelancer's scene (base, its override base_ov; None to skip) shares, the same act by the same spot with both at
    the same place, moved to the second list."""
    found = privacy(scene, privacy_places(ov, names, scene), scene_acts(scene, names), names)
    same = set()
    if base is not None and base_ov is not None:
        bplaces = privacy_places(base_ov, names, base)
        vpos = {sid: q for _, sid, _, q, _, _ in privacy_places(ov, names, scene)}
        for (sid, h, eid), _ in privacy(base, bplaces, scene_acts(base, names), names).items():
            bq = next(q for _, s, _, q, _, _ in bplaces if s == sid)
            if sid not in vpos or math.dist(vpos[sid], bq) > SAME_M or h not in scene.B or h not in base.B:
                continue
            try:
                i = scene.B[h].index_of(eid)
            except be.BrickError:
                continue
            p, bp = scene.pos(h, i), base.pos(h, base.B[h].index_of(eid))
            if p is not None and bp is not None and math.dist(p, bp) < SAME_M:
                same.add((sid, h, eid))
    busy = [line for k, line in sorted(found.items(), key=lambda kv: kv[1]) if k not in same]
    vanilla = [line for k, line in sorted(found.items(), key=lambda kv: kv[1]) if k in same]
    return busy, vanilla


def scene_navmesh(newest, closure, dirs):
    """(hash, NavMesh) of the one NAVP the bricks of closure reference, read from dirs; (hash, None) when it is not
    extracted; None when there is not exactly one."""
    hs = {r for h in closure for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "NAVP"}
    if len(hs) != 1:
        return None
    h = hs.pop()
    path = find_bin(dirs, h, "NAVP")
    if path is None:
        return h, None
    with open(path, "rb") as f:
        return h, navp.NavMesh(f.read())


FLOAT_M = 0.1  # floating(): a place this far above the floor under it is listed
FLOAT_MAX_M = 0.5  # ... up to this: higher, it is on a table, a shelf or another floor


def floating(places, mesh, acts, litter=()):
    """[line] per place (not a table clue) that stands FLOAT_M to FLOAT_MAX_M above the walking floor under it: the
    navmesh floor there less the local navmesh offset (placement.Placement.floor_z, from the acts and the litter,
    which lie on the floor). A place snapped onto the navmesh and left there floats by that offset (as the full Icon's
    moved spots once did)."""
    judge = pl.Placement(mesh, None, acts, floor_refs=litter)
    out = []
    for desc, q in places:
        if q is None:
            continue
        hit = mesh.area_at(q, up=FLOAT_MAX_M + 0.5, down=0.3)
        if hit is None:
            continue
        floor = judge.floor_z((q[0], q[1], hit[1]))
        if FLOAT_M < q[2] - floor <= FLOAT_MAX_M:
            out.append("%s at (%.2f, %.2f, %.2f): %.2f m above the floor (navmesh %.2f, floor %.2f)" % (
                desc, q[0], q[1], q[2], q[2] - floor, hit[1], floor))
    return out


SUNK_M = 0.01  # sunk(): a floor place this far below the act or litter beside it is listed
SUNK_REF_M = 2.0  # ... the nearest act or litter within this in plan
SUNK_REF_DZ = 0.3  # ... and this in height (higher: a step, a shelf, another floor)
FLOOR_MAX_UP = 0.5  # sunk(): the navmesh floor under a reference, this far above or below it


def scene_floor_refs(scene, names, skip=()):
    """[(kind, description, world position)] of the acts and the litter (flat on the floor: its origin is the
    floor's height; not other small props, a garbage bag on a pile, a cup on a shelf) of the scene's bricks but skip
    (ours: our moved acts are placed as the spots are, so they are no witness) that are not gone: what stands on the
    floor, for sunk()."""
    out = []
    for h, b in scene.B.items():
        if h in skip:
            continue
        for i, e in enumerate(b.temp["subEntities"]):
            path = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")
            k = kind_of(path)
            if not (k == "act" or (k == "small" and "litter" in leaf(path))) or scene.gone(h, i):
                continue
            k = "act" if k == "act" else "litter"
            p = scene.pos(h, i)
            if p is not None and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                out.append((k, "%s %s \"%s\"" % (k, bb.gp_name(h), b.entity_name(i)), p))
    return out


def sunk(places, mesh, refs):
    """(sunk, sloped): [line] per floor place (with a mesh: navmesh floor from FLOAT_MAX_M below it to SUNK_REF_DZ
    above, so not a table clue) that lies more than SUNK_M below the nearest act or litter beside it (refs:
    scene_floor_refs; within SUNK_REF_M in plan and SUNK_REF_DZ in height). The acts and the litter stand on the floor,
    so a place below them is in it (the full Bad Boy's town-hall safe's documents, 3.4 cm under the basement floor,
    when placement.Placement.floor_z took the median navmesh offset of acts up to 15 m away). On a slope the
    nearest act is higher by right: when the navmesh rises from the place to it by as much (within SUNK_M), the line
    goes to sloped instead (the backstage street's clues, 0.13-0.21 m below acts 1 m up the street)."""
    out, sloped = [], []
    for desc, q in places:
        if q is None:
            continue
        here = mesh.area_at(q, up=SUNK_REF_DZ, down=FLOAT_MAX_M) if mesh is not None else None
        if mesh is not None and here is None:
            continue
        near = [(math.hypot(p[0] - q[0], p[1] - q[1]), k, d, p) for k, d, p in refs
                if abs(p[2] - q[2]) < SUNK_REF_DZ and math.hypot(p[0] - q[0], p[1] - q[1]) < SUNK_REF_M]
        if not near:
            continue
        dp, k, d, p = min(near)
        if p[2] - q[2] <= SUNK_M:
            continue
        line = "%s at (%.2f, %.2f, %.2f): %.3f m below the %s at (%.2f, %.2f, %.2f), %.2f m away in plan" % (
            desc, q[0], q[1], q[2], p[2] - q[2], d, p[0], p[1], p[2], dp)
        there = mesh.area_at(p, up=FLOOR_MAX_UP, down=FLOOR_MAX_UP) if mesh is not None else None
        if here is not None and there is not None:
            rise = there[1] - here[1]
            line += " (the navmesh rises %+.3f m)" % rise
            if p[2] - q[2] - rise <= SUNK_M:
                sloped.append(line)
                continue
        out.append(line)
    return out, sloped


def navmesh_notes(places, freelancer, variant):
    """[(description, lost, line)] for the places whose navmesh floor the variant's navmesh lost (lost True), or where
    its clearance (distance to the mesh's edge) dropped by NAV_DROP_M to under NAV_LOW_M: IOI bake props into the
    navmesh, so a new solid prop on a place cuts the floor under it. A place with no floor under Freelancer's navmesh
    either (a clue on a table, an exit button by a door) says nothing."""
    out = []
    for desc, q in places:
        if q is None:
            continue
        r = []
        for m in (freelancer, variant):
            hit = m.area_at(q, up=0.5, down=0.5)
            r.append(None if not hit else (hit[1], m.clearance((q[0], q[1], hit[1]))))
        f, v = r
        if f and not v:
            out.append((desc, True, "%s: floor under Freelancer's navmesh (clearance %.2f m), none under the "
                                    "variant's" % (desc, f[1])))
        elif f and v and v[1] < f[1] - NAV_DROP_M and v[1] < NAV_LOW_M:
            out.append((desc, False, "%s: clearance %.2f m under Freelancer's navmesh, %.2f m under the variant's" %
                        (desc, f[1], v[1])))
    return out


def load_built(built_dir, s):
    """Our two built bricks of spec s: {TEMP hash: Brick}, the override's hash."""
    with open(os.path.join(built_dir, bb.MANIFEST)) as f:
        entry = [v for v in json.load(f)["variants"] if v["name"] == s.NAME][0]
    built = {}
    for r in entry["resources"]:
        with open(os.path.join(built_dir, r["json"])) as f:
            built[(r["path"], r["type"])] = (int(r["rid"], 16), json.load(f), [(x, int(y, 16)) for x, y in r["refs"]])
    out = {}
    for path in (s.MISSION_PATH, s.OVERRIDE_PATH):
        th, t, tr = built[(path, "TEMP")]
        _, u, ur = built[(path, "TBLU")]
        out[th] = be.Brick(path.rsplit("/", 1)[-1], t, u, tr, ur, editable=False)
        out[th].hash = th
    return out, built[(s.OVERRIDE_PATH, "TEMP")][0]


def built_spec(s):
    """The spec whose built bricks a variant loads: its own, or for a layer (LAYER_OF) its base's."""
    if getattr(s, "LAYER_OF", None):
        return next(x for x in bb.VARIANTS if x.NAME == s.LAYER_OF)
    return s


def layer_temps(s):
    """The TEMP hashes a layer spec appends to its base's bricks (none for a variant of its own)."""
    return [temp for _, temp, _ in getattr(s, "LAYER_BRICKS", [])]


# A moved crate stands level (placement.CRATE_TILT_DEG; build_bricks.check_crates asks the same).
CRATE_TILT_DEG = pl.CRATE_TILT_DEG

# Doors and gates (placement.crate_in_the_way): a placed entity whose template's name has one of DOOR_WORDS as a word
# and none of NOT_DOOR_WORDS (a door's frame or handle, furniture with doors, sound and music gates, trigger switches).
DOOR_WORDS = {"door", "doors", "doorway", "gate", "gates"}
NOT_DOOR_WORDS = {"frame", "handle", "doorhandle", "cabinet", "closet", "bookcase", "wardrobe", "locker", "fridge",
                  "oven", "microwave", "cupboard", "shower", "nodoor", "ambience", "sound", "music", "trigger",
                  "triggerswitch", "decal", "sign"}


def is_door(path):
    t = (path or "").lower()
    if kind_of(t) == "logic":
        return False
    words = set(re.split(r"[^a-z]+", leaf(t).split(".")[0]))
    return bool(words & DOOR_WORDS) and not words & NOT_DOOR_WORDS


def scene_doors(scene, names):
    """[(description, position, yaw, (brick, entity id))] of the scene's doors and gates (is_door) still there."""
    out = []
    for h, b in scene.B.items():
        for i, e in enumerate(b.temp["subEntities"]):
            path = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")
            if not is_door(path) or scene.gone(h, i):
                continue
            w = scene.world(h, i)
            if w is None:
                continue
            eid = b.tblu["subEntities"][i]["entityId"]
            out.append(("door %016x \"%s\" (%s)" % (eid, b.entity_name(i), leaf(path)), (w[0][3], w[1][3], w[2][3]),
                        pl.yaw_of((w[0][0], w[1][0], w[2][0])), (h, eid)))
    return out


def crate_way(mesh, q, yaw, doors, base_mesh=None, base_doors=None):
    """(problems, notes) of placement.crate_in_the_way for a crate at q: a tight passage is a note; with base_mesh (a
    vanilla crate, against Freelancer's navmesh and base_doors), so is a finding that Freelancer's own scene has too
    (the same door, or a passage already narrow there)."""
    problems, notes = [], []
    found = pl.crate_in_the_way(mesh, q, yaw, doors)
    if not found:
        return problems, notes
    before = None
    if base_mesh is not None:
        before = {(d[3] if d else None) for d, _, _ in pl.crate_in_the_way(base_mesh, q, yaw, base_doors or [])}
    for d, why, hard in found:
        if before is not None and (d[3] if d else None) in before:
            notes.append(why + " (as under Freelancer)")
        elif not hard:
            notes.append(why)
        else:
            problems.append(why)
    return problems, notes


def crate_faces(scene, ov, names, mesh, base=None, base_ov=None, base_mesh=None):
    """(problems, notes): each supply crate a territory slot of ov names (ov.hash in scene), against mesh (the navmesh
    the scene loads). A crate our bricks moved or turned (against base_ov in base, Freelancer's own scene: more than
    5 cm or a degree off; no base: every crate) is a problem when placement.crate_problem finds its open side cramped
    (under 0.8 m of navmesh in front, or facing the tighter side), when it is tilted more than CRATE_TILT_DEG, or when
    it stands in the way (placement.crate_in_the_way: in a door's doorway, or narrowing a passage below 0.8 m; below
    1 m a note). A
    vanilla crate with under placement.CRATE_FRONT_M in front is a problem when base_mesh (Freelancer's navmesh) gives
    it that much (the option's navmesh takes the room away), else a note (as under Freelancer: the navmesh is cut
    round some crates); one in the way is a problem unless it is so in Freelancer's scene too (crate_way)."""
    problems, notes = [], []
    if mesh is None:
        return problems, notes
    doors = scene_doors(scene, names)
    base_doors = scene_doors(base, names) if base is not None else None
    way_mesh = (base_mesh if base_mesh is not None else mesh) if base is not None else None
    h = ov.hash
    for i, ks in sorted(slot_spots(ov, scene).items()):
        t = leaf(names.get(ov.temp_refs[ov.temp["subEntities"][i]["entityTypeResourceIndex"]][1], ""))
        if not t.startswith("setpiece_evergreen_stash_unique"):
            continue
        w = scene.world(h, i)
        if w is None:
            continue
        q = (w[0][3], w[1][3], w[2][3])
        yaw = pl.yaw_of((w[0][0], w[1][0], w[2][0]))
        tilt = math.degrees(math.acos(max(-1.0, min(1.0, w[2][2]))))
        desc = "stash %s (%s) at (%.2f, %.2f, %.2f)" % (ov.describe(i), ", ".join(ks), q[0], q[1], q[2])
        eid = ov.tblu["subEntities"][i]["entityId"]
        moved = True
        if base is not None and base_ov is not None:
            try:
                bw = base.world(base_ov.hash, base_ov.index_of(eid))
            except (KeyError, ValueError, be.BrickError):
                bw = None
            if bw is not None:
                byaw = pl.yaw_of((bw[0][0], bw[1][0], bw[2][0]))
                moved = math.dist(q, (bw[0][3], bw[1][3], bw[2][3])) > 0.05 or \
                    abs((yaw - byaw + 180.0) % 360.0 - 180.0) > 1.0
        if moved:
            bad = pl.crate_problem(mesh, q, yaw)
            if bad:
                best = pl.crate_yaw(mesh, q, yaw)
                problems.append("%s (moved): %s; yaw=%.1f gives %.2f m (placement.crate_yaw)" % (
                    desc, bad, best[0], best[1]))
            if tilt > CRATE_TILT_DEG:
                problems.append("%s (moved): tilted %.1f degrees (its vanilla slope kept; a yaw levels it)" % (
                    desc, tilt))
            way, way_notes = crate_way(mesh, q, yaw, doors)
            problems += ["%s (moved): %s" % (desc, why) for why in way]
            notes += ["%s (moved): %s" % (desc, why) for why in way_notes]
            continue
        way, way_notes = crate_way(mesh, q, yaw, doors, way_mesh, base_doors)
        problems += ["%s: %s" % (desc, why) for why in way]
        notes += ["%s: %s" % (desc, why) for why in way_notes]
        room = pl.crate_room(mesh, q, yaw)
        if room >= pl.CRATE_FRONT_M:
            continue
        before = pl.crate_room(base_mesh, q, yaw) if base_mesh is not None and base_mesh is not mesh else None
        if before is not None and before >= pl.CRATE_FRONT_M:
            problems.append("%s: its open side (yaw %.0f) has %.2f m of navmesh in front, %.2f m under Freelancer's "
                            "navmesh" % (desc, yaw, room, before))
        else:
            notes.append("%s: its open side (yaw %.0f) has %.2f m of navmesh in front (as under Freelancer: the "
                         "navmesh is cut round it)" % (desc, yaw, room))
    return problems, notes


def run_variant(s, args, newest, names, read, base, base_index=None):
    bs = built_spec(s)
    ours, ov_hash = load_built(args.built, bs)
    ov = ours[ov_hash]
    # What loads: the variant's scene, our two bricks and everything their external scenes load (and a layer's
    # bricks, appended after ours).
    loaded = bb.scene_closure(newest, names, [s.SCENE] + [h for b in ours.values() for h in b.scenes
                                                          if h not in ours] + layer_temps(s))
    unread = []
    bricks = {}
    for h in sorted(loaded, key=lambda x: (x != s.SCENE, x)):
        if h not in read:
            read[h] = read_brick(h, newest, args.json, args.bin or [])
        if read[h] is None:
            unread.append(h)
        else:
            bricks[h] = read[h]
    bricks.update(ours)
    own = loaded - set(base.B) - set(ours)
    print("=== %s (%s): the scene and our bricks load %d bricks, %d of them not loaded by Freelancer's scene" % (
        s.DISPLAY, bb.gp_name(s.SCENE), len(loaded), len(own)))
    print("Not read (no JSON or binary; their entities are not checked): %s" % (", ".join(
        "%s (%016X, %s%s)" % (bb.gp_name(h), h, newest[h]["package"], "" if h in own else "; Freelancer's too")
        for h in unread) or "none"))
    navs = None
    vn, fn = scene_navmesh(newest, loaded, args.bin or []), scene_navmesh(newest, set(base.B), args.bin or [])
    if vn and fn and vn[1] and fn[1]:
        navs = (fn[1], vn[1])
        print("Navmeshes compared: Freelancer's %016X, %s's %016X" % (fn[0], s.DISPLAY, vn[0]))
    else:
        print("Navmeshes NOT compared (extract the scenes' NAVP into a --bin folder): Freelancer's %s, %s's %s" % (
            "%016X" % fn[0] if fn else "?", s.DISPLAY, "%016X" % vn[0] if vn else "?"))
    fix = {(h, eid) for h, items in getattr(bs, "DELETE_SCENE_PROPS", {}).items() for eid, _, _ in items}
    summary = {"unread": len(unread), "navmesh": navs is not None}
    for title, extra in (("our override brick as built", set()), ("with %s.DELETE_SCENE_PROPS" % s.__name__, fix)):
        scene = Scene(bricks, extra)
        places = spots(ov, names, scene) + starts_and_exits(scene, set(ours))
        if not extra:
            print("Places checked (%d):" % len(places))
            for desc, q in places:
                print("  %s at %s" % (desc, "(%.2f, %.2f, %.2f)" % q if q else "NO PLACE"))
        content = new_content(scene, base, names, set(ours), base_index)
        nav = navmesh_notes(places, navs[0], navs[1]) if navs is not None else []
        clashes, notes = audit(scene, content, places, getattr(bs, "CLASH_ACCEPTED", {}),
                               [d for d, lost, _ in nav if lost])
        notes.sort(key=lambda x: not x.startswith("ACCEPTED"))
        print("== %s, %s: %d new placed entities, %d clashes" % (s.DISPLAY, title, len(content), len(clashes)))
        summary["clashes" if not extra else "clashes with fix"] = len(clashes)
        if not extra:
            busy, vanilla = privacy_report(scene, ov, base, base.B.get(bb.src_override(bs)[0]), names)
            print("== %s: privacy (acts within %.1f m of a supplier or in its private volume, within %.1f m of a safe "
                  "or stash): %d busy, %d as under Freelancer" % (s.DISPLAY, SUPPLIER_PRIVACY_M, SPOT_PRIVACY_M,
                                                                  len(busy), len(vanilla)))
            summary["busy"] = len(busy)
            for line in busy:
                print("  BUSY  " + line)
            for line in vanilla:
                print("  as under Freelancer  " + line)
        for line in clashes:
            print("  CLASH " + line)
        for line in notes:
            print("  near  " + line)
        for _, _, line in nav:
            print("  navmesh " + line)
        if vn and vn[1]:
            float_mesh = vn[1]
        else:  # the scene loads several navmeshes: the spec's own (NAVMESH), when extracted
            nh = getattr(bs, "NAVMESH", (None,))[0]
            path = find_bin(args.bin or [], nh, "NAVP") if nh else None
            float_mesh = None
            if path:
                with open(path, "rb") as f:
                    float_mesh = navp.NavMesh(f.read())
        refs = scene_floor_refs(scene, names, set(ours))
        if float_mesh is not None:
            fl = floating(places, float_mesh, [p for _, _, p in scene_acts(scene, names)],
                          [p for k, _, p in refs if k == "litter"])
            summary["floating" if not extra else "floating with fix"] = len(fl)
            for line in fl:
                print("  FLOATING " + line)
        sk, sloped = sunk(places, float_mesh, refs)
        summary["sunk" if not extra else "sunk with fix"] = len(sk)
        for line in sk:
            print("  SUNK " + line)
        for line in sloped:
            print("  sloped " + line)
        cramped, crate_notes = crate_faces(scene, ov, names, float_mesh, base, base.B.get(bb.src_override(bs)[0]),
                                           navs[0] if navs is not None else None)
        summary["crates" if not extra else "crates with fix"] = len(cramped)
        for line in cramped:
            print("  CRATE " + line)
        for line in crate_notes:
            print("  crate " + line)
        for (h, i), prop, x, y in scene.conflicts:
            print("  WARNING: %s %s: %s is overridden by both %s and %s (the later one is used)" % (
                bb.gp_name(h), scene.B[h].describe(i), prop, bb.gp_name(x), bb.gp_name(y)))
        for (h, sc), n in sorted(scene.unplaced.items()):
            if h in own:
                print("  not placed: %d entities of %s hang off %s (not read)" % (n, bb.gp_name(h), bb.gp_name(sc)))
        if not fix:
            break
    return summary


def print_missing(args, newest, names):
    loaded = bb.scene_closure(newest, names, [FREELANCER_SCENE])
    for name in args.variant or list(SPECS):
        s = SPECS[name]
        ours, _ = load_built(args.built, built_spec(s))
        loaded |= bb.scene_closure(newest, names, [s.SCENE] + [h for b in ours.values() for h in b.scenes
                                                               if h not in ours] + layer_temps(s))
    want = {}
    for h in sorted(loaded):
        tb = [r for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "TBLU"]
        for x, typ in [(h, "TEMP")] + [(r, "TBLU") for r in tb[:1]]:
            if bb.find_json(args.json, x, typ) is None and find_bin(args.bin or [], x, typ) is None:
                want.setdefault(newest[x]["package"].split("patch")[0].split(".")[0], []).append(x)
        for _, r in newest[h]["refs"]:
            if newest.get(r, {}).get("type") == "NAVP" and find_bin(args.bin or [], r, "NAVP") is None:
                want.setdefault(newest[r]["package"].split("patch")[0].split(".")[0], []).append(r)
    for chunk, hs in sorted(want.items()):
        print("%s: %s" % (chunk, " ".join("%016X" % h for h in sorted(set(hs)))))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--json", action="append", required=True, help="folder of ResourceTool JSON (repeatable)")
    ap.add_argument("--bin", action="append", help="folder of extracted binaries (tools/rpkg/rpkg.py extract; "
                                                   "read when a brick has no JSON; repeatable)")
    ap.add_argument("--built", required=True, help="build_bricks.py's --out (package_bricks.json and the bricks)")
    ap.add_argument("--cache", required=True, help="rpkg metadata folder")
    ap.add_argument("--names", required=True, help="hash list (hash.TYPE,path lines)")
    ap.add_argument("--variant", action="append", choices=sorted(SPECS), help="default: all four")
    ap.add_argument("--patch-level", type=int, default=4)
    ap.add_argument("--missing", action="store_true",
                    help="only print the hashes to extract (TEMP and TBLU of every brick either scene loads that has "
                         "no JSON or binary, and the scenes' navmeshes), by chunk, for tools/rpkg/rpkg.py extract")
    args = ap.parse_args()
    newest, _ = bb.gp.load_newest(args.cache, args.patch_level, (0, 26))
    names = bb.load_names(args.names)
    bb.NAMES.update(names)
    if args.missing:
        return print_missing(args, newest, names)

    read = {}
    fl = bb.scene_closure(newest, names, [FREELANCER_SCENE])
    fb = {}
    for h in sorted(fl, key=lambda x: (x != FREELANCER_SCENE, x)):
        read[h] = read_brick(h, newest, args.json, args.bin or [])
        if read[h] is not None:
            fb[h] = read[h]
    print("Freelancer's scene (%s): %d bricks, not read: %s" % (bb.gp_name(FREELANCER_SCENE), len(fl), ", ".join(
        "%s (%016X)" % (bb.gp_name(h), h) for h in sorted(fl) if h not in fb) or "none"))
    base = Scene(fb)
    base_index = placed_index(base, names)
    for name in args.variant or list(SPECS):
        run_variant(SPECS[name], args, newest, names, read, base, base_index)


if __name__ == "__main__":
    main()
