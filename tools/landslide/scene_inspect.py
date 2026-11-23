#!/usr/bin/env python3
"""Look around a full variant's scene as built (landslide_audit's Scene: the variant's scene, our built bricks and a
layer's), for placing or checking a spot by hand:

  near   <entity id>...         what stands within --radius of each (plan; 3 m in height), with blockers that hold
                                it and doors within 8 m, and the floor references (acts, litter) beside it with their
                                navmesh offsets (navmesh floor less their height) and placement.Placement.floor_z there
  box    x0 x1 y0 y1 z0 z1      every placed entity in the box (--match: a substring of its name or type)
  navmap x0 x1 y0 y1 z          an ASCII plan of the navmesh at about height z ('.' floor, --mark x,y,z points by
                                letter A, B, ...), with each mark's floor, island and clearance

python3 -I tools/landslide/scene_inspect.py --variant badboy near 42856f6e1c25f4a8
python3 -I tools/landslide/scene_inspect.py --variant icon box 78.3 81 -21 -18.8 11.5 14.5 --match shelving
python3 -I tools/landslide/scene_inspect.py --variant icon navmap 66 92 -46 -10 12 --mark 82.96,-22.61,12.04

The defaults are the repo's build-landslide folders (--built build-landslide/built, the Sapienza read)."""
import argparse
import math
import os
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "rpkg"))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))

import build_bricks as bb  # noqa: E402
import landslide_audit as la  # noqa: E402
import navp  # noqa: E402
import placement as pl  # noqa: E402


def load(args):
    """(Scene, names, the navmesh or None, our built bricks' hashes)."""
    newest, _ = bb.gp.load_newest(args.cache, args.patch_level, (0, args.partition))
    names = bb.load_names(args.names)
    bb.NAMES.update(names)
    s = la.SPECS[args.variant]
    bs = la.built_spec(s)
    ours, _ = la.load_built(args.built, bs)
    loaded = bb.scene_closure(newest, names, [s.SCENE] + [h for b in ours.values() for h in b.scenes if h not in ours]
                              + la.layer_temps(s))
    bricks = {}
    for h in sorted(loaded, key=lambda x: (x != s.SCENE, x)):
        b = la.read_brick(h, newest, args.json, args.bin)
        if b is not None:
            bricks[h] = b
    bricks.update(ours)
    mesh = None
    vn = la.scene_navmesh(newest, loaded, args.bin)
    if vn and vn[1]:
        mesh = vn[1]
    elif getattr(bs, "NAVMESH", None):
        path = la.find_bin(args.bin, bs.NAVMESH[0], "NAVP")
        if path:
            with open(path, "rb") as f:
                mesh = navp.NavMesh(f.read())
    return la.Scene(bricks), names, mesh, set(ours)


def path_of(sc, names, h, j):
    b = sc.B[h]
    return names.get(b.temp_refs[b.temp["subEntities"][j]["entityTypeResourceIndex"]][1], "")


def line(sc, names, h, j, q, extra=""):
    b = sc.B[h]
    path = path_of(sc, names, h, j)
    return "%-8s %s %016x \"%s\" %s%s%s" % (la.kind_of(path), bb.gp_name(h), b.tblu["subEntities"][j]["entityId"],
                                         b.entity_name(j), path.rsplit("/", 1)[-1][:60],
                                         " GONE" if sc.gone(h, j) else "", extra)


def near(args, sc, names, mesh, ours):
    want = {int(t, 16) for t in args.ids}
    refs = la.scene_floor_refs(sc, names, ours)
    judge = pl.Placement(mesh, None, [p for k, _, p in refs if k == "act"],
                         floor_refs=[p for k, _, p in refs if k == "litter"]) if mesh is not None else None
    for h, b in sc.B.items():
        for i, e in enumerate(b.tblu["subEntities"]):
            if e["entityId"] not in want:
                continue
            p = sc.pos(h, i)
            print("== %s at %s" % (line(sc, names, h, i, p), pl.fmt(p) if p else "NO PLACE"))
            if p is None:
                continue
            hit = mesh.area_at(p, up=0.5, down=1.5) if mesh is not None else None
            if hit is not None:
                print("   navmesh %.3f (%.3f below it), clearance %.2f m, island %d; floor_z %.3f (%+.3f from it)" % (
                    hit[1], hit[1] - p[2], mesh.clearance((p[0], p[1], hit[1])), hit[0].island,
                    judge.floor_z((p[0], p[1], hit[1])), p[2] - judge.floor_z((p[0], p[1], hit[1]))))
            print("   floor references within %.1f m:" % args.radius)
            for d, k, desc, q in sorted((pl.plan_d(p, q), k, desc, q) for k, desc, q in refs
                                        if pl.plan_d(p, q) < args.radius and abs(q[2] - p[2]) < 1.0)[:10]:
                rh = mesh.area_at(q, up=0.5, down=0.5) if mesh is not None else None
                print("     %5.2f m  z %.3f (%+.3f)  %s%s" % (d, q[2], q[2] - p[2], desc,
                                                          "  navmesh offset %.3f" % (rh[1] - q[2]) if rh else ""))
            print("   around it:")
            for h2, b2 in sc.B.items():
                for j in range(len(b2.temp["subEntities"])):
                    q = sc.pos(h2, j)
                    if q is None or (h2, j) == (h, i):
                        continue
                    path = path_of(sc, names, h2, j)
                    k = la.kind_of(path)
                    if k == "logic":
                        continue
                    d, dz = pl.plan_d(p, q), q[2] - p[2]
                    shape = la.blocker_shape(sc.P[h2][j]) if k == "blocker" else None
                    w = sc.world(h2, j)
                    inside = shape is not None and w is not None and la.in_blocker(p, w, shape, 0.0)[0]
                    if (d < args.radius and abs(dz) < 3) or inside or ("door" in path.lower() and d < 8 and
                                                                       abs(dz) < 4):
                        print("     %5.2f m dz %+5.2f %s%s" % (d, dz, line(sc, names, h2, j, q),
                                                             " INSIDE-BLOCKER" if inside else ""))


def box(args, sc, names, mesh, ours):
    x0, x1, y0, y1, z0, z1 = args.bounds
    for h, b in sc.B.items():
        for j in range(len(b.temp["subEntities"])):
            w = sc.world(h, j)
            if w is None:
                continue
            q = (w[0][3], w[1][3], w[2][3])
            if not (x0 <= q[0] <= x1 and y0 <= q[1] <= y1 and z0 <= q[2] <= z1):
                continue
            path = path_of(sc, names, h, j)
            if la.kind_of(path) == "logic":
                continue
            if args.match and args.match.lower() not in (b.entity_name(j) + path).lower():
                continue
            extra = ""
            if la.kind_of(path) == "blocker":
                shape = la.blocker_shape(sc.P[h][j])
                if shape:
                    extra = " %s %s collideHitman=%s" % (shape[0], shape[1], la.prop_value(sc.P[h][j],
                                                                                         "m_bCollideHitman"))
            print("%s X(%.2f, %.2f) %s" % (pl.fmt(q), w[0][0], w[1][0], line(sc, names, h, j, q, extra)))


def navmap(args, sc, names, mesh, ours):
    if mesh is None:
        sys.exit("navmap: the variant's navmesh is not in --bin")
    x0, x1, y0, y1, z = args.bounds
    marks = {chr(ord("A") + n): tuple(map(float, m.split(","))) for n, m in enumerate(args.mark or [])}
    for k, p in marks.items():
        hit = mesh.area_at(p)
        print("%s %s %s" % (k, pl.fmt(p), "floor %.2f, island %d, clearance %.2f m" % (
            hit[1], hit[0].island, mesh.clearance((p[0], p[1], hit[1]))) if hit else "NO FLOOR"))
    st = args.step
    y = y1
    while y >= y0:
        row = ""
        x = x0
        while x <= x1:
            ch = next((k for k, p in marks.items() if abs(p[0] - x) < st / 2 and abs(p[1] - y) < st / 2), None)
            if ch is None:
                ch = "." if mesh.area_at((x, y, z), up=0.6, down=0.6) else " "
            row += ch
            x += st
        print("%7.1f %s" % (y, row))
        y -= st


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--variant", required=True, choices=sorted(la.SPECS))
    ap.add_argument("--built", default=os.path.join(REPO, "build-landslide", "built"))
    ap.add_argument("--json", action="append")
    ap.add_argument("--bin", action="append", help="folders of extracted binaries (default: the Sapienza read)")
    ap.add_argument("--cache", default=os.path.join(REPO, "build-landslide", "cache"))
    ap.add_argument("--names", default=os.path.join(REPO, "third_party", "zhmtools", "hash_list.txt"))
    ap.add_argument("--partition", type=int, default=26, help="the map's chunk (26: Sapienza)")
    ap.add_argument("--patch-level", type=int, default=4)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("near")
    p.add_argument("ids", nargs="+")
    p.add_argument("--radius", type=float, default=2.0)
    p = sub.add_parser("box")
    p.add_argument("bounds", nargs=6, type=float)
    p.add_argument("--match")
    p = sub.add_parser("navmap")
    p.add_argument("bounds", nargs=5, type=float)
    p.add_argument("--mark", action="append", help="x,y,z (repeatable)")
    p.add_argument("--step", type=float, default=0.4)
    args = ap.parse_args()
    args.json = args.json or [os.path.join(REPO, "build-landslide", "json")]
    args.bin = args.bin or [os.path.join(REPO, "build-landslide", "audit", "sapienza")]
    sc, names, mesh, ours = load(args)
    {"near": near, "box": box, "navmap": navmap}[args.cmd](args, sc, names, mesh, ours)


if __name__ == "__main__":
    main()
