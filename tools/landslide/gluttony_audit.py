#!/usr/bin/env python3
"""The Gluttony clash audit: where IOI's Seven Deadly Sins set for Chongqing (scenario_azalea) meets what Freelancer's
Chongqing loads instead of the escalation's own scene, from the bricks' binaries (tools/rpkg/entity.py; no
ResourceTool, no Docker).

IOI's Gluttony scene (scene_wet_azalea) is scenario_rat + commentary_rat + scenario_azalea, and the scene entity itself
deletes and sets a few things in scenario_rat. Freelancer's Chongqing (scene_wet_mild_mahjong, scene_wet_hot_mahjong)
is scenario_rat + mission_mild_mahjong or mission_hot_mahjong + override_mahjong (+ a difficulty brick on easy and
hard contracts); the gluttony map variant appends scenario_azalea to it. So the set is built for scenario_rat as the
Gluttony scene leaves it, and three things can clash:

1. what the Gluttony scene deletes or moves in scenario_rat and Freelancer's scene does not (it stays where the set
   goes): an entity of scenario_rat present under Freelancer + scenario_azalea (+ the fix brick's deletes) but deleted or
   moved under the Gluttony scene, within LARGE_GAP_M in plan and PROP_GAP_DZ in height of one of the set's props;
2. what Freelancer's bricks add or move: their own placed entities and the scenario_rat entities they move, within
   PROP_GAP_M / PROP_GAP_DZ of one of the set's props;
3. the reverse: a reference of scenario_azalea (a property, an override, a pin) into a scenario_rat entity Freelancer's
   bricks delete.

    python3 -I tools/landslide/gluttony_audit.py --raw <folder> [--raw ...] --cache <rpkg metadata> --names <hash list>

--raw: the extracted resources (tools/rpkg/rpkg.py extract: <package stem>_<HASH>.<TYPE>) of gluttony_spec.READS.
It prints the audit twice: as the variant was (scenario_azalea alone) and with the fix brick's deletes. build_bricks.py
runs the second (audit()) on every build, and stops when a clash is left.
"""
import argparse
import glob
import math
import os
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "rpkg"))

import entity  # noqa: E402
import gluttony_spec as spec  # noqa: E402
import rpkg  # noqa: E402

IDENT = [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]]


class AuditError(Exception):
    pass


def matrix(m):
    X, Y, Z, T = m["XAxis"], m["YAxis"], m["ZAxis"], m["Trans"]
    return [[X["x"], Y["x"], Z["x"], T["x"]], [X["y"], Y["y"], Z["y"], T["y"]], [X["z"], Y["z"], Z["z"], T["z"]]]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0) for j in range(4)]
            for i in range(3)]


def props(e):
    d = {}
    for kind in ("propertyValues", "postInitPropertyValues"):
        for p in e[kind]:
            d[p["nPropertyID"]] = p["value"]["$val"]
    return d


def leaf(path):
    return path.split("]")[0].rsplit("/", 1)[-1].lower()


class Brick:
    """A TEMP and its TBLU as entity.py reads them, with their reference tables."""

    def __init__(self, name, temp_hash, temp, tblu, temp_refs, tblu_refs):
        self.name, self.hash = name, temp_hash
        self.temp, self.tblu = temp, tblu
        self.temp_refs, self.tblu_refs = temp_refs, tblu_refs
        self.P = [props(e) for e in temp["subEntities"]]
        self.scenes = [temp_refs[i][1] for i in temp["externalSceneTypeIndicesInResourceHeader"]]
        self.tblu_scenes = [tblu_refs[i][1] for i in tblu["externalSceneTypeIndicesInResourceHeader"]]
        self._ids = [e["entityId"] for e in tblu["subEntities"]]

    def ids(self):
        return self._ids

    def type_hash(self, i):
        return self.temp_refs[self.temp["subEntities"][i]["entityTypeResourceIndex"]][1]


def find_raw(dirs, h, typ):
    hits = []
    for d in dirs:
        hits += glob.glob(os.path.join(d, "*%016X.%s" % (h, typ)))
    return sorted(hits)[0] if hits else None


def read_brick(dirs, newest, name, pair):
    """A brick (TEMP, TBLU) from the raw folders; None when either file is missing."""
    t, u = find_raw(dirs, pair[0], "TEMP"), find_raw(dirs, pair[1], "TBLU")
    if not t or not u:
        return None
    if pair[0] not in newest or pair[1] not in newest:
        raise AuditError("%s %016X/%016X is in no mounted package (--cache)" % (name, pair[0], pair[1]))
    with open(t, "rb") as f:
        temp = entity.read_temp(f.read(), t)
    with open(u, "rb") as f:
        tblu = entity.read_tblu(f.read(), u)
    if len(temp["subEntities"]) != len(tblu["subEntities"]):
        raise AuditError("%s: TEMP has %d entities, TBLU %d" % (name, len(temp["subEntities"]),
                                                              len(tblu["subEntities"])))
    return Brick(name, pair[0], temp, tblu, newest[pair[0]]["refs"], newest[pair[1]]["refs"])


class Scene:
    """Bricks loaded together: world positions across bricks (m_eidParent into another brick), with every loaded
    brick's property overrides of m_mTransform and m_eidParent applied, and their deletes (an entity is gone when it or
    one of its logical parents is deleted)."""

    def __init__(self, bricks, deletes=()):
        """deletes: more (brick hash, entity id) deletes, as a brick loaded after these would make them."""
        self.bricks = list(bricks)
        self.by_hash = {b.hash: b for b in self.bricks}
        self.index = {}
        for b in self.bricks:
            for i, eid in enumerate(b.ids()):
                self.index[(b.hash, eid)] = (b, i)
        self.overrides = {}  # (brick hash, id) -> {property: (value, the overriding brick)}
        self.deleted = set(deletes)  # (brick hash, id)
        for b in self.bricks:
            for p in b.temp["propertyOverrides"]:
                o = p["propertyOwner"]
                if o["externalSceneIndex"] < 0:
                    key = (b.hash, b.ids()[o["entityIndex"]])
                else:
                    key = (b.scenes[o["externalSceneIndex"]], o["entityID"])
                prop = p["propertyValue"]["nPropertyID"]
                self.overrides.setdefault(key, {})[prop] = (p["propertyValue"]["value"]["$val"], b)
            for r in b.tblu["overrideDeletes"]:
                self.deleted.add((b.tblu_scenes[r["externalSceneIndex"]], r["entityID"]))
        self.cache = {}
        self.gone = {}

    def prop(self, b, i, name):
        ov = self.overrides.get((b.hash, b.ids()[i]), {})
        if name in ov:
            return ov[name]
        return (b.P[i].get(name), b)

    def world(self, b, i, depth=0):
        """The world matrix of entity i of b; None when its parent chain leaves the loaded bricks."""
        k = (b.hash, i)
        if k in self.cache:
            return self.cache[k]
        if i == b.temp["rootEntityIndex"] or depth > 64:
            return IDENT
        m, _ = self.prop(b, i, "m_mTransform")
        local = matrix(m) if m else IDENT
        par, owner = self.prop(b, i, "m_eidParent")  # a reference held by owner (b, or the brick overriding it)
        if par is None or (par["externalSceneIndex"] < 0 and par["entityIndex"] < 0):
            w = local
        else:
            if par["externalSceneIndex"] < 0:
                target = (owner, par["entityIndex"])
            else:
                target = self.index.get((owner.scenes[par["externalSceneIndex"]], par["entityID"]))
            if target is None:
                w = None
            else:
                pw = self.world(target[0], target[1], depth + 1)
                w = mul(pw, local) if pw is not None else None
        self.cache[k] = w
        return w

    def pos(self, b, i):
        w = self.world(b, i)
        return None if w is None else (w[0][3], w[1][3], w[2][3])

    def is_gone(self, b, i):
        k = (b.hash, i)
        if k not in self.gone:
            j, depth, out = i, 0, False
            ids = b.ids()
            while j is not None and depth < 256:
                if (b.hash, ids[j]) in self.deleted:
                    out = True
                    break
                lp = b.temp["subEntities"][j]["logicalParent"]
                j = lp["entityIndex"] if lp["externalSceneIndex"] < 0 and lp["entityIndex"] >= 0 else None
                depth += 1
            self.gone[k] = out
        return self.gone[k]


def placed_kind(type_path):
    """"prop" for placed geometry (props, vehicles, set pieces, kits, prims), "act" for acts, else None."""
    t = type_path.lower()
    lf = leaf(t)
    if "/acts" in t or lf.startswith("act_"):
        return "act"
    if any(s in t for s in ("/props/", "/vehicles/", "setpiece", "/kits/", "/geometry/", ".prim]")):
        return "prop"
    return None


# Small loose items (food, litter, tableware, bottles, cans): the set strews thousands over its tables and floors.
# Nothing solid stands on them, so they count only in the notes.
SMALL = ("/props/food/", "/props/trash/", "/props/utensils/", "/props/accessories/", "/collections/props/",
         "food_stands_azalea", "backdrop_buildings_ext_a.template?/beer_", "litter", "llama_winebottle",
         "glass_wine", "container_styrofoam", "container_plastic_b", "container_beer", "box_styrofoam", "napkin",
         "aspectdummy", ".linkedprim", ".mi]", "dish_stainlesssteel", "chinese_rotating_tray", "tray_bowls",
         "asado_barbeque_food", "can_soda", "coffeemakerpot", "decals", "carpet_")


def is_small(type_path):
    t = type_path.lower()
    return any(s in t for s in SMALL)


def describe(b, i, names):
    u = b.tblu["subEntities"][i]
    return "%s %016x \"%s\" (%s)" % (b.name, u["entityId"], u["entityName"], leaf(names.get(b.type_hash(i), "?")))


def near(p, q, gap, dz):
    return math.hypot(p[0] - q[0], p[1] - q[1]) < gap and abs(p[2] - q[2]) < dz


def audit(read, names, deletes=()):
    """The three checks for each Freelancer scene of the spec. read: brick name -> Brick (gluttony_spec's READS that
    were read); deletes: the fix brick's deletes ((brick hash, entity id), appended after scenario_azalea), or none
    (the variant as it was). Returns (report lines, clashes: [(scene, kind, description)])."""
    lines, clashes = [], []
    base, layer = read[spec.BASE_NAME], read[spec.LAYER_NAME]
    official = Scene([read[n] for n in spec.OFFICIAL_SCENE])
    # The scenario_rat entities the Gluttony scene's own bricks (not scenario_azalea, which Freelancer loads too) move.
    moved_by_official = set()
    for n in spec.OFFICIAL_SCENE:
        b = read[n]
        if b in (base, layer):
            continue
        for p in b.temp["propertyOverrides"]:
            o = p["propertyOwner"]
            if o["externalSceneIndex"] >= 0 and b.scenes[o["externalSceneIndex"]] == base.hash and \
                    p["propertyValue"]["nPropertyID"] in ("m_mTransform", "m_eidParent"):
                moved_by_official.add(o["entityID"])
    for scene_name, brick_names in spec.FREELANCER_SCENES:
        missing = [n for n in brick_names if n not in read]
        if missing:
            lines.append("%s: not checked (%s not read)" % (scene_name, ", ".join(missing)))
            continue
        loaded = [read[n] for n in brick_names] + [layer]
        fl = Scene(loaded, deletes)
        own = [b for b in loaded if b not in (base, layer)]

        # The set's solid props (furniture, stalls, tents, carts, crates, vehicles, set pieces; not its loose small
        # items), as Freelancer's scene places them (as the Gluttony scene does: nothing else overrides them).
        set_props = []
        for i in range(len(layer.ids())):
            t = names.get(layer.type_hash(i), "")
            if placed_kind(t) == "prop" and not is_small(t) and not fl.is_gone(layer, i):
                p = fl.pos(layer, i)
                if p and (abs(p[0]) > 0.01 or abs(p[1]) > 0.01):
                    set_props.append((i, p))

        cell = max(spec.LARGE_GAP_M, spec.PROP_GAP_M)
        grid = {}
        for j, q in set_props:
            grid.setdefault((int(q[0] // cell), int(q[1] // cell)), []).append((j, q))

        def nearest_prop(p, gap, dz):
            best = None
            cx, cy = int(p[0] // cell), int(p[1] // cell)
            for j, q in [x for dx in (-1, 0, 1) for dy in (-1, 0, 1) for x in grid.get((cx + dx, cy + dy), ())]:
                if near(p, q, gap, dz):
                    d = math.hypot(p[0] - q[0], p[1] - q[1])
                    if best is None or d < best[0]:
                        best = (d, j, q)
            return best

        # 1. What the Gluttony scene removes or moves in scenario_rat and Freelancer's scene leaves in place.
        n1 = 0
        for i in range(len(base.ids())):
            if fl.is_gone(base, i):
                continue
            off_gone = official.is_gone(base, i)
            moved = not off_gone and base.ids()[i] in moved_by_official and official.pos(base, i) != fl.pos(base, i)
            if not (off_gone or moved):
                continue
            p = fl.pos(base, i)
            if p is None or placed_kind(names.get(base.type_hash(i), "")) is None:
                continue
            hit = nearest_prop(p, spec.LARGE_GAP_M, spec.PROP_GAP_DZ)
            if hit:
                n1 += 1
                d, j, q = hit
                text = "%s at (%.1f, %.1f, %.1f), %s by the Gluttony scene, is %.1f m from the set's %s at " \
                       "(%.1f, %.1f, %.1f)" % (describe(base, i, names), p[0], p[1], p[2],
                                               "deleted" if off_gone else "moved", d, describe(layer, j, names),
                                               q[0], q[1], q[2])
                clashes.append((scene_name, "left in place", text))
                lines.append("%s: CLASH (left in place): %s" % (scene_name, text))

        # 2. What Freelancer's bricks add or move, against the set's props.
        n2 = 0
        moved_by_fl = set()
        for b in own:
            for p in b.temp["propertyOverrides"]:
                o = p["propertyOwner"]
                if o["externalSceneIndex"] >= 0 and b.scenes[o["externalSceneIndex"]] == base.hash and \
                        p["propertyValue"]["nPropertyID"] in ("m_mTransform", "m_eidParent"):
                    k = fl.index.get((base.hash, o["entityID"]))
                    if k:
                        moved_by_fl.add(k[1])
        things = [(b, i) for b in own for i in range(len(b.ids()))] + [(base, i) for i in sorted(moved_by_fl)]
        notes = 0
        for b, i in things:
            t = names.get(b.type_hash(i), "")
            kind = placed_kind(t)
            if kind is None or fl.is_gone(b, i):
                continue
            p = fl.pos(b, i)
            if p is None or (abs(p[0]) < 0.01 and abs(p[1]) < 0.01):
                continue
            # A Freelancer prop (a clue, a safe, a chair, a camera) where one of the set's solid props stands (within
            # ON_GAP_M in plan and ON_GAP_DZ in height: the two occupy the same place) is a clash; one within
            # PROP_GAP_M, or an act (an NPC's place) within ACT_GAP_M, is only noted.
            on = kind == "prop" and nearest_prop(p, spec.ON_GAP_M, spec.ON_GAP_DZ)
            hit = on or nearest_prop(p, spec.PROP_GAP_M if kind == "prop" else spec.ACT_GAP_M, spec.PROP_GAP_DZ)
            if hit:
                d, j, q = hit
                text = "%s%s at (%.1f, %.1f, %.1f) is %.1f m from the set's %s at (%.1f, %.1f, %.1f)" % (
                    describe(b, i, names), " (moved by Freelancer)" if b is base else "", p[0], p[1], p[2], d,
                    describe(layer, j, names), q[0], q[1], q[2])
                if on:
                    n2 += 1
                    clashes.append((scene_name, "Freelancer's prop", text))
                    lines.append("%s: CLASH (Freelancer's prop): %s" % (scene_name, text))
                else:
                    notes += 1
                    lines.append("%s: note (Freelancer's %s): %s" % (scene_name, kind, text))

        # 3. The set's references into what Freelancer's bricks delete.
        n3 = 0
        gone = {base.ids()[i] for i in range(len(base.ids())) if fl.is_gone(base, i) and not official.is_gone(base, i)}
        refs = []
        for i, e in enumerate(layer.temp["subEntities"]):
            for p in e["propertyValues"] + e["postInitPropertyValues"]:
                found = []
                _walk_refs(p["value"], found)
                refs += [("%s.%s" % (describe(layer, i, names), p["nPropertyID"]), r) for r in found]
        for p in layer.tblu["pinConnectionOverrides"]:
            for side in ("fromEntity", "toEntity"):
                refs.append(("pin %s -> %s" % (p["fromPinName"], p["toPinName"]), p[side]))
        for where, r in refs:
            if r["externalSceneIndex"] >= 0 and layer.scenes[r["externalSceneIndex"]] == base.hash and \
                    r["entityID"] in gone:
                n3 += 1
                b, i = fl.index[(base.hash, r["entityID"])]
                text = "%s names %s, which Freelancer's bricks delete" % (where, describe(b, i, names))
                clashes.append((scene_name, "deleted under the set", text))
                lines.append("%s: CLASH (deleted under the set): %s" % (scene_name, text))
        overridden = sum(1 for p in layer.temp["propertyOverrides"]
                         if p["propertyOwner"]["externalSceneIndex"] >= 0 and p["propertyOwner"]["entityID"] in gone)
        lines.append("%s: %d solid set props; %d left in place, %d Freelancer props on the set (%d props and acts "
                     "near it noted), %d references into Freelancer's deletes%s" % (scene_name, len(set_props), n1, n2,
                                                                                notes, n3,
                                                 "; %d of the set's property overrides land on entities Freelancer "
                                                 "deletes (no effect)" % overridden if overridden else ""))
    return lines, clashes


def references(b):
    """Every entity reference b holds, as (where, brick hash, entity id, holder): its entities' properties
    (m_eidParent included) and logical parents, its property overrides (owner and value), its pin connection
    overrides and its own pin connections. holder is the id of b's entity holding it (a pin's other end), or None for
    an override."""
    ids = b.ids()

    def target(r):
        if r["externalSceneIndex"] >= 0:
            return (b.scenes[r["externalSceneIndex"]] if r["externalSceneIndex"] < len(b.scenes) else None,
                    r["entityID"])
        return (b.hash, ids[r["entityIndex"]]) if 0 <= r["entityIndex"] < len(ids) else None

    out = []
    for i, e in enumerate(b.temp["subEntities"]):
        owner = "%016x \"%s\"" % (ids[i], b.tblu["subEntities"][i]["entityName"])
        found = [("logicalParent", e["logicalParent"])]
        for p in e["propertyValues"] + e["postInitPropertyValues"]:
            refs = []
            _walk_refs(p["value"], refs)
            found += [(p["nPropertyID"], r) for r in refs]
        for where, r in found:
            t = target(r)
            if t and t[0] is not None:
                out.append(("%s %s %s" % (b.name, owner, where), t[0], t[1], ids[i]))
    for p in b.temp["propertyOverrides"]:
        refs = [p["propertyOwner"]]
        _walk_refs(p["propertyValue"]["value"], refs)
        for r in refs:
            t = target(r)
            if t and t[0] is not None:
                out.append(("%s property override %s" % (b.name, p["propertyValue"]["nPropertyID"]), t[0], t[1],
                            None))
    for p in b.tblu["pinConnectionOverrides"]:
        for side in ("fromEntity", "toEntity"):
            r = p[side]
            where = "%s pin override %s -> %s" % (b.name, p["fromPinName"], p["toPinName"])
            if r["externalSceneIndex"] >= 0 and r["externalSceneIndex"] < len(b.tblu_scenes):
                out.append((where, b.tblu_scenes[r["externalSceneIndex"]], r["entityID"], None))
            elif r["externalSceneIndex"] < 0 and 0 <= r["entityIndex"] < len(ids):
                out.append((where, b.hash, ids[r["entityIndex"]], None))
    for p in b.tblu["pinConnections"] + b.tblu["inputPinForwardings"] + b.tblu["outputPinForwardings"]:
        if 0 <= p["fromID"] < len(ids) and 0 <= p["toID"] < len(ids):
            where = "%s pin %s -> %s" % (b.name, p["fromPinName"], p["toPinName"])
            out.append((where, b.hash, ids[p["toID"]], ids[p["fromID"]]))
            out.append((where, b.hash, ids[p["fromID"]], ids[p["toID"]]))
    return out


def check_deletes(read, deletes):
    """The checks of the fix brick's deletes (gluttony_spec.DELETES: (brick hash, entity id, name, why)): each is an
    entity of a read brick with that name; no Freelancer brick (FREELANCER_BRICKS) names it or anything logically
    under it; and nothing else of its own brick or of the set names one of them, unless IOI's Gluttony scene deletes
    that entity too (IOI's own delete, which the game plays). Returns (audit lines, problems)."""
    lines, problems = [], []
    by_hash = {b.hash: b for b in read.values()}
    official = Scene([read[n] for n in spec.OFFICIAL_SCENE])
    for h, eid, name, why in deletes:
        b = by_hash.get(h)
        if b is None or eid not in b.ids():
            problems.append("%016X:%016x (%s) is not an entity of a read brick" % (h, eid, name))
            continue
        i = b.ids().index(eid)
        if b.tblu["subEntities"][i]["entityName"] != name:
            problems.append("%s %016x is \"%s\", not \"%s\"" % (b.name, eid, b.tblu["subEntities"][i]["entityName"],
                                                             name))
            continue
        gone = subtree(b, eid)
        by_ioi = (h, eid) in official.deleted
        holders = [read[n] for n in spec.FREELANCER_BRICKS if n in read]
        others = [b] + ([read[spec.LAYER_NAME]] if b is not read[spec.LAYER_NAME] else [])
        for x in holders + others:
            for where, th, te, holder in references(x):
                if th != h or te not in gone or (x is b and holder in gone):
                    continue
                if x in holders:
                    problems.append("%s names %s %016x, under the deleted \"%s\"" % (where, b.name, te, name))
                elif not by_ioi:
                    problems.append("%s names %s %016x, under the deleted \"%s\" (IOI's Gluttony scene does not "
                                    "delete it)" % (where, b.name, te, name))
                else:
                    lines.append("delete: %s names %s %016x under \"%s\" (IOI's Gluttony scene deletes it too)" %
                                 (where, b.name, te, name))
        lines.append("delete: %s %016x \"%s\" (%d entities with it%s): %s" % (
            b.name, eid, name, len(gone), "; IOI's Gluttony scene deletes it too" if by_ioi else "", why))
    return lines, problems


def subtree(b, eid):
    """The ids of b's entity eid and of every entity logically under it."""
    kids = {}
    for i, e in enumerate(b.temp["subEntities"]):
        lp = e["logicalParent"]
        if lp["externalSceneIndex"] < 0 and lp["entityIndex"] >= 0:
            kids.setdefault(lp["entityIndex"], []).append(i)
    ids = b.ids()
    out, stack = set(), [ids.index(eid)]
    while stack:
        i = stack.pop()
        if ids[i] not in out:
            out.add(ids[i])
            stack += kids.get(i, [])
    return out


def _walk_refs(v, out):
    if isinstance(v, dict):
        if "entityID" in v and "externalSceneIndex" in v:
            out.append(v)
            return
        for x in v.values():
            _walk_refs(x, out)
    elif isinstance(v, list):
        for x in v:
            _walk_refs(x, out)


def read_all(dirs, newest):
    """{name: Brick} for every READS brick found in dirs."""
    out = {}
    for name, pair in spec.READS:
        b = read_brick(dirs, newest, name, pair)
        if b is not None:
            out[name] = b
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--raw", action="append", required=True, help="folder of extracted resources (repeatable)")
    ap.add_argument("--cache", required=True, help="rpkg metadata folder (<package>.rpkg.<size>.meta)")
    ap.add_argument("--names", required=True, help="hash list (hash.TYPE,path lines)")
    ap.add_argument("--patch-level", type=int, default=4)
    args = ap.parse_args()
    res = rpkg.Resolver(None, [spec.PARTITION[0], 0], args.cache, vanilla=True, patch_level=args.patch_level)
    newest = {h: {"refs": e.refs} for h, (_, e) in res.newest.items()}
    read = read_all(args.raw, newest)
    need = [n for n in (spec.BASE_NAME, spec.LAYER_NAME) + tuple(spec.OFFICIAL_SCENE) if n not in read]
    if need:
        sys.exit("gluttony_audit: %s not in --raw" % ", ".join(need))
    wanted = set()
    for b in read.values():
        wanted |= {h for _, h in b.temp_refs}
    names = rpkg.load_names(args.names, wanted)
    for title, deletes in (("as appended until now (scenario_azalea alone)", ()),
                           ("with the fix brick (gluttony_spec.DELETES)", {(h, e) for h, e, _, _ in spec.DELETES})):
        lines, clashes = audit(read, names, deletes)
        print("== %s" % title)
        print("\n".join(lines))
        print("%d clashes" % len(clashes))
    lines, problems = check_deletes(read, spec.DELETES)
    print("== the fix brick's deletes")
    print("\n".join(lines + ["PROBLEM: " + p for p in problems]))


if __name__ == "__main__":
    main()
