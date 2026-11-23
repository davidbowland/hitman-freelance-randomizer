#!/usr/bin/env python3
"""Finds the entity templates (TEMP) in the map chunks that use one of the given resources as a sub-entity type, and
prints the pin connections and forwardings into and out of those sub-entities from the users' blueprints. Answers
"what input pins does the game fire on this template, and what does it listen to from it" when the template's own
blueprint is unreadable (the Deck copy of chunk0.rpkg is sparse; the map chunks are whole).

    python3 -I tools/rpkg/template_users.py --deck build-landslide/deckcopy --cache build-landslide/cache \
        [--chunks 3,4,5] 00A824E80BF70DFE,00E8BB33DEDD9125

The deck folder holds chunk<N>/ copies of the packages (IOI's patches only are read).
"""
import argparse
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import entity  # noqa: E402
import rpkg  # noqa: E402

MAP_CHUNKS = "3,4,5,6,15,16,17,18,19,22,23,24,25,26,27"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--deck", required=True)
    ap.add_argument("--cache")
    ap.add_argument("--chunks", default=MAP_CHUNKS)
    ap.add_argument("--max-users", type=int, default=8, help="users printed per chunk")
    ap.add_argument("hashes", help="resource hashes, comma separated")
    a = ap.parse_args(argv)
    want = {rpkg.parse_hash(h) for h in a.hashes.split(",")}
    for c in [int(x) for x in a.chunks.split(",")]:
        d = os.path.join(a.deck, "chunk%d" % c)
        if not os.path.isdir(d):
            continue
        dirs = [d] + ([os.path.join(a.deck, "chunk0sparse")] if c == 0 else [])  # sparse_parts.py's rebuild
        try:
            r = rpkg.Resolver(dirs, [c], a.cache, True)
        except rpkg.RpkgError as err:
            print("chunk%d: %s" % (c, err))
            continue
        shown = 0
        for h, (p, e) in sorted(r.newest.items()):
            if e.type != "TEMP":
                continue
            refs = [x for _, x in e.refs]
            hit = {k for k, x in enumerate(refs) if x in want}
            if not hit:
                continue
            try:
                temp = entity.read_temp(p.read(e))
            except (rpkg.RpkgError, entity.Bin1Error) as err:
                print("chunk%d TEMP %016X unreadable: %s" % (c, h, err))
                continue
            subs = [i for i, s in enumerate(temp["subEntities"]) if s["entityTypeResourceIndex"] in hit]
            b = r.get(refs[temp["blueprintIndexInResourceHeader"]])
            tblu = None
            try:
                tblu = entity.read_tblu(b[0].read(b[1])) if b else None
            except (rpkg.RpkgError, entity.Bin1Error):
                pass
            shown += 1
            if shown > a.max_users:
                continue
            print("chunk%d TEMP %016X (%s): sub-entities %s; blueprint %s" % (
                c, h, p.name, subs, "read" if tblu else "unreadable"))
            if not tblu:
                continue
            names = [s["entityName"] for s in tblu["subEntities"]]

            def nm(k):
                return names[k] if 0 <= k < len(names) else "?"
            for i in subs:
                print("    sub %d %r (type %016X)" % (i, nm(i), refs[temp["subEntities"][i]["entityTypeResourceIndex"]]))
            for kind in ("pinConnections", "inputPinForwardings", "outputPinForwardings"):
                for pc in tblu.get(kind, []):
                    if pc["fromID"] in subs or pc["toID"] in subs:
                        print("    %s %r.%s -> %r.%s" % (kind[:3], nm(pc["fromID"]), pc["fromPinName"], nm(pc["toID"]),
                                                       pc["toPinName"]))
        if shown > a.max_users:
            print("chunk%d: %d more users not printed" % (c, shown - a.max_users))


if __name__ == "__main__":
    main()
