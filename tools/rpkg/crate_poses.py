"""Where the game lays each item in a Freelancer supply crate, and the box over it, from the chunk0 entity templates
(standard library only; run with python3 -I). The source of kCrateTwins in src/core/CrateItems.cpp and of
src/core/CrateItemBoxes.inc.

    python3 -I tools/rpkg/crate_poses.py --dir build-landslide/deckcopy/chunk0 --names third_party/zhmtools/hash_list.txt
        [--twins] [--boxes src/core/CrateItemBoxes.inc]
        [--sizes src/core/CrateItemSizes.inc --dir build-landslide/deckcopy/chunk0sparse --cache build-landslide/cache]

What it reads (chunk0patch3 and 4 hold every template used here; chunk0.rpkg is not needed):
- Each crate option is a helper_evergreen_selectableitem_<item> template (296 of them): a ZCompositeEntity root, the
  shared helper_evergreen_selectableitem (m_RepositoryId: the item; its own m_mTransform; ItemTransform: the "Pose when
  resting" its ZItemSpawner hangs under, through "Final resting post" and "ZoomedIn Item Manipulation Target"
  5986ac5dc53d42aa, the spawner's m_eidParent in the logs) and exactly one ZBoxVolumeEntity "Boundary" under that pose
  (m_vGlobalSize, m_mTransform). The helper's m_boundary alias names that box and hands it to its
  ZEvergreenMenuEntryEntity "EvergreenMenuEntry" (54a19f285df200e5): the menu entry's boundary, the selector box.
- setpiece_evergreen_stash_items places one template per option under its slot (helper_evergreen_vendorgear_category:
  "Small lethal items" 22, "Small nonlethal items" 25, "Large items" 18, all unrotated; "Rarity items" is the weapon
  pool). The spawner in the slot's frame = the option's m_mTransform x the helper's x ItemTransform.
- helper_evergreen_vendorcategory_consumables / _melee / _tools (the supplier) place the same templates on a wall.

Twins (--twins): an item with no crate option of its own is laid as a crate item with the same box (its "twin": the
same model, the boxes match to a few millimeters), turned by the quarter turns that take the twin's axes to the
item's on the supplier's wall (both hang there). The wall-to-crate map is one quarter-turn map for 10 of the 22
tool-slot items on both (the three flat devices among them: Proximity Taser, Remote Flash Device, Disposable
Scrambler); the turn carries the twin's map over to the item. Rows: {item, twin, turn}, the turn as AxisTurn T with
T[3 * j + i] = (twin axis j) . (item axis i): TurnAxes(twin's pose, T) is the item's pose.

Boxes (--boxes FILE): every item template's box center and size along its spawner's axes (m), one row per item (its
repository id), the rows of kCrateItemBoxes (CrateItemBoxCenter, CrateItemBoxSize). The box is a card drawn over the
item as the menu shows it, one side a centimeter or less: in 91 of the crate's 94 options with a box that thin axis is
the spawner axis the crate lays up (the other three are balls and a cord), so an item with no option of its own, no
twin and no row in the table lies with its box's thin axis up (CrateBoxRestAxis: the Explosive Phone, 14.4 x 7.5 x 1
cm, on its back, not on its end as +X laid it). An item with no option of its own in the crate and no twin is
centered in the slot it is written into by it (the Proximity Explosive S3's box is centered 4 mm from its spawner, so
the slot's spawners' middle, 4.8 cm nearer the player than the Large items' box centers, put it there). A Boundary
that is not under the helper's ItemTransform but beside the helper (Sieger 300 Advanced's) is taken into the
spawner's frame.

Slot room (always printed): how long an item the game itself lays in a three-item crate's tool slot. The three slots
sit 0.46 and 0.52 m apart along the crate (stash X 0.52, 0.06, -0.46); the slots' options reach 0.43 m across it
(slot Y -0.205 to 0.225). The longest option is the Rusty Crowbar (0.614 m, the Large items slot, along the crate
into its neighbors' room); after it the Lead Pipe (0.452 m). An item laid in any slot at any yaw fits only when its
longest side is about that (kCrateSlotLengthMeters in src/core/CrateItems.h; the Hatchet, 0.61 m, laid across slot 1
in Bangkok, stood out of the crate).

Model sizes (--sizes FILE): for the pro.repo items with no option template (no box above), the size of their model:
the item's Runtime template, its item art template(s) (_pro/items/templates/...), the PRIM(s) they place, each
PRIM's bounding box (the PrimObjectHeader's min and max). Rows {item, size}, the size the box's sides along the
model's axes (m), the rows of kCrateItemModelSizes (src/core/CrateItemSizes.inc, CrateItemLongestSide). An item is
left out when a model is not in the packages given (most are in chunk0.rpkg, which the Deck copy holds only in
part: pass --dir build-landslide/deckcopy/chunk0sparse --cache build-landslide/cache for the parts there), when its
models' longest sides differ by more than 2 cm (a wrapped and an unwrapped fiber wire: which one lies in the crate
is not known), and for firearms (never in a three-item crate).

Game files are IOI's: run it on a local copy, keep the output out of the repository (the twins' turns and the box
centers are numbers derived from them).
"""
import argparse
import json
import math
import os
import re
import struct
import sys
import zlib
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import entity  # noqa: E402
import rpkg  # noqa: E402

ITEM_TEMPLATE = "helpers_evergreen_selectableitems.template?/helper_evergreen_selectableitem_"
STASH = 0x00100757521CC642
SUPPLIER = {0x00D6775180DC3BFF: "consumables", 0x00A5B42E2FCF2CB5: "melee", 0x008952EC954F63C1: "tools"}
TOOL_SLOTS = ("Small lethal items", "Small nonlethal items", "Large items")
P_ITEM_TRANSFORM = zlib.crc32(b"ItemTransform")
P_BOUNDARY = zlib.crc32(b"m_boundary")
P_REPOSITORY = zlib.crc32(b"m_RepositoryId")
P_TRANSFORM = zlib.crc32(b"m_mTransform")
P_PARENT = zlib.crc32(b"m_eidParent")
P_SIZE = zlib.crc32(b"m_vGlobalSize")
TWIN_SIZE = 0.006   # m: the boxes' sides agree to this
TWIN_CENTER = 0.01  # m: and their centers

IDENTITY = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, 0.0))
PRO_REPO = 0x00204D1AFD76AB13  # [assembly:/repository/pro.repo].pc_repo
ITEM_ART = "/_pro/items/templates/"
ITEM_DESIGN = "/_pro/design/items/"
NOT_MODEL = re.compile(r"gizmo|debug_|fx_")
MODELS_AGREE = 0.02  # m: an item's models' longest sides within this are one size
FIREARM_ICONS = {"pistol", "smg", "assaultrifle", "shotgun", "sniperrifle", "sniper"}


def newest(dirs, cache=None):
    """hash -> (package, entry): chunk0's packages in patch order (IOI's only)."""
    out = {}
    for pkg in rpkg.find_packages(dirs, cache, 0, vanilla=True):
        for h in pkg.deletions:
            out.pop(h, None)
        for e in pkg.entries:
            out[e.hash] = (pkg, e)
    return out


def props(sub):
    """A sub-entity's properties by crc32 of the name."""
    out = {}
    for p in sub["propertyValues"] + sub["postInitPropertyValues"]:
        n = p["nPropertyID"]
        out[int(n[2:], 16) if n.startswith("0x") else zlib.crc32(n.encode())] = p["value"]
    return out


def matrix(v):
    if not v or v.get("$val") is None:
        return IDENTITY
    m = v["$val"]
    return tuple(tuple(m[k][c] for c in "xyz") for k in ("XAxis", "YAxis", "ZAxis", "Trans"))


def compose(parent, child):
    """child (rows: its axes and origin in parent's frame) in the frame parent is in."""
    px, py, pz, pt = parent

    def rot(v):
        return tuple(v[0] * px[i] + v[1] * py[i] + v[2] * pz[i] for i in range(3))
    o = rot(child[3])
    return (rot(child[0]), rot(child[1]), rot(child[2]), tuple(o[i] + pt[i] for i in range(3)))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


class Template:
    def __init__(self, idx, h):
        pkg, e = idx[h]
        self.refs = [x for _, x in e.refs]
        self.temp = entity.read_temp(pkg.read(e))
        bp, be = idx[self.refs[self.temp["blueprintIndexInResourceHeader"]]]
        self.tblu = entity.read_tblu(bp.read(be))
        self.subs = self.temp["subEntities"]
        self.props = [props(s) for s in self.subs]

    def name(self, i):
        return self.tblu["subEntities"][i]["entityName"]

    def type_of(self, i):
        return self.refs[self.subs[i]["entityTypeResourceIndex"]]

    def parent(self, i):
        v = self.props[i].get(P_PARENT)
        if v and v["$val"] and v["$val"]["entityIndex"] >= 0 and v["$val"]["externalSceneIndex"] == -1:
            return v["$val"]["entityIndex"], v["$val"]["exposedEntity"]
        return None, ""


def item_info(t):
    """An item template's repository id, its spawner in the template root's frame (the helper's m_mTransform times its
    ItemTransform), and its Boundary box (size, transform) in the spawner's frame."""
    info = {"repo": None, "pose": IDENTITY, "box": None, "why": "no helper"}
    helper = None
    for i, p in enumerate(t.props):
        if P_REPOSITORY in p and P_ITEM_TRANSFORM in p:
            helper = i
            info["repo"] = p[P_REPOSITORY]["$val"]
            info["pose"] = compose(matrix(p.get(P_TRANSFORM)), matrix(p[P_ITEM_TRANSFORM]))
    if helper is None:
        return info
    b = t.props[helper].get(P_BOUNDARY)
    if not b or not b["$val"] or b["$val"]["entityIndex"] < 0:
        info["why"] = "no m_boundary"
        return info
    bi = b["$val"]["entityIndex"]
    size = t.props[bi].get(P_SIZE)
    par, exposed = t.parent(bi)
    if size is not None and par == helper and exposed == "ItemTransform":
        bt = matrix(t.props[bi].get(P_TRANSFORM))
    elif size is not None and par == t.parent(helper)[0] and not exposed:
        # Beside the helper (Sieger 300 Advanced's): from the frame the helper's m_mTransform is in to the spawner's.
        bt = relative(info["pose"], matrix(t.props[bi].get(P_TRANSFORM)))
    else:
        info["why"] = "%s not under the helper's ItemTransform (%s %r)" % (t.name(bi), par, exposed)
        return info
    s = size["$val"]
    info["box"] = ((s["x"], s["y"], s["z"]), bt)
    return info


def relative(frame, child):
    """child (rows in the frame frame is given in) in frame's own frame: the inverse of compose (unit axes)."""
    d = tuple(child[3][i] - frame[3][i] for i in range(3))
    return tuple(tuple(dot(child[k], frame[i]) for i in range(3)) for k in range(3)) + \
        (tuple(dot(d, frame[i]) for i in range(3)),)


def box_in(frame, box):
    """The box's center and half sizes along the axes of the frame the spawner frame is given in (frame: the
    spawner's rows there)."""
    size, bt = box
    c = compose(frame, bt)
    half = [sum(abs(c[k][i]) * size[k] / 2 for k in range(3)) for i in range(3)]
    return c[3], half


def up_axis(rot):
    best = max(range(3), key=lambda k: abs(rot[k][2]))
    return "%s%s" % ("-" if rot[best][2] < 0 else "+", "XYZ"[best]), math.degrees(math.acos(min(1.0, abs(rot[best][2]))))


def yaw(rot):
    """The spawner's local X (or, when X points up or down, its Y) about the slot's up, in degrees from the slot's X."""
    for k in (0, 1):
        v = rot[k]
        if math.hypot(v[0], v[1]) > 0.5:
            return "%s %4.0f" % ("XY"[k], math.degrees(math.atan2(v[1], v[0])))
    return "-"


def fmt(v, n=3):
    return "(" + ", ".join("%.*f" % (n, x) for x in v) + ")"


def placements(idx, items, h, skip=()):
    """item name -> (spawner rows in its slot's frame, slot name, template hash) for the item templates container h
    places."""
    t = Template(idx, h)
    out = {}
    for i in range(len(t.subs)):
        ty = t.type_of(i)
        if ty not in items:
            continue
        par, _ = t.parent(i)
        slot = t.name(par) if par is not None else "-"
        if slot in skip or (items[ty][0] in out and out[items[ty][0]][1] in TOOL_SLOTS):
            continue  # an item in a tool slot and the weapon pool (Kukri Knife): its tool slot's
        out[items[ty][0]] = (compose(matrix(t.props[i].get(P_TRANSFORM)), items[ty][1]["pose"]), slot, ty)
    return out


def rounded(rot):
    """A rotation's rows rounded to -1, 0, 1 (the crate's and supplier's are quarter turns)."""
    return tuple(tuple(int(round(x)) for x in rot[k]) for k in range(3))


# --- Model sizes (--sizes) ------------------------------------------------------------------------------------------

def prim_box_size(data):
    """A PRIM's bounding box sides (m): the resource's first word is the offset of its SPrimObjectHeader (u8 draw
    destination, u8 pack type, u16 type, u32 property flags, u32 bone rig index, u32 object count, u32 object table,
    then the box: min x, y, z and max x, y, z as float32)."""
    off = struct.unpack_from("<I", data, 0)[0]
    v = struct.unpack_from("<6f", data, off + 20)
    size = tuple(v[i + 3] - v[i] for i in range(3))
    if not all(math.isfinite(x) and 0.0 <= x < 100.0 for x in size):
        raise ValueError("not a bounding box: %r" % (v,))
    return size


def refs_of(idx, h):
    if h not in idx:
        return None, []
    return idx[h][1].type, [r for _, r in idx[h][1].refs]


def item_arts(idx, names, h, depth=0, seen=None):
    """The item art templates (_pro/items/templates/...) an item's Runtime template places, through the item design
    templates it places (_pro/design/items/...)."""
    seen = set() if seen is None else seen
    if h in seen or depth > 3:
        return []
    seen.add(h)
    ty, refs = refs_of(idx, h)
    if ty != "TEMP":
        return []
    out = []
    for r in refs:
        n = names.get(r, "")
        if ITEM_ART in n and n.endswith(".pc_entitytype"):
            out.append(r)
        elif ITEM_DESIGN in n and n.endswith(".pc_entitytype"):
            out += [a for a in item_arts(idx, names, r, depth + 1, seen) if a not in out]
    return out


def art_models(idx, names, h, depth=0):
    """The PRIMs an item art template places: its geometry entities' types ("...<model>.prim].pc_entitytype", or a
    template built on one) and the PRIMs those name; gizmos, debug shapes and effects left out."""
    out = []
    if depth > 3:
        return out
    for r in refs_of(idx, h)[1]:
        n = names.get(r, "")
        if not (n.endswith("prim].pc_entitytype") or "prim].entitytype" in n):
            continue
        ty, refs = refs_of(idx, r)
        for p in refs:
            if refs_of(idx, p)[0] == "PRIM" and not NOT_MODEL.search(names.get(p, "")) and p not in out:
                out.append(p)
        if ty == "TEMP":
            out += [p for p in art_models(idx, names, r, depth + 1) if p not in out]
    return out


def model_size(idx, names, runtime):
    """(size, why): an item's model size from its Runtime template's PRIMs (the largest when they agree), or None and
    why not."""
    arts = item_arts(idx, names, runtime)
    if not arts:
        return None, "no item art template"
    prims = []
    for a in arts:
        prims += [p for p in art_models(idx, names, a) if p not in prims]
    if not prims:
        return None, "no model"
    sizes = []
    for p in prims:
        pkg, e = idx[p]
        try:
            sizes.append(prim_box_size(pkg.read(e)))
        except (rpkg.RpkgError, struct.error, ValueError) as ex:
            return None, "model %s unread (%s: %s)" % (names.get(p, "%016X" % p).split("?/")[-1].split("]")[0],
                                                     pkg.name, str(ex)[:40])
    longest = [max(s) for s in sizes]
    if max(longest) - min(longest) > MODELS_AGREE:
        return None, "%d models of different sizes (%s m)" % (len(sizes), ", ".join("%.2f" % x for x in longest))
    return max(sizes, key=max), ""


def slot_room(crate, items):
    """Per tool slot: its options' boxes' reach along the slot's X and Y and the longest side of any of them."""
    out = {}
    for name, (rot, slot, ty) in crate.items():
        box = items[ty][1]["box"]
        if slot not in TOOL_SLOTS or not box:
            continue
        center, half = box_in(rot, box)
        r = out.setdefault(slot, {"x": [9.0, -9.0], "y": [9.0, -9.0], "longest": []})
        for k, axis in ((0, "x"), (1, "y")):
            r[axis][0] = min(r[axis][0], center[k] - half[k])
            r[axis][1] = max(r[axis][1], center[k] + half[k])
        r["longest"].append((2 * max(half), name))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", action="append", required=True, help="folder with chunk0's packages")
    ap.add_argument("--names", required=True, help="ZHMTools hash_list.txt")
    ap.add_argument("--twins", action="store_true", help="print the C++ rows of kCrateTwins")
    ap.add_argument("--boxes", metavar="FILE", help="write the C++ rows of kCrateItemBoxes (src/core/CrateItemBoxes.inc)")
    ap.add_argument("--sizes", metavar="FILE",
                    help="write the C++ rows of kCrateItemModelSizes (src/core/CrateItemSizes.inc)")
    ap.add_argument("--cache", help="package metadata cache (for --dir build-landslide/deckcopy/chunk0sparse)")
    args = ap.parse_args(argv)
    idx = newest(args.dir, args.cache)
    names = rpkg.load_names(args.names, {h for h, (_, e) in idx.items() if e.type in ("TEMP", "PRIM")})
    items = {}
    for h, n in names.items():
        if ITEM_TEMPLATE in n and n.endswith(".pc_entitytype"):
            short = n.split(ITEM_TEMPLATE)[1].split(".entitytemplate")[0]
            try:
                items[h] = (short, item_info(Template(idx, h)))
            except (entity.Bin1Error, KeyError, struct.error, rpkg.RpkgError) as e:
                print("# %s: unread (%s)" % (short, e), file=sys.stderr)
    print("# %d item templates read, %d with a box" % (len(items), sum(1 for _, i in items.values() if i["box"])))

    crate = placements(idx, items, STASH)
    supplier = {}
    for h in SUPPLIER:
        supplier.update(placements(idx, items, h))

    print("\n== the crate's options (spawner in its slot's frame; box: size along the slot's axes, center)")
    for name, (rot, slot, ty) in sorted(crate.items(), key=lambda kv: (kv[1][1], kv[0])):
        up, tilt = up_axis(rot[:3])
        line = "%-22s %-30s up %s (%2.0f deg) yaw %s at %s" % (slot, name, up, tilt, yaw(rot[:3]), fmt(rot[3]))
        box = items[ty][1]["box"]
        line += "; box %s center %s" % (fmt([2 * x for x in box_in(rot, box)[1]]), fmt(box_in(rot, box)[0])) \
            if box else "; no box (%s)" % items[ty][1]["why"]
        print(line)

    print("\n== slot room (the tool slots' options' boxes: reach along the slot's X and Y, the longest sides)")
    room = slot_room(crate, items)
    for slot in TOOL_SLOTS:
        if slot in room:
            r = room[slot]
            top = sorted(r["longest"], reverse=True)[:3]
            print("%-22s X %+.3f..%+.3f  Y %+.3f..%+.3f  longest: %s" % (
                slot, r["x"][0], r["x"][1], r["y"][0], r["y"][1], ", ".join("%s %.3f" % (n, x) for x, n in top)))

    # The supplier pose to crate pose map, item by item (rotations as parent-from-local matrices).
    maps = Counter()
    for name in set(crate) & set(supplier):
        if crate[name][1] not in TOOL_SLOTS:
            continue
        c, s = rounded(crate[name][0]), rounded(supplier[name][0])
        maps[tuple(tuple(sum(c[k][i] * s[k][j] for k in range(3)) for j in range(3)) for i in range(3))] += 1
    print("\n== supplier wall to crate, per item in both (count: map)")
    for m, n in maps.most_common():
        print("%3d %s" % (n, m))

    # Twins: items on the supplier's wall with no crate option, whose box is a crate tool-slot item's box turned.
    rows = []
    for name in sorted(set(supplier) - set(crate)):
        rx, _, tx = supplier[name]
        if not items[tx][1]["box"]:
            continue
        best = None
        for twin in sorted(set(supplier) & set(crate)):
            if crate[twin][1] not in TOOL_SLOTS or not items[crate[twin][2]][1]["box"]:
                continue
            rt = supplier[twin][0]
            turn = [[int(round(dot(rt[j], rx[i]))) for i in range(3)] for j in range(3)]
            # The item's box in the twin's axes (twin axis j in the item's own axes: turn[j]).
            frame = (tuple(turn[0]), tuple(turn[1]), tuple(turn[2]), (0.0, 0.0, 0.0))
            cx, hx = box_in(frame, items[tx][1]["box"])
            ct, ht = box_in(IDENTITY, items[crate[twin][2]][1]["box"])
            size = max(abs(2 * a - 2 * b) for a, b in zip(hx, ht))
            center = max(abs(a - b) for a, b in zip(cx, ct))
            if size <= TWIN_SIZE and center <= TWIN_CENTER and (best is None or size + center < best[0]):
                best = (size + center, twin, turn, size, center)
        if best:
            rows.append((name, best))
    print("\n== twins (item: twin, box sides / center apart, turn; the twin's crate pose)")
    for name, (_, twin, turn, size, center) in rows:
        rot = crate[twin][0]
        print("%-34s %-26s %.4f / %.4f m, turn %s; twin %s up yaw %s, %.3f m up in %s" % (
            name, twin, size, center, turn, up_axis(rot[:3])[0], yaw(rot[:3]), rot[3][2], crate[twin][1]))

    if args.twins:
        print("\n// --- kCrateTwins (tools/rpkg/crate_poses.py --twins) ---")
        for name, (_, twin, turn, _, _) in rows:
            print("    // %s: as %s" % (name, twin))
            print("    {\"%s\", \"%s\", {%s}}," % (items[supplier[name][2]][1]["repo"], items[crate[twin][2]][1]["repo"],
                                               ", ".join(str(x) for r in turn for x in r)))

    if args.boxes:
        rows = {}
        for short, info in items.values():
            if not info["box"] or not info["repo"]:
                continue
            center, half = box_in(IDENTITY, info["box"])
            row = '    {"%s", {%s}, {%s}}, // %s\n' % (
                info["repo"], ", ".join("%.3f" % (round(x, 3) + 0.0) for x in center),
                ", ".join("%.3f" % (round(2 * x, 3) + 0.0) for x in half), short)
            if rows.get(info["repo"], row) != row:
                print("# %s: a second template with another box (%s), the first kept" % (info["repo"], short),
                      file=sys.stderr)
            rows.setdefault(info["repo"], row)
        with open(args.boxes, "w", newline="\n") as f:
            f.write("// Generated by tools/rpkg/crate_poses.py --boxes (chunk0patch3 and 4): each item's box center and size\n"
                    "// along its spawner's axes (m), from its helper_evergreen_selectableitem_<item> template.\n"
                    "// kCrateItemBoxes.\n")
            for repo in sorted(rows):
                f.write(rows[repo])
        print("\n# %d boxes written to %s" % (len(rows), args.boxes))

    if args.sizes:
        if PRO_REPO not in idx:
            sys.exit("--sizes: pro.repo (%016X) is not in the packages given" % PRO_REPO)
        pkg, e = idx[PRO_REPO]
        repo = [x for x in json.loads(pkg.read(e)) if "InventoryCategoryIcon" in x]
        boxed = {info["repo"] for _, info in items.values() if info["box"] and info["repo"]}
        rows, why = {}, Counter()
        print("\n== model sizes (items with no option template; the longest side first)")
        for it in repo:
            repo_id = it.get("ID_", "")
            icon = (it.get("InventoryCategoryIcon") or "").lower()
            try:
                runtime = int(it.get("Runtime") or 0)
            except ValueError:
                runtime = 0
            if repo_id in boxed or icon in FIREARM_ICONS or not runtime or repo_id in rows:
                continue
            size, no = model_size(idx, names, runtime)
            name = it.get("Title") or it.get("CommonName") or ""
            if size is None:
                why[no.split(" (")[0].split(" ")[0] if no.startswith("model") else no.split(" (")[0]] += 1
                continue
            rows[repo_id] = '    {"%s", {%s}}, // %s (%s)\n' % (
                repo_id, ", ".join("%.3f" % (round(x, 3) + 0.0) for x in size), name, it.get("ItemSize") or "-")
        for repo_id in sorted(rows, key=lambda r: rows[r].split("//")[1]):
            print(rows[repo_id].rstrip())
        print("# left out: %s" % ", ".join("%s %d" % kv for kv in why.most_common()))
        with open(args.sizes, "w", newline="\n") as f:
            f.write("// Generated by tools/rpkg/crate_poses.py --sizes (chunk0's packages; pro.repo of chunk0patch4): the\n"
                    "// model size (m, its bounding box's sides along the model's axes) of each item with no option\n"
                    "// template of its own, from the PRIMs its Runtime template's item art places. kCrateItemModelSizes.\n")
            for repo_id in sorted(rows):
                f.write(rows[repo_id])
        print("\n# %d model sizes written to %s" % (len(rows), args.sizes))


if __name__ == "__main__":
    main()
