"""The items a Freelancer supply crate may take from around the level (crate_level_items), from the game files
(standard library only; run with python3 -I). The source of src/core/NotPickupItems.inc.

    python3 -I tools/rpkg/level_items.py --deck build-landslide/deckcopy --cache build-landslide/cache \\
        --names third_party/zhmtools/hash_list.txt [--inc src/core/NotPickupItems.inc] [--map Berlin ...] [--walk]

Not for picking up (--inc): the pro.repo items the player gets no pick-up for. pro.repo names each item's runtime
template (Runtime, a resource id); a template built on one of the poisonable item cores (_pro/design/gamecore/
itemcore.template: itemcorepoisonable, itemcorepoisonable_interruptible, itemcorepoisonablefreealign, read from the
packages' reference tables) is a prop a target drinks or eats in an act and 47 poisons where it stands: its core
carries the act's keywords (STATE_ACT_ITEM_POISONABLE, STATE_ITEM_OWNED_BY_ACT), the poison interaction
(SetPieceHelpers_PoisonSource_*) and destroys the item one second after it is dropped. The ordinary cores
(itemcore, itemcorenophys, ccweaponcore...) carry the pick-up interaction alone. Also pro.repo's items with
EnablePickUpOnSpawn false (spawned with pick-up off: an NPC's phone). pro.repo is read from chunk0patch4; the
reference tables from the metadata cache (--cache), so chunk0.rpkg itself is not needed.

The walk (--walk): an approximation of CrateGuns' scene-playing walk, map by map: Freelancer's mild scene and its
contract's bricks (tools/landslide/variant_audit.py's), every entity of every brick and of every template instance
inside them, each ZItemSpawner outside the stash items and the crate option helpers, with its m_bSpawnOnStart (unset:
true, the class default; the runtime walk reads it), its m_rMainItemKey's m_RepositoryId, brick property overrides,
template instance aliases and brick deletes. A template whose TEMP is in chunk0.rpkg (only byte ranges of it are
copied from the Deck) is not read: its spawners are missing, and the walk counts the templates not read that hold an
item spawner (their reference tables say so). The items are then sorted by the mod's rules (ClassifyLevelItem, before
the not-for-picking-up rule) and the ones the rule removes are marked.

Game files are IOI's: keep extracted resources out of the repository (the .inc holds pro.repo ids and names only).
"""
import argparse
import json
import os
import sys
import zlib
from collections import Counter

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "landslide"))
import entity  # noqa: E402
import rpkg  # noqa: E402

PRO_REPO = 0x00204D1AFD76AB13  # [assembly:/repository/pro.repo].pc_repo
POISONABLE_CORES = {
    0x00A1598A3E2CCE19: "itemcorepoisonable",
    0x00559FC48BD38970: "itemcorepoisonable_interruptible",
    0x00D2B0BD56BC802D: "itemcorepoisonablefreealign",
}
SPAWNER_TYPES = {0x007E90F35FE99B5A, 0x00F946A2C274B5BE}  # [modules:/zitemspawner.class], game.item.moduleinfo's
STASH_TEMP, STASH_TBLU, OPTION_TBLU = 0x00100757521CC642, 0x00512EC270635802, 0x00BCA591C00A579D
SETUP = rpkg.resource_id("[assembly:/_pro/scenes/bricks/evergreen_setup.brick].pc_entitytype")
MAPS = ("Hokkaido", "Marrakesh", "Bangkok", "Paris", "Colorado", "Miami", "Mumbai", "Santa Fortuna", "Isle of Sgail",
        "Whittleton Creek", "Dartmoor", "Berlin", "Mendoza", "Chongqing", "Sapienza")
# QuestItems 47 uses on the level, kept in crates (src/core/CrateItems.cpp kUsableQuestProps; the other QuestItems
# found on the 15 maps are story items: ID cards, invitations, notes, codes).
USABLE_QUEST_PROPS = {
    "daa20d0d-5da4-4ab0-9c7b-39bcee192241",  # Fuse Cell
    "e65953cb-f954-4d21-9f11-52b454cac15e",  # Gun Powder
    "01048280-0358-4f0a-95b7-1f9f665c1648",  # Insecticide
    "ac77e98d-4ffa-4755-80fc-cd6e7adc63fb",  # Expired Canned Bolognese (Sick)
}
P_SPAWN = zlib.crc32(b"m_bSpawnOnStart")
P_KEY = zlib.crc32(b"m_rMainItemKey")
P_REPO = zlib.crc32(b"m_RepositoryId")


def crc(name):
    return int(name[2:], 16) if name.startswith("0x") else zlib.crc32(name.encode())


# --- pro.repo and the item cores -----------------------------------------------------------------------------------

def read_repo(deck, cache):
    dirs = [d for d in (os.path.join(deck, "chunk0"), os.path.join(deck, "chunk0sparse")) if os.path.isdir(d)]
    res = rpkg.Resolver(dirs, [0], cache, vanilla=True)
    pkg, e = res.get(PRO_REPO)
    return [x for x in json.loads(pkg.read(e)) if "InventoryCategoryIcon" in x]


def reference_tables(cache, chunks):
    """hash -> [referenced hashes], the newest copy across the chunks' vanilla packages (metadata cache only)."""
    import gen_package_inc as gp  # tools/landslide
    refs = {}
    for ch in chunks:
        newest, _ = gp.load_newest(cache, 4, (ch,))
        for h, e in newest.items():
            refs.setdefault(h, [r for _, r in e["refs"]])
    return refs


def runtime_of(item):
    try:
        return int(item.get("Runtime") or 0)
    except ValueError:
        return 0


def not_pickup_reason(item, refs):
    """Why the player gets no pick-up for a pro.repo item, or None."""
    cores = [POISONABLE_CORES[r] for r in refs.get(runtime_of(item), ()) if r in POISONABLE_CORES]
    if cores:
        return "poisonable prop (%s)" % cores[0]
    if item.get("EnablePickUpOnSpawn") is False:
        return "spawned with pick-up off (EnablePickUpOnSpawn false)"
    return None


def write_inc(path, items, refs):
    rows = []
    for it in items:
        why = not_pickup_reason(it, refs)
        if why:
            rows.append((it["ID_"], it.get("CommonName") or it.get("Title") or "", why))
    rows.sort()
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by tools/rpkg/level_items.py --inc (pro.repo of chunk0patch4, the packages' reference\n"
                "// tables): the items the player gets no pick-up for. kNotPickupItems.\n")
        for g, name, why in rows:
            f.write('    "%s", // %s: %s\n' % (g, name, why))
    return rows


# --- the mod's rules (src/core/CrateItems.cpp: ClassifyLevelItem) ----------------------------------------------------

def low(s):
    return (s or "").lower()


def classify(item):
    """ClassifyLevelItem's verdict before the not-for-picking-up rule (and before its name filter for drinks)."""
    if item is None:
        return "unnamed"
    name, common, icon, typ = item.get("Title") or item.get("CommonName") or "", item.get("CommonName") or "", \
        item.get("InventoryCategoryIcon") or "", item.get("ItemType") or ""
    if not name:
        return "unnamed"
    if not icon or "invalid" in low(icon):
        return "not inventory items"
    if low(icon) in ("pistol", "smg", "assaultrifle", "shotgun", "sniperrifle", "sniper"):
        return "firearms"
    if icon == "evergreen_payout" or "Mercer_Pickup" in name or "Mercer_Pickup" in common or "merces" in low(name) \
            or "payout" in low(icon) or "money" in low(icon):
        return "merces"
    if any(p in low(t) for t in (icon, typ, name, common) for p in ("container", "suitcase", "briefcase")):
        return "containers"
    if "ammo" in low(typ) or "ammo" in low(icon):
        return "ammo"
    if not runtime_of(item):
        return "no model"
    if any(p in low(t) for t in (name, common) for p in ("intel", "evidence", "dossier", "document")):
        return "story"
    if low(icon) == "questitem" and item.get("ID_") not in USABLE_QUEST_PROPS:
        return "quest items"
    return "kept"


def drink_filter(item):
    """The name filter this rule replaces (IsPoisonableDrink, commit ce1a078)."""
    name, common, icon = item.get("Title") or item.get("CommonName") or "", item.get("CommonName") or "", \
        item.get("InventoryCategoryIcon") or ""
    if "poisonable" in low(name) or "poisonable" in low(common):
        return True
    return "poison" in low(icon) and any(p in low(t) for t in (name, common) for p in ("glass", "cup", "mug", "plate"))


# --- the walk ------------------------------------------------------------------------------------------------------

class Template:
    def __init__(self, res, h):
        self.h = h
        pkg, e = res.get(h)
        self.refs = [x for _, x in e.refs]
        self.temp = entity.read_temp(pkg.read(e), "%016X" % h)
        self.subs = self.temp["subEntities"]
        self.tb = self.refs[self.temp["blueprintIndexInResourceHeader"]]
        try:
            bp, be = res.get(self.tb)
            self.tblu = entity.read_tblu(bp.read(be), "%016X" % self.tb)
        except Exception:  # noqa: BLE001 (a blueprint in chunk0.rpkg: no names, aliases or exposed entities)
            self.tblu = {"rootEntityIndex": self.temp["rootEntityIndex"], "overrideDeletes": [],
                         "subEntities": [{"entityId": -1 - i, "editorOnly": False, "propertyAliases": [],
                                          "exposedEntities": []} for i in range(len(self.subs))]}
        self.props = [{crc(p["nPropertyID"]): p["value"]["$val"]
                       for p in s["propertyValues"] + s["postInitPropertyValues"]} for s in self.subs]
        self.root = self.tblu["rootEntityIndex"]
        self.idx = {e["entityId"]: i for i, e in enumerate(self.tblu["subEntities"])}

    def type_of(self, i):
        t = self.subs[i]["entityTypeResourceIndex"]
        return self.refs[t] if 0 <= t < len(self.refs) else 0


class Instance:
    """A template instance: its template, the property values coming in (by entity index), its children."""

    def __init__(self, t, incoming, brick):
        self.t, self.incoming, self.brick = t, incoming, brick
        self.children = {}

    def props(self, i):
        d = dict(self.t.props[i])
        d.update(self.incoming.get(i, {}))
        return d


def incoming_of(t, values):
    """The values an instance's properties set inside template t: on its root, and through property aliases."""
    inc = {}

    def put(i, vals, depth):
        inc.setdefault(i, {}).update(vals)
        if depth > 10:
            return
        for a in t.tblu["subEntities"][i]["propertyAliases"]:
            c = crc(a["sAliasName"])
            if c in vals and 0 <= a["entityID"] < len(t.subs):
                put(a["entityID"], {zlib.crc32(a["sPropertyName"].encode()): vals[c]}, depth + 1)
    if 0 <= t.root < len(t.subs):
        put(t.root, values, 0)
    return inc


class Walker:
    def __init__(self, res, refs):
        self.res, self.refs = res, refs
        self.cache = {}
        self.unread = Counter()  # templates not read that hold an item spawner -> instances

    def template(self, h):
        if h not in self.cache:
            try:
                self.cache[h] = Template(self.res, h)
            except Exception:  # noqa: BLE001 (in chunk0.rpkg: not copied)
                self.cache[h] = None
        return self.cache[h]

    def holds_spawner(self, h, seen=None):
        seen = set() if seen is None else seen
        if h in seen:
            return False
        seen.add(h)
        rs = self.refs.get(h, ())
        return any(r in SPAWNER_TYPES for r in rs) or any(self.holds_spawner(r, seen) for r in rs
                                                          if self.kind(r) == "temp")

    def kind(self, h):
        if h in SPAWNER_TYPES:
            return "spawner"
        hit = self.res.get(h)
        typ = hit[1].type if hit else None
        if typ == "TEMP":
            return "temp"
        if typ == "ASET":
            rs = [r for _, r in hit[1].refs]
            if any(r in SPAWNER_TYPES for r in rs):
                return "spawner"
            return "aset"
        return None

    def nested(self, h):
        """The template an entity type instantiates (a TEMP, or an aspect's TEMP), or None."""
        k = self.kind(h)
        if k == "temp":
            return h
        if k == "aset":
            for _, r in self.res.get(h)[1].refs:
                if self.kind(r) == "temp":
                    return r
        return None

    def walk(self, inst, out, gone, bricks, depth=0):
        t = inst.t
        for i in range(len(t.subs)):
            if i in gone or t.tblu["subEntities"][i].get("editorOnly"):
                continue
            typ = t.type_of(i)
            if self.kind(typ) == "spawner":
                out.append((inst, i))
                continue
            n = self.nested(typ)
            if n is None or n in bricks or depth > 25:
                continue
            sub = self.template(n)
            if sub is None:
                if self.holds_spawner(n):
                    self.unread[n] += 1
                continue
            if n == STASH_TEMP or sub.tb in (STASH_TBLU, OPTION_TBLU):
                continue  # the stash items and the option helpers: the crates' and the map's own sources
            child = Instance(sub, incoming_of(sub, inst.props(i)), inst.brick)
            inst.children[i] = child
            self.walk(child, out, set(), bricks, depth + 1)


def resolve(inst, ref, depth=0):
    if not ref or ref["externalSceneIndex"] >= 0 or depth > 10:
        return None
    i = ref["entityIndex"]
    if i < 0 or i >= len(inst.t.subs):
        return None
    if not ref.get("exposedEntity"):
        return inst, i
    sub = inst.children.get(i)
    if sub is None:
        return None
    for e in sub.t.tblu["subEntities"][sub.t.root]["exposedEntities"]:
        if e["sName"] == ref["exposedEntity"] and e["aTargets"]:
            return resolve(sub, e["aTargets"][0], depth + 1)
    return None


def repository_id(inst, i, depth=0):
    p = inst.props(i)
    if P_REPO in p:
        return p[P_REPO]
    sub = inst.children.get(i)
    return repository_id(sub, sub.t.root, depth + 1) if sub is not None and depth < 5 else None


def scene_spawners(walker, closure, first):
    bricks = {}
    for h in list(first) + sorted(set(closure) - set(first)):
        if h not in bricks:
            t = walker.template(h)
            if t is not None:
                bricks[h] = t
    overrides, deletes = {}, set()
    for h, t in bricks.items():
        scenes = [t.refs[k] for k in t.temp["externalSceneTypeIndicesInResourceHeader"]]
        for o in t.temp["propertyOverrides"]:
            r = o["propertyOwner"]
            if 0 <= r["externalSceneIndex"] < len(scenes):
                sc = scenes[r["externalSceneIndex"]]
                if sc not in bricks or r["entityID"] not in bricks[sc].idx:
                    continue
                key = (sc, bricks[sc].idx[r["entityID"]])
            elif r["externalSceneIndex"] < 0 and r["entityIndex"] >= 0:
                key = (h, r["entityIndex"])
            else:
                continue
            pv = o["propertyValue"]
            overrides.setdefault(key, {})[crc(pv["nPropertyID"])] = pv["value"]["$val"]
        for r in t.tblu["overrideDeletes"]:
            if 0 <= r["externalSceneIndex"] < len(scenes):
                sc = scenes[r["externalSceneIndex"]]
                if sc in bricks and r["entityID"] in bricks[sc].idx:
                    deletes.add((sc, bricks[sc].idx[r["entityID"]]))
    found = []
    for h, t in bricks.items():
        inst = Instance(t, {i: d for (b, i), d in overrides.items() if b == h}, h)
        dead = {i for (b, i) in deletes if b == h}
        gone = set()
        for i in range(len(t.subs)):  # a deleted entity and its logical children in its own brick
            k, n = i, 0
            while k is not None and n < 100:
                if k in dead:
                    gone.add(i)
                    break
                lp = t.subs[k]["logicalParent"]
                k = lp["entityIndex"] if lp["externalSceneIndex"] < 0 and 0 <= lp["entityIndex"] != k else None
                n += 1
        walker.walk(inst, found, gone, set(bricks))
    out = []
    for inst, i in found:
        p = inst.props(i)
        key = resolve(inst, p.get(P_KEY)) if p.get(P_KEY) else None
        out.append({"setup": inst.brick == SETUP, "start": p.get(P_SPAWN, True) is not False,
                    "item": repository_id(*key) if key else None})
    return out


def run_map(args, name, items, refs, names):
    import build_bricks as bb  # tools/landslide
    import variant_audit as va  # tools/landslide
    chunk = va.CHUNKS[name]
    newest, _ = bb.gp.load_newest(args.cache, 4, (0, chunk))
    dirs = [os.path.join(args.deck, d) for d in ("chunk%d" % chunk, "chunk0sparse", "chunk0")]
    res = rpkg.Resolver([d for d in dirs if os.path.isdir(d)], [chunk, 0], args.cache, vanilla=True, patch_level=4)
    maps, _, _ = va.parse_variants(os.path.join(REPO, "src", "core"))
    path = maps[name]["mild"][0]
    first = va.freelancer_start(newest, args.contracts, path)
    closure = bb.scene_closure(newest, names, first)
    walker = Walker(res, refs)
    spawners = scene_spawners(walker, closure, first)
    by_id = {it["ID_"]: it for it in items}
    distinct = sorted({s["item"] for s in spawners if s["start"] and not s["setup"] and s["item"]})
    rows = []
    for g in distinct:
        it = by_id.get(g)
        verdict = classify(it)
        label = (it.get("Title") or it.get("CommonName")) if it else g
        rows.append({"guid": g, "name": label, "common": it.get("CommonName", "") if it else "",
                     "icon": it.get("InventoryCategoryIcon", "") if it else "", "verdict": verdict,
                     "not_pickup": not_pickup_reason(it, refs) if it else None,
                     "drink_filter": drink_filter(it) if it else False})
    stats = {"scene": path.rsplit("/", 1)[-1], "bricks": len(closure), "spawners": len(spawners),
             "at_start": sum(1 for s in spawners if s["start"] and not s["setup"] and s["item"]),
             "unread": sum(walker.unread.values()), "unread_templates": len(walker.unread),
             "unread_names": ["%s x%d" % (names.get(h, "%016X" % h).split("?/")[-1].split("]")[0].rsplit("/", 1)[-1]
                                          .replace(".entitytemplate", ""), n)
                              for h, n in walker.unread.most_common()]}
    return stats, rows


def markdown(name, stats, rows):
    kept = [r for r in rows if r["verdict"] == "kept"]
    removed = [r for r in kept if r["not_pickup"]]
    lines = ["### %s (%s)" % (name, stats["scene"]), "",
             "%d bricks; %d item spawners read, %d spawn at start with an item; %d distinct items, %d kept by the "
             "rules before this one, %d removed by it; %d template instances not read hold item spawners (%d "
             "templates, chunk0.rpkg)." % (stats["bricks"], stats["spawners"], stats["at_start"], len(rows), len(kept),
                                           len(removed), stats["unread"], stats["unread_templates"]), ""]
    if stats["unread_names"]:
        lines.append("Not read (chunk0.rpkg), holding item spawners: " + ", ".join(stats["unread_names"]) + ".")
        lines.append("")
    if removed:
        lines.append("Removed (no pick-up):")
        lines.append("")
        for r in removed:
            lines.append("- %s (`%s`, %s): %s%s" % (r["name"], r["common"], r["icon"], r["not_pickup"],
                                                    "" if r["drink_filter"] else "; the name filter missed it"))
        lines.append("")
    missed = [r for r in kept if r["drink_filter"] and not r["not_pickup"]]
    if missed:
        lines.append("The name filter dropped, the new rule keeps: " + ", ".join(r["name"] for r in missed) + ".")
        lines.append("")
    stay = [r for r in kept if not r["not_pickup"]]
    lines.append("Kept (%d): %s." % (len(stay), ", ".join(
        "%s (%s%s)" % (r["name"], r["icon"], ", kept: usable prop" if r["guid"] in USABLE_QUEST_PROPS else "")
        for r in stay)))
    other = Counter(r["verdict"] for r in rows if r["verdict"] != "kept")
    if other:
        lines.append("")
        lines.append("Left out by the other rules: " + ", ".join("%d %s" % (n, v) for v, n in sorted(other.items()))
                     + " (" + "; ".join("%s: %s" % (r["verdict"], r["name"]) for r in rows
                                        if r["verdict"] not in ("kept",)) + ").")
    lines.append("")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--deck", required=True, help="folder of chunk<N> package folders (build-landslide/deckcopy)")
    ap.add_argument("--cache", required=True, help="rpkg metadata cache")
    ap.add_argument("--names", required=True, help="hash list (hash.TYPE,path lines)")
    ap.add_argument("--inc", help="write the not-for-picking-up table (src/core/NotPickupItems.inc)")
    ap.add_argument("--walk", action="store_true", help="print each map's level items (markdown)")
    ap.add_argument("--map", action="append", help="only these maps (default: all 15 with packages)")
    ap.add_argument("--contracts", default=os.path.expanduser("~/peacock/contractdata"),
                    help="Peacock's contractdata (the contracts' bricks)")
    args = ap.parse_args(argv)
    items = read_repo(args.deck, args.cache)
    chunks = [0] + [c for c in (3, 4, 5, 6, 15, 16, 17, 18, 19, 22, 23, 24, 25, 26, 27)]
    refs = reference_tables(args.cache, chunks)
    if args.inc:
        rows = write_inc(args.inc, items, refs)
        print("%s: %d items" % (args.inc, len(rows)))
    if args.walk:
        import build_bricks as bb  # tools/landslide
        names = bb.load_names(args.names)
        bb.NAMES.update(names)
        for name in args.map or MAPS:
            stats, rows = run_map(args, name, items, refs, names)
            print(markdown(name, stats, rows), flush=True)


if __name__ == "__main__":
    main()
