#!/usr/bin/env python3
"""Generates src/mod/LandslideRecipes.inc (committed): recipes that rebuild what FreelancerRandomizer's packages hold
from vanilla resources the player's game already has, and the tables of their safety checks, per full variant. One
package per partition: Sapienza's Runtime/chunk26patch290.rpkg with the full Landslide's, the full Author's and the
full Icon's bricks; Marrakesh's chunk25patch290.rpkg with the full Marrakesh night's; Hokkaido's chunk22patch290.rpkg
with the full Patient Zero's; Chongqing's chunk6patch290.rpkg with the full Gluttony's fix brick; and the spot fixes'.

    python3 -I tools/landslide/gen_package_inc.py --cache <rpkg metadata folder> --raw <extracted resources folder> \
        [--raw <another> ...] --built <build_bricks.py output folder> [--names <hash list>] \
        --recipes src/mod/LandslideRecipes.inc

--cache holds the start of every rpkg of the variants' chunks and chunk 0 (header, deletions, index and info table),
named <package>.rpkg.<size>.meta, as tools/rpkg/rpkg.py meta caches them. --raw folders (searched with their
subfolders) hold decompressed vanilla resources named <package stem>_<HASH>.<TYPE> (rpkg.py extract): the newest copy
of every candidate source below. --names is a hash list (hash.TYPE,path lines) for the sources' names in the log.

--built holds build_bricks.py's output after ResourceTool `generate` (scripts/build-landslide-pkg.sh does both): the
manifest package_bricks.json and the binaries <HASH>.<TYPE>.

A recipe is a list of Copy ops (a byte range of one source) and Literal ops (a byte range of kLiterals). A resource's
own candidate sources are the vanilla resource it was made from and every fingerprinted resource of its variant; the
candidates of the other resources in its partition (its package) come next. Every 32-byte window of the candidates is
indexed; walking the built bytes, the longest match of 32 bytes or more in an own candidate is copied, else in another
one of the package, anything else is literal (adjacent literal bytes merged). Checks (fail the run): replaying every
recipe on the extracted sources gives the built bytes; kLiterals holds no run of 32 or more bytes of any candidate
source of the same package: not inside one literal run (a resource's contiguous literal bytes), nor across stored
runs, where a pad byte no op reads may separate two runs no output joins. It also reports how many runs of kLiterals
match another package's candidates (allowed). It prints each resource's and the total output, copied and literal bytes.

What it writes:
- the packages' resources: each variant's two Freelancer bricks (TEMP and TBLU each, the full Landslide's first) from
  --built, as recipes (kSources, kLiterals, kResources of fr::RecipeResource, src/core/Recipe.h), with the reference
  tables build_bricks.py made and the partition whose package holds them (the manifest's, 26 when it names none);
  plus byte copies of vanilla resources under a new path hash (COPIES, none now);
- whether the package carries the Freelancer layer (targets, supplier, exits: true with the built bricks);
- per variant (kVariants): its name, the scene its swap loads and the bricks it loads instead of the contract's (the
  manifest's), its own resources, its fingerprints and its dependencies:
  - the fingerprints (each spec's FINGERPRINTS, through the manifest): the final size and package of the newest copy
    of each vanilla resource the variant was built against (each TEMP with its blueprint), as the game mounts them
    with the variant's map (chunk0 and the partition's chunk, each with its patches 1 to --patch-level; mod packages
    such as chunk26patch300 are left out);
  - the dependencies: every hash its resources' reference tables name, plus every fingerprinted resource;
- the former resource hashes (FORMER): hashes earlier builds of Sapienza's package held, so a stale file is still
  recognized as the mod's own.

Checks (fails loudly): every source and fingerprint is found; the extracted file is the newest copy (its package
prefix) and has the final size; a TEMP/TBLU's BIN1 data size equals its memory size (a built resource's memory size
is taken from it); a TEMP's blueprint index names a TBLU in its reference table (a built TEMP's must be the built
TBLU of the same brick); every reference of a built resource is in a mounted package, one of the package's own, or an
engine type; no new hash is the source's or any hash in the cache; each variant's scene path is its scene's hash and
no two variants swap in the same scene, except a layer (manifest "layerOf": the full Bad Boy on the full Icon), whose
tables are its base's resources, the base's swap bricks plus its vanilla layer bricks, the base's fingerprints plus
its own and the base's dependencies plus every reference of its own fingerprinted resources; an appending variant
(manifest "appends": the full Gluttony and the spot fixes, which keep the contract's scene) swaps in no scene: its
swap bricks are its vanilla appendBricks (each fingerprinted) and then its own. Several appending variants may share
a contract scene (one per option: the runtime picks the variant by the option, never by the scene), but none appends
to a scene a variant swaps in, and no two variants share a name (scene_problem).
"""
import argparse
import bisect
import glob
import hashlib
import json
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools", "rpkg"))

import rpkg  # noqa: E402

# The shortest copy, and the longest byte run of a source kLiterals may hold, plus one.
WINDOW = 32
# Matches kept per window and source while indexing (enough to find a long one; a run of zeros has thousands).
HITS_PER_SOURCE = 8
FNV_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
COPY, LITERAL = 0, 1  # fr::OpKind
# Bytes that break up a run of kLiterals a source also holds (none is read), tried in this order.
PAD_BYTES = (0xA5, 0x5A, 0xC3, 0x3C, 0x96, 0x69)
PAD_ROUNDS = 8

# Byte copies of vanilla resources under a new path: (new path, source hash). None now.
COPIES = []

# Resource hashes of earlier builds of the package, so a stale file is still recognized as the mod's: the first
# test brick (frandomizer_spike_dressing.brick).
FORMER = [0x00D1C948471CA87F]

CHUNKS = (0, 26)
# A variant's partition when the manifest names none (builds before partitions): Sapienza's.
SAPIENZA_PARTITION = (26, "coastaltown")
# The partition whose package FORMER belongs to.
FORMER_PARTITION = 26


def rid(path):
    """ZRuntimeResourceID of a resource path: 00 + md5 bytes 1..7 of the lower-case path."""
    return int("00" + hashlib.md5(path.lower().encode()).hexdigest()[2:16], 16)


def parse_meta(raw, is_patch):
    """(deletions, {hash: dict(type, final, mem, vmem, refs=[(flags, hash)])}) from an rpkg's leading metadata."""
    if raw[:4] == b"2KPR":
        pos = 13
    elif raw[:4] == b"GKPR":
        pos = 4
    else:
        sys.exit("metadata: bad magic %r" % raw[:4])
    count, index_size, info_size = struct.unpack_from("<III", raw, pos)
    pos += 12
    deletions = []
    if is_patch:
        ndel = struct.unpack_from("<I", raw, pos)[0]
        deletions = list(struct.unpack_from("<%dQ" % ndel, raw, pos + 4))
        pos += 4 + 8 * ndel
    index = [struct.unpack_from("<QQI", raw, pos + 20 * k) for k in range(count)]
    p = pos + index_size
    out = {}
    for h, _off, _size in index:
        typ = raw[p:p + 4][::-1].decode("ascii")
        refsize, _zero, final, mem, vmem = struct.unpack_from("<IIIII", raw, p + 4)
        q = p + 24
        refs = []
        if refsize:
            n = struct.unpack_from("<I", raw, q)[0] & 0x3FFFFFFF
            if 4 + 9 * n != refsize:
                sys.exit("metadata: reference table size %d != 4 + 9 * %d" % (refsize, n))
            flags = raw[q + 4:q + 4 + n]
            hashes = struct.unpack_from("<%dQ" % n, raw, q + 4 + n)
            refs = list(zip(flags, hashes))
        out[h] = {"type": typ, "final": final, "mem": mem, "vmem": vmem, "refs": refs}
        p += 24 + refsize
    return deletions, out


def load_newest(cache, patch_level, chunks=None):
    """{hash: entry + package} of the newest copy of every resource the game mounts from chunks (default CHUNKS),
    and every hash seen in any cached package of them (mods' included)."""
    newest = {}
    every = set()
    for chunk in chunks or CHUNKS:
        found = {}
        for path in glob.glob(os.path.join(cache, "chunk%d*.rpkg.*.meta" % chunk)):
            m = re.match(r"chunk%d(?:patch(\d+))?\.rpkg\." % chunk, os.path.basename(path))
            if m:
                found[int(m.group(1) or 0)] = path
        if 0 not in found:
            sys.exit("no chunk%d.rpkg metadata in %s" % (chunk, cache))
        for patch in sorted(found):
            name = "chunk%d.rpkg" % chunk if patch == 0 else "chunk%dpatch%d.rpkg" % (chunk, patch)
            with open(found[patch], "rb") as f:
                deletions, entries = parse_meta(f.read(), patch != 0)
            every.update(entries)
            if patch > patch_level:
                continue
            for h in deletions:
                newest.pop(h, None)
            for h, e in entries.items():
                e["package"] = name
                newest[h] = e
    return newest, every


def blueprint_index(data):
    """A TEMP's blueprintIndexInResourceHeader (STemplateEntityFactory: subType, then the blueprint index)."""
    return struct.unpack_from("<i", data, 16 + 4)[0]


def check_bin1(data, e, what):
    if data[:4] != b"BIN1" or struct.unpack(">I", data[8:12])[0] != e["mem"]:
        sys.exit("%s: BIN1 data size %r != memory size %d" % (what, data[8:12], e["mem"]))


def scene_problem(variants):
    """Why the manifest's variants cannot share their scenes as they do, or None.

    The runtime keys a variant three ways (src/core/LandslideFull.cpp, src/mod/LandslidePackage.cpp): its tables by
    its name (TableOf: name, scene and partition), the variant of a pick by its option and map (FullVariantOf), and
    the variant of a swapped load by the scene it swaps in (FullVariantOfScene, behind the target pool's mover rows
    and never-targets; a layer through its scene alias's key, FullVariantOfLoad). So:
    - names are unique (the tables, the sentinel files and the alias key would otherwise name two);
    - a scene swap's scene is no other swap's: the scene alone must name it. A layer (layerOf) is the exception the
      alias key tells apart: it shares its base's scene;
    - an appending variant (appends: Gluttony, the spot fixes) swaps no scene and is never looked up by it: it is
      used only on a pick of its own option, so several may append to the same contract scene (The Revolutionary
      and Embrace of the Serpent on Santa Fortuna; the map variant pick chooses one option per load, so only one is
      ever active). Its scene must not be one a variant swaps in, though, or that scene would name two.
    """
    names = set()
    swapped = {}  # scene (lower case) -> the variant that swaps it in
    for v in variants:
        if v["name"] in names:
            return "%s: two variants have this name" % v["name"]
        names.add(v["name"])
        if not v.get("layerOf") and not v.get("appends"):
            scene = v["scene"].lower()
            if scene in swapped:
                return "%s: %s swaps in %s too" % (v["name"], swapped[scene], v["scene"])
            swapped[scene] = v["name"]
    for v in variants:
        if v.get("layerOf") and v.get("appends"):
            return "%s: a layer cannot append" % v["name"]
        if v.get("appends") and v["scene"].lower() in swapped:
            return "%s: appends to %s, which %s swaps in" % (v["name"], v["scene"], swapped[v["scene"].lower()])
    return None


class RecipeError(Exception):
    pass


def fnv1a64(data, h=FNV_BASIS):
    """fr::Fnv1a64 (src/core/Rng.h)."""
    for b in data:
        h = ((h ^ b) * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def common_length(a, ai, b, bi):
    """How many bytes a[ai:] and b[bi:] share from their start."""
    limit = min(len(a) - ai, len(b) - bi)
    n = 0
    for step in (4096, 256, 16, 1):
        while n + step <= limit and a[ai + n:ai + n + step] == b[bi + n:bi + n + step]:
            n += step
    return n


def diff(target, tiers, sources, hits):
    """Ops [(COPY, source key, offset, length) or (LITERAL, target offset, length)] that rebuild target: at each
    position the longest match of WINDOW bytes or more in the first tier of sources with one (the first of equals in
    hits' order), else a literal byte; adjacent literal bytes merged, so two literal ops always have a copy between
    them."""
    ops = []
    i = 0
    while i < len(target):
        best = (0, None, 0)
        found = hits.get(target[i:i + WINDOW], ()) if i + WINDOW <= len(target) else ()
        for allowed in tiers:
            for key, o in found:
                if key in allowed:
                    n = WINDOW + common_length(target, i + WINDOW, sources[key], o + WINDOW)
                    if n > best[0]:
                        best = (n, key, o)
            if best[0]:
                break
        n, key, o = best
        if n:
            ops.append((COPY, key, o, n))
            i += n
        else:
            if ops and ops[-1][0] == LITERAL:
                ops[-1] = (LITERAL, ops[-1][1], ops[-1][2] + 1)
            else:
                ops.append((LITERAL, i, 1))
            i += 1
    return ops


def replay(ops, sources, literals):
    """The bytes ops (as in a recipe: COPY by index into sources, LITERAL by offset in literals) give."""
    out = bytearray()
    for kind, source, offset, length in ops:
        data = sources[source] if kind == COPY else literals
        if offset + length > len(data):
            raise RecipeError("op (%d, %d, %d, %d) is past its data's %d bytes" % (kind, source, offset, length,
                                                                                    len(data)))
        out += data[offset:offset + length]
    return bytes(out)


def source_runs(literals, sources):
    """{literals offset: (source key, source offset)} of every WINDOW-byte run of literals (anywhere, so across the
    runs' boundaries too) a source holds (the first source and offset that does)."""
    at = {}
    for k in range(len(literals) - WINDOW + 1):
        at.setdefault(literals[k:k + WINDOW], []).append(k)
    found = {}
    for key, data in sources.items():
        for o in range(len(data) - WINDOW + 1):
            for k in at.get(data[o:o + WINDOW], ()):
                found.setdefault(k, (key, o))
    return found


def lay_out(pieces, pads):
    """kLiterals from pieces [bytes] and pads {piece: byte} (a pad byte before that piece), and each piece's
    kLiterals offset."""
    blob = bytearray()
    starts = []
    for p, piece in enumerate(pieces):
        if p in pads:
            blob.append(pads[p])
        starts.append(len(blob))
        blob += piece
    return bytes(blob), starts


def make_recipes(resources, sources):
    """Recipes for resources [(built bytes, [[candidate keys], ...], package)] from sources {key: bytes}: a later
    tier of candidates is copied from only where no earlier one matches.

    Returns (kept, literals, recipes, pads): the source keys any op copies from, in first use; kLiterals; per
    resource (its kept sources as indices into kept, its ops with COPY sources as indices into those, copied bytes,
    literal bytes); how many pad bytes kLiterals holds. kLiterals holds each package's literal runs together; no
    WINDOW-byte run of it that touches a package's runs may be a candidate's of that package. A pad (read by no op)
    goes between two stored runs no output joins, where such a run spans them. Raises RecipeError when a replay
    differs, or such a run lies inside one output's contiguous literal bytes."""
    order = []
    for _, tiers, _ in resources:
        for key in [k for tier in tiers for k in tier]:
            if key not in sources:
                raise RecipeError("no bytes for candidate source %r" % (key,))
            if key not in order:
                order.append(key)
    hits = {}
    for data, _, _ in resources:
        for i in range(len(data) - WINDOW + 1):
            hits.setdefault(data[i:i + WINDOW], [])
    for key in order:
        data = sources[key]
        for o in range(len(data) - WINDOW + 1):
            found = hits.get(data[o:o + WINDOW])
            if found is not None and (len(found) < HITS_PER_SOURCE or found[-HITS_PER_SOURCE][0] != key):
                found.append((key, o))
    diffs = [diff(data, [set(tier) for tier in tiers], sources, hits) for data, tiers, _ in resources]

    packages = []  # in first appearance
    candidates = {}  # package -> {key: bytes} of its resources' candidates
    for _, tiers, package in resources:
        if package not in candidates:
            packages.append(package)
            candidates[package] = {}
        for key in [k for tier in tiers for k in tier]:
            candidates[package][key] = sources[key]
    pieces, owner = [], []  # each literal op's bytes, and its (resource, target offset, package), package by package
    for package in packages:
        for i, ((data, _, mine), ops) in enumerate(zip(resources, diffs)):
            if mine == package:
                for op in ops:
                    if op[0] == LITERAL:
                        pieces.append(data[op[1]:op[1] + op[2]])
                        owner.append((i, op[1], package))

    # A run across stored pieces gets a pad at a boundary inside it, or its pad the next value, until none is left.
    pads = {}
    for rounds in range(PAD_ROUNDS + 1):
        literals, starts = lay_out(pieces, pads)
        found = {}
        for package in packages:
            mine = [p for p in range(len(pieces)) if owner[p][2] == package]
            lo = max(0, starts[mine[0]] - (mine[0] in pads) - WINDOW + 1) if mine else 0
            hi = min(len(literals), starts[mine[-1]] + len(pieces[mine[-1]]) + WINDOW - 1) if mine else 0
            for k, at in source_runs(literals[lo:hi], candidates[package]).items():
                found.setdefault(lo + k, at)
        if not found:
            break
        if rounds == PAD_ROUNDS:
            k = min(found)
            raise RecipeError("kLiterals[%d:%d] is %s's bytes at %d after %d rounds of pads" % (
                k, k + WINDOW, found[k][0], found[k][1], PAD_ROUNDS))
        bumped, last_pad = set(), -1
        for k in sorted(found):
            first = bisect.bisect_right(starts, k) - 1  # the piece holding byte k (or the pad before piece first + 1)
            inside = []  # the pieces after it the window reaches into
            for p in range(first + 1, len(pieces)):
                if starts[p] - (p in pads) >= k + WINDOW:
                    break
                inside.append(p)
            mine = [p for p in inside if p in pads]
            if mine:
                for p in mine:
                    if p not in bumped:
                        bumped.add(p)
                        pads[p] = PAD_BYTES[(PAD_BYTES.index(pads[p]) + 1) % len(PAD_BYTES)]
            elif not inside:
                key, o = found[k]
                i, at, _ = owner[first]
                raise RecipeError("resource %d's literal bytes at %d hold %s's bytes at %d" % (
                    i, at + k - starts[first], key, o))
            elif last_pad <= k:
                pads[inside[0]] = PAD_BYTES[0]
                last_pad = starts[inside[0]]

    start_of = {(i, at): starts[p] for p, (i, at, _) in enumerate(owner)}
    kept, recipes = [], []
    for i, ((data, _, _), ops) in enumerate(zip(resources, diffs)):
        mine, out, copied = [], [], 0
        for op in ops:
            if op[0] == COPY:
                _, key, offset, length = op
                if key not in mine:
                    mine.append(key)
                out.append((COPY, mine.index(key), offset, length))
                copied += length
            else:
                out.append((LITERAL, 0, start_of[(i, op[1])], op[2]))
        for key in mine:
            if key not in kept:
                kept.append(key)
        recipes.append(([kept.index(k) for k in mine], out, copied, len(data) - copied))
    for i, ((data, _, _), (mine, ops, _, _)) in enumerate(zip(resources, recipes)):
        if replay(ops, [sources[kept[k]] for k in mine], literals) != data:
            raise RecipeError("resource %d: its recipe's replay differs from its built bytes" % i)
    return kept, literals, recipes, len(pads)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cache", required=True, help="folder of rpkg metadata (tools/rpkg/rpkg.py meta)")
    ap.add_argument("--raw", required=True, action="append",
                    help="folder of extracted resources (<package stem>_<HASH>.<TYPE>), with its subfolders; "
                         "repeatable")
    ap.add_argument("--built", required=True, help="build_bricks.py's output after ResourceTool generate")
    ap.add_argument("--names", help="hash list (hash.TYPE,path lines) for the sources' names")
    ap.add_argument("--patch-level", type=int, default=4, help="the game's patch level of chunks 0 and 26")
    ap.add_argument("--recipes", required=True, help="the .inc to write (src/mod/LandslideRecipes.inc)")
    args = ap.parse_args()

    extracted = {}  # file name -> path, the first of each name in --raw order
    for folder in args.raw:
        for path in sorted(glob.glob(os.path.join(folder, "**", "*"), recursive=True)):
            if os.path.isfile(path):
                extracted.setdefault(os.path.basename(path), path)

    def extracted_name(h, e):
        return "%s_%016X.%s" % (e["package"][:-len(".rpkg")], h, e["type"])

    def read_extracted(h, e, what):
        """The extracted newest copy of h (e: its newest_of entry), checked against its final size."""
        name = extracted_name(h, e)
        if name not in extracted:
            sys.exit("%s is in no --raw folder: extract %016X (%s) from %s, its newest copy" % (name, h, what,
                                                                                               e["package"]))
        with open(extracted[name], "rb") as f:
            data = f.read()
        if len(data) != e["final"]:
            sys.exit("%s is %d bytes; %s says %d" % (extracted[name], len(data), e["package"], e["final"]))
        return name, data

    with open(os.path.join(args.built, "package_bricks.json")) as f:
        manifest = json.load(f)
    # Each variant's partition, and what the game mounts with it: chunk0 and that partition's chunk.
    partition_ids = {}
    for v in manifest["variants"]:
        index, pid = v.get("partition", SAPIENZA_PARTITION)
        if partition_ids.setdefault(index, pid) != pid:
            sys.exit("partition %d is named both %r and %r" % (index, partition_ids[index], pid))
        v["partitionIndex"] = index
    if not partition_ids:
        partition_ids[SAPIENZA_PARTITION[0]] = SAPIENZA_PARTITION[1]
    newest_of = {}
    every = set()
    for index in sorted(partition_ids):
        newest_of[index], seen = load_newest(args.cache, args.patch_level, (0, index))
        every |= seen
    chunks = sorted({0} | set(partition_ids))

    def entry(h, what, index):
        if h not in newest_of[index]:
            sys.exit("%s %016X is in no mounted package of chunks 0 and %d" % (what, h, index))
        return newest_of[index][h]

    resources = []
    own = set()
    built = [dict(r, partition=v["partitionIndex"]) for v in manifest["variants"] for r in v["resources"]]
    own_built = {int(r["rid"], 16) for r in built}
    if len(own_built) != len(built):
        sys.exit("two built resources share a hash")
    by_path = {}
    for r in built:
        h, source = int(r["rid"], 16), int(r["source"], 16)
        typ, path = r["type"], r["path"]
        suffix = {"TEMP": "pc_entitytype", "TBLU": "pc_entityblueprint"}[typ]
        if h != rid("[%s].%s" % (path, suffix)):
            sys.exit("built %s %s: hash %016X is not the path's" % (path, typ, h))
        if h in every:
            sys.exit("%016X ([%s].%s) is already a hash in the game's or a mod's packages" % (h, path, suffix))
        part = r["partition"]
        newest = newest_of[part]
        src = entry(source, "built resource's source", part)
        if src["type"] != typ:
            sys.exit("built %s %s copies a %s" % (path, typ, src["type"]))
        file = os.path.join(args.built, "%016X.%s" % (h, typ))
        if not os.path.exists(file):
            sys.exit("%s is missing: run ResourceTool generate on %s" % (file, r["json"]))
        with open(file, "rb") as f:
            data = f.read()
        if data[:4] != b"BIN1":
            sys.exit("%s is not a BIN1 resource" % file)
        refs = [(fl, int(x, 16)) for fl, x in r["refs"]]
        vanilla = {x for _, x in src["refs"]}
        for _, x in refs:
            if x not in newest and x not in own_built and x not in vanilla:
                sys.exit("built %s %s references %016X, which no mounted package holds" % (path, typ, x))
        e = {"refs": refs, "mem": struct.unpack(">I", data[8:12])[0], "vmem": src["vmem"], "final": len(data)}
        blueprint = 0
        if typ == "TEMP":
            index = blueprint_index(data)
            if not 0 <= index < len(refs):
                sys.exit("%s: blueprint index %d outside its %d references" % (file, index, len(refs)))
            blueprint = refs[index][1]
            if blueprint != rid("[%s].pc_entityblueprint" % path) or blueprint not in own_built:
                sys.exit("%s: blueprint reference %016X is not the built TBLU of %s" % (file, blueprint, path))
        own.add(h)
        by_path.setdefault(path, []).append(typ)
        resources.append({"rid": h, "path": path, "source": source, "type": typ, "data": data, "e": e,
                          "blueprint": blueprint, "package": src["package"], "partition": part})
    for path, types in by_path.items():
        if sorted(types) != ["TBLU", "TEMP"]:
            sys.exit("built %s: needs a TEMP and a TBLU, has %s" % (path, types))
    freelancer_layer = bool(built)

    for path, source in COPIES:
        newest = newest_of[FORMER_PARTITION]
        e = entry(source, "source", FORMER_PARTITION)
        typ = e["type"]
        suffix = {"TEMP": "pc_entitytype", "TBLU": "pc_entityblueprint"}[typ]
        new = rid("[%s].%s" % (path, suffix))
        if new == source or new in every:
            sys.exit("%016X ([%s].%s) is already a hash in the game's or a mod's packages" % (new, path, suffix))
        file, data = read_extracted(source, e, "a copy's source")
        check_bin1(data, e, file)
        blueprint = 0
        if typ == "TEMP":
            index = blueprint_index(data)
            if not 0 <= index < len(e["refs"]):
                sys.exit("%s: blueprint index %d outside its %d references" % (file, index, len(e["refs"])))
            blueprint = e["refs"][index][1]
            if newest.get(blueprint, {}).get("type") != "TBLU":
                sys.exit("%s: blueprint reference %016X is not a TBLU the game mounts" % (file, blueprint))
        own.add(new)
        resources.append({"rid": new, "path": path, "source": source, "type": typ, "data": data, "e": e,
                          "blueprint": blueprint, "package": e["package"], "partition": FORMER_PARTITION})

    # Each variant's tables: its swap bricks, its own resources, its fingerprints and its dependencies. A layer
    # variant (layerOf: the full Bad Boy on the full Icon) has its base's resources, the base's swap bricks plus its
    # vanilla layer bricks, the base's fingerprints plus its own, and the base's dependencies plus every reference of
    # its own fingerprinted resources.
    by_rid = {r["rid"]: r for r in resources}
    variants = []
    problem = scene_problem(manifest["variants"])
    if problem:
        sys.exit(problem)
    for v in manifest["variants"]:
        name = v["name"]
        part = v["partitionIndex"]
        newest = newest_of[part]
        if not re.fullmatch(r"[a-z]+", name):
            sys.exit("variant name %r: lower-case letters only" % name)
        scene_rid = int(v["sceneRid"], 16)
        if rid("[%s].pc_entitytemplate" % v["scene"]) != scene_rid or scene_rid not in newest:
            sys.exit("%s: scene %s is not %016X in a mounted package" % (name, v["scene"], scene_rid))
        base = None
        if v.get("layerOf"):
            base = next((x for x in variants if x["name"] == v["layerOf"]), None)
            if base is None:
                sys.exit("%s: a layer on %s, which no earlier variant is" % (name, v["layerOf"]))
            if base["scene"] != v["scene"] or base["partition"] != part:
                sys.exit("%s: its scene and partition are not %s's" % (name, base["name"]))
            if v["resources"]:
                sys.exit("%s: a layer has no resources of its own" % name)
            layer = [b["path"] for b in v["layerBricks"]]
            if not layer or v["swapBricks"] != base["swap"] + layer:
                sys.exit("%s: its swap bricks are not %s's plus its layer bricks" % (name, base["name"]))
            for b in v["layerBricks"]:
                h = int(b["rid"], 16)
                if rid("[%s].pc_entitytype" % b["path"]) != h or newest.get(h, {}).get("type") != "TEMP":
                    sys.exit("%s: the layer brick %s is not %016X in a mounted package" % (name, b["path"], h))
                if h not in {int(x, 16) for x, _ in v["fingerprints"]}:
                    sys.exit("%s: the layer brick %s is not one of its fingerprints" % (name, b["path"]))
        v_own = list(base["own"]) if base else [int(r["rid"], 16) for r in v["resources"]]
        v_paths = {r["path"] for r in v["resources"]}
        # An appending variant (manifest "appends": the full Gluttony) keeps the contract's scene and bricks and
        # appends its swap bricks: its vanilla appendBricks (each a mounted TEMP and one of its fingerprints), then its
        # own.
        appended = []
        if v.get("appends"):
            appended = [b["path"] for b in v["appendBricks"]]
            for b in v["appendBricks"]:
                h = int(b["rid"], 16)
                if rid("[%s].pc_entitytype" % b["path"]) != h or newest.get(h, {}).get("type") != "TEMP":
                    sys.exit("%s: the appended brick %s is not %016X in a mounted package" % (name, b["path"], h))
                if h not in {int(x, 16) for x, _ in v["fingerprints"]}:
                    sys.exit("%s: the appended brick %s is not one of its fingerprints" % (name, b["path"]))
            own_order = [b for b in v["swapBricks"] if b in v_paths]
            if not v_paths or set(own_order) != v_paths or v["swapBricks"] != appended + own_order:
                sys.exit("%s: its swap bricks are not its appended bricks and then its own" % name)
        for b in ([] if base else v["swapBricks"]):
            if b not in v_paths and b not in appended:
                sys.exit("%s: the swap brick %s is not one of its built bricks" % (name, b))
        fingerprints = list(base["fingerprints"]) if base else []
        for hx, fname in v["fingerprints"]:
            h = int(hx, 16)
            e = entry(h, fname, part)
            fingerprints.append((h, e["final"], e["package"], fname))
            if e["type"] == "TEMP":
                tblus = [r for _, r in e["refs"] if newest.get(r, {}).get("type") == "TBLU"]
                # A TEMP names its own blueprint; ours are checked by index above, vanilla ones by type.
                if len(tblus) != 1:
                    sys.exit("%s %016X references %d blueprints; expected 1" % (fname, h, len(tblus)))
                b = entry(tblus[0], fname + " blueprint", part)
                fingerprints.append((tblus[0], b["final"], b["package"], fname + " blueprint"))
        deps = set(h for h, _, _, _ in fingerprints)
        for h in v_own:
            deps.update(x for _, x in by_rid[h]["e"]["refs"])
        if base:
            deps.update(base["deps"])
            for h, _, _, _ in fingerprints[len(base["fingerprints"]):]:
                deps.update(x for _, x in newest[h]["refs"])
        deps -= own
        missing = sorted(h for h in deps if h not in newest)
        if missing:
            # Engine types (ZSpatialEntity ...) have no package; they are not dependencies the container holds.
            print("not in any package (engine types, left out of %s's dependencies): %s" %
                  (name, ", ".join("%016X" % h for h in missing)))
            deps -= set(missing)
        for h in v_own:
            if by_rid[h]["partition"] != part:
                sys.exit("%s: its resource %016X is in partition %d's package, not %d's" % (name, h,
                                                                                            by_rid[h]["partition"], part))
        variants.append({"name": name, "scene": v["scene"], "swap": v["swapBricks"], "own": v_own,
                         "fingerprints": fingerprints, "deps": sorted(deps), "partition": part})

    # Each resource's candidate sources: the resource it was made from, then its variant's fingerprints (a layer's
    # resources are its base's).
    candidates = {r["rid"]: [r["source"]] for r in resources}
    for m, v in zip(manifest["variants"], variants):
        for h in ([] if m.get("layerOf") else v["own"]):
            for f in v["fingerprints"]:
                if f[0] not in candidates[h]:
                    candidates[h].append(f[0])
    names = rpkg.load_names(args.names, {h for c in candidates.values() for h in c})
    for v in variants:
        for h, _, _, fname in v["fingerprints"]:
            names.setdefault(h, fname)
    source_bytes, source_of = {}, {}  # file name -> bytes, and -> (rid, entry)
    keys_of = {}
    for r in resources:
        keys = keys_of[r["rid"]] = []
        for h in candidates[r["rid"]]:
            e = entry(h, "candidate source", r["partition"])
            key = extracted_name(h, e)
            if key not in source_bytes:
                source_bytes[key] = read_extracted(h, e, "a candidate source of %016X" % r["rid"])[1]
                source_of[key] = (h, e)
            keys.append(key)
    # Where none of its own matches, a resource copies from the other candidates of its partition (its package).
    recipe_inputs = []
    for r in resources:
        own = keys_of[r["rid"]]
        others = []
        for x in resources:
            if x["partition"] == r["partition"]:
                others += [k for k in keys_of[x["rid"]] if k not in own and k not in others]
        recipe_inputs.append((r["data"], [own, others], r["partition"]))
    try:
        kept, literals, recipes, pads = make_recipes(recipe_inputs, source_bytes)
    except RecipeError as err:
        sys.exit("recipes: %s" % err)
    if len(kept) > 0xFFFF or len(literals) > 0xFFFFFFFF:
        sys.exit("recipes: %d sources and %d literal bytes do not fit fr::RecipeOp" % (len(kept), len(literals)))
    total = sum(len(r["data"]) for r in resources)
    copied_total = sum(x[2] for x in recipes)
    # Allowed, and only reported: runs of kLiterals another package's candidates hold.
    elsewhere = sorted(source_runs(literals, source_bytes))
    covered = set(x for k in elsewhere for x in range(k, k + WINDOW))
    print("recipes: %d windows of kLiterals (%d bytes) match a candidate of another package" % (len(elsewhere),
                                                                                             len(covered)))

    def arr(values, fmt, per_line):
        items = [fmt % v for v in values]
        return "\n".join("    " + ", ".join(items[i:i + per_line]) + "," for i in range(0, len(items), per_line))

    def table(ctype, name, values, fmt, per_line):
        if not values:
            return "constexpr std::array<%s, 0> %s {};\n" % (ctype, name)
        return "constexpr std::array<%s, %d> %s {{\n%s\n}};\n" % (ctype, len(values), name,
                                                                arr(values, fmt, per_line))

    def short_name(h):
        """A hash list path's file name ("[assembly:/.../scenario_rat.brick].pc_entitytype": scenario_rat.brick)."""
        path = names.get(h, "")
        if path.startswith("[") and "]" in path:
            path = path[1:path.index("]")]
        return path.rsplit("/", 1)[-1]

    parts = ["""// Generated by tools/landslide/gen_package_inc.py --recipes -- do not edit. Committed: it holds recipes
// that rebuild the packages' resources from the player's own game files (src/core/Recipe.h), and no run of 32 or
// more bytes of any candidate source of the same package. Included inside a namespace by
// src/mod/LandslidePackage.cpp, after src/core/LandslideFull.h and src/core/Recipe.h. One package per partition
// (Runtime/chunk<partition>patch290.rpkg): each resource and each variant names its partition.
// Built against chunks %(chunks)s at patch level %(level)d.
// %(total)d bytes of resources: %(copied)d copied from %(sources)d sources, %(literal)d literal.
// kLiterals adds %(pads)d pad bytes no op reads, between stored literal runs no output joins.

// One full map variant the package carries (fr::FullVariant, core/LandslideFull.h, by name).
struct Variant {
    const char* name;  // "landslide", "author"
    const char* scene; // the scene the swap loads
    std::span<const char* const> swapBricks;           // the bricks it loads instead of the contract's, in order
    std::span<const uint64_t> own;                      // its resources in the package
    std::span<const fr::LandslideFingerprint> fingerprints; // the vanilla resources it was built against
    std::span<const uint64_t> dependencies;            // every resource its references and fingerprints name
    int partition;                                      // its package's partition (fr::FullVariant::partition)
};
""" % {"chunks": ", ".join(map(str, chunks)), "level": args.patch_level, "total": total, "copied": copied_total,
       "sources": len(kept), "literal": total - copied_total, "pads": pads}]

    rows = []
    for key in kept:
        h, e = source_of[key]
        data = source_bytes[key]
        chunk = int(re.match(r"chunk(\d+)", e["package"]).group(1))
        rows.append('    {0x%016XULL, "%s", "%s", %du, 0x%016XULL, %d}, // %s' % (
            h, e["type"], short_name(h), len(data), fnv1a64(data), chunk, e["package"]))
    parts.append("// The vanilla resources the recipes copy from (each one's newest copy in its chunk's packages).\n"
                 "constexpr std::array<fr::RecipeSource, %d> kSources {{\n%s\n}};\n" % (len(rows), "\n".join(rows)))
    parts.append("// The recipes' literal bytes.\n" + table("uint8_t", "kLiterals", literals, "0x%02X", 16))

    print("recipes: %d resources, %d sources, %d bytes of kLiterals (%d pads)" % (len(resources), len(kept),
                                                                              len(literals), pads))
    for r, (_, tiers, _), (mine, ops, _, _) in zip(resources, recipe_inputs, recipes):
        for k, key in enumerate(kept[x] for x in mine):
            if key not in tiers[0]:
                users = [x["path"].rsplit("/", 1)[-1] + " " + x["type"] for x in resources if key in keys_of[x["rid"]]]
                print("  %s %016X [%s] copies %d bytes from %s, a candidate of %s in its package" % (
                    r["type"], r["rid"], r["path"].rsplit("/", 1)[-1], sum(op[3] for op in ops if op[0] == COPY and
                                                                          op[1] == k), key, ", ".join(users)))
    kinds = {COPY: "fr::OpKind::Copy", LITERAL: "fr::OpKind::Literal"}
    for i, (r, (mine, ops, copied, literal)) in enumerate(zip(resources, recipes)):
        e = r["e"]
        print("  %s %016X [%s]: %d bytes, %d copied, %d literal (%.1f%%), %d ops" % (
            r["type"], r["rid"], r["path"], len(r["data"]), copied, literal, 100.0 * literal / max(1, len(r["data"])),
            len(ops)))
        parts.append("""// %(type)s %(rid)016X [%(path)s]: from %(source)016X.
// %(final)d bytes (%(copied)d copied, %(literal)d literal), %(nrefs)d references.
%(flags)s%(hashes)s%(sources)s%(ops)s""" % {
            "type": r["type"], "rid": r["rid"], "path": r["path"], "source": r["source"], "final": e["final"],
            "nrefs": len(e["refs"]), "copied": copied, "literal": literal,
            "flags": table("uint8_t", "kRes%dRefFlags" % i, [f for f, _ in e["refs"]], "0x%02X", 16),
            "hashes": table("uint64_t", "kRes%dRefHashes" % i, [h for _, h in e["refs"]], "0x%016XULL", 4),
            "sources": table("uint16_t", "kRes%dSources" % i, mine, "%d", 16),
            "ops": table("fr::RecipeOp", "kRes%dOps" % i,
                         ["{%s, %d, %du, %du}" % (kinds[k], s, o, n) for k, s, o, n in ops], "%s", 3)})
    print("  total: %d bytes, %d copied, %d literal (%.1f%%)" % (total, copied_total, total - copied_total,
                                                                  100.0 * (total - copied_total) / max(1, total)))

    rows = []
    for i, r in enumerate(resources):
        e = r["e"]
        rows.append('    {0x%016XULL, "%s", "%s", 0x%016XULL, "%s", 0x%016XULL, %du, 0x%08Xu, kRes%dRefFlags, '
                    'kRes%dRefHashes, %d, kRes%dSources, kRes%dOps, %du, 0x%016XULL},' %
                    (r["rid"], r["type"], r["path"], r["source"], r["package"], r["blueprint"], e["mem"], e["vmem"],
                     i, i, r["partition"], i, i, len(r["data"]), fnv1a64(r["data"])))
    parts.append("constexpr std::array<fr::RecipeResource, %d> kResources {{\n%s\n}};\n" % (len(rows), "\n".join(rows)))

    parts.append("""// The package carries the Freelancer layer (targets, supplier, safes, exits).
constexpr bool kFreelancerLayer = %s;

// Resource hashes earlier builds of partition kFormerPartition's package held (a stale file is still the mod's).
constexpr int kFormerPartition = %d;
constexpr std::array<uint64_t, %d> kFormerRids {%s};
""" % ("true" if freelancer_layer else "false", FORMER_PARTITION, len(FORMER),
       ", ".join("0x%016XULL" % h for h in FORMER)))

    rows = []
    for k, v in enumerate(variants):
        parts.append("""// The full %(name)s: %(scene)s.
constexpr const char* kVariant%(k)dSwapBricks[] = {
%(swap)s
};
constexpr uint64_t kVariant%(k)dOwn[] = {
%(own)s
};
constexpr fr::LandslideFingerprint kVariant%(k)dFingerprints[] = {
%(prints)s
};
constexpr uint64_t kVariant%(k)dDependencies[] = {
%(deps)s
};
""" % {"k": k, "name": v["name"], "scene": v["scene"], "swap": "\n".join('    "%s",' % b for b in v["swap"]),
       "own": arr(v["own"], "0x%016XULL", 4),
       "prints": "\n".join('    {0x%016XULL, %du, "%s", "%s"},' % f for f in v["fingerprints"]),
       "deps": arr(v["deps"], "0x%016XULL", 4)})
        rows.append('    {"%s", "%s", kVariant%dSwapBricks, kVariant%dOwn, kVariant%dFingerprints, kVariant%dDependencies, %d},'
                    % (v["name"], v["scene"], k, k, k, k, v["partition"]))
    parts.append("constexpr Variant kVariants[] = {\n%s\n};\n" % "\n".join(rows))

    out = args.recipes
    if os.path.dirname(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out + ".tmp", "w", newline="\n") as f:
        f.write("\n".join(parts))
    os.replace(out + ".tmp", out)
    print("wrote %s: %d resources (%s), %d variants" % (
        out, len(resources), ", ".join("%016X" % r["rid"] for r in resources), len(variants)))
    for v in variants:
        print("  %s (partition %d): %s; bricks %s; %d own resources, %d fingerprints, %d dependencies" % (
            v["name"], v["partition"], v["scene"], ", ".join(v["swap"]), len(v["own"]), len(v["fingerprints"]),
            len(v["deps"])))
        for f in v["fingerprints"]:
            print("    fingerprint %016X %d %s %s" % f)


if __name__ == "__main__":
    main()
