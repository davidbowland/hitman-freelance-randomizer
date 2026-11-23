#!/usr/bin/env python3
"""The clash audit of every map variant (src/core/Variants.cpp), map by map: what each variant adds to Freelancer's
map, against every place Freelancer spawns something or puts 47.

A variant is either appended (its option's bricks loaded after Freelancer's own scene: the elusive target layers, the
escalation sets, the dressings) or full (core/LandslideFull.*: a scene swap with our two bricks, or, for the full
Gluttony, the option's bricks plus our fix brick). Full scene swaps are audited by landslide_audit.run_variant (our built
bricks in --built, as on Sapienza); everything else here.

For an appended option on a map's mild (and, when offered there, showdown) scene, the scene is Freelancer's scene's
bricks followed by the option's bricks and everything they load. Checked:

1. The spots (landslide_audit.audit): every place a territory slot of the map's Freelancer override brick names (every
   stash, safe, supplier and money mule spot: all candidates, not only the ones a run picks) and the clues of those
   safes, every start of Freelancer's scene (a layer's own starts are not Freelancer's) and Freelancer's exit buttons,
   at the place the variant's bricks leave them (an override that moves one counts), against everything the option
   adds, moves or brings back: a solid prop within 0.6 m, a collision box or capsule that holds the place, and, with
   both navmeshes extracted, a prop within 1 m of a place whose floor the option's navmesh lost.
2. Privacy (landslide_audit.privacy_report): an act within 5 m of a supplier or inside its private volume, within 2 m
   of a safe or a stash, that Freelancer's own scene does not have.
3. Left in place (the Gluttony truck): an entity both scenes load that the option's official scene (its contract's
   ScenePath and bricks, from Peacock's contractdata, or the one scene that references the option's brick) deletes or
   moves, and Freelancer's scene keeps, within LEFT_M in plan and LEFT_DZ in height of one of the option's solid props.
4. On the set: a solid prop of Freelancer's own bricks (mission and override) within ON_M / ON_DZ of the option's.
5. Hanging: an option entity placed under (m_eidParent) an entity Freelancer's scene deletes and the official scene
   keeps.
6. Exits: a Freelancer exit button whose exit sequence (kept) names something the option deletes (the car it drives
   off in: The Guru's getaway car); a Freelancer exit (an exit_evergreen_* instance: push buttons, keys, items, disguises) the
   option deletes, itself or through a logical parent in its own brick (the game does not delete another brick's
   logical children: Lust's scenario_ambrosia deletes scenario_fox's Exit locations, and override_meyer's eight exits
   under it stay); a Freelancer exit whose exit sequence (its Exit output's pin override) the option deletes.
7. Sealed: with both navmeshes, a Freelancer start or exit that has floor under Freelancer's navmesh and none near it
   under the option's, or that moves off the main island (The Egg Hunt's navmesh drops Berlin's club, where
   Start_Evergreen_Bar is). Options without a navmesh of their own are not checked for this (only for a prop or a
   blocker on the place, 1.); each run lists the option's own starts. The same for every stash, safe, supplier and
   money mule spot (Reflection's navmesh has no floor near Hokkaido's garage Stash01, by exit 02) and every clue of a
   safe a slot names (Garden Show's closed mansion held all eight safes' clues).
8. Crates (landslide_audit.crate_faces): a supply crate a territory slot names opens on its local -Y side; one the
   fix brick moved or turned needs 0.8 m of navmesh in front of that side and must not face the tighter side
   (placement.crate_problem), nor keep a vanilla tilt; a vanilla one with under 0.8 m on the option's navmesh is a
   clash when Freelancer's navmesh gives it that much (a note otherwise: the navmesh is cut round some crates). The
   full variants get the same check (run_variant). In game Garden Show's Stash09 and The Icon's Fortress
   stash (the full Bad Boy) opened toward a wall. A crate is also not in the way (placement.crate_in_the_way): no
   part of it within 1.5 m of a door's line across the door's opening on the navmesh, and no passage narrowed below
   0.8 m of navmesh (below 1 m: a note); a vanilla crate only when Freelancer's own scene does not have it so (Garden Show's Stash05, moved, stood in
   the greenhouse's north doorway).
9. Trespassing (zone_findings): a start of Freelancer's scene inside an area (a zone's volume boxes) of a trespassing
   rule of the option's own bricks: hostile (deep trespassing) or trespassing; a note when the rule or zone is off at
   load. A stash, safe, supplier or mule spot inside one is a note. Each line names the areas Freelancer's own scene
   has there.
10. Company (start_company): a start of Freelancer's scene with an NPC or an act of the option's own bricks within
   START_NPC_M (plan; START_NPC_DZ in height).
11. Props by their extent (1., landslide_audit.audit's extent, Extents): a place (not a clue) inside a new prop's model
   bounds (its PRIMs' bounding boxes); when the model is not readable from the copied packages, a prop's origin within
   UNKNOWN_EXTENT_M, "extent unknown" (A Bitter Pill's fridge held Whittleton's Safe08, its origin 0.91 m away).
Freelancer's scene is the scene plus its contract's bricks (contractdata/SNUG/<MILD|HOT>/<GAME>.json: Hokkaido's
scene does not load mission_mild_shogi itself, nor do showdown scenes their mission_hot_<game>).
Notes: places near new things, navmesh clearance drops, and references of Freelancer's bricks into what the option
deletes (a start, a helper's NPC; not the target pool's actors, which the mod's variant check handles at load).

Each finding is a CLASH, unless a spec (FIX_SPECS: the option's fix brick's deletes) removes it. Lighting and time of
day options place nothing: their runs say so.

    python3 -I tools/landslide/variant_audit.py --map Chongqing --cache build-landslide/cache \\
        --names third_party/zhmtools/hash_list.txt --json build-landslide/json --bin build-landslide/audit/chongqing \\
        --deck build-landslide/deckcopy/chunk6 --deck build-landslide/deckcopy/chunk0sparse \\
        --deck build-landslide/deckcopy/chunk0 [--option the_rage] [--built build-landslide/built]

--deck: package folders; a brick or navmesh with neither JSON nor a binary in a --bin folder is extracted from them
into the first --bin folder (tools/rpkg/rpkg.py's names), so one command per map reads everything. --deck-files prints,
per map, the Runtime packages to copy from the Deck (with the sizes the metadata cache knows).
"""
import argparse
import glob
import json
import math
import os
import re
import struct
import sys
import zlib

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "rpkg"))

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import gluttony_spec  # noqa: E402
import landslide_audit as la  # noqa: E402
import rpkg  # noqa: E402
import spot_fix  # noqa: E402
import spotfix_spec  # noqa: E402

# Each map's partition: the chunk of Runtime/chunk<N>.rpkg (and its patches) that holds its Freelancer scenes, its
# Freelancer bricks and its variants' bricks. From the mod's own logs ("variant: <brick> resource ...: in 1 chunks
# [chunkN]", "scene resource <hash>: in 1 chunks [chunkN]") and the packages read here; None: not confirmed (no log
# line names it; see DECK_NOTES).
CHUNKS = {
    "Bangkok": 24, "Hokkaido": 22, "Sapienza": 26, "Paris": 27, "Haven": None, "Chongqing": 6, "Colorado": 23,
    "Dartmoor": 3, "Berlin": 4, "Whittleton Creek": 16, "Mendoza": 5, "Isle of Sgail": 15, "Santa Fortuna": 18,
    "Marrakesh": 25, "Dubai": None, "Miami": 19, "Mumbai": 17,
}
DECK_NOTES = {
    "Haven": "partition not confirmed: no log names a Haven brick's chunk. After one Haven load with the mod, the "
             "log line 'variant: scene resource 00E2F7B520C9074C: in 1 chunks [chunkN]' names it",
    "Dubai": "partition not confirmed (The Ascensionist's scenario_gibson is in chunk0, Freelancer's Dubai bricks are "
             "not): after one Dubai load, 'variant: scene resource 00AE8BB177E31C93: in 1 chunks [chunkN]' names it",
    "Miami": "chunk19 from scenario_sambuca2's place (Variants.cpp); the scene's own line has not been logged yet",
}

# The full versions' specs, by FullVariant key (LandslideFull.cpp).
FULL_SPECS = {s.NAME: s for s in bb.VARIANTS}

# The option fix specs: an option whose appended bricks are followed by a fix brick of ours (its DELETES; a spot fix's
# moves and re-points too, spot_fix.audit_brick).
FIX_SPECS = dict({"gluttony": gluttony_spec}, **{s.OPTION: s for s in spotfix_spec.SPECS})

# Left in place: an entity the official scene removes within this of one of the option's solid props (plan, height);
# closer than LEFT_CLASH_M it is a clash, farther a note (a canopy tent is about 3 m across: the Gluttony truck was
# 0.3 m from its tent).
LEFT_M = 3.0
LEFT_CLASH_M = 1.0
LEFT_DZ = 2.5
# On the set: a solid prop of Freelancer's bricks this close to one of the option's (the two in the same place).
ON_M = 0.3
ON_DZ = 0.4


def rid(path):
    return bb.gp.rid(path)


# --- Variants.cpp and LandslideFull.cpp ------------------------------------------------------------------------------

def parse_variants(src):
    """(maps, options, full): maps {map: {"mild"|"hot": (scene path, [option names])}}, options {name: {"bricks":
    [(name, path, hash)], "check": bool, "contract": "SAPIENZA/ELUSIVE/THECONGRESSMAN" or None}}, full {option name:
    FullVariant key}."""
    with open(os.path.join(src, "Variants.cpp"), encoding="utf-8") as f:
        text = f.read()
    bricks = {}
    for m in re.finditer(r'const VariantBrick (k\w+)\s*\{\s*"([^"]+)",\s*"([^"]+)",\s*0x([0-9A-Fa-f]+)ull\s*\}', text):
        bricks[m.group(1)] = (m.group(2), m.group(3), int(m.group(4), 16))
    options, by_ident = {}, {}
    for m in re.finditer(r'const VariantOption (k\w+)\s*\{\s*"(\w+)",\s*\{([^}]*)\}\s*,\s*(\d+)\s*(?:,\s*(true|false))?',
                         text):
        lines = text[:m.start()].rstrip("\n").split("\n")
        k = len(lines)
        while k > 0 and lines[k - 1].lstrip().startswith("//"):
            k -= 1
        block = "\n".join(lines[k:])  # the comment right above the definition
        c = re.search(r"Never targets: ([A-Z0-9_/]+?)\.?\s*(?:\(|$)", block, re.M)
        options[m.group(2)] = {"bricks": [bricks[k] for k in re.findall(r"&(k\w+)", m.group(3))],
                               "check": m.group(5) == "true", "contract": c.group(1) if c else None}
        by_ident[m.group(1)] = m.group(2)
    lists = {m.group(1): re.findall(r"&(k\w+)", m.group(2))
             for m in re.finditer(r"static const Options (k\w+)\s*\{([^}]*)\};", text)}
    maps = {}
    for m in re.finditer(r'\{"([^"]+) (mild|hot)",\s*"([^"]+)",\s*(k\w+|\{[^}]*\})\}', text):
        idents = lists[m.group(4)] if m.group(4).startswith("k") else re.findall(r"&(k\w+)", m.group(4))
        maps.setdefault(m.group(1), {})[m.group(2)] = (m.group(3), [by_ident[k] for k in idents])
    with open(os.path.join(src, "LandslideFull.cpp"), encoding="utf-8") as f:
        ftext = f.read()
    full = {m.group(3): m.group(1) for m in
            re.finditer(r'constexpr FullVariant k\w+\s*\{\s*"(\w+)",\s*"([^"]*)",\s*"(\w+)"', ftext)}
    return maps, options, full


def contract(contracts, name):
    """(scene path, [brick paths]) of a Peacock contract file (contractdata/<name>.json), or None."""
    if not contracts or not name:
        return None
    path = os.path.join(contracts, *name.split("/")) + ".json"
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    return d["Metadata"].get("ScenePath"), list(d.get("Data", {}).get("Bricks") or [])


# --- Reading ---------------------------------------------------------------------------------------------------------

class Reader:
    """Bricks and navmeshes of one map: ResourceTool JSON (--json), binaries (--bin), else extracted from the
    packages (--deck) into the first --bin folder."""

    def __init__(self, args, chunk, newest):
        self.args, self.chunk, self.newest = args, chunk, newest
        self.json = args.json or []
        self.bins = args.bin or []
        self.read = {}
        self.res = None
        self.failed = {}

    def resolver(self):
        if self.res is None and self.args.deck:
            self.res = rpkg.Resolver(self.args.deck, [self.chunk, 0], self.args.cache, vanilla=True,
                                     patch_level=self.args.patch_level)
        return self.res

    def extract(self, h, typ):
        """Extract h into the first --bin folder; its path, or None (not in a --deck package, or not readable there:
        a package of the local copy that holds only some byte ranges)."""
        r = self.resolver()
        if r is None or not self.bins:
            return None
        hit = r.get(h)
        if hit is None:
            self.failed[h] = "in no --deck package"
            return None
        pkg, e = hit
        try:
            data = pkg.read(e)
        except (rpkg.RpkgError, ValueError, IndexError) as x:
            self.failed[h] = "%s: %s" % (pkg.name, x)
            return None
        os.makedirs(self.bins[0], exist_ok=True)
        path = os.path.join(self.bins[0], "%s_%016X.%s" % (pkg.stem, h, e.type))
        with open(path + ".tmp", "wb") as f:
            f.write(data)
        os.replace(path + ".tmp", path)
        return path

    def have(self, h, typ):
        if typ in ("TEMP", "TBLU") and bb.find_json(self.json, h, typ) is not None:
            return True
        return la.find_bin(self.bins, h, typ) is not None or self.extract(h, typ) is not None

    def brick(self, h):
        if h not in self.read:
            tb = [r for _, r in self.newest[h]["refs"] if self.newest.get(r, {}).get("type") == "TBLU"]
            b = None
            if len(tb) == 1 and self.have(h, "TEMP") and self.have(tb[0], "TBLU"):
                try:
                    b = la.read_brick(h, self.newest, self.json, self.bins)
                except Exception as x:  # a resource the binary reader cannot decode
                    self.failed[h] = "not decoded: %s" % x
                    b = None
            self.read[h] = b
        return self.read[h]

    def navmesh(self, h):
        if la.find_bin(self.bins, h, "NAVP") is None and self.extract(h, "NAVP") is None:
            return None
        with open(la.find_bin(self.bins, h, "NAVP"), "rb") as f:
            return la.navp.NavMesh(f.read())


def navps(newest, closure):
    return {r for h in closure for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "NAVP"}


def load(reader, closure, first):
    """{hash: Brick} of the closure's read bricks (first ones first, then by hash) and the unread hashes."""
    out, unread = {}, []
    for h in list(first) + sorted(set(closure) - set(first)):
        if h in out or h not in closure:
            continue
        b = reader.brick(h)
        if b is None:
            unread.append(h)
        else:
            out[h] = b
    return out, unread


# --- The checks beyond landslide_audit's ------------------------------------------------------------------------------

def solid(names, b, i):
    t = b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1]
    return la.kind_of(names.get(t, "")) in ("prop", "blocker")


def option_props(scene, names, own):
    """[(brick, index, position)] of the solid props (not loose small ones) of the option's own bricks that the
    scene keeps."""
    out = []
    for h in own:
        if h not in scene.B:
            continue
        b = scene.B[h]
        for i in range(len(b.temp["subEntities"])):
            t = b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1]
            if la.kind_of(names.get(t, "")) != "prop" or i == b.temp["rootEntityIndex"] or scene.gone(h, i):
                continue
            p = scene.pos(h, i)
            if p is not None and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                out.append((h, i, p))
    return out


class Grid:
    def __init__(self, items, cell=3.0):
        self.cell, self.g = cell, {}
        for it in items:
            p = it[-1]
            self.g.setdefault((int(p[0] // cell), int(p[1] // cell)), []).append(it)

    def near(self, p, gap, dz):
        """The nearest item within gap in plan and dz in height: (distance, item), or None."""
        best = None
        cx, cy = int(p[0] // self.cell), int(p[1] // self.cell)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for it in self.g.get((cx + dx, cy + dy), ()):
                    q = it[-1]
                    d = math.hypot(p[0] - q[0], p[1] - q[1])
                    if d < gap and abs(p[2] - q[2]) < dz and (best is None or d < best[0]):
                        best = (d, it)
        return best


def describe(scene, names, h, i):
    b = scene.B[h]
    t = b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1]
    return "%s %016x \"%s\" (%s)" % (bb.gp_name(h), b.tblu["subEntities"][i]["entityId"], b.entity_name(i),
                                     la.leaf(names.get(t, "")))


def left_in_place(scene, official, names, props, own):
    """(clash lines, note lines): entities of bricks both scenes load (not the option's own) that the official scene
    deletes or moves and our scene keeps where it was, near one of the option's solid props."""
    grid = Grid(props)
    clashes, notes = [], []
    for h, b in scene.B.items():
        if h in own or h not in official.B:
            continue
        for i in range(len(b.temp["subEntities"])):
            if scene.gone(h, i) or not solid(names, b, i):
                continue
            p = scene.pos(h, i)
            if p is None:
                continue
            if official.gone(h, i):
                why = "deleted by the official scene"
            else:
                q = official.pos(h, i)
                if q is None or math.dist(p, q) < la.SAME_M:
                    continue
                why = "moved by the official scene to (%.2f, %.2f, %.2f)" % q
            hit = grid.near(p, LEFT_M, LEFT_DZ)
            if hit is None:
                continue
            d, (h2, j, q) = hit
            line = "%s at (%.2f, %.2f, %.2f), %s, is %.2f m from the option's %s at (%.2f, %.2f, %.2f)" % (
                describe(scene, names, h, i), p[0], p[1], p[2], why, d, describe(scene, names, h2, j), q[0], q[1],
                q[2])
            (clashes if d < LEFT_CLASH_M else notes).append(line)
    return clashes, notes


def on_the_set(scene, names, props, ours):
    """Clash lines: a solid prop or set piece of Freelancer's bricks within ON_M / ON_DZ of one of the option's."""
    grid = Grid(props)
    out = []
    for h in ours:
        if h not in scene.B:
            continue
        b = scene.B[h]
        for i in range(len(b.temp["subEntities"])):
            t = b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1]
            if la.kind_of(names.get(t, "")) not in ("prop", "setpiece") or scene.gone(h, i):
                continue
            p = scene.pos(h, i)
            if p is None or (abs(p[0]) < 0.01 and abs(p[1]) < 0.01):
                continue
            hit = grid.near(p, ON_M, ON_DZ)
            if hit:
                d, (h2, j, q) = hit
                out.append("%s at (%.2f, %.2f, %.2f) is %.2f m from the option's %s at (%.2f, %.2f, %.2f)" % (
                    describe(scene, names, h, i), p[0], p[1], p[2], d, describe(scene, names, h2, j), q[0], q[1],
                    q[2]))
    return out


def hanging(scene, official, names, own):
    """Lines: an entity of the option's bricks whose m_eidParent is an entity our scene deletes and the official
    scene keeps (it would be placed off a deleted entity)."""
    out = []
    for h in own:
        if h not in scene.B:
            continue
        b = scene.B[h]
        for i in range(len(b.temp["subEntities"])):
            if scene.gone(h, i) or not solid(names, b, i):
                continue
            par, ctx = scene.prop(h, i, "m_eidParent")
            if not be.is_ref(par) or par["externalSceneIndex"] < 0:
                continue
            k = scene.resolve(ctx, par)
            if k is None or not scene.gone(k[0], k[1]):
                continue
            if official is not None and k[0] in official.B and official.gone(k[0], k[1]):
                continue
            out.append("%s hangs off %s, which Freelancer's scene deletes" % (
                describe(scene, names, h, i), describe(scene, names, k[0], k[1])))
    return out


def dangling(scene, base, names, ours):
    """Note lines: a reference of Freelancer's bricks (a property, a property override, a pin override) into an
    entity the option deletes (gone in our scene, there in Freelancer's): a start, an NPC a helper names, a prop
    Freelancer moves. Notes, not clashes: nothing is placed on anything. The target pool's actors (Spawn_Targets'
    m_aActors) are left out: the mod's variant check handles a pool actor an option deletes at load."""
    out = {}
    for h in ours:
        if h not in scene.B:
            continue
        b = scene.B[h]
        found = []
        for i, e in enumerate(b.temp["subEntities"]):
            if scene.gone(h, i):
                continue
            for p in e.get("propertyValues", []) + e.get("postInitPropertyValues", []):
                refs = []
                be.walk_refs(p.get("value"), refs)
                if p["nPropertyID"] != "m_aActors":
                    found += [("%s %s" % (b.describe(i), p["nPropertyID"]), r) for r in refs]
        for o in b.temp.get("propertyOverrides", []):
            refs = [o["propertyOwner"]]
            be.walk_refs(o["propertyValue"].get("value"), refs)
            found += [("property override %s" % o["propertyValue"]["nPropertyID"], r) for r in refs]
        for o in b.tblu.get("pinConnectionOverrides", []):
            for side in ("fromEntity", "toEntity"):
                found.append(("pin override %s -> %s" % (o["fromPinName"], o["toPinName"]), o[side]))
        for where, r in found:
            if not be.is_ref(r) or r["externalSceneIndex"] < 0:
                continue
            k = scene.resolve(h, r)
            if k is None or not scene.gone(k[0], k[1]) or k[0] not in base.B or base.gone(k[0], k[1]):
                continue
            out.setdefault(k, []).append(where)
    return ["%s (deleted by the option) is named by %s: %s" % (
        describe(scene, names, k[0], k[1]), bb.gp_name(next(iter(ours))) if len(ours) == 1 else "Freelancer's bricks",
        "; ".join(sorted(set(w))[:6]) + (" and %d more" % (len(set(w)) - 6) if len(set(w)) > 6 else ""))
        for k, w in sorted(out.items())]


def subtree_of(b, i):
    """Indexes of b's entity i and everything logically under it (in b)."""
    kids = getattr(b, "_kids", None)
    if kids is None:
        kids = {}
        for j, e in enumerate(b.temp["subEntities"]):
            lp = e["logicalParent"]
            if lp["externalSceneIndex"] < 0 and lp["entityIndex"] >= 0 and lp["entityIndex"] != j:
                kids.setdefault(lp["entityIndex"], []).append(j)
        b._kids = kids
    out, stack = set(), [i]
    while stack:
        j = stack.pop()
        if j not in out:
            out.add(j)
            stack += kids.get(j, [])
    return out


# Sealed: a start or exit with floor within SEALED_ON_M under Freelancer's navmesh and none within SEALED_OFF_M under
# the option's (an area the option removes or walls off: The Egg Hunt's navmesh has no club below ground, where
# Freelancer's Start_Evergreen_Bar is), or on Freelancer's main island but a lesser island of the option's.
SEALED_ON_M = 2.0
# An option navmesh with fewer areas than this share of Freelancer's is an extra mesh loaded beside the map's (Dartmoor
# Harbinger's 649 areas, all 10-20 m underground, against 10089), not the map's replacement: not compared.
EXTRA_NAV_SHARE = 0.5
SEALED_OFF_M = 3.0


SEALED_SPOTS = ("stash ", "safe ", "supplier ", "mule ", "clue ")


def sealed(places, freelancer, variant, kinds=("start ", "exit ")):
    """[line] for each start or exit (places' "start ..."/"exit ...", or the given kinds: SEALED_SPOTS for the
    territory's spots) the option's navmesh cuts off; with both navmeshes. Islands ignore off-mesh links (ladders,
    vaults), so only a change against Freelancer's mesh counts."""
    def main(m):
        isl = m.islands()
        return max(isl, key=isl.get) if isl else None
    fmain, vmain = main(freelancer), main(variant)
    out = []
    for desc, q in places:
        if q is None or not desc.startswith(tuple(kinds)):
            continue
        f = freelancer.snap(q, min_clear=0.0, radius=SEALED_ON_M)
        if f is None:
            continue
        v = variant.snap(q, min_clear=0.0, radius=SEALED_OFF_M)
        if v is None:
            out.append("%s at (%.1f, %.1f, %.1f): floor %.1f m away under Freelancer's navmesh, none within %.1f m "
                       "under the option's (an area it removes or seals off)" % ((desc,) + tuple(q) +
                                                                                 (f[3], SEALED_OFF_M)))
        elif f[1].island == fmain and v[1].island != vmain:
            isl = variant.islands()
            out.append("%s at (%.1f, %.1f, %.1f): on the option's navmesh island %d (%d areas), not its main one (%d "
                       "areas)" % (desc, q[0], q[1], q[2], v[1].island, isl[v[1].island], isl[vmain]))
    return out


def hero_spawns(scene, bricks, deleted):
    """[(brick, index, description, position, default)] of the hero spawns of these bricks that the scene deletes
    (deleted True; position as placed) or keeps (False)."""
    out = []
    for h in sorted(bricks):
        b = scene.B.get(h)
        if b is None:
            continue
        for i, e in enumerate(b.temp["subEntities"]):
            if b.temp_refs[e["entityTypeResourceIndex"]][1] != la.HERO_SPAWN_TYPE or scene.gone(h, i) != deleted:
                continue
            if not deleted and scene.prop(h, i, "m_bHide")[0] is True:
                continue
            out.append((h, i, "%s %s" % (bb.gp_name(h), b.describe(i)), scene.pos(h, i),
                        scene.prop(h, i, "m_bDefaultHeroSpawn")[0] is True))
    return out


# src/core/HeroStart.*: kSameSpotMeters (a kept spawn this close to a deleted one in KnownDeletedSpawns takes its
# place), the deleted spawns it knows, and kOffMapHeightMeters (a spawn this far up or down is never re-pointed).
SAME_SPOT_M = 3.0
KNOWN_DELETED_SPAWNS = {0x5901C4EB74132892}  # snow_festival: agencymarkup_snowcrane's HeroSpawn_Restaurant
OFF_MAP_Z = 250.0


def deleted_starts(scene, fl, own):
    """[line] per start of Freelancer's scene that the option deletes, with the spawn the mod's start fix
    (HeroStart.cpp DecideHeroStart) re-points when a campaign asks for it: the kept spawn on its spot (only for a spawn
    in KnownDeletedSpawns), else a default spawn (the option's first, then the other bricks'), else any spawn (the
    option's first). Spawns more than OFF_MAP_Z up or down are skipped, as the fix skips them."""
    kept = [x for x in hero_spawns(scene, set(scene.B), False) if x[3] is not None and abs(x[3][2]) <= OFF_MAP_Z]
    order = [x for x in kept if x[4] and x[0] in own] + [x for x in kept if x[4] and x[0] not in own] + \
        [x for x in kept if x[0] in own] + [x for x in kept if x[0] not in own]
    out = []
    for h, i, desc, p, _ in hero_spawns(scene, fl, True):
        eid = scene.B[h].tblu["subEntities"][i]["entityId"]
        near = sorted(((math.dist(p, x[3]), k) for k, x in enumerate(kept)), key=lambda t: t[0]) if p else []
        if eid in KNOWN_DELETED_SPAWNS and near and near[0][0] <= SAME_SPOT_M:
            out.append("%s -> %s, %.1f m away (same spot)" % (desc, kept[near[0][1]][2], near[0][0]))
            continue
        if not order:
            out.append("%s -> no spawn to re-point (47 may start at the level's placeholder)" % desc)
            continue
        x = order[0]
        d = math.dist(p, x[3]) if p else None
        out.append("%s -> %s%s at (%.1f, %.1f, %.1f), %s m away%s" % (
            desc, x[2], " (default)" if x[4] else "", x[3][0], x[3][1], x[3][2], "%.0f" % d if d is not None else "?",
            "" if d is not None and d <= SAME_SPOT_M else ": NOT where the campaign's start is"))
    return out


# Freelancer's exits: every instance of an exit_evergreen_* template in its bricks (pushbutton, usekey, useitem,
# usedisguise, area_crowd, ...), whatever its name (Berlin's "Scooter exit Evergreen 1" is an exit_evergreen_usekey).
EXIT_TEMPLATE = re.compile(r"/exit_evergreen_[a-z0-9_]+\.entitytemplate\]")


def freelancer_exits(scene, ours, names):
    """[(description, position)] of Freelancer's exits (EXIT_TEMPLATE) in its bricks that the scene keeps, not hidden;
    a volume or other part of an exit (exit_evergreen_*_volumebox) is not one."""
    out = []
    for h in sorted(ours):
        b = scene.B.get(h)
        if b is None:
            continue
        for i, e in enumerate(b.temp["subEntities"]):
            t = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")
            if not EXIT_TEMPLATE.search(t) or "volumebox" in t or scene.gone(h, i) or \
                    scene.prop(h, i, "m_bHide")[0] is True:
                continue
            p = scene.pos(h, i)
            if p is not None:
                out.append(("exit %s %s" % (b.name, b.describe(i)), p))
    return out


def deleted_exits(scene, base, ours, names):
    """Lines: a Freelancer exit that Freelancer's scene has and the option's deletes (itself or a logical parent in its
    own brick, Scene.gone; Lust deletes scenario_fox's Exit locations, the logical parent of Berlin's eight, but
    they are override_meyer's, so they stay), with how many are left."""
    def exits(sc):
        return dict(freelancer_exits(sc, ours, names))
    had, kept = exits(base), exits(scene)
    gone = sorted(d for d in had if d not in kept)
    return ["%s at (%.1f, %.1f, %.1f): deleted by the option (%d of Freelancer's %d exits left%s)" % (
        (d,) + tuple(had[d]) + (len(kept), len(had), ": NO Freelancer exit, the mission cannot be left through "
                                "Freelancer's flow" if not kept else "")) for d in gone]


def gone_sequences(scene, base, ours, names):
    """Lines: a Freelancer exit the option keeps whose exit sequence (what its Exit output starts through a pin
    override into another brick) the option deletes: confirming it starts Freelancer's flow, but no sequence ends the
    level (The Reflection's Hokkaido exits; Lust's Berlin ones). The mod's exit fix sends the end itself
    (src/core/VariantExits.cpp: kBroken, after kExitGapSeconds; any other exit after kExitEndWaitSeconds)."""
    out = []
    for h in sorted(ours):
        b = scene.B.get(h)
        if b is None:
            continue
        for o in b.tblu.get("pinConnectionOverrides", []):
            f, t = o["fromEntity"], o["toEntity"]
            if o["fromPinName"] != "Exit" or f["externalSceneIndex"] >= 0 or f["entityIndex"] < 0 or \
                    t["externalSceneIndex"] < 0:
                continue
            i = f["entityIndex"]
            e = b.temp["subEntities"][i]
            tn = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")
            if not EXIT_TEMPLATE.search(tn) or "volumebox" in tn or scene.gone(h, i):
                continue
            k = scene.resolve(h, t)
            if k is None or not scene.gone(k[0], k[1]) or k[0] not in base.B or base.gone(k[0], k[1]):
                continue
            line = "exit %s %s starts %s (%s), deleted by the option: no sequence ends the level" % (
                b.name, b.describe(i), describe(scene, names, k[0], k[1]), o["toPinName"])
            if line not in out:
                out.append(line)
    return out


def broken_exits(scene, base, names, ours):
    """Lines: a Freelancer exit button (Exit_Evergreen_PushButton*) whose exit sequence (an entity of another brick its
    pin overrides start) is kept, but something that sequence names (a track's car) is deleted by the option and
    there under Freelancer: the button would start a sequence whose car is gone (as under The Icon)."""
    out = []
    for h in ours:
        if h not in scene.B:
            continue
        b = scene.B[h]
        for o in b.tblu.get("pinConnectionOverrides", []):
            ends = [o["fromEntity"], o["toEntity"]]
            local = [r for r in ends if r["externalSceneIndex"] < 0 and r["entityIndex"] >= 0]
            ext = [r for r in ends if r["externalSceneIndex"] >= 0]
            if len(local) != 1 or len(ext) != 1:
                continue
            i = local[0]["entityIndex"]
            j, depth = i, 0  # the button itself, or an entity under it (its exit trigger)
            while j is not None and depth < 8 and not b.entity_name(j).startswith("Exit_Evergreen_PushButton"):
                lp = b.temp["subEntities"][j]["logicalParent"]
                j = lp["entityIndex"] if lp["externalSceneIndex"] < 0 and lp["entityIndex"] >= 0 and \
                    lp["entityIndex"] != j else None
                depth += 1
            if j is None or depth >= 8 or scene.gone(h, i):
                continue
            i = j
            k = scene.resolve(h, ext[0])
            if k is None or scene.gone(k[0], k[1]):
                continue
            xb = scene.B[k[0]]
            for j in subtree_of(xb, k[1]):
                e = xb.temp["subEntities"][j]
                for p in e.get("propertyValues", []) + e.get("postInitPropertyValues", []):
                    refs = []
                    be.walk_refs(p.get("value"), refs)
                    for r in refs:
                        t = scene.resolve(k[0], r)
                        if t is None or not scene.gone(t[0], t[1]) or t[0] not in base.B or base.gone(t[0], t[1]):
                            continue
                        line = "%s starts %s, whose %s %s names %s, deleted by the option" % (
                            b.describe(i), describe(scene, names, k[0], k[1]), xb.entity_name(j), p["nPropertyID"],
                            describe(scene, names, t[0], t[1]))
                        if line not in out:
                            out.append(line)
    return out


# --- Trespassing areas, NPCs at the starts, props' extents ------------------------------------------------------------

TRESPASS_RULE = "ztrespassingruleentity.class"
# A start inside an area of a trespassing rule the option adds clashes (deep: hostile, shot on sight; else
# trespassing), unless the area is off at load (m_bEnabled false: a note); a stash, safe, supplier or mule spot
# inside one is a note. A Bitter Pill's "First Floor" (deep) holds Whittleton's Evergreen_Spawn_House, The Banker's
# "Basement Tresspass Shoot on sight" Paris's HeroSpawn_Basement_Kitchen and _Lockeroom.
ZONE_SPOTS = ("stash ", "safe ", "supplier ", "mule ")


def pvalue(scene, h, i, name):
    """(value, context brick) of a property by name, or by its crc when the binary reader only had that."""
    v, ctx = scene.prop(h, i, name)
    if v is None:
        v, ctx = scene.prop(h, i, "0x%08X" % zlib.crc32(name.encode()))
    return v, ctx


def trespass_zones(scene, names, bricks):
    """[(rule description, deep, note, zone name, [(box name, world matrix, size)])] of the trespassing rules
    (ZTrespassingRuleEntity) of these bricks that the scene keeps: each of its zones (m_Zones) with the volume boxes
    of the zone (m_Boxes, their m_vGlobalSize); note is "off at load" when the rule or the zone has m_bEnabled
    false."""
    out = []
    for h in sorted(bricks):
        b = scene.B.get(h)
        if b is None:
            continue
        for i, e in enumerate(b.temp["subEntities"]):
            if la.leaf(names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")) != TRESPASS_RULE or \
                    scene.gone(h, i):
                continue
            deep = pvalue(scene, h, i, "m_bDeepTrespassing")[0] is True
            zones, ctx = pvalue(scene, h, i, "m_Zones")
            for zr in zones or []:
                z = scene.resolve(ctx, zr) if be.is_ref(zr) else None
                if z is None or scene.gone(*z):
                    continue
                off = pvalue(scene, h, i, "m_bEnabled")[0] is False or \
                    pvalue(scene, z[0], z[1], "m_bEnabled")[0] is False
                boxes = []
                brefs, bctx = pvalue(scene, z[0], z[1], "m_Boxes")
                for br in brefs or []:
                    x = scene.resolve(bctx, br) if be.is_ref(br) else None
                    if x is None or scene.gone(*x):
                        continue
                    w, size = scene.world(*x), scene.prop(x[0], x[1], "m_vGlobalSize")[0]
                    if w is not None and isinstance(size, dict):
                        boxes.append((scene.B[x[0]].entity_name(x[1]), w, (size["x"], size["y"], size["z"])))
                out.append(("%s %016x \"%s\"" % (bb.gp_name(h), b.tblu["subEntities"][i]["entityId"], b.entity_name(i)),
                            deep, "off at load" if off else "", scene.B[z[0]].entity_name(z[1]), boxes))
    return out


def zones_at(q, zones):
    """[(rule description, deep, note, zone name, [box names])] of the zones whose boxes hold the place q (la.in_volume:
    a place on the floor under a box's bottom face counts)."""
    out = []
    for rule, deep, note, zone, boxes in zones:
        inside = [n for n, w, size in boxes if la.in_volume(q, w, size)[0]]
        if inside:
            out.append((rule, deep, note, zone, inside))
    return out


def zone_findings(places, zones, base_zones):
    """(clash lines, note lines): each Freelancer start (places' "start ...") inside a zone of a trespassing rule the
    option adds (zones: trespass_zones of the option's own bricks) is a clash, a note when the zone is off at load;
    each stash, safe, supplier or mule spot inside one is a note. Each line says the zones Freelancer's own scene has
    there (base_zones)."""
    clashes, notes = [], []
    for desc, q in places:
        if q is None or not desc.startswith(("start ",) + ZONE_SPOTS):
            continue
        hits = zones_at(q, zones)
        if not hits:
            continue
        had = ["%s%s" % (z, " (hostile)" if d else "") for _, d, _, z, _ in zones_at(q, base_zones)]
        for rule, deep, note, zone, boxes in hits:
            line = "%s at (%.2f, %.2f, %.2f): inside %s area \"%s\" (%s) of the option's rule %s%s; under " \
                   "Freelancer: %s" % (desc, q[0], q[1], q[2], "a HOSTILE (deep trespassing)" if deep else
                                        "a trespassing", zone, ", ".join(boxes), rule, " (%s)" % note if note else "",
                                        ", ".join(sorted(set(had))) or "no trespassing area")
            (clashes if desc.startswith("start ") and not note else notes).append(line)
    return clashes, notes


# A Freelancer start with an NPC or an act of the option's own bricks this close (plan; height): 47 starts in someone's
# face (A Bitter Pill's target Galen Vholes stands 0.91 m from Whittleton's Evergreen_Spawn_House).
START_NPC_M = 3.0
START_NPC_DZ = 2.0


def is_npc(path):
    """An NPC actor template (npcactor.entitytemplate and its kin under actors.template)."""
    t = (path or "").lower()
    return la.leaf(t).startswith("npcactor") or "/actors.template?/" in t


def start_company(scene, names, own, places):
    """[line] per Freelancer start (places' "start ...") with NPCs (is_npc) or acts of the option's own bricks within
    START_NPC_M in plan and START_NPC_DZ in height: the nearest few and how many."""
    who = []
    for h in sorted(own):
        b = scene.B.get(h)
        if b is None:
            continue
        for i, e in enumerate(b.temp["subEntities"]):
            path = names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "")
            what = "NPC" if is_npc(path) else "act" if la.kind_of(path) == "act" else None
            if what is None or scene.gone(h, i):
                continue
            p = scene.pos(h, i)
            if p is not None and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                who.append((what, h, i, p))
    grid = Grid(who)
    out = []
    for desc, q in places:
        if q is None or not desc.startswith("start "):
            continue
        near = []
        cx, cy = int(q[0] // grid.cell), int(q[1] // grid.cell)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for what, h, i, p in grid.g.get((cx + dx, cy + dy), ()):
                    d = math.hypot(p[0] - q[0], p[1] - q[1])
                    if d < START_NPC_M and abs(p[2] - q[2]) < START_NPC_DZ:
                        near.append((what != "NPC", d, what, h, i, p))
        if not near:
            continue
        near.sort(key=lambda x: (x[1], x[0]))
        npcs = sum(1 for x in near if x[2] == "NPC")
        out.append("%s at (%.2f, %.2f, %.2f): %d NPCs and %d acts of the option within %.1f m: %s" % (
            desc, q[0], q[1], q[2], npcs, len(near) - npcs, START_NPC_M, "; ".join(
                "%s %s %016x \"%s\" %.2f m%s" % (what, bb.gp_name(h), scene.B[h].tblu["subEntities"][i]["entityId"],
                                                 scene.B[h].entity_name(i), d, " %+.2f m up" % (p[2] - q[2])
                                                 if abs(p[2] - q[2]) >= 0.05 else "")
                for _, d, what, h, i, p in near[:4]) + ("; and %d more" % (len(near) - 4) if len(near) > 4 else "")))
    return out


class Extents:
    """A prop's model bounds in its own frame (landslide_audit.audit's extent): the union of the bounding boxes in the
    headers of the PRIMs its type (the template, and the templates it is made of, to EXTENT_DEPTH) references, read
    from a --bin folder or the --deck packages. None when its type is not in the mounted packages or a PRIM is not
    readable (the local copies hold only parts of chunk0 and no chunk1): the audit then says "extent unknown". The
    sub-entities' own transforms inside a template are not applied (a prop's model sits at its origin)."""
    EXTENT_DEPTH = 3

    def __init__(self, reader, newest):
        self.reader, self.newest = reader, newest
        self.by_type = {}
        self.read = {}  # PRIM hash -> bounds or None

    def prims(self, t, depth=0, seen=None):
        """The PRIM hashes type t references (through other TEMPs); None when t is not in the packages."""
        e = self.newest.get(t)
        if e is None:
            return None
        seen = seen if seen is not None else set()
        out = []
        for _, r in e.get("refs", ()):
            typ = self.newest.get(r, {}).get("type")
            if typ == "PRIM":
                out.append(r)
            elif typ == "TEMP" and depth < self.EXTENT_DEPTH and r not in seen:
                seen.add(r)
                out += self.prims(r, depth + 1, seen) or []
        return out

    def prim_bounds(self, h):
        if h not in self.read:
            data = None
            path = la.find_bin(self.reader.bins, h, "PRIM")
            if path is not None:
                with open(path, "rb") as f:
                    data = f.read()
            else:
                r = self.reader.resolver()
                hit = r.get(h) if r is not None else None
                if hit is not None:
                    try:
                        data = hit[0].read(hit[1])
                    except (rpkg.RpkgError, ValueError, IndexError, OSError):
                        data = None
            self.read[h] = prim_bounds(data) if data else None
        return self.read[h]

    def of_type(self, t):
        if t not in self.by_type:
            ps = self.prims(t)
            box = None
            if ps:
                bounds = [self.prim_bounds(p) for p in ps]
                if all(x is not None for x in bounds):
                    box = (tuple(min(x[0][k] for x in bounds) for k in range(3)),
                           tuple(max(x[1][k] for x in bounds) for k in range(3)))
            self.by_type[t] = box
        return self.by_type[t]

    def __call__(self, scene, h, i):
        b = scene.B[h]
        return self.of_type(b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1])


def prim_bounds(data):
    """((min x, y, z), (max x, y, z)) of a PRIM resource: the first word is the offset of its object header, whose
    bounding box follows its draw destination, pack type and type (4 bytes), property flags (2, and 2 of padding),
    bone rig index (4), object count (4) and object table offset (4). None when it does not read as one."""
    try:
        off = struct.unpack_from("<I", data, 0)[0]
        v = struct.unpack_from("<6f", data, off + 20)
    except struct.error:
        return None
    lo, hi = v[:3], v[3:]
    if not all(math.isfinite(x) for x in v) or any(hi[k] < lo[k] for k in range(3)) or \
            max(hi[k] - lo[k] for k in range(3)) > 1000.0:
        return None
    return lo, hi


# --- Runs ------------------------------------------------------------------------------------------------------------

def freelancer_start(newest, contracts, scene_path):
    """The TEMP hashes Freelancer's scene of scene_path loads from: the scene and its contract's bricks. The game loads
    a contract's Data.Bricks with its ScenePath (Peacock's contractdata/SNUG/<MILD|HOT>/<GAME>.json), and some scenes
    do not reference their mission brick themselves (Hokkaido's scene_snowcrane_mild_shogi loads mission_snowcrane
    only; its contract adds mission_mild_shogi, which loads override_shogi; showdown scenes such as Sgail's lack
    mission_hot_<game>). Without contractdata: the scene's folder's mission_<mild|hot>_<game>.brick when mounted."""
    m = re.match(r"scene_\w+?_(mild|hot)_(\w+)\.entity$", scene_path.rsplit("/", 1)[-1], re.I)
    start = [rid("[%s].pc_entitytemplate" % scene_path)]
    if not m:
        return start
    kind, game = m.group(1).lower(), m.group(2).lower()
    c = contract(contracts, "SNUG/%s/%s" % (kind.upper(), game.upper()))
    paths = c[1] if c and c[0] and c[0].lower() == scene_path.lower() else \
        ["%s/mission_%s_%s.brick" % (scene_path.rsplit("/", 1)[0], kind, game)]
    return start + [h for h in (rid("[%s].pc_entitytype" % p) for p in paths) if h in newest and h not in start]


def freelancer_bricks(newest, closure, scene_path, start=()):
    """(ours, override): Freelancer's own bricks of the scene (mission_<mild|hot>_<game>, override_<game>, and the
    contract's bricks in start after the scene) and the override's hash."""
    game = re.sub(r"\.entity$", "", scene_path.rsplit("/", 1)[-1]).split("_")[-1].lower()
    ours, ov = set(start[1:]), None
    for h in closure:
        n = bb.gp_name(h).lower()
        if n in ("mission_mild_%s.brick" % game, "mission_hot_%s.brick" % game, "override_%s.brick" % game):
            ours.add(h)
            if n.startswith("override_"):
                ov = h
    return ours, ov


def official_scene(ctx, opt, newest, names):
    """(description, closure) of the option's official scene: its contract's scene and bricks, else the scenes that
    reference its first brick; None when neither is found."""
    c = contract(ctx.args.contracts, opt["contract"])
    if c and c[0]:
        start = [rid("[%s].pc_entitytemplate" % c[0])] + [rid("[%s].pc_entitytype" % p) for p in c[1]]
        start = [h for h in start if h in newest]
        if start:
            return "%s (contract %s)" % (c[0].rsplit("/", 1)[-1], opt["contract"]), bb.scene_closure(newest, names,
                                                                                                     start)
    first = opt["bricks"][0][2]
    scenes = sorted(h for h, e in newest.items() if e.get("type") == "TEMP" and ".entity]" in names.get(h, "") and
                    any(r == first for _, r in e["refs"]))
    if scenes:
        return ", ".join(bb.gp_name(h) for h in scenes), bb.scene_closure(newest, names, scenes)
    return None


def run_appended(ctx, map_name, kind, scene_path, opt_name, opt):
    """The checks of an appended option on one Freelancer scene. Returns its summary dict."""
    newest, names, reader = ctx.newest, ctx.names, ctx.reader
    scene_hash = rid("[%s].pc_entitytemplate" % scene_path)
    title = "%s %s: %s" % (map_name, kind, opt_name)
    if scene_hash not in newest:
        print("=== %s: the scene %016X is in no package of the metadata cache" % (title, scene_hash))
        return {"title": title, "error": "scene not in cache"}
    start = freelancer_start(newest, ctx.args.contracts, scene_path)
    fl = bb.scene_closure(newest, names, start)
    # an outfits brick places nothing (Sloth's outfits_skunk, in chunk1, which is not copied): left out with a note
    skipped = [n for n, _, h in opt["bricks"] if h not in newest and n.startswith("outfits_")]
    opt_hashes = [h for n, _, h in opt["bricks"] if n not in skipped]
    missing = [n for n, _, h in opt["bricks"] if h not in newest and n not in skipped]
    if missing:
        print("=== %s: %s in no mounted package" % (title, ", ".join(missing)))
        return {"title": title, "error": "bricks not mounted"}
    var = fl | bb.scene_closure(newest, names, opt_hashes)
    own = var - fl
    ours, ov_hash = freelancer_bricks(newest, fl, scene_path, start)
    fix = FIX_SPECS.get(opt_name)
    extra = {(h, eid) for h, eid, _, _ in fix.DELETES} if fix else set()
    fb, fl_unread = load(reader, fl, start)
    vb, var_unread = load(reader, var, start + sorted(fl) + opt_hashes)
    own_unread = [h for h in var_unread if h in own]
    print("=== %s (%s): %d bricks of Freelancer's scene, %d added by the option (%s)" % (
        title, bb.gp_name(scene_hash), len(fl), len(own), ", ".join(bb.gp_name(h) for h in sorted(own))))
    if skipped:
        print("Left out (an outfits brick in no mounted package; outfits place nothing): %s" % ", ".join(skipped))
    print("Not read: %s" % (", ".join("%s (%016X%s)" % (bb.gp_name(h), h, "; the option's" if h in own else "")
                                      for h in var_unread) or "none"))
    summary = {"title": title, "unread_own": len(own_unread), "unread": len(var_unread)}
    if ov_hash is None or ov_hash not in vb:
        print("NOT CHECKED: Freelancer's override brick of %s is not read" % bb.gp_name(scene_hash))
        summary["error"] = "override not read"
        return summary
    base = Scene(fb)
    out = {}
    runs = [("as appended", set())]
    if fix:
        runs.append(("with the fix brick's deletes (%s)" % fix.__name__, extra))
    for label, deletes in runs:
        bricks = vb
        if label != "as appended" and getattr(fix, "SPOT_FIX", False):
            bricks = dict(vb)
            bricks.update(spot_fix.audit_brick(fix, vb, names, Scene))
        scene = Scene(bricks, deletes)
        ov = vb[ov_hash]
        places = la.spots(ov, names, scene) + la.starts_and_exits(scene, ours, start_bricks=set(fl),
                                                                  exit_prefix=None) + \
            freelancer_exits(scene, ours, names)
        content = la.new_content(scene, base, names, set(), ctx.base_index(scene_hash, base))
        placed = [c for c in content if c[0] in own]
        navs = None
        fn, vn = navps(newest, fl), navps(newest, var) - navps(newest, fl)
        extra_nav = None
        if len(fn) == 1 and len(vn) == 1:
            a, v = reader.navmesh(next(iter(fn))), reader.navmesh(next(iter(vn)))
            if a is not None and v is not None:
                if len(v.areas) >= EXTRA_NAV_SHARE * len(a.areas):
                    navs = (a, v)
                else:
                    extra_nav = "the option's %016X has %d areas against Freelancer's %d: an extra mesh (an " \
                        "underground part, as Harbinger's), not a replacement; not compared" % (
                            next(iter(vn)), len(v.areas), len(a.areas))
        nav = la.navmesh_notes(places, navs[0], navs[1]) if navs else []
        clashes, notes = la.audit(scene, content, places, getattr(fix, "CLASH_ACCEPTED", {}),
                                  [d for d, lost, _ in nav if lost],
                                  (lambda h, i: ctx.extents(scene, h, i)) if ctx.extents else None)
        zoned, zone_notes = zone_findings(places, trespass_zones(scene, names, own),
                                          trespass_zones(base, names, set(base.B)))
        company = start_company(scene, names, own, places)
        own_starts = [x for x in la.starts_and_exits(scene, ours, start_bricks=own) if x[0].startswith("start ")]
        seal = sealed(places + own_starts, navs[0], navs[1]) + sealed(places, navs[0], navs[1], SEALED_SPOTS) \
            if navs else []
        if label == "as appended":
            print("Places checked: %d (%s); navmesh: %s" % (
                len(places), ", ".join("%d %s" % (sum(1 for d, _ in places if d.startswith(k + " ")), k) for k in
                                       ("stash", "safe", "clue", "supplier", "mule", "start", "exit")),
                "the option's %016X against Freelancer's %016X" % (next(iter(vn)), next(iter(fn))) if navs else
                extra_nav if extra_nav else ("no navmesh of its own" if not vn else "NOT compared (%s)" % ", ".join("%016X" % h for h in vn | fn))))
            print("Supplier spots (a campaign enables Supplier_GivenNumber of them, picked by its seed; every one is "
                  "checked): %s" % "; ".join("%s at (%.2f, %.2f, %.2f)" % ((d,) + q) for d, q in places
                                             if d.startswith("supplier ") and q))
            print("The option's own starts (hero spawns of its bricks; the mod's start fix re-points one when the "
                  "option deletes the contract's): %s" % ("; ".join("%s at (%.2f, %.2f, %.2f)" % ((d,) + q)
                                                                    for d, q in own_starts) or "none"))
            moved = deleted_starts(scene, fl, own)
            print("Starts of Freelancer's scene the option deletes (and where the mod's start fix puts 47 instead): "
                  "%s" % ("none" if not moved else ""))
            for line in moved:
                print("  START " + line)
            print("New or moved placed entities: %d (%d of the option's own bricks)" % (len(content), len(placed)))
            if not content:
                print("  nothing placed: a lighting or time of day option (nothing to clash)")
        props = option_props(scene, names, own)
        off = official_scene(ctx, opt, newest, names) if label == "as appended" else ctx.last_official
        left, left_notes, hang = [], [], []
        if off:
            od, oc = off
            ob, _ = load(reader, oc, [])
            official = Scene(ob)
            left, left_notes = left_in_place(scene, official, names, props, own)
            hang = hanging(scene, official, names, own)
            if label == "as appended":
                ctx.last_official = off
                print("Official scene: %s (%d bricks, %d read)" % (od, len(oc), len(ob)))
        elif label == "as appended":
            print("Official scene: not found (no contract, no scene references %s): left-in-place not checked" %
                  opt["bricks"][0][0])
        onset = on_the_set(scene, names, props, ours)
        dang = broken_exits(scene, base, names, ours)
        gone_exits = deleted_exits(scene, base, ours, names)
        gone_seq = gone_sequences(scene, base, ours, names)
        named = dangling(scene, base, names, ours)
        # a hero spawn, its director or its sequence director that Freelancer's bricks name, deleted (The Egg Hunt
        # deletes scenario_fox's HeroSpawnDirector): a note on the start, as the start fix handles deleted spawns
        start_named = [x for x in named if re.search(r'\((herospawn\w*)\.entitytemplate\)', x)]
        named = [x for x in named if x not in start_named]
        busy, vanilla = la.privacy_report(scene, ov, base, base.B.get(ov_hash), names)
        # Supply crates: the open side of each one the fix moved, and of every other one on the option's navmesh.
        crate_mesh = navs[1] if navs else reader.navmesh(next(iter(fn))) if len(fn) == 1 else None
        cramped, crate_notes = la.crate_faces(scene, ov, names, crate_mesh, base, base.B.get(ov_hash),
                                              navs[0] if navs else None)
        n = len(clashes) + len(left) + len(onset) + len(hang) + len(dang) + len(seal) + len(gone_exits) + \
            len(gone_seq) + len(cramped) + len(zoned) + len(company)
        out[label] = n
        print("== %s, %s: %d clashes (%d at spots, %d left in place, %d on the set, %d hanging, %d of Freelancer's "
              "exits broken, %d deleted, %d without their sequence, %d places sealed off, %d crates cramped, %d starts "
              "in the option's trespassing areas, %d starts by the option's NPCs or acts)%s" % (
                  title, label, n, len(clashes), len(left), len(onset), len(hang), len(dang), len(gone_exits),
                  len(gone_seq), len(seal), len(cramped), len(zoned), len(company),
            "; privacy: %d busy, %d as under Freelancer" % (len(busy), len(vanilla))))
        for line in clashes:
            print("  CLASH spot  " + line)
        for line in left:
            print("  CLASH left  " + line)
        for line in onset:
            print("  CLASH onset " + line)
        for line in hang:
            print("  CLASH hang  " + line)
        for line in dang:
            print("  CLASH exit  " + line)
        for line in gone_exits:
            print("  CLASH exit-gone " + line)
        for line in gone_seq:
            print("  CLASH exit-seq " + line)
        for line in seal:
            print("  CLASH seal  " + line)
        for line in cramped:
            print("  CLASH crate " + line)
        for line in zoned:
            print("  CLASH zone  " + line)
        for line in company:
            print("  CLASH npc   " + line)
        if label == "as appended":
            summary["busy"] = len(busy)
            for line in busy:
                print("  BUSY  " + line)
            for line in vanilla:
                print("  as under Freelancer  " + line)
            for line in notes:
                print("  near  " + line)
            for line in left_notes:
                print("  near-left  " + line)
            for line in crate_notes:
                print("  crate " + line)
            for line in zone_notes:
                print("  zone  " + line)
            for _, _, line in nav:
                print("  navmesh " + line)
            for line in start_named:
                print("  start-named " + line)
            for line in named:
                print("  named " + line)
        else:
            summary["busy with fix"] = len(busy)
            for line in busy:
                print("  BUSY  " + line)
            if getattr(fix, "SPOT_FIX", False):
                # The spots the fix moves: none below the acts and litter beside them (la.sunk).
                mesh = navs[1] if navs else reader.navmesh(next(iter(fn))) if len(fn) == 1 else None
                moved = ["%016x" % eid for eid in fix.MOVES]
                sk, sloped = la.sunk([(d, q) for d, q in places if any(m in d for m in moved)], mesh,
                                     la.scene_floor_refs(scene, names, {fix.FIX_IDS[0]}))
                summary["sunk with fix"] = len(sk)
                for line in sk:
                    print("  SUNK " + line)
                for line in sloped:
                    print("  sloped " + line)
    summary["clashes"] = out.get("as appended")
    if fix:
        summary["clashes with fix"] = out[[k for k in out if k != "as appended"][0]]
    return summary


class Scene(la.Scene):
    """landslide_audit's Scene, with an entity gone when a loaded brick deletes it or a logical parent IN ITS OWN BRICK:
    the game deletes a deleted entity's subtree within its brick (Reflection deletes agencymarkup_snowcrane's
    HeroSpawns_Snowcrane group and its nine spawns go: the mod's start fix finds 1 hero spawn), not another brick's
    entities whose logicalParent names it (Lust deletes scenario_fox's "Exit locations", the logical parent of
    override_meyer's eight Freelancer exits: in game the mod's exit walk found all eight, alive)."""

    def gone(self, h, i, depth=0):
        k = (h, i)
        if k in self.gone_cache:
            return self.gone_cache[k]
        g = k in self.dels
        if not g and depth < 200:
            lp = self.B[h].temp["subEntities"][i]["logicalParent"]
            p = self.resolve(h, lp) if lp["externalSceneIndex"] < 0 else None
            g = p is not None and p != k and self.gone(p[0], p[1], depth + 1)
        self.gone_cache[k] = g
        return g


class Context:
    def __init__(self, args, names):
        self.args, self.names = args, names
        self.newest = self.reader = None
        self._base_index = {}
        self.last_official = None
        self.extents = None

    def base_index(self, h, base):
        if h not in self._base_index:
            self._base_index[h] = la.placed_index(base, self.names)
        return self._base_index[h]


def run_full(ctx, map_name, scene_path, opt_name, key):
    """A full scene swap (landslide_audit.run_variant on our built bricks), or the full Gluttony (appended + fix)."""
    s = FULL_SPECS[key]
    if not ctx.args.built:
        print("=== %s mild: %s: the full version (%s) needs --built (our built bricks)" % (map_name, opt_name, key))
        return {"title": "%s mild: %s (full)" % (map_name, opt_name), "error": "no --built"}
    scene_hash = rid("[%s].pc_entitytemplate" % scene_path)
    start = freelancer_start(ctx.newest, ctx.args.contracts, scene_path)
    fl = bb.scene_closure(ctx.newest, ctx.names, start)
    fb, unread = load(ctx.reader, fl, start)
    base = Scene(fb)
    if unread:
        print("Freelancer's scene %s: not read: %s" % (bb.gp_name(scene_hash), ", ".join(bb.gp_name(h) for h in
                                                                                         unread)))
    args = argparse.Namespace(built=ctx.args.built, json=ctx.args.json or [], bin=ctx.args.bin or [])
    # read_brick through the Reader, so a brick only a --deck package has is extracted first.
    read = {}
    for h in bb.scene_closure(ctx.newest, ctx.names, [s.SCENE] + [x for _, x, _ in getattr(s, "LAYER_BRICKS", [])]):
        read[h] = ctx.reader.brick(h)
    built = la.load_built(ctx.args.built, la.built_spec(s))[0]
    for b in built.values():
        for h in bb.scene_closure(ctx.newest, ctx.names, [x for x in b.scenes if x not in built]):
            read[h] = ctx.reader.brick(h)
    try:
        summary = la.run_variant(s, args, ctx.newest, ctx.names, read, base, ctx.base_index(scene_hash, base))
    except (be.BrickError, KeyError, IndexError) as x:
        print("NOT CHECKED: %s" % x)
        return {"title": "%s mild: %s (full: %s)" % (map_name, opt_name, s.DISPLAY), "error": str(x)}
    summary["title"] = "%s mild: %s (full: %s)" % (map_name, opt_name, s.DISPLAY)
    return summary


def deck_files(args, maps):
    """The Runtime packages to copy per map, with the sizes the metadata cache knows."""
    sizes = {}
    for f in os.listdir(args.cache):
        m = re.match(r"(chunk(\d+)(?:patch(\d+))?\.rpkg)\.(\d+)\.meta$", f)
        if m and int(m.group(3) or 0) < 100:
            sizes.setdefault(int(m.group(2)), []).append((int(m.group(3) or 0), m.group(1), int(m.group(4))))
    local = set()
    for d in args.deck or []:
        for f in os.listdir(d):
            m = re.match(r"chunk(\d+)\.rpkg$", f)
            if m and os.path.getsize(os.path.join(d, f)) > 0:
                local.add(int(m.group(1)))
    for name in maps:
        c = CHUNKS.get(name)
        note = DECK_NOTES.get(name, "")
        if c is None:
            print("%s: %s" % (name, note))
            continue
        have = "already copied" if c in local else "to copy"
        if c in sizes:
            files = sorted(sizes[c])
            print("%s (partition %d, %s): %s; %.2f GB" % (name, c, have, ", ".join(
                "Runtime/%s (%d bytes)" % (n, s) for _, n, s in files), sum(s for _, _, s in files) / 1e9))
        else:
            print("%s (partition %d, %s): Runtime/chunk%d.rpkg and every Runtime/chunk%dpatch<K>.rpkg with K below "
                  "100 (IOI's patches, 1 to 4 today; not 290, the mod's, nor 300, another mod's)%s" % (
                      name, c, have, c, c, "; " + note if note else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", action="append", help="a map of Variants.cpp (\"Sapienza\", \"Chongqing\"; default: all)")
    ap.add_argument("--option", action="append", help="only these options (their Variants.cpp names)")
    ap.add_argument("--cache", required=True, help="rpkg metadata folder")
    ap.add_argument("--names", required=True, help="hash list (hash.TYPE,path lines)")
    ap.add_argument("--json", action="append", help="folder of ResourceTool JSON (repeatable)")
    ap.add_argument("--bin", action="append", help="folder of extracted binaries (repeatable; the first gets what "
                                                   "--deck extracts)")
    ap.add_argument("--deck", action="append", help="folder of packages (a copy of the Deck's Runtime; repeatable)")
    ap.add_argument("--built", help="build_bricks.py's --out, for the full versions' scene swaps")
    ap.add_argument("--contracts", default=os.path.expanduser("~/peacock/contractdata"),
                    help="Peacock's contractdata folder (the official scenes)")
    ap.add_argument("--src", default=os.path.join(REPO, "src", "core"), help="the folder of Variants.cpp")
    ap.add_argument("--patch-level", type=int, default=4)
    ap.add_argument("--hot", action="store_true", help="also each option's showdown scene, where it is offered")
    ap.add_argument("--deck-files", action="store_true", help="only print the packages to copy per map")
    args = ap.parse_args()
    maps, options, full = parse_variants(args.src)
    if args.deck_files:
        return deck_files(args, [m for m in maps if not args.map or m in args.map])
    names = bb.load_names(args.names)
    bb.NAMES.update(names)
    ctx = Context(args, names)
    results = []
    for map_name, scenes in maps.items():
        if args.map and map_name not in args.map:
            continue
        chunk = CHUNKS.get(map_name)
        if chunk is None or not glob.glob(os.path.join(args.cache, "chunk%d.rpkg.*.meta" % chunk)):
            print("=== %s: no package metadata for its partition (%s): not checked; --deck-files lists what to copy"
                  % (map_name, "chunk%d" % chunk if chunk is not None else DECK_NOTES.get(map_name, "?")))
            results.append({"title": map_name, "error": "packages not copied"})
            continue
        ctx.newest, _ = bb.gp.load_newest(args.cache, args.patch_level, (0, chunk))
        ctx.reader = Reader(args, chunk, ctx.newest)
        ctx.extents = Extents(ctx.reader, ctx.newest)
        for kind in ("mild", "hot"):
            if kind not in scenes or (kind == "hot" and not args.hot):
                continue
            path, opts = scenes[kind]
            for opt_name in opts:
                if args.option and opt_name not in args.option:
                    continue
                if opt_name in full:
                    key = full[opt_name]
                    if FULL_SPECS[key].NAME in FIX_SPECS or getattr(FULL_SPECS[key], "FIX_PATH", None):
                        results.append(run_appended(ctx, map_name, kind, path, opt_name, options[opt_name]))
                    elif kind == "mild":
                        results.append(run_full(ctx, map_name, path, opt_name, key))
                    continue
                results.append(run_appended(ctx, map_name, kind, path, opt_name, options[opt_name]))
        if ctx.reader.failed:
            print("Could not extract: %s" % ", ".join("%016X (%s)" % kv for kv in sorted(ctx.reader.failed.items())))
    print("=== Summary")
    for r in results:
        print("  %s: %s" % (r["title"], ", ".join("%s %s" % (k, v) for k, v in r.items() if k != "title")))


if __name__ == "__main__":
    main()
