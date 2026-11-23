#!/usr/bin/env python3
"""Reads HITMAN 3 resource packages (RPKG v1 "GKPR" and v2 "2KPR", base and patch) with the standard library only:
lists what a partition's packages hold, a resource's references, and extracts resources the way the game resolves
them (its newest copy, with each patch's deletions), decompressed.

    python3 -I tools/rpkg/rpkg.py info    PACKAGE.rpkg
    python3 -I tools/rpkg/rpkg.py list    --dir D --chunks 6 [--type TEMP] [--names hash_list.txt]
    python3 -I tools/rpkg/rpkg.py refs    --dir D --chunks 6 [--names ...] HASH
    python3 -I tools/rpkg/rpkg.py extract --dir D --chunks 6 [--out OUT] HASH...
    python3 -I tools/rpkg/rpkg.py meta    --dir D --chunks 6 --cache CACHE

Common options: --dir, the folder of chunk<N>.rpkg and chunk<N>patch<K>.rpkg (the game's Runtime folder or a copy of
part of it); --chunks, one or more chunk numbers (comma separated: "0,6"), searched in order, the first holding a hash
wins; --vanilla, only IOI's own patches (numbered below 100: mods use 100 and up, such as chunk6patch300.rpkg, and
FreelancerRandomizer writes patch 290), else --patch-level K, only patches up to K (default: every patch present);
--cache, a folder of package metadata (below), read instead of the package's head when present, written when not.

Patch order: chunkN.rpkg, then chunkNpatch1, chunkNpatch2, ... by number. A patch first deletes the hashes on its
deletion list from the older packages, then adds its own resources (a newer copy of a hash replaces the older one).

The metadata cache holds each package's leading bytes (header, deletion list, resource index, resource info), named
<package>.rpkg.<file size>.meta, the form tools/landslide/gen_package_inc.py, build_bricks.py and
scripts/build-landslide-pkg.sh --cache read. A file whose size changed gets a new cache name, so a stale cache is never
read for it. `meta` writes the cache of every package of the chunks (--vanilla and --patch-level apply).

extract writes <package stem>_<HASH>.<TYPE> (chunk6patch4_00CAFC0C93568981.TEMP), decompressed and de-obfuscated, the
names scripts/build-landslide-pkg.sh --raw expects; HASH is 16 hex digits, or a resource path
([assembly:/...].pc_entitytype), hashed as the game does.
"""
import argparse
import hashlib
import os
import re
import struct
import sys

sys.dont_write_bytecode = True  # no __pycache__ next to the tools

XOR_KEY = bytes([0xDC, 0x45, 0xA6, 0x9C, 0xD3, 0x72, 0x4C, 0xAB])
MOD_PATCHES = 100  # patch numbers from here up are mods' (IOI's are 1, 2, ...)


class RpkgError(Exception):
    pass


# --- LZ4 block format (no frame): what IOI compresses each resource with ---------------------------------------------

def lz4_block(src, size):
    """Decodes one LZ4 block of src into exactly size bytes."""
    dst = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                if i >= n:
                    raise RpkgError("lz4: literal length runs past the end")
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        if i + lit > n:
            raise RpkgError("lz4: literals run past the end")
        dst += src[i:i + lit]
        i += lit
        if i >= n:
            break  # the last sequence has literals only
        if i + 2 > n:
            raise RpkgError("lz4: truncated match offset")
        off = src[i] | (src[i + 1] << 8)
        i += 2
        ml = token & 15
        if ml == 15:
            while True:
                if i >= n:
                    raise RpkgError("lz4: match length runs past the end")
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        start = len(dst) - off
        if off == 0 or start < 0:
            raise RpkgError("lz4: match offset %d outside the %d bytes written" % (off, len(dst)))
        if off >= ml:
            dst += dst[start:start + ml]
        else:  # an overlapping match repeats the last off bytes
            pattern = bytes(dst[start:])
            dst += (pattern * (ml // off + 1))[:ml]
    if len(dst) != size:
        raise RpkgError("lz4: decoded %d bytes, expected %d" % (len(dst), size))
    return bytes(dst)


def xor(data):
    key = XOR_KEY * (len(data) // 8 + 1)
    return bytes(a ^ b for a, b in zip(data, key))


def resource_id(path):
    """ZRuntimeResourceID of a resource path: 00 + md5 bytes 1..7 of the lower-case path."""
    return int("00" + hashlib.md5(path.lower().encode()).hexdigest()[2:16], 16)


# --- One package ----------------------------------------------------------------------------------------------------

class Entry:
    __slots__ = ("hash", "offset", "stored", "xored", "type", "final", "mem", "vmem", "refs", "package")

    def __repr__(self):
        return "%016X.%s" % (self.hash, self.type)


def head_size(raw, is_patch):
    """The length of a package's metadata (header to the end of its info table), from its first bytes; None when raw
    is too short to tell."""
    pos = _header_pos(raw)
    if len(raw) < pos + 12:
        return None
    _count, index_size, info_size = struct.unpack_from("<III", raw, pos)
    pos += 12
    if is_patch:
        if len(raw) < pos + 4:
            return None
        pos += 4 + 8 * struct.unpack_from("<I", raw, pos)[0]
    return pos + index_size + info_size


def _header_pos(raw):
    if raw[:4] == b"2KPR":
        return 13  # magic, then 9 bytes (version and chunk/patch fields)
    if raw[:4] == b"GKPR":
        return 4
    raise RpkgError("not a resource package (magic %r)" % raw[:4])


def parse_meta(raw, is_patch, package=""):
    """(deletions, [Entry]) from a package's metadata (its leading bytes)."""
    pos = _header_pos(raw)
    count, index_size, info_size = struct.unpack_from("<III", raw, pos)
    pos += 12
    deletions = []
    if is_patch:
        ndel = struct.unpack_from("<I", raw, pos)[0]
        deletions = list(struct.unpack_from("<%dQ" % ndel, raw, pos + 4))
        pos += 4 + 8 * ndel
    if index_size != 20 * count:
        raise RpkgError("%s: index size %d for %d resources" % (package, index_size, count))
    if len(raw) < pos + index_size + info_size:
        raise RpkgError("%s: metadata is %d bytes, needs %d" % (package, len(raw), pos + index_size + info_size))
    entries = []
    p = pos + index_size
    for k in range(count):
        e = Entry()
        e.hash, e.offset, size = struct.unpack_from("<QQI", raw, pos + 20 * k)
        e.xored = bool(size & 0x80000000)
        e.stored = size & 0x3FFFFFFF  # the compressed size; 0: stored uncompressed
        e.type = raw[p:p + 4][::-1].decode("ascii")
        refsize, _states, e.final, e.mem, e.vmem = struct.unpack_from("<IIIII", raw, p + 4)
        q = p + 24
        e.refs = []
        if refsize:
            n = struct.unpack_from("<I", raw, q)[0] & 0x3FFFFFFF
            if 4 + 9 * n != refsize:
                raise RpkgError("%s: %016X: reference table size %d != 4 + 9 * %d" % (package, e.hash, refsize, n))
            flags = raw[q + 4:q + 4 + n]
            hashes = struct.unpack_from("<%dQ" % n, raw, q + 4 + n)
            e.refs = list(zip(flags, hashes))
        e.package = package
        entries.append(e)
        p += 24 + refsize
    if p != pos + index_size + info_size:
        raise RpkgError("%s: info table ends at %d, header says %d" % (package, p, pos + index_size + info_size))
    return deletions, entries


class Package:
    """One rpkg file (or only its cached metadata when the file is not there)."""

    def __init__(self, name, path=None, meta=None, size=None):
        self.name = name  # "chunk6patch4.rpkg"
        self.path = path
        m = PACKAGE_NAME.match(name)
        if not m:
            raise RpkgError("%s: not a chunk<N>[patch<K>].rpkg name" % name)
        self.chunk = int(m.group(1))
        self.patch = int(m.group(2) or 0)
        self.size = os.path.getsize(path) if path else size
        if meta is None:
            meta = read_head(path, self.patch > 0)
        self.meta = meta
        self.deletions, self.entries = parse_meta(meta, self.patch > 0, name)
        self.header = meta[:_header_pos(meta)]

    @property
    def stem(self):
        return self.name[:-len(".rpkg")]

    def read(self, e):
        """An entry's resource, decompressed."""
        if not self.path:
            raise RpkgError("%s is not in --dir (only its metadata is cached)" % self.name)
        with open(self.path, "rb") as f:
            f.seek(e.offset)
            data = f.read(e.stored or e.final)
        if len(data) != (e.stored or e.final):
            raise RpkgError("%s: %016X runs past the end of the file" % (self.name, e.hash))
        if e.xored:
            data = xor(data)
        if e.stored:
            data = lz4_block(data, e.final)
        if len(data) != e.final:
            raise RpkgError("%s: %016X is %d bytes, its info says %d" % (self.name, e.hash, len(data), e.final))
        return data


PACKAGE_NAME = re.compile(r"chunk(\d+)(?:patch(\d+))?\.rpkg$", re.IGNORECASE)


def read_head(path, is_patch):
    with open(path, "rb") as f:
        raw = f.read(1 << 16)
        n = head_size(raw, is_patch)
        if n is None:
            raise RpkgError("%s: too short for a package header" % path)
        if n > len(raw):
            f.seek(0)
            raw = f.read(n)
    if len(raw) < n:
        raise RpkgError("%s: truncated metadata" % path)
    return raw[:n]


def cache_name(name, size):
    return "%s.%d.meta" % (name, size)


# --- A set of chunks, resolved in patch order ------------------------------------------------------------------------

def wanted_patch(patch, vanilla, patch_level):
    if patch == 0:
        return True
    if vanilla and patch >= MOD_PATCHES:
        return False
    return patch_level is None or patch <= patch_level


def find_packages(dirs, cache, chunk, vanilla=False, patch_level=None):
    """The packages of one chunk, oldest first: each a file in dirs (its metadata from cache when there, written to it
    when cache is given and the entry is missing), or, with no file, a cached metadata file alone."""
    found = {}
    for d in dirs or ():
        for f in os.listdir(d):
            m = PACKAGE_NAME.match(f)
            if m and int(m.group(1)) == chunk and f.lower().endswith(".rpkg"):
                found.setdefault(int(m.group(2) or 0), (f, os.path.join(d, f)))
    cached = {}
    if cache and os.path.isdir(cache):
        for f in os.listdir(cache):
            m = re.match(r"(chunk(\d+)(?:patch(\d+))?\.rpkg)\.(\d+)\.meta$", f, re.IGNORECASE)
            if m and int(m.group(2)) == chunk:
                cached.setdefault(int(m.group(3) or 0), {})[int(m.group(4))] = (m.group(1), os.path.join(cache, f))
    out = []
    for patch in sorted(set(found) | set(cached)):
        if not wanted_patch(patch, vanilla, patch_level):
            continue
        if patch in found:
            name, path = found[patch]
            size = os.path.getsize(path)
            hit = cached.get(patch, {}).get(size)
            if hit:
                with open(hit[1], "rb") as f:
                    out.append(Package(name, path, meta=f.read()))
                continue
            pkg = Package(name, path)
            if cache:
                os.makedirs(cache, exist_ok=True)
                tmp = os.path.join(cache, cache_name(name, size) + ".tmp")
                with open(tmp, "wb") as f:
                    f.write(pkg.meta)
                os.replace(tmp, os.path.join(cache, cache_name(name, size)))
            out.append(pkg)
        else:
            sizes = cached[patch]
            if len(sizes) != 1:
                raise RpkgError("chunk%d patch %d: %d cached sizes and no package file to choose: %s" %
                                (chunk, patch, len(sizes), ", ".join(p for _, p in sizes.values())))
            size, (name, path) = next(iter(sizes.items()))
            with open(path, "rb") as f:
                out.append(Package(name, None, meta=f.read(), size=size))
    return out


class Resolver:
    """What the game mounts from a list of chunks: the newest copy of each hash (per chunk, in patch order with each
    patch's deletions; across chunks the first listed wins)."""

    def __init__(self, dirs, chunks, cache=None, vanilla=False, patch_level=None):
        self.packages = []
        self.newest = {}
        self.deleted = {}  # hash -> the patch that deleted its last copy (for messages)
        for chunk in chunks:
            pkgs = find_packages(dirs, cache, chunk, vanilla, patch_level)
            if not pkgs:
                raise RpkgError("no chunk%d packages in %s%s" % (chunk, ", ".join(dirs or ()) or "-",
                                                                 " or %s" % cache if cache else ""))
            if pkgs[0].patch != 0:
                raise RpkgError("chunk%d: no chunk%d.rpkg (first package is %s)" % (chunk, chunk, pkgs[0].name))
            mine = {}
            for pkg in pkgs:
                self.packages.append(pkg)
                for h in pkg.deletions:
                    if h in mine:
                        del mine[h]
                        self.deleted[h] = pkg.name
                for e in pkg.entries:
                    mine[e.hash] = (pkg, e)
                    self.deleted.pop(e.hash, None)
            for h, v in mine.items():
                self.newest.setdefault(h, v)

    def get(self, h):
        return self.newest.get(h)

    def copies(self, h):
        """Every package holding h, oldest first: [(package, entry)]."""
        return [(p, e) for p in self.packages for e in p.entries if e.hash == h]


# --- Names --------------------------------------------------------------------------------------------------------

def load_names(path, wanted=None):
    """hash -> resource path from a hash list (hash.TYPE,path lines); only the wanted hashes when given."""
    names = {}
    if not path:
        return names
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            hh, _, rest = line.rstrip("\n").partition(",")
            h, _, _typ = hh.partition(".")
            try:
                v = int(h, 16)
            except ValueError:
                continue
            if wanted is None or v in wanted:
                names[v] = rest
    return names


def parse_hash(text):
    t = text.strip()
    if t.startswith("["):
        return resource_id(t)
    if re.fullmatch(r"(0x)?[0-9A-Fa-f]{1,16}", t):
        return int(t, 16)
    raise RpkgError("%r is neither a hex hash nor a [resource path]" % text)


# --- Commands -----------------------------------------------------------------------------------------------------

def resolver(args):
    chunks = [int(c) for c in args.chunks.split(",")]
    return Resolver(args.dir, chunks, args.cache, args.vanilla, args.patch_level)


def cmd_info(args):
    for path in args.package:
        name = os.path.basename(path)
        pkg = Package(name, path)
        types = {}
        for e in pkg.entries:
            types[e.type] = types.get(e.type, 0) + 1
        print("%s: %s, %d bytes, %d resources, %d deletions, metadata %d bytes" % (
            name, pkg.header[:4].decode("ascii", "replace"), pkg.size, len(pkg.entries), len(pkg.deletions),
            len(pkg.meta)))
        print("  types: %s" % ", ".join("%s %d" % kv for kv in sorted(types.items(), key=lambda kv: -kv[1])))
        if args.deletions:
            for h in pkg.deletions:
                print("  deletes %016X" % h)


def cmd_list(args):
    r = resolver(args)
    rows = sorted(r.newest.values(), key=lambda pe: (pe[1].type, pe[1].hash))
    if args.type:
        rows = [(p, e) for p, e in rows if e.type == args.type.upper()]
    if args.package:
        rows = [(p, e) for p, e in rows if p.name.lower() == args.package.lower()]
    names = load_names(args.names, {e.hash for _, e in rows}) if args.names else {}
    for p, e in rows:
        print("%016X %s %9d %s%s" % (e.hash, e.type, e.final, p.name, " " + names[e.hash] if e.hash in names else ""))
    print("%d resources" % len(rows), file=sys.stderr)


def cmd_refs(args):
    r = resolver(args)
    h = parse_hash(args.hash)
    hit = r.get(h)
    if hit is None:
        raise RpkgError("%016X is in none of the packages%s" % (h, " (deleted by %s)" % r.deleted[h]
                                                                 if h in r.deleted else ""))
    pkg, e = hit
    names = load_names(args.names, {h} | {x for _, x in e.refs}) if args.names else {}
    print("%016X %s %d bytes in %s%s" % (h, e.type, e.final, pkg.name, " " + names[h] if h in names else ""))
    for k, (flags, x) in enumerate(e.refs):
        t = r.get(x)
        print("  %3d %02X %016X %s %s%s" % (k, flags, x, t[1].type if t else "----", t[0].name if t else "(not here)",
                                            " " + names[x] if x in names else ""))
    if args.reverse:
        for p, e2 in sorted(r.newest.values(), key=lambda pe: pe[1].hash):
            if any(x == h for _, x in e2.refs):
                print("  referenced by %016X %s %s" % (e2.hash, e2.type, p.name))


def cmd_extract(args):
    r = resolver(args)
    os.makedirs(args.out, exist_ok=True)
    missing = 0
    for text in args.hash:
        h = parse_hash(text)
        hit = r.get(h)
        if hit is None:
            print("%016X: in none of the packages%s" % (h, " (deleted by %s)" % r.deleted[h] if h in r.deleted else ""),
                  file=sys.stderr)
            missing += 1
            continue
        pkg, e = hit
        data = pkg.read(e)
        name = "%s_%016X.%s" % (pkg.stem, h, e.type)
        with open(os.path.join(args.out, name + ".tmp"), "wb") as f:
            f.write(data)
        os.replace(os.path.join(args.out, name + ".tmp"), os.path.join(args.out, name))
        print("%s %d bytes" % (name, len(data)))
    if missing:
        sys.exit("%d of %d not found" % (missing, len(args.hash)))


def cmd_meta(args):
    if not args.cache:
        raise RpkgError("meta needs --cache (the folder to write)")
    r = resolver(args)
    for p in r.packages:
        print("%s %s" % (os.path.join(args.cache, cache_name(p.name, p.size)), len(p.meta)))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)

    def common(p, chunks=True):
        p.add_argument("--dir", action="append", help="folder of the rpkg files (repeatable)")
        if chunks:
            p.add_argument("--chunks", required=True, help="chunk numbers, comma separated (first wins)")
        p.add_argument("--cache", help="metadata cache folder (<package>.rpkg.<size>.meta)")
        p.add_argument("--vanilla", action="store_true", help="IOI's patches only (numbered below 100)")
        p.add_argument("--patch-level", type=int, help="only patches up to this number")
        p.add_argument("--names", help="hash list (hash.TYPE,path lines) for resource names")

    p = sub.add_parser("info", help="one package's header, types and deletions")
    p.add_argument("package", nargs="+")
    p.add_argument("--deletions", action="store_true", help="list the deleted hashes too")
    p.set_defaults(func=cmd_info)
    p = sub.add_parser("list", help="the newest copy of every resource: hash, type, size, package")
    common(p)
    p.add_argument("--type", help="only this resource type (TEMP, TBLU, ...)")
    p.add_argument("--package", help="only the resources whose newest copy is in this package")
    p.set_defaults(func=cmd_list)
    p = sub.add_parser("refs", help="a resource's references (index, flags, hash, type, package)")
    common(p)
    p.add_argument("--reverse", action="store_true", help="also list the resources that reference it")
    p.add_argument("hash")
    p.set_defaults(func=cmd_refs)
    p = sub.add_parser("extract", help="write resources as <package stem>_<HASH>.<TYPE>, decompressed")
    common(p)
    p.add_argument("--out", default=".", help="output folder (default: the current one)")
    p.add_argument("hash", nargs="+")
    p.set_defaults(func=cmd_extract)
    p = sub.add_parser("meta", help="write the metadata cache of every package of the chunks")
    common(p)
    p.set_defaults(func=cmd_meta)
    args = ap.parse_args(argv)
    try:
        args.func(args)
    except RpkgError as e:
        sys.exit("rpkg: %s" % e)


if __name__ == "__main__":
    main()
