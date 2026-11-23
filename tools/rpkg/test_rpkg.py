#!/usr/bin/env python3
"""Self-test of rpkg.py (packages, patch order, LZ4, the metadata cache, extract) and entity.py (BIN1 TEMP and TBLU)
on small synthetic files (no game data): python3 -I tools/rpkg/test_rpkg.py"""
import contextlib
import io
import os
import struct
import sys
import tempfile
import zlib

sys.dont_write_bytecode = True  # no __pycache__ next to the tools
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import entity  # noqa: E402
import rpkg  # noqa: E402


# --- writers for the tests (the tool itself only reads) -----------------------------------------------------------

def lz4_literals(data):
    """A valid LZ4 block holding data as literals only."""
    n = len(data)
    out = bytearray([min(n, 15) << 4])
    if n >= 15:
        rest = n - 15
        while rest >= 255:
            out.append(255)
            rest -= 255
        out.append(rest)
    return bytes(out) + data


def package(version, resources, deletions=None):
    """An rpkg: resources are (hash, type, payload, refs [(flags, hash)], how) with how "plain", "lz4" or "lz4+xor";
    deletions (a patch's list) or None for a base package."""
    index, info, blobs = [], [], []
    header_len = (13 if version == 2 else 4) + 12 + (4 + 8 * len(deletions) if deletions is not None else 0)
    for h, typ, payload, refs, how in resources:
        stored = lz4_literals(payload) if how.startswith("lz4") else payload
        if how.endswith("xor"):
            stored = rpkg.xor(stored)
        size = (len(stored) if how.startswith("lz4") else 0) | (0x80000000 if how.endswith("xor") else 0)
        index.append((h, size))
        blobs.append(stored)
        table = b""
        if refs:
            table = struct.pack("<I", len(refs)) + bytes(f for f, _ in refs) + struct.pack(
                "<%dQ" % len(refs), *[x for _, x in refs])
        info.append(typ.encode()[::-1] + struct.pack("<IIIII", len(table), 0, len(payload), len(payload), 0) + table)
    index_size = 20 * len(resources)
    info_bytes = b"".join(info)
    offset = header_len + index_size + len(info_bytes)
    index_bytes = b""
    for (h, size), blob in zip(index, blobs):
        index_bytes += struct.pack("<QQI", h, offset, size)
        offset += len(blob)
    head = (b"2KPR" + b"\x01\x00\x00\x00" + b"\x06\x00\x00\x00\x00") if version == 2 else b"GKPR"
    head += struct.pack("<III", len(resources), index_size, len(info_bytes))
    if deletions is not None:
        head += struct.pack("<I", len(deletions)) + struct.pack("<%dQ" % len(deletions), *deletions)
    return head + index_bytes + info_bytes + b"".join(blobs)


class Bin1Writer:
    """A BIN1 resource: data placed by hand, ZVariant type slots recorded for the type segment."""

    def __init__(self):
        self.data = bytearray()
        self.types = []
        self.slots = []

    def alloc(self, size, count=None):
        """size bytes at an 8-aligned offset (a TArray's count u32 just before it, as IOI's files have)."""
        while (len(self.data) + 4) % 8:
            self.data += b"\0"
        self.data += struct.pack("<I", count or 0)
        at = len(self.data)
        self.data += b"\0" * size
        return at

    def put(self, at, fmt, *values):
        struct.pack_into(fmt, self.data, at, *values)

    def array(self, at, begin, count, stride):
        if count:
            self.put(at, "<QQQ", begin, begin + count * stride, begin + count * stride)
        else:
            self.put(at, "<QQQ", entity.NULL, entity.NULL, entity.NULL)

    def string(self, at, text):
        raw = text.encode() + b"\0"
        p = self.alloc(len(raw))
        self.data[p:p + len(raw)] = raw
        self.put(at, "<IIQ", len(text) | 0x40000000, 0, p)

    def ref(self, at, entity_id, scene, index):
        self.put(at, "<Qii", entity_id, scene, index)
        self.string(at + 16, "")

    def variant(self, at, type_name, payload):
        if type_name not in self.types:
            self.types.append(type_name)
        p = self.alloc(len(payload))
        self.data[p:p + len(payload)] = payload
        self.put(at, "<QQ", self.types.index(type_name), p)
        self.slots.append(at)

    def bytes(self):
        body = struct.pack("<I", len(self.slots)) + b"".join(struct.pack("<I", s) for s in self.slots)
        body += struct.pack("<I", len(self.types))
        for k, name in enumerate(self.types):
            while len(body) % 4:
                body += b"\0"
            raw = name.encode() + b"\0"
            body += struct.pack("<IiI", k, -1, len(raw)) + raw
        seg = struct.pack("<II", entity.TYPE_SEGMENT, len(body)) + body
        return b"BIN1\x00\x08\x01\x00" + struct.pack(">I", len(self.data)) + b"\0" * 4 + bytes(self.data) + seg


def matrix_bytes(x, y, z):
    return struct.pack("<12f", 1, 0, 0, 0, 1, 0, 0, 0, 1, x, y, z)


def write_temp():
    """A TEMP: root (index 0) and one entity with m_mTransform and an m_eidParent into external scene 0, plus a
    property override on another brick's entity."""
    w = Bin1Writer()
    w.data = bytearray(0x58)  # STemplateEntityFactory at data offset 0
    w.put(0, "<iii", 2, 1, 0)
    ents = w.alloc(2 * 0x70, 2)
    for k in range(2):
        e = ents + k * 0x70
        w.ref(e, 0xFFFFFFFFFFFFFFFF, -1, -1 if k == 0 else 0)
        w.put(e + 0x20, "<i", k)
        w.array(e + 0x40, 0, 0, 0x18)
        w.array(e + 0x58, 0, 0, 0x18)
        if k == 0:
            w.array(e + 0x28, 0, 0, 0x18)
            continue
        props = w.alloc(2 * 0x18, 2)
        w.array(e + 0x28, props, 2, 0x18)
        w.put(props, "<I", zlib.crc32(b"m_mTransform"))
        w.variant(props + 8, "SMatrix43", matrix_bytes(-85.0, 70.0, 96.9))
        w.put(props + 0x18, "<I", zlib.crc32(b"m_eidParent"))
        ref = bytearray(0x20)
        struct.pack_into("<Qii", ref, 0, 0x1234, 0, -2)
        w.variant(props + 0x18 + 8, "SEntityTemplateReference", bytes(ref))
    w.array(0x10, ents, 2, 0x70)
    over = w.alloc(0x38, 1)
    w.ref(over, 0x9249724A74D21096, 0, -2)
    w.put(over + 0x20, "<I", zlib.crc32(b"m_bVisible"))
    w.variant(over + 0x28, "bool", b"\0")
    w.array(0x28, over, 1, 0x38)
    ext = w.alloc(4, 1)
    w.put(ext, "<i", 3)
    w.array(0x40, ext, 1, 4)
    return w.bytes()


def write_tblu():
    """Its TBLU: two named entities, one pin, one delete into external scene 0."""
    w = Bin1Writer()
    w.data = bytearray(0xC8)
    w.put(0, "<ii", 2, 0)
    ents = w.alloc(2 * 0xA8, 2)
    for k, (eid, name) in enumerate(((0xFFFFFFFFFFFFFFFE, "Scene"), (0x2B, "Old_Truck_A_01"))):
        e = ents + k * 0xA8
        w.ref(e, 0xFFFFFFFFFFFFFFFF, -1, -1 if k == 0 else 0)
        w.put(e + 0x20, "<i", k)
        w.put(e + 0x28, "<Q", eid)
        w.string(e + 0x38, name)
        for at in (0x48, 0x60, 0x78, 0x90):
            w.array(e + at, 0, 0, 8)
    w.array(0x08, ents, 2, 0xA8)
    ext = w.alloc(4, 1)
    w.put(ext, "<i", 7)
    w.array(0x20, ext, 1, 4)
    pin = w.alloc(0x38, 1)
    w.put(pin, "<ii", 0, 1)
    w.string(pin + 8, "Out")
    w.string(pin + 0x18, "In")
    w.array(0x38, pin, 1, 0x38)
    for at in (0x50, 0x68, 0x98, 0xB0):
        w.array(at, 0, 0, 8)
    dele = w.alloc(0x20, 1)
    w.ref(dele, 0x9249724A74D21096, 0, -2)
    w.array(0x80, dele, 1, 0x20)
    return w.bytes()


class Lz4Test(unittest.TestCase):
    def test_literals(self):
        for n in (0, 1, 14, 15, 16, 300, 600):
            data = bytes(range(256)) * 3
            data = data[:n]
            self.assertEqual(rpkg.lz4_block(lz4_literals(data), n), data)

    def test_overlapping_match(self):
        # "ab" then a 10-byte match at offset 2 (it repeats the pattern), then the last literal "!".
        block = bytes([0x26]) + b"ab" + struct.pack("<H", 2) + bytes([0x10]) + b"!"
        self.assertEqual(rpkg.lz4_block(block, 13), b"abababababab!")

    def test_long_match(self):
        # A 1-byte literal and a 4 + 15 + 3 = 22 byte match at offset 1.
        block = bytes([0x1F]) + b"x" + struct.pack("<H", 1) + bytes([3]) + bytes([0x10]) + b"y"
        self.assertEqual(rpkg.lz4_block(block, 24), b"x" * 23 + b"y")

    def test_errors(self):
        with self.assertRaises(rpkg.RpkgError):
            rpkg.lz4_block(lz4_literals(b"abc"), 4)  # wrong size
        with self.assertRaises(rpkg.RpkgError):
            rpkg.lz4_block(bytes([0x10]) + b"a" + struct.pack("<H", 5) + bytes([0x10]) + b"b", 8)  # offset too far

    def test_xor(self):
        data = bytes(range(20))
        self.assertNotEqual(rpkg.xor(data), data)
        self.assertEqual(rpkg.xor(rpkg.xor(data)), data)


class PackageTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        a = (0xA1, "TEMP", b"base a", [(0x1F, 0xA2), (0x5F, 0xEE)], "lz4")
        b = (0xA2, "TBLU", b"base b", [], "plain")
        c = (0xA3, "PRIM", b"base c", [], "lz4+xor")
        self.write("chunk6.rpkg", package(2, [a, b, c]))
        self.write("chunk6patch1.rpkg", package(2, [(0xA1, "TEMP", b"patch1 a", [(0x1F, 0xA2)], "lz4+xor")], [0xA3]))
        self.write("chunk6patch2.rpkg", package(2, [(0xA4, "TEXT", b"patch2 d", [], "plain")], []))
        self.write("chunk6patch300.rpkg", package(1, [(0xA1, "TEMP", b"a mod's a", [], "plain")], []))
        self.write("chunk0.rpkg", package(2, [(0xA2, "TBLU", b"chunk0 b", [], "plain"),
                                              (0xB1, "CPPT", b"chunk0 e", [], "plain")]))

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, data):
        with open(os.path.join(self.dir, name), "wb") as f:
            f.write(data)

    def test_patch_order_and_deletions(self):
        r = rpkg.Resolver([self.dir], [6], vanilla=True)
        self.assertEqual([p.name for p in r.packages], ["chunk6.rpkg", "chunk6patch1.rpkg", "chunk6patch2.rpkg"])
        pkg, e = r.get(0xA1)
        self.assertEqual((pkg.name, pkg.read(e)), ("chunk6patch1.rpkg", b"patch1 a"))
        self.assertIsNone(r.get(0xA3))  # deleted by patch 1
        self.assertEqual(r.deleted[0xA3], "chunk6patch1.rpkg")
        self.assertEqual(r.get(0xA2)[0].name, "chunk6.rpkg")
        self.assertEqual(r.get(0xA4)[1].type, "TEXT")
        self.assertEqual(len(r.copies(0xA1)), 2)

    def test_mod_patches(self):
        every = rpkg.Resolver([self.dir], [6])
        self.assertEqual(every.get(0xA1)[0].name, "chunk6patch300.rpkg")
        self.assertEqual(every.get(0xA1)[0].read(every.get(0xA1)[1]), b"a mod's a")
        self.assertEqual(rpkg.Resolver([self.dir], [6], patch_level=1).get(0xA4), None)
        self.assertEqual(rpkg.Resolver([self.dir], [6], vanilla=True).get(0xA1)[0].name, "chunk6patch1.rpkg")

    def test_chunks_first_wins(self):
        r = rpkg.Resolver([self.dir], [6, 0], vanilla=True)
        self.assertEqual(r.get(0xA2)[0].name, "chunk6.rpkg")
        self.assertEqual(r.get(0xB1)[0].name, "chunk0.rpkg")
        self.assertEqual(rpkg.Resolver([self.dir], [0, 6], vanilla=True).get(0xA2)[0].name, "chunk0.rpkg")

    def test_refs_and_info(self):
        r = rpkg.Resolver([self.dir], [6], vanilla=True)
        base = r.packages[0]
        e = next(x for x in base.entries if x.hash == 0xA1)
        self.assertEqual(e.refs, [(0x1F, 0xA2), (0x5F, 0xEE)])
        self.assertEqual((e.final, e.xored, e.stored > 0), (6, False, True))
        c = next(x for x in base.entries if x.hash == 0xA3)
        self.assertTrue(c.xored)
        self.assertEqual(base.read(c), b"base c")
        self.assertEqual(r.packages[1].deletions, [0xA3])
        with self.assertRaises(rpkg.RpkgError):
            rpkg.Resolver([self.dir], [9])

    def test_metadata_cache(self):
        cache = os.path.join(self.dir, "cache")
        rpkg.Resolver([self.dir], [6], cache, vanilla=True)
        names = sorted(os.listdir(cache))
        size = os.path.getsize(os.path.join(self.dir, "chunk6.rpkg"))
        self.assertIn("chunk6.rpkg.%d.meta" % size, names)
        self.assertEqual(len(names), 3)
        with open(os.path.join(cache, "chunk6.rpkg.%d.meta" % size), "rb") as f:
            meta = f.read()
        # The cache is the package's head: what gen_package_inc.parse_meta reads.
        with open(os.path.join(self.dir, "chunk6.rpkg"), "rb") as f:
            self.assertEqual(meta, f.read(len(meta)))
        self.assertEqual(rpkg.head_size(meta, False), len(meta))
        # Metadata alone (no package files): list and refs work, extract says why it cannot.
        only = rpkg.Resolver(None, [6], cache, vanilla=True)
        pkg, e = only.get(0xA1)
        self.assertEqual((pkg.name, e.refs), ("chunk6patch1.rpkg", [(0x1F, 0xA2)]))
        with self.assertRaises(rpkg.RpkgError):
            pkg.read(e)

    def test_extract_names(self):
        out = os.path.join(self.dir, "out")
        with contextlib.redirect_stdout(io.StringIO()):
            rpkg.main(["extract", "--dir", self.dir, "--chunks", "6,0", "--vanilla", "--out", out, "A1",
                       "00000000000000B1"])
        self.assertEqual(sorted(os.listdir(out)), ["chunk0_00000000000000B1.CPPT", "chunk6patch1_00000000000000A1.TEMP"])
        with open(os.path.join(out, "chunk6patch1_00000000000000A1.TEMP"), "rb") as f:
            self.assertEqual(f.read(), b"patch1 a")
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()), \
                contextlib.redirect_stdout(io.StringIO()):
            rpkg.main(["extract", "--dir", self.dir, "--chunks", "6", "--vanilla", "--out", out, "A3"])

    def test_resource_id(self):
        # The full Gluttony's fix brick (tools/landslide/gluttony_spec.FIX_IDS), and a path's case does not matter.
        self.assertEqual(rpkg.resource_id("[assembly:/_pro/scenes/missions/wet/fr_gluttony.brick].pc_entitytype"),
                         0x004A22FBEC6ACD93)
        self.assertEqual(rpkg.parse_hash("[ASSEMBLY:/_pro/scenes/missions/wet/scenario_azalea.brick].pc_entitytype"),
                         0x00CAFC0C93568981)
        self.assertEqual(rpkg.parse_hash("0x00CAFC0C93568981"), 0x00CAFC0C93568981)
        with self.assertRaises(rpkg.RpkgError):
            rpkg.parse_hash("not a hash")


class EntityTest(unittest.TestCase):
    def test_temp(self):
        t = entity.read_temp(write_temp())
        self.assertEqual((t["subType"], t["blueprintIndexInResourceHeader"], t["rootEntityIndex"]), (2, 1, 0))
        self.assertEqual(len(t["subEntities"]), 2)
        self.assertEqual(t["externalSceneTypeIndicesInResourceHeader"], [3])
        e = t["subEntities"][1]
        self.assertEqual(e["logicalParent"]["entityIndex"], 0)
        self.assertEqual(e["entityTypeResourceIndex"], 1)
        props = {p["nPropertyID"]: p["value"] for p in e["propertyValues"]}
        m = props["m_mTransform"]
        self.assertEqual(m["$type"], "SMatrix43")
        self.assertAlmostEqual(m["$val"]["Trans"]["x"], -85.0, places=4)
        self.assertAlmostEqual(m["$val"]["Trans"]["z"], 96.9, places=4)
        self.assertEqual(m["$val"]["XAxis"], {"x": 1.0, "y": 0.0, "z": 0.0})
        par = props["m_eidParent"]["$val"]
        self.assertEqual((par["entityID"], par["externalSceneIndex"], par["entityIndex"]), (0x1234, 0, -2))
        (o,) = t["propertyOverrides"]
        self.assertEqual(o["propertyOwner"]["entityID"], 0x9249724A74D21096)
        self.assertEqual(o["propertyValue"]["nPropertyID"], "m_bVisible")
        self.assertEqual(o["propertyValue"]["value"], {"$type": "bool", "$val": False})

    def test_tblu(self):
        b = entity.read_tblu(write_tblu())
        self.assertEqual([(e["entityId"], e["entityName"]) for e in b["subEntities"]],
                         [(0xFFFFFFFFFFFFFFFE, "Scene"), (0x2B, "Old_Truck_A_01")])
        self.assertEqual(b["subEntities"][1]["logicalParent"]["entityIndex"], 0)
        self.assertEqual(b["externalSceneTypeIndicesInResourceHeader"], [7])
        self.assertEqual(b["pinConnections"], [{"fromID": 0, "toID": 1, "fromPinName": "Out", "toPinName": "In"}])
        self.assertEqual([(r["entityID"], r["externalSceneIndex"]) for r in b["overrideDeletes"]],
                         [(0x9249724A74D21096, 0)])
        self.assertEqual(b["pinConnectionOverrides"], [])
        for e in b["subEntities"]:
            self.assertEqual((e["propertyAliases"], e["exposedEntities"], e["exposedInterfaces"], e["entitySubsets"]),
                             ([], [], [], []))

    def test_property_names(self):
        self.assertEqual(entity.property_name(zlib.crc32(b"m_eidParent")), "m_eidParent")
        self.assertEqual(entity.property_name(0x12345678), "0x12345678")

    def test_not_bin1(self):
        with self.assertRaises(entity.Bin1Error):
            entity.read_temp(b"NOPE" + b"\0" * 32)


class PrimSizeTest(unittest.TestCase):
    """crate_poses.prim_box_size: a PRIM's bounding box from its object header."""

    def prim(self, lo, hi, at=16):
        head = struct.pack("<BBHIIII", 0, 0, 1, 0, 0xFFFFFFFF, 1, 0) + struct.pack("<6f", *(lo + hi))
        return struct.pack("<I", at) + b"\0" * (at - 4) + head

    def test_box(self):
        import crate_poses
        size = crate_poses.prim_box_size(self.prim((-0.02, -0.1, -0.3), (0.02, 0.11, 0.31)))
        self.assertEqual([round(x, 3) for x in size], [0.04, 0.21, 0.61])  # the Hatchet's, as spawned

    def test_not_a_box(self):
        import crate_poses
        with self.assertRaises(ValueError):
            crate_poses.prim_box_size(self.prim((0.0, 0.0, 0.0), (float("nan"), 1.0, 1.0)))
        with self.assertRaises(ValueError):
            crate_poses.prim_box_size(self.prim((1.0, 0.0, 0.0), (0.0, 1.0, 1.0)))  # max below min
        with self.assertRaises(struct.error):
            crate_poses.prim_box_size(b"\x40\0\0\0")


if __name__ == "__main__":
    unittest.main()
