"""Reads HITMAN 3 entity templates and blueprints (TEMP: STemplateEntityFactory, TBLU: STemplateEntityBlueprint) from
their binary form (ResourceLib "BIN1"), standard library only, into dicts shaped like ZHMTools ResourceTool's JSON for
the parts tools/landslide reads: sub-entities with their logical parent, type index, properties (by name when known,
else "0x<crc32>"), the TBLU's ids and names, the external scenes and the overrides. Property values of the common
types are decoded (SMatrix43, SEntityTemplateReference, bool, numbers, ZString, ZGuid, ZRuntimeResourceID, SVector*,
TArray of those); any other type is {"$type": name, "$raw": offset} (not decoded).

Read only: tools/landslide/build_bricks.py still edits ResourceTool's JSON (converted by scripts/build-landslide-pkg.sh),
since only ResourceTool writes the binary back. This reader is for looking: where a brick's entities are and what they
are, without Docker.

BIN1 layout: "BIN1", alignment and segment count bytes, the data size (big-endian u32), 4 zero bytes, the data, then
segments (u32 id, u32 size, body): 0x12EBA5ED lists the data offsets holding pointers, 0x3989BF9F the type-ID slots and
the type names (each ZVariant's type is an index into it). Pointers are data offsets (-1 for none); a TArray is
(begin, end, capacity), its count is the u32 just before begin.
"""
import struct
import zlib

NULL = 0xFFFFFFFFFFFFFFFF

TYPE_SEGMENT = 0x3989BF9F
RELOC_SEGMENT = 0x12EBA5ED

# Property names tools/landslide and the rpkg tools read (the binary keeps only their crc32).
PROPERTY_NAMES = [
    "m_mTransform", "m_eidParent", "m_bVisible", "m_bStartEnabled", "m_sActorName", "m_bEnabled", "m_eRoomBehaviour",
    "m_PrimitiveScale", "m_vGlobalSize", "m_vSize", "m_ResourceID", "m_IDHigh", "m_IDLow", "m_eidSubjectEntity",
    "m_bIsPrivate", "m_eidSpatial", "m_aActors", "m_sId", "m_RepositoryId", "m_eRequiredPhysicsMode",
    "m_ePhysicsMode", "m_bRemovePhysics", "m_bIsStatic", "m_fMass", "m_bKinematic", "m_bCollisionEnabled",
    "m_bCastShadows", "m_bEnableCollision", "m_eCollisionLayer", "m_eCollisionLayerType", "m_bDisabled",
    "m_bForceVisible", "m_bAddToRoom", "m_bStatic", "m_bIsDynamic", "m_bVisibleInMirrors", "m_pHelper",
    "m_rHelper", "m_bAllowGlobalUsage", "m_OutfitRepositoryID", "m_bVisibleInScene", "m_eGlobalTimeOfDay",
]
# Freelancer's helpers (the territory randomizer's slots, a safe's clues, a supplier's NPC and private volume) and the
# collision volumes, for tools/landslide/variant_audit.py on bricks that have no ResourceTool JSON.
PROPERTY_NAMES += ["available%s%02d" % (kind, n) for kind in ("Stash", "Safe", "Supplier", "Mule") for n in range(1, 41)]
PROPERTY_NAMES += ["Clue%02d" % n for n in range(1, 21)] + [
    "Stash_GivenNumber", "Safe_GivenNumber", "Supplier_GivenNumber", "Mule_GivenNumber", "m_rSeedEntity",
    "m_rActorProvider", "exclusionList", "privateVolume", "m_aHigherPriority", "m_pActor", "m_bHide",
    "m_bCollideHitman", "m_vBoxSize", "m_fCapsuleRadius", "m_vCapsuleLength"]
PROPERTY_BY_CRC = {zlib.crc32(n.encode()): n for n in PROPERTY_NAMES}


class Bin1Error(Exception):
    pass


def property_name(crc):
    return PROPERTY_BY_CRC.get(crc, "0x%08X" % crc)


class Bin1:
    """One BIN1 resource: its data and its type names."""

    def __init__(self, raw, what=""):
        if raw[:4] != b"BIN1":
            raise Bin1Error("%s: not a BIN1 resource" % what)
        size = struct.unpack_from(">I", raw, 8)[0]
        self.data = raw[16:16 + size]
        if len(self.data) != size:
            raise Bin1Error("%s: data is %d bytes, header says %d" % (what, len(self.data), size))
        self.types = {}
        p = 16 + size
        while p + 8 <= len(raw):
            sid, ssize = struct.unpack_from("<II", raw, p)
            body = raw[p + 8:p + 8 + ssize]
            if sid == TYPE_SEGMENT:
                self._types(body)
            p += 8 + ssize

    def _types(self, body):
        n = struct.unpack_from("<I", body, 0)[0]
        q = 4 + 4 * n
        count = struct.unpack_from("<I", body, q)[0]
        q += 4
        for _ in range(count):
            q = (q + 3) & ~3
            index, _unk, ln = struct.unpack_from("<IiI", body, q)
            q += 12
            self.types[index] = body[q:q + ln].rstrip(b"\0").decode("ascii", "replace")
            q += ln

    # --- primitives ---

    def u32(self, o):
        return struct.unpack_from("<I", self.data, o)[0]

    def i32(self, o):
        return struct.unpack_from("<i", self.data, o)[0]

    def u64(self, o):
        return struct.unpack_from("<Q", self.data, o)[0]

    def array(self, o, stride):
        """The element offsets of the TArray at o."""
        begin, end = self.u64(o), self.u64(o + 8)
        if begin == NULL or end == NULL or end <= begin:
            return []
        return range(begin, end, stride)

    def zstring(self, o):
        ln = self.u32(o) & 0x3FFFFFFF
        ptr = self.u64(o + 8)
        if ptr == NULL or ln == 0:
            return ""
        return self.data[ptr:ptr + ln].decode("utf-8", "replace")

    def ref(self, o):
        """SEntityTemplateReference (0x20 bytes)."""
        return {"entityID": self.u64(o), "externalSceneIndex": self.i32(o + 8), "entityIndex": self.i32(o + 12),
                "exposedEntity": self.zstring(o + 16)}

    def variant(self, o):
        """ZVariant (type slot, data pointer) -> {"$type", "$val"}."""
        index, ptr = self.u64(o), self.u64(o + 8)
        name = self.types.get(index, "?%d" % index)
        if ptr == NULL:
            return {"$type": name, "$val": None}
        return {"$type": name, "$val": self.value(name, ptr)}

    def value(self, t, p):
        d = self.data
        if t == "SMatrix43":
            f = struct.unpack_from("<12f", d, p)
            v = lambda k: {"x": f[k], "y": f[k + 1], "z": f[k + 2]}  # noqa: E731
            return {"XAxis": v(0), "YAxis": v(3), "ZAxis": v(6), "Trans": v(9)}
        if t == "SEntityTemplateReference":
            return self.ref(p)
        if t == "bool":
            return d[p] != 0
        if t in ("float32", "float"):
            return struct.unpack_from("<f", d, p)[0]
        if t == "float64":
            return struct.unpack_from("<d", d, p)[0]
        if t in ("int32", "uint32", "int16", "uint16", "int8", "uint8", "int64", "uint64"):
            fmt = {"int32": "<i", "uint32": "<I", "int16": "<h", "uint16": "<H", "int8": "<b", "uint8": "<B",
                   "int64": "<q", "uint64": "<Q"}[t]
            return struct.unpack_from(fmt, d, p)[0]
        if t == "ZString":
            return self.zstring(p)
        if t == "ZGuid":
            a, b, c = struct.unpack_from("<IHH", d, p)
            rest = d[p + 8:p + 16]
            return "%08x-%04x-%04x-%s-%s" % (a, b, c, rest[:2].hex(), rest[2:].hex())
        if t == "ZRuntimeResourceID":
            return {"m_IDHigh": self.u32(p), "m_IDLow": self.u32(p + 4)}
        if t == "SVector2":
            return dict(zip("xy", struct.unpack_from("<2f", d, p)))
        if t == "SVector3":
            return dict(zip("xyz", struct.unpack_from("<3f", d, p)))
        if t == "SVector4":
            return dict(zip("xyzw", struct.unpack_from("<4f", d, p)))
        if t.startswith("TArray<") and t.endswith(">"):
            inner = t[7:-1]
            stride = {"SEntityTemplateReference": 0x20, "ZString": 0x10, "ZGuid": 0x10, "bool": 1, "float32": 4,
                      "int32": 4, "uint32": 4, "uint8": 1, "SVector3": 12, "SMatrix43": 48,
                      "ZRuntimeResourceID": 8}.get(inner)
            if stride is None:
                return {"$raw": p}
            return [self.value(inner, q) for q in self.array(p, stride)]
        if t.startswith("E") or t.endswith("_E") or "::E" in t:
            return struct.unpack_from("<i", d, p)[0]  # an enum: its number
        return {"$raw": p}

    def property(self, o):
        """SEntityTemplateProperty (0x18 bytes)."""
        return {"nPropertyID": property_name(self.u32(o)), "value": self.variant(o + 8)}


def read_temp(raw, what="TEMP"):
    """STemplateEntityFactory as a dict like ResourceTool's JSON (property values decoded where the type is known)."""
    b = Bin1(raw, what)
    subs = []
    for o in b.array(0x10, 0x70):
        e = {"logicalParent": b.ref(o), "entityTypeResourceIndex": b.i32(o + 0x20)}
        for key, at in (("propertyValues", 0x28), ("postInitPropertyValues", 0x40)):
            e[key] = [b.property(q) for q in b.array(o + at, 0x18)]
        e["platformSpecificPropertyValues"] = []  # not read
        subs.append(e)
    overrides = [{"propertyOwner": b.ref(o), "propertyValue": b.property(o + 0x20)} for o in b.array(0x28, 0x38)]
    return {"subType": b.i32(0), "blueprintIndexInResourceHeader": b.i32(4), "rootEntityIndex": b.i32(8),
            "subEntities": subs, "propertyOverrides": overrides,
            "externalSceneTypeIndicesInResourceHeader": [b.i32(o) for o in b.array(0x40, 4)]}


def read_tblu(raw, what="TBLU"):
    """STemplateEntityBlueprint as a dict like ResourceTool's JSON: ids, names, parents, property aliases, exposed
    entities and interfaces, entity subsets, pins and overrides (pin constant values are not read)."""
    b = Bin1(raw, what)
    subs = []
    for o in b.array(0x08, 0xA8):
        subs.append({
            "logicalParent": b.ref(o), "entityTypeResourceIndex": b.i32(o + 0x20), "entityId": b.u64(o + 0x28),
            "editorOnly": b.data[o + 0x30] != 0, "entityName": b.zstring(o + 0x38),
            "propertyAliases": [{"sAliasName": b.zstring(q), "entityID": b.i32(q + 0x10),
                                 "sPropertyName": b.zstring(q + 0x18)} for q in b.array(o + 0x48, 0x28)],
            "exposedEntities": [{"sName": b.zstring(q), "bIsArray": b.data[q + 0x10] != 0,
                                 "aTargets": [b.ref(r) for r in b.array(q + 0x18, 0x20)]}
                                for q in b.array(o + 0x60, 0x30)],
            "exposedInterfaces": [[b.zstring(q), b.i32(q + 0x10)] for q in b.array(o + 0x78, 0x18)],
            "entitySubsets": [[b.zstring(q), {"entities": [b.i32(r) for r in b.array(q + 0x10, 4)]}]
                              for q in b.array(o + 0x90, 0x28)]})

    def pins(at):
        return [{"fromID": b.i32(q), "toID": b.i32(q + 4), "fromPinName": b.zstring(q + 8),
                 "toPinName": b.zstring(q + 0x18)} for q in b.array(at, 0x38)]

    def ext_pins(at):
        return [{"fromEntity": b.ref(q), "toEntity": b.ref(q + 0x20), "fromPinName": b.zstring(q + 0x40),
                 "toPinName": b.zstring(q + 0x50)} for q in b.array(at, 0x70)]
    return {"subType": b.i32(0), "rootEntityIndex": b.i32(4), "subEntities": subs,
            "externalSceneTypeIndicesInResourceHeader": [b.i32(o) for o in b.array(0x20, 4)],
            "pinConnections": pins(0x38), "inputPinForwardings": pins(0x50), "outputPinForwardings": pins(0x68),
            "overrideDeletes": [b.ref(o) for o in b.array(0x80, 0x20)],
            "pinConnectionOverrides": ext_pins(0x98), "pinConnectionOverrideDeletes": ext_pins(0xB0)}
