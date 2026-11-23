#!/usr/bin/env python3
"""Rebuild partial package copies as sparse files of the packages' full sizes.

A part is a byte range copied out of a package on the Deck, named `<package>.<offset>.part`
(`chunk0patch2.rpkg.653107366.part`: the bytes of chunk0patch2.rpkg from offset 653107366). Each package gets a sparse
file of its full size (taken from the metadata cache, `<package>.<size>.meta`), with every part written at its offset.
`rpkg.py --cache` reads the package's head from the cache (the size matches), so `extract` works for every resource
that lies wholly inside the parts; others read as zeros and fail their decompression.

    python3 -I tools/rpkg/sparse_parts.py --parts build-landslide/deckcopy/chunk0 --cache build-landslide/cache \\
        --out build-landslide/deckcopy/chunk0sparse
"""

import argparse
import os
import re
import sys

PART_NAME = re.compile(r"(chunk\d+(?:patch\d+)?\.rpkg)\.(\d+)\.part$", re.IGNORECASE)
META_NAME = re.compile(r"(chunk\d+(?:patch\d+)?\.rpkg)\.(\d+)\.meta$", re.IGNORECASE)


def find_parts(folder):
    """package name -> [(offset, path)], offsets ascending."""
    parts = {}
    for f in sorted(os.listdir(folder)):
        m = PART_NAME.match(f)
        if m:
            parts.setdefault(m.group(1), []).append((int(m.group(2)), os.path.join(folder, f)))
    for v in parts.values():
        v.sort()
    return parts


def cached_sizes(cache):
    """package name -> set of sizes with a cached head."""
    sizes = {}
    for f in os.listdir(cache):
        m = META_NAME.match(f)
        if m:
            sizes.setdefault(m.group(1), set()).add(int(m.group(2)))
    return sizes


def build(package, size, parts, out):
    """Write out/<package>: size bytes, sparse, each part at its offset. Returns the bytes written."""
    path = os.path.join(out, package)
    written = 0
    with open(path + ".tmp", "wb") as f:
        f.truncate(size)
        for offset, part in parts:
            n = os.path.getsize(part)
            if offset + n > size:
                raise ValueError("%s runs past the end of %s (%d bytes)" % (part, package, size))
            with open(part, "rb") as p:
                f.seek(offset)
                f.write(p.read())
            written += n
    os.replace(path + ".tmp", path)
    return written


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--parts", required=True, help="folder of <package>.<offset>.part files")
    ap.add_argument("--cache", required=True, help="metadata cache folder (gives each package's size)")
    ap.add_argument("--out", required=True, help="folder to write the sparse packages to")
    args = ap.parse_args(argv)
    parts = find_parts(args.parts)
    if not parts:
        sys.exit("no <package>.<offset>.part files in %s" % args.parts)
    sizes = cached_sizes(args.cache)
    os.makedirs(args.out, exist_ok=True)
    for package, plist in sorted(parts.items()):
        s = sizes.get(package, set())
        if len(s) != 1:
            sys.exit("%s: %d cached sizes in %s (need exactly one)" % (package, len(s), args.cache))
        size = next(iter(s))
        n = build(package, size, plist, args.out)
        print("%s: %d bytes, %d parts, %d bytes of data" % (os.path.join(args.out, package), size, len(plist), n))


if __name__ == "__main__":
    main()
