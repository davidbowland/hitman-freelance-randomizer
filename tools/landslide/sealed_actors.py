#!/usr/bin/env python3
"""Build src/core/SealedActors.inc: per map variant, the NPCs that stand or walk in an area the variant seals off, so
the target pool never makes one of them a Freelancer target (src/core/Variants.h VariantTargetExclusions).

The Garden Show (Dartmoor, scenario_smoothsnake) closes the mansion: its navmesh has no floor in the rooms, and the
mansion staff of Freelancer's own target pool (mission_mild_bridge's dedicated targets, start disabled until picked)
stay there. A campaign that picked Ross Holt (a valet, upstairs at (36.9, 42.1, 11.0)) gave 47 a target he could not
reach.

For every appended option of a map's mild scene with a navmesh of its own (variant_audit.py's sealed check: both
navmeshes, the option's a replacement and not an extra mesh), every NPC (npcactor) of the option's scene (Freelancer's
bricks, the option's bricks and its fix brick, spotfix_spec.py) that the scene keeps is checked at its start and at
every act it plays (an act's m_pActor, through a role's m_rActorProvider: its route). A place is sealed off when
Freelancer's navmesh has floor within SEALED_ON_M of it and the option's has none on its main island within
SEALED_ON_M (spot_fix.py's test for the spots: the audit's 3 m let a floor behind a wall count). A place with no floor
under Freelancer's navmesh either (a seat, a parked actor below the map) says nothing. An NPC is listed when its start
or any of its acts is sealed off; it is never a target while the option is loaded (an entry of the pool is replaced or
dropped, a candidate is rejected), whatever its brick.

Full scene swaps (core/LandslideFull.*) load their own scene and navmesh: their pools are built by build_bricks.py
from actors chosen by hand and checked there; --full lists their pool actors on their navmesh against Freelancer's
(information only: nothing is written for them unless a pool actor is sealed, which fails the run).

    python3 -I tools/landslide/sealed_actors.py --cache build-landslide/cache \\
        --names third_party/zhmtools/hash_list.txt --json build-landslide/json --audit build-landslide/audit \\
        [--map Dartmoor] [--out src/core/SealedActors.inc]

--audit: the folder of variant_audit.py's per-map --bin folders (<audit>/<map, lower case, spaces as _>): every
brick and navmesh is read from there (or --json); nothing is extracted. A map whose folder or navmeshes are missing is
reported and left out of the table (its rows are kept from the --out file as it was, so a partial run never drops
them).
"""
import argparse
import os
import re
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "rpkg"))

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402
import landslide_audit as la  # noqa: E402
import spot_fix  # noqa: E402
import variant_audit as va  # noqa: E402

# spot_fix.SEALED_ON_M: floor within this (plan) under Freelancer's navmesh, none on the option's main island.
SEALED_ON_M = spot_fix.SEALED_ON_M
# The repository id of an npcactor (ZActor's RepositoryId; the hash list has no name for it).
REPO_PROPS = ("m_RepositoryId", "0xB9D7A9A0")
NPC_TEMPLATE = "npcactor"
GUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$")
ROW = re.compile(r'^\{"(\w+)", "([0-9a-f-]{36})", "[^"]*"\},')


def repo_of(scene, h, i):
    for name in REPO_PROPS:
        v, _ = scene.prop(h, i, name)
        if isinstance(v, str) and GUID.match(v.lower()):
            return v.lower()
    return None


def npcs(scene, names):
    """[(brick, index)] of every npcactor the scene keeps."""
    out = []
    for h, b in scene.B.items():
        for i, e in enumerate(b.temp["subEntities"]):
            if NPC_TEMPLATE in names.get(b.temp_refs[e["entityTypeResourceIndex"]][1], "").lower() and \
                    not scene.gone(h, i):
                out.append((h, i))
    return out


def acts_by_actor(scene, names):
    """{(brick, index) of an NPC: [act positions]}: every act the scene keeps, by the NPC it plays for."""
    out = {}
    for h, i, p in la.scene_acts(scene, names):
        k = la.actor_of(scene, h, i)
        if k is not None:
            out.setdefault(k, []).append(p)
    return out


def pool_actors(scene, bricks):
    """{(brick, index)} the target pools (any m_aActors) of these bricks name."""
    out = set()
    for h in bricks:
        b = scene.B.get(h)
        if b is None:
            continue
        for i in range(len(b.temp["subEntities"])):
            v, ctx = scene.prop(h, i, "m_aActors")
            for r in v if isinstance(v, list) else []:
                k = scene.resolve(ctx, r) if be.is_ref(r) else None
                if k is not None:
                    out.add(k)
    return out


class Sealer:
    """The sealed test of one option's navmesh against Freelancer's."""

    def __init__(self, freelancer, variant):
        self.f, self.v = freelancer, variant
        isl = variant.islands()
        self.main = max(isl, key=isl.get) if isl else None

    def sealed(self, q):
        """True: floor under Freelancer's navmesh, none on the option's main island (within SEALED_ON_M); False: the
        option has floor there; None: no floor under Freelancer's either (says nothing)."""
        if q is None or self.f.snap(q, min_clear=0.0, radius=SEALED_ON_M) is None:
            return None
        v = self.v.snap(q, min_clear=0.0, radius=SEALED_ON_M)
        return v is None or v[1].island != self.main


def fmt(q):
    return "(%.1f, %.1f, %.1f)" % tuple(q)


def check_npcs(scene, names, sealer, pool):
    """[(repo, comment)] of the scene's NPCs sealed off at their start or an act, sorted by repo; and the NPCs with no
    repository id that would be (a note)."""
    acts = acts_by_actor(scene, names)
    found, no_repo = {}, []
    for h, i in npcs(scene, names):
        b = scene.B[h]
        start = scene.pos(h, i)
        s = sealer.sealed(start)
        mine = acts.get((h, i), [])
        tests = [sealer.sealed(p) for p in mine]
        bad = [p for p, t in zip(mine, tests) if t]
        if not s and not bad:
            continue
        name = (scene.prop(h, i, "m_sActorName")[0] or "?").replace('"', "'").replace("\\", "/")
        why = []
        if s:
            why.append("start %s sealed" % fmt(start))
        if bad:
            why.append("%d of its %d acts sealed (first %s)" % (len(bad), len(mine), fmt(bad[0])))
        comment = '"%s" %s %016x%s: %s' % (name, b.name, b.tblu["subEntities"][i]["entityId"],
                                           " (Freelancer's target pool)" if (h, i) in pool else "", "; ".join(why))
        repo = repo_of(scene, h, i)
        if repo is None:
            no_repo.append(comment)
            continue
        if repo in found:
            found[repo] += " / " + comment
        else:
            found[repo] = comment
    return sorted(found.items(), key=lambda kv: (kv[1], kv[0])), no_repo


def run_option(ctx, map_name, scene_path, opt_name, opt):
    """(rows [(repo, comment)], lines) of one appended option on the map's mild scene, or (None, lines) when it has
    no navmesh of its own to compare."""
    newest, names, reader = ctx.newest, ctx.names, ctx.reader
    title = "%s mild: %s" % (map_name, opt_name)
    start = va.freelancer_start(newest, ctx.args.contracts, scene_path)
    fl = bb.scene_closure(newest, names, start)
    opt_hashes = [h for n, _, h in opt["bricks"] if h in newest]
    if len(opt_hashes) != len(opt["bricks"]):
        missing = [n for n, _, h in opt["bricks"] if h not in newest and not n.startswith("outfits_")]
        if missing:
            return None, ["%s: %s in no package of the metadata cache: not checked" % (title, ", ".join(missing))]
    var = fl | bb.scene_closure(newest, names, opt_hashes)
    fn, vn = va.navps(newest, fl), va.navps(newest, var) - va.navps(newest, fl)
    if not vn:
        return None, ["%s: no navmesh of its own: nothing sealed" % title]
    if len(fn) != 1 or len(vn) != 1:
        return None, ["%s: navmeshes NOT compared (Freelancer's %s, the option's %s)" % (
            title, ", ".join("%016X" % h for h in sorted(fn)), ", ".join("%016X" % h for h in sorted(vn)))]
    a, v = reader.navmesh(next(iter(fn))), reader.navmesh(next(iter(vn)))
    if a is None or v is None:
        return None, ["%s: NOT CHECKED: navmesh %s not in the audit folder" % (
            title, ", ".join("%016X" % h for h, m in ((next(iter(fn)), a), (next(iter(vn)), v)) if m is None))]
    if len(v.areas) < va.EXTRA_NAV_SHARE * len(a.areas):
        return None, ["%s: the option's navmesh %016X is an extra mesh (%d areas against %d): nothing sealed" % (
            title, next(iter(vn)), len(v.areas), len(a.areas))]
    vb, unread = va.load(reader, var, start + sorted(fl) + opt_hashes)
    unread_own = [h for h in unread if h not in fl]
    lines = []
    if unread:
        lines.append("%s: not read: %s" % (title, ", ".join(bb.gp_name(h) for h in unread)))
    if unread_own:
        return None, lines + ["%s: NOT CHECKED: the option's bricks are not all read" % title]
    fix = va.FIX_SPECS.get(opt_name)
    deletes = {(h, eid) for h, eid, _, _ in fix.DELETES} if fix else set()
    bricks = vb
    if fix and getattr(fix, "SPOT_FIX", False):
        bricks = dict(vb)
        bricks.update(spot_fix.audit_brick(fix, vb, names, va.Scene))
    scene = va.Scene(bricks, deletes)
    ours, _ = va.freelancer_bricks(newest, fl, scene_path, start)
    rows, no_repo = check_npcs(scene, names, Sealer(a, v), pool_actors(scene, ours))
    lines.append("%s: navmesh %016X against Freelancer's %016X%s: %d NPCs checked, %d sealed off" % (
        title, next(iter(vn)), next(iter(fn)), " (with the fix brick %s)" % fix.__name__ if fix else "",
        len(npcs(scene, names)), len(rows)))
    lines += ["  %s %s" % (r, c) for r, c in rows]
    lines += ["  NO REPOSITORY ID (not listed) %s" % c for c in no_repo]
    return rows, lines


def run_full(ctx, map_name, scene_path, opt_name, key):
    """(sealed pool actors' lines, lines) of a full scene swap: its built pool (build_bricks.py's mission brick, in
    --built) on its own navmesh against Freelancer's; ([], lines) when it cannot be checked."""
    newest, names, reader = ctx.newest, ctx.names, ctx.reader
    title = "%s mild: %s (full: %s)" % (map_name, opt_name, key)
    s = va.FULL_SPECS[key]
    if not ctx.args.built or not os.path.exists(os.path.join(ctx.args.built, bb.MANIFEST)):
        return [], ["%s: not checked (no --built)" % title]
    bs = la.built_spec(s)
    try:
        ours, _ = la.load_built(ctx.args.built, bs)
    except (IndexError, KeyError, OSError) as x:
        return [], ["%s: not checked (its built bricks: %s)" % (title, x)]
    start = va.freelancer_start(newest, ctx.args.contracts, scene_path)
    fl = bb.scene_closure(newest, names, start)
    loaded = bb.scene_closure(newest, names, [s.SCENE] + [h for b in ours.values() for h in b.scenes if h not in ours] +
                              la.layer_temps(s))
    fn, vn = va.navps(newest, fl), va.navps(newest, loaded)
    nav = getattr(bs, "NAVMESH", (None,))[0]
    if len(vn) != 1 and nav in vn:
        vn = {nav}
    if len(fn) != 1 or len(vn) != 1:
        return [], ["%s: navmeshes NOT compared (Freelancer's %s, its %s)" % (
            title, ", ".join("%016X" % h for h in sorted(fn)), ", ".join("%016X" % h for h in sorted(vn)))]
    a, v = reader.navmesh(next(iter(fn))), reader.navmesh(next(iter(vn)))
    if a is None or v is None:
        return [], ["%s: navmeshes NOT compared (%s not in the audit folder)" % (
            title, ", ".join("%016X" % h for h, m in ((next(iter(fn)), a), (next(iter(vn)), v)) if m is None))]
    bricks, _ = va.load(reader, loaded, [s.SCENE])
    bricks.update(ours)
    fix = {(h, eid) for h, items in getattr(bs, "DELETE_SCENE_PROPS", {}).items() for eid, _, _ in items}
    scene = va.Scene(bricks, fix)
    pool = pool_actors(scene, set(ours))
    sealer = Sealer(a, v)
    acts = acts_by_actor(scene, names)
    out = []
    for h, i in sorted(pool):
        q = scene.pos(h, i)
        bad = [p for p in [q] + acts.get((h, i), []) if sealer.sealed(p)]
        if bad:
            out.append('"%s" %s %016x (%s): %d of its places sealed (first %s)' % (
                scene.prop(h, i, "m_sActorName")[0], scene.B[h].name, scene.B[h].tblu["subEntities"][i]["entityId"],
                repo_of(scene, h, i), len(bad), fmt(bad[0])))
    return out, ["%s: navmesh %016X against Freelancer's %016X: %d pool actors checked, %d sealed off" % (
        title, next(iter(vn)), next(iter(fn)), len(pool), len(out))] + ["  SEALED POOL ACTOR " + x for x in out]


def read_rows(path):
    """{option: [(repo, comment line)]} of an existing table."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = ROW.match(line)
            if m:
                out.setdefault(m.group(1), []).append((m.group(2), line.rstrip("\n")))
    return out


def write_table(path, table, checked):
    """table {option: [(repo, comment)] or [(repo, line)] kept}; checked {option: what was checked}."""
    lines = [
        "// Generated by tools/landslide/sealed_actors.py from the game's bricks and navmeshes; rerun it after a map",
        "// variant's bricks change. Rows: {map variant (VariantOption::name), repository id, actor name}: an NPC whose",
        "// start or an act of its route is sealed off by the variant (floor within %.1f m under Freelancer's navmesh," %
        SEALED_ON_M,
        "// none on the variant's main island): never a Freelancer target while the variant is loaded",
        "// (VariantTargetExclusions). The comment: its brick and entity id, whether it is in Freelancer's own target",
        "// pool, and which of its places are sealed off.",
    ]
    for opt in sorted(table):
        rows = table[opt]
        lines.append("")
        lines.append("// %s: %s" % (opt, checked.get(opt, "kept from the previous table (not checked this run)")))
        for repo, comment in rows:
            if comment.startswith("{"):  # kept from the previous table as it was
                lines.append(comment)
                continue
            name = re.match(r'"([^"]*)"', comment)
            lines.append('{"%s", "%s", "%s"}, // %s' % (opt, repo, name.group(1) if name else "?",
                                                        comment[name.end() + 1:] if name else comment))
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(path + ".tmp", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", required=True, help="rpkg metadata folder")
    ap.add_argument("--names", required=True, help="hash list (hash.TYPE,path lines)")
    ap.add_argument("--json", action="append", help="folder of ResourceTool JSON (repeatable)")
    ap.add_argument("--audit", required=True, help="variant_audit.py's --bin folders' parent (one folder per map)")
    ap.add_argument("--built", help="build_bricks.py's --out (the full variants' pools; without it they are not "
                                    "checked)")
    ap.add_argument("--map", action="append", help="only these maps (Variants.cpp names)")
    ap.add_argument("--option", action="append", help="only these options")
    ap.add_argument("--contracts", default=os.path.expanduser("~/peacock/contractdata"))
    ap.add_argument("--src", default=os.path.join(REPO, "src", "core"))
    ap.add_argument("--out", default=os.path.join(REPO, "src", "core", "SealedActors.inc"))
    ap.add_argument("--patch-level", type=int, default=4)
    ap.add_argument("--dry-run", action="store_true", help="print only, write nothing")
    args = ap.parse_args()
    maps, options, full = va.parse_variants(args.src)
    names = bb.load_names(args.names)
    bb.NAMES.update(names)
    old = read_rows(args.out)
    table, checked = {}, {}
    full_sealed = []
    for map_name, scenes in maps.items():
        if args.map and map_name not in args.map or "mild" not in scenes:
            continue
        folder = os.path.join(args.audit, map_name.lower().replace(" ", "_"))
        chunk = va.CHUNKS.get(map_name)
        if chunk is None or not os.path.isdir(folder):
            print("=== %s: no audit folder (%s) or partition: not checked" % (map_name, folder))
            continue
        rargs = argparse.Namespace(json=args.json or [], bin=[folder], deck=None, cache=args.cache, built=args.built,
                                   patch_level=args.patch_level, contracts=args.contracts)
        ctx = va.Context(rargs, names)
        ctx.newest, _ = bb.gp.load_newest(args.cache, args.patch_level, (0, chunk))
        ctx.reader = va.Reader(rargs, chunk, ctx.newest)
        path, opts = scenes["mild"]
        print("=== %s (%s)" % (map_name, path.rsplit("/", 1)[-1]))
        for opt_name in opts:
            if args.option and opt_name not in args.option:
                continue
            if opt_name in full and opt_name not in va.FIX_SPECS:
                bad, lines = run_full(ctx, map_name, path, opt_name, full[opt_name])
                for line in lines:
                    print(line)
                full_sealed += bad
                continue
            rows, lines = run_option(ctx, map_name, path, opt_name, options[opt_name])
            for line in lines:
                print(line)
            if rows is None:
                continue
            checked[opt_name] = "%s mild, %d sealed off" % (map_name, len(rows))
            if rows:
                table[opt_name] = rows
            old.pop(opt_name, None)
    for opt, rows in old.items():
        table.setdefault(opt, rows)
    if full_sealed:
        # A full variant's pool is chosen by hand (build_bricks.py POOL_ADDITIONS, its spec's kept actors): fix the
        # spec, not this table.
        sys.exit("sealed_actors: %d pool actors of full variants are sealed off (see SEALED POOL ACTOR above): take "
                 "them out of the spec's pool" % len(full_sealed))
    if not args.dry_run:
        write_table(args.out, table, checked)
        print("wrote %s: %d rows for %d variants" % (args.out, sum(len(r) for r in table.values()), len(table)))


if __name__ == "__main__":
    main()
