#!/usr/bin/env python3
"""Checks the entity subsets of already built bricks (a build_bricks.py --out folder, such as build-landslide/built):
every member's type must be one a vanilla brick has in the same subset, or a template whose blueprint's root (read
from --json or --bin) has the subset. A member of any other type crashes the scene load in
ZTemplateEntityBlueprintFactory's constructor (HITMAN3.exe+0xF2DE19; see brick_edit.subset_type_problems). The
evidence is each built brick's vanilla source (the manifest's "source"). build_bricks.py runs the same check on what
it builds; this one looks at a package built before the check existed.

    python3 -I tools/landslide/subset_check.py --built build-landslide/built --json build-landslide/json \\
        --cache build-landslide/cache [--bin <extracted folder> ...]

Exits 1 when a member is found whose type is not known to carry its subset.
"""
import argparse
import json
import os
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import brick_edit as be  # noqa: E402
import build_bricks as bb  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--built", required=True)
    ap.add_argument("--json", required=True, action="append")
    ap.add_argument("--cache", required=True)
    ap.add_argument("--bin", action="append")
    ap.add_argument("--patch-level", type=int, default=4)
    args = ap.parse_args()
    args.navp = None
    with open(os.path.join(args.built, bb.MANIFEST)) as f:
        manifest = json.load(f)
    newest = {}
    for v in manifest["variants"]:
        part = v["partition"][0]
        if part not in newest:
            newest[part], _ = bb.gp.load_newest(args.cache, args.patch_level, (0, part))
    bad = 0
    for v in manifest["variants"]:
        res = {(r["path"], r["type"]): r for r in v.get("resources", [])}
        for path in dict.fromkeys(p for p, _ in res):
            t, u = res[(path, "TEMP")], res[(path, "TBLU")]
            js = []
            for r in (t, u):
                with open(os.path.join(args.built, r["json"])) as f:
                    js.append(json.load(f))
            built = be.Brick(path.rsplit("/", 1)[-1], js[0], js[1], [(x, int(y, 16)) for x, y in t["refs"]],
                             [(x, int(y, 16)) for x, y in u["refs"]], editable=False)
            src = (int(t["source"], 16), int(u["source"], 16))
            st, su = bb.find_json(args.json, src[0], "TEMP"), bb.find_json(args.json, src[1], "TBLU")
            nw = newest[v["partition"][0]]
            vanilla = [] if st is None or su is None else [
                be.Brick("source", st, su, nw[src[0]]["refs"], nw[src[1]]["refs"], editable=False)]
            problems = be.subset_type_problems(built, be.subset_type_evidence(vanilla),
                                               lambda h: bb.blueprint_root_subsets(args, h))
            print("%s %s: %d subset members, %d of a type not known to carry the subset%s" % (
                v["name"], built.name, len(built.subset_members()), len(problems),
                "" if vanilla else " (its source %016X is not in --json: no evidence)" % src[0]))
            for text, definite in problems:
                print("  %s %s" % ("CRASH" if definite else "UNKNOWN", text))
            bad += len(problems)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
