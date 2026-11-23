"""One entity template (TEMP and its TBLU) as text: each sub-entity's index, sub-entity id, name, type, logical
parent and properties, the template root's property aliases and exposed entities, and the pins (standard library
only; run with python3 -I). Used for the sick spot steer (src/core/SickSpots.h: the sick spot templates' sub-entity ids,
the m_nPriority alias, the toilet core's ZContextKillGuide).

    python3 -I tools/rpkg/template_dump.py --dir build-landslide/deckcopy/chunk0 \\
        --dir build-landslide/deckcopy/chunk0sparse --chunks 0 --cache build-landslide/cache \\
        --names third_party/zhmtools/hash_list.txt 0098C7BD51E1C742 [HASH ...]

Lines: "E <index> <sub-entity id> '<name>' type=<type> parent=<index>", then its properties ("    <name or id>
(<type>) = <value>"), the root's "    alias ..." and "    exposed ..." lines, then "P <kind> <from> '<name>' <pin> ->
<to> '<name>' <pin>" (kind: con for a connection, in / out for an input / output forwarding).

Game files are IOI's: print, do not commit what this extracts.
"""
import argparse
import json
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import entity  # noqa: E402
import rpkg  # noqa: E402

KINDS = (("pinConnections", "con"), ("inputPinForwardings", "in"), ("outputPinForwardings", "out"))


def value_text(value, names):
    val = value.get("$val")

    if isinstance(val, dict) and "m_IDLow" in val:
        rid = (val["m_IDHigh"] << 32) | val["m_IDLow"]
        return names.get(rid, "%016X" % rid)

    return json.dumps(val, default=str)


def dump(resolver, names, temp_hash, width):
    found = resolver.get(temp_hash)

    if not found:
        print("== %016X: not found" % temp_hash)
        return

    pkg, e = found
    temp = entity.read_temp(pkg.read(e))
    refs = [x for _, x in e.refs]
    blueprint = refs[temp["blueprintIndexInResourceHeader"]]
    found = resolver.get(blueprint)

    if not found:
        print("== %016X: blueprint %016X not found" % (temp_hash, blueprint))
        return

    pkg2, e2 = found
    tblu = entity.read_tblu(pkg2.read(e2))
    subs = tblu["subEntities"]
    print("== %016X %s (blueprint %016X, root %d)" % (temp_hash, names.get(temp_hash, ""), blueprint,
                                                     tblu.get("rootEntityIndex", -1)))

    for i, (s, t) in enumerate(zip(subs, temp["subEntities"])):
        ti = t["entityTypeResourceIndex"]
        type_name = names.get(refs[ti], "%016X" % refs[ti]) if 0 <= ti < len(refs) else "?"
        print("E %d %016x %r type=%s parent=%d" % (i, s["entityId"], s["entityName"], type_name,
                                                   s["logicalParent"]["entityIndex"]))

        for p in t["propertyValues"] + t["postInitPropertyValues"]:
            print("    %s (%s) = %s" % (p["nPropertyID"], p["value"].get("$type"), value_text(p["value"], names)[:width]))

        for alias in s.get("propertyAliases") or []:
            print("    alias %s -> entity %s %s" % (alias.get("sAliasName"), alias.get("entityID"),
                                                  alias.get("sPropertyName")))

        for exposed in s.get("exposedEntities") or []:
            targets = [x.get("entityIndex") for x in exposed.get("aTargets", [])]
            print("    exposed %s -> %s" % (exposed.get("sName"), targets))

    def sub_name(k):
        return subs[k]["entityName"] if 0 <= k < len(subs) else "?"

    for key, kind in KINDS:
        for p in tblu.get(key, []):
            f, t = p["fromID"], p["toID"]
            print("P %s %d %r %s -> %d %r %s" % (kind, f, sub_name(f), p["fromPinName"], t, sub_name(t),
                                                p["toPinName"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--dir", action="append", required=True, help="a folder of packages; repeatable")
    parser.add_argument("--chunks", default="0", help="chunk numbers, comma separated")
    parser.add_argument("--cache", help="the metadata cache")
    parser.add_argument("--vanilla", action="store_true", help="IOI's own patches only")
    parser.add_argument("--names", help="hash list (hash.TYPE,path lines)")
    parser.add_argument("--width", type=int, default=300, help="property values are cut at this many characters")
    parser.add_argument("hashes", nargs="+", help="TEMP hashes (hex)")
    args = parser.parse_args()

    chunks = [int(c) for c in args.chunks.split(",")]
    resolver = rpkg.Resolver(args.dir, chunks, args.cache, args.vanilla)
    names = rpkg.load_names(args.names) if args.names else {}

    for h in args.hashes:
        dump(resolver, names, int(h, 16), args.width)


if __name__ == "__main__":
    main()
