#!/usr/bin/env python3
"""Reads a Glacier 2 NAVP (NavPower navmesh) and answers the full variants' placement questions offline: is a point
on the navmesh, how far is it from the mesh's edge (walls, drops, obstacles), which island is it on, and where is the
nearest good floor point to a nominal spot. build_bricks.py uses it for a spec with NAVMESH (the full Icon).

Layout (third_party/devpkg/include/NavPower.h, Binary namespace):
  Header 24 B | SectionHeader 12 B | NavSetHeader 12 B | NavGraphHeader 324 B | areas (m_areaBytes) | kd tree ...
  Area 72 B: pProxy, dynAreaData, pFirstLink, pSearchParent (4 x u64) | pos vec3 | radius f | searchCost u32 |
             usageFlags u32 | flags1..4 u32  (flags1 & 0x7F = edge count; (flags1 >> 7) & 0x3FFFF = island, which
             the game files leave unset: 0x3FFFF in every area of the Icon's NAVP)
  Edge 32 B: pAdjArea u64 (0 = boundary edge; else the adjacent area's offset from the NavGraphHeader's start, i.e.
             its offset in the area block + 324) | pos vec3 (the polygon's vertex) | flags1 u32 | flags2 u32 | pad 4
Islands are therefore computed here: the connected components of the areas' adjacency, numbered 1, 2, ... in the
order of their first area. (Off-mesh links, the kd tree and the link block are not read.)

    python3 -I navp.py FILE.NAVP                      summary (areas, islands, bbox)
    python3 -I navp.py FILE.NAVP x,y,z [x,y,z ...]    per point: on mesh?, island, clearance, nearest mesh point
    python3 -I navp.py --selftest

Checked on a real file: the Icon's NAVP 00E920F30DEF45C1 (v40, 2426 areas, 526132 bytes) walks to the
header's area size exactly and every adjacency pointer lands on an area. A layout that does not walk to the header's
area size exactly, or a pointer that lands on no area, raises ValueError, so a wrong guess fails loudly.
"""
import math
import struct
import sys

HDR = 24 + 12 + 12
GRAPH = 6 * 4 + 5 * 4 + 24 + 4 + 252  # NavGraphHeader
AREA = 72
EDGE = 32
CELL = 4.0  # the plan grid of the area and edge indexes (m)


class Area:
    __slots__ = ("off", "pos", "radius", "island", "verts", "adj", "zmin", "zmax", "box")


def _cells(x0, y0, x1, y1):
    for cx in range(int(math.floor(x0 / CELL)), int(math.floor(x1 / CELL)) + 1):
        for cy in range(int(math.floor(y0 / CELL)), int(math.floor(y1 / CELL)) + 1):
            yield cx, cy


class NavMesh:
    def __init__(self, data):
        self.data = data
        if len(data) < HDR + GRAPH:
            raise ValueError("%d bytes: too short for a NAVP" % len(data))
        g = HDR
        (ver, _layer, area_bytes, _kd_bytes, _link_bytes, _total) = struct.unpack_from("<6I", data, g)
        self.version = ver
        self.bbox = struct.unpack_from("<6f", data, g + 24 + 20)
        start = g + GRAPH
        if start + area_bytes > len(data):
            raise ValueError("area bytes %d run past the file's %d" % (area_bytes, len(data)))
        self.areas = []
        o = start
        while o < start + area_bytes:
            a = Area()
            a.off = o - start
            a.pos = struct.unpack_from("<3f", data, o + 32)
            a.radius = struct.unpack_from("<f", data, o + 44)[0]
            f1 = struct.unpack_from("<I", data, o + 56)[0]
            n = f1 & 0x7F
            if n < 3:
                raise ValueError("area at %d has %d edges: not a NAVP v%d layout" % (a.off, n, ver))
            a.island = None  # computed below (the file's island number is unset)
            a.verts, a.adj = [], []
            for k in range(n):
                e = o + AREA + k * EDGE
                a.adj.append(struct.unpack_from("<Q", data, e)[0])
                a.verts.append(struct.unpack_from("<3f", data, e + 8))
            a.zmin = min(v[2] for v in a.verts)
            a.zmax = max(v[2] for v in a.verts)
            a.box = (min(v[0] for v in a.verts), min(v[1] for v in a.verts),
                     max(v[0] for v in a.verts), max(v[1] for v in a.verts))
            self.areas.append(a)
            o += AREA + n * EDGE
        if o != start + area_bytes:
            raise ValueError("area walk ended at %d, not %d: not a NAVP v%d layout" % (o - start, area_bytes, ver))
        # Adjacent-area pointers are offsets from the NavGraphHeader's start (the area block starts GRAPH bytes in);
        # 0 = boundary. Resolved to areas, then islands = connected components.
        by_off = {a.off: a for a in self.areas}
        for a in self.areas:
            for k, ptr in enumerate(a.adj):
                if ptr:
                    b = by_off.get(ptr - GRAPH)
                    if b is None:
                        raise ValueError("area at %d: edge %d points at %d, not an area: not a NAVP v%d layout" %
                                         (a.off, k, ptr, ver))
                    a.adj[k] = b
                else:
                    a.adj[k] = None
        n_isl = 0
        for a in self.areas:
            if a.island is not None:
                continue
            n_isl += 1
            a.island = n_isl
            todo = [a]
            while todo:
                for b in todo.pop().adj:
                    if b is not None and b.island is None:
                        b.island = n_isl
                        todo.append(b)
        self.boundary = []
        for a in self.areas:
            n = len(a.verts)
            for k in range(n):
                if a.adj[k] is None:
                    self.boundary.append((a.verts[k], a.verts[(k + 1) % n], a))
        self._area_grid = {}
        for a in self.areas:
            for c in _cells(*a.box):
                self._area_grid.setdefault(c, []).append(a)
        self._edge_grid = {}
        for e in self.boundary:
            v0, v1 = e[0], e[1]
            for c in _cells(min(v0[0], v1[0]), min(v0[1], v1[1]), max(v0[0], v1[0]), max(v0[1], v1[1])):
                self._edge_grid.setdefault(c, []).append(e)

    # --- queries -------------------------------------------------------------------------------------------------

    def area_at(self, p, up=1.0, down=1.5):
        """(area, floor z) whose polygon holds p in plan and whose floor is between down below and up above p."""
        best = None
        for a in self._area_grid.get((int(math.floor(p[0] / CELL)), int(math.floor(p[1] / CELL))), ()):
            if p[2] < a.zmin - up or p[2] > a.zmax + down:
                continue
            if inside(p, a.verts):
                z = floor_z(p, a.verts)
                if -up <= p[2] - z <= down and (best is None or abs(p[2] - z) < abs(p[2] - best[1])):
                    best = (a, z)
        return best

    def clearance(self, p, island=None, dz=2.0, limit=8.0):
        """Distance in plan from p to the nearest boundary edge at about p's height (at most limit: farther edges are
        not searched)."""
        d = limit
        seen = set()
        for c in _cells(p[0] - limit, p[1] - limit, p[0] + limit, p[1] + limit):
            for e in self._edge_grid.get(c, ()):
                if id(e) in seen:
                    continue
                seen.add(id(e))
                v0, v1, a = e
                if island is not None and a.island != island:
                    continue
                if abs((v0[2] + v1[2]) / 2 - p[2]) > dz:
                    continue
                d = min(d, seg_dist(p, v0, v1))
        return d

    def snap(self, p, min_clear=0.6, radius=6.0, step=0.25, up=1.5, down=1.5, ok=None):
        """Nearest point within radius (plan) on the mesh, at p's floor level (floor within up above / down below p),
        with at least min_clear to the edge: (point, area, clearance, distance in plan); the best-cleared point found
        when none has min_clear; None when there is no floor at all. ok: an optional test of a floor point (x, y, z);
        points it refuses are not considered at all."""
        best = None
        r = 0.0
        while r <= radius + 1e-9:
            ring = [(0.0, 0.0)] if r == 0 else [(r * math.cos(t * math.pi / 16), r * math.sin(t * math.pi / 16))
                                                for t in range(32)]
            for dx, dy in ring:
                q = (p[0] + dx, p[1] + dy, p[2])
                hit = self.area_at(q, up=up, down=down)
                if not hit:
                    continue
                q = (q[0], q[1], hit[1])
                if ok is not None and not ok(q):
                    continue
                c = self.clearance(q)
                if c >= min_clear:
                    return q, hit[0], c, r
                if best is None or c > best[2]:
                    best = (q, hit[0], c, r)
            r += step
        return best

    def islands(self):
        out = {}
        for a in self.areas:
            out[a.island] = out.get(a.island, 0) + 1
        return out


def inside(p, poly):
    x, y = p[0], p[1]
    c = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i][0], poly[i][1]
        x2, y2 = poly[(i + 1) % n][0], poly[(i + 1) % n][1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def floor_z(p, poly):
    """Height of the polygon's plane at p (from its first three vertices; NavPower areas are planar)."""
    a, b, c = poly[0], poly[1], poly[2]
    ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
    vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    if abs(nz) < 1e-9:
        return sum(v[2] for v in poly) / len(poly)
    return a[2] - (nx * (p[0] - a[0]) + ny * (p[1] - a[1])) / nz


def seg_dist(p, a, b):
    ax, ay, bx, by = a[0], a[1], b[0], b[1]
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - ax) * dx + (p[1] - ay) * dy) / L))
    return math.hypot(p[0] - (ax + t * dx), p[1] - (ay + t * dy))


def build(areas):
    """A synthetic NAVP (for the tests): areas = [(island, [(x, y, z), ...], [adjacent area index or None])]. The
    island is written to flags1 as a real file would not (navp.py ignores it and numbers the components itself)."""
    body, offs = b"", []
    o = 0
    for _isl, verts, _adj in areas:
        offs.append(o)
        o += AREA + EDGE * len(verts)
    for (isl, verts, adj), _off in zip(areas, offs):
        cx = sum(v[0] for v in verts) / len(verts)
        cy = sum(v[1] for v in verts) / len(verts)
        cz = sum(v[2] for v in verts) / len(verts)
        body += struct.pack("<4Q3ffII4I", 0, 0, 0, 0, cx, cy, cz, 1.0, 0xFFFFFFFF, 1,
                            (len(verts) & 0x7F) | (isl << 7), 0, 0, 0)
        for k, v in enumerate(verts):
            body += struct.pack("<Q3fII4x", 0 if adj[k] is None else offs[adj[k]] + GRAPH, v[0], v[1], v[2], 0, 0)
    head = struct.pack("<6I", 0, 2, 0, 0, 0, 0) + struct.pack("<3I", 0x10000, 0, 1) + struct.pack("<3I", 0, 0x28, 1)
    graph = struct.pack("<6I", 0x28, 0, len(body), 0, 0, 0) + struct.pack("<5f", 2, 0.1, 0.2, 0.3, 1.8) + \
        struct.pack("<6f", -100, -100, -10, 100, 100, 50) + struct.pack("<I", 2) + bytes(252)
    return head + graph + body


def selftest():
    # Two 4 m squares side by side on island 1 (sharing the edge x = 4), one raised square on island 2.
    sq1 = [(0, 0, 0), (4, 0, 0), (4, 4, 0), (0, 4, 0)]
    sq2 = [(4, 0, 0), (8, 0, 0), (8, 4, 0), (4, 4, 0)]
    sq3 = [(20, 20, 5), (24, 20, 5), (24, 24, 5), (20, 24, 5)]
    m = NavMesh(build([(1, sq1, [None, 1, None, None]), (1, sq2, [None, None, None, 0]), (2, sq3, [None] * 4)]))
    assert len(m.areas) == 3 and m.islands() == {1: 2, 2: 1}, m.islands()
    hit = m.area_at((3, 2, 0.3))
    assert hit and hit[0].island == 1 and abs(hit[1]) < 1e-6
    assert m.area_at((3, 2, 4.0)) is None  # too high above the floor
    assert m.area_at((22, 22, 5.2))[0].island == 2
    assert abs(m.clearance((4, 2, 0)) - 2.0) < 1e-6  # the shared edge is not a boundary
    q, a, c, r = m.snap((0.1, 2, 0), min_clear=1.0)
    assert c >= 1.0 and 0.8 <= r <= 1.0, (q, c, r)
    assert m.snap((50, 50, 0), radius=2.0) is None
    print("navp selftest: ok")


def main():
    if sys.argv[1:] == ["--selftest"]:
        return selftest()
    with open(sys.argv[1], "rb") as f:
        m = NavMesh(f.read())
    print("NAVP v%d: %d areas, %d boundary edges, islands %s, bbox %s" %
          (m.version, len(m.areas), len(m.boundary), sorted(m.islands().items(), key=lambda kv: -kv[1])[:10],
           tuple(round(x, 1) for x in m.bbox)))
    for s in sys.argv[2:]:
        p = tuple(float(x) for x in s.split(","))
        hit = m.area_at(p)
        sn = m.snap(p)
        print("%s: %s; snap %s" % (s, "on island %d, floor %.2f, clearance %.2f" %
                                   (hit[0].island, hit[1], m.clearance((p[0], p[1], hit[1]))) if hit else "OFF the mesh",
                                   "none within 6 m" if not sn else "(%.2f, %.2f, %.2f) %.2f m away, clearance %.2f" %
                                   (sn[0][0], sn[0][1], sn[0][2], sn[3], sn[2])))


if __name__ == "__main__":
    main()
