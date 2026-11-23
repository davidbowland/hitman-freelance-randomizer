#!/usr/bin/env python3
"""Prints one entity template in full: every sub-entity (name, type resource, logical parent) with all its property
values, then the blueprint's pin connections and input/output pin forwardings (the pins the template answers to and
fires). Used to find a spawnable template's input pin names (what to signal) and output pins (what it reports).

    python3 -I tools/rpkg/entity_dump.py --deck build-landslide/deckcopy --chunks 0 \
        --cache build-landslide/cache --names third_party/zhmtools/hash_list.txt 00A824E80BF70DFE

The packages are read from <deck>/chunk<N> for each chunk plus <deck>/chunk0sparse (the Deck copy's rebuilt base
package, see sparse_parts.py), IOI's patches only. Property names print as their CRC-32 (0x...) unless entity.py
knows them.
"""
import argparse
import json
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import entity  # noqa: E402
import rpkg  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--deck", required=True, help="folder of chunk<N> package copies (build-landslide/deckcopy)")
    ap.add_argument("--chunks", default="0", help="chunk numbers, comma separated (first holding a hash wins)")
    ap.add_argument("--cache", help="package metadata cache")
    ap.add_argument("--names", help="hash list (third_party/zhmtools/hash_list.txt)")
    ap.add_argument("temp", help="TEMP hash (16 hex digits) or [resource path]")
    a = ap.parse_args(argv)
    chunks = [int(c) for c in a.chunks.split(",")]
    dirs = [os.path.join(a.deck, "chunk%d" % c) for c in chunks] + [os.path.join(a.deck, "chunk0sparse")]
    r = rpkg.Resolver([d for d in dirs if os.path.isdir(d)], chunks, a.cache, True)
    names = rpkg.load_names(a.names)
    hit = r.get(rpkg.parse_hash(a.temp))
    if not hit:
        sys.exit("%s: not in chunks %s" % (a.temp, a.chunks))
    pkg, e = hit
    temp = entity.read_temp(pkg.read(e))
    refs = [x for _, x in e.refs]
    tblu_hash = refs[temp["blueprintIndexInResourceHeader"]]
    hit2 = r.get(tblu_hash)
    tblu = {}
    try:
        if hit2:
            tblu = entity.read_tblu(hit2[0].read(hit2[1]))
    except (rpkg.RpkgError, entity.Bin1Error) as err:
        # The Deck copy of chunk0.rpkg is sparse: most base blueprints are zeros there.
        print("# blueprint %016X (%s) unreadable: %s; names and pins unknown" % (tblu_hash, hit2[0].name, err))
    if not hit2:
        print("# blueprint %016X not in the packages read; names and pins unknown" % tblu_hash)
    subs = tblu.get("subEntities") or [{"entityName": "?", "logicalParent": {"entityIndex": -2}}] * len(
        temp["subEntities"])

    def rn(rid):
        return names.get(rid, "%016X" % rid)

    print("# TEMP %s, refs:" % a.temp)
    for k, x in enumerate(refs):
        print("#   [%d] %s" % (k, rn(x)))
    for i, (s, t) in enumerate(zip(subs, temp["subEntities"])):
        ti = t["entityTypeResourceIndex"]
        tn = rn(refs[ti]) if 0 <= ti < len(refs) else "?"
        parent = s["logicalParent"]["entityIndex"] if tblu else t["logicalParent"]["entityIndex"]
        print("E %d %r type=%s parent=%d" % (i, s["entityName"], tn[-110:], parent))
        for p in t["propertyValues"] + t["postInitPropertyValues"]:
            v = p["value"]
            val = v.get("$val")
            text = json.dumps(val, default=str)
            if isinstance(val, dict) and "m_IDLow" in val:
                text = rn((val["m_IDHigh"] << 32) | val["m_IDLow"])
            print("    %s (%s) = %s" % (p["nPropertyID"], v.get("$type"), text[:300]))
    for kind in ("pinConnections", "inputPinForwardings", "outputPinForwardings"):
        for p in tblu.get(kind, []):
            f, t = p["fromID"], p["toID"]

            def nm(k):
                return subs[k]["entityName"] if 0 <= k < len(subs) else "?"
            print("P %s %d %r %s -> %d %r %s" % (kind[:3], f, nm(f), p["fromPinName"], t, nm(t), p["toPinName"]))


if __name__ == "__main__":
    main()
