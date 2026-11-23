"""The spot fixes of appended map variants (tools/landslide/spotfix_spec.py): a fix brick of ours, loaded after the
option's bricks on Freelancer's own scene (as the full Gluttony's fr_gluttony, build_bricks.build_fix), that moves or
re-points the territory spots the option spoils.

An appended option's bricks were made for IOI's own scene, so they can put a crowd at a supplier's table, a sofa on a
stash or a closed door between Freelancer's spots and the player (variant_audit.py finds them). The fix
brick holds only property overrides on Freelancer's override brick (override_<game>) and, when a spec has them,
deletes:

- MOVES: {entity id: dict(kind, to, why)}: a territory spot's m_mTransform, the translation set to `to` (world
  coordinates) and its rotation kept. A supplier's NPC and private volume hang off the supplier (m_eidParent) and
  move with it, as on The Icon (icon_spec.py, where the same spots were moved in our copy of the override brick).
  A safe's clues, guard and guard's acts are entities of their own (hung off the brick root): a moved safe moves
  them too, each an entry of its kind (clue, guard, act; MEMBER_KINDS), and the brick is refused when the group is
  not whole (a moved safe without one of them, or one of them without its safe), as icon_spec's SPOT_MOVES are.
- REPOINTS: {slot property: (old entity id, new entity id, why)}: one of the territory randomizer's availableXxNN
  slots named another spot of the same kind (its exposed entity kept), as the full Landslide's SLOT_REPOINTS do in
  our copy of override_bingo. The randomizer (helper_evergreen_setuprandomizer: a SignalRandomRoute over its slots,
  each output setting one slot's bool) enables GivenNumber distinct slots, so two slots naming one spot enable it
  twice: the campaign may get fewer distinct spots of that kind, never a slot that names nothing. No spot entity is
  deleted: a slot always names a live entity, and the spot left out stays in the scene, never enabled.
- DELETES: [(brick, entity id, name, why)] (the option's props), as fr_gluttony's.

The checks (check(), run by build_bricks.build_fix on every build of a spot fix; it stops on any problem), on each of
the spec's scenes (Freelancer's mild and showdown scene with the option's bricks, everything they load read in binary
from --bin) with the fix brick applied:

- each moved or re-pointed spot is a territory slot's spot of the kind the spec says (a moved clue, guard or act an
  entity of that kind of a moved safe's group), each slot named by REPOINTS names the old entity, and no slot is left
  naming a spot of BROKEN (the spots the audit found spoiled) that the fix does not move;
- no safe, stash or supplier a slot names, and no clue of a named safe, is sealed off: where Freelancer's navmesh has
  floor within SEALED_ON_M of it, the option's has floor on its main island within SEALED_ON_M too (a closed room,
  as Garden Show's mansion; a floor behind a wall 2-3 m away does not count);
- a moved spot: at `to`, with navmesh floor within MOVE_TOLERANCE_M (the option's own navmesh, else Freelancer's)
  with its kind's clearance (CLEARANCE) on the mesh's main island, on the walking floor (within FLOOR_TOLERANCE_M of
  placement.Placement.floor_z: not at the navmesh's height, 0.05-0.2 m above it), no act within
  placement.ACT_GAP_M of it, other spots at least placement.SPOT_GAP_M away, and no solid prop or collision box of
  any brick on it (landslide_audit's 0.6 m); a moved supplier also has no act within placement.SUPPLIER_GAP_M
  (5 m, as The Icon's), no act in its private volume that Freelancer's own scene does not have there, and its NPC
  faces open floor (placement.supplier_problem: CRATE_ROOM_M of navmesh straight in front; else a yaw); a moved safe
  no act within landslide_audit.SPOT_PRIVACY_M (the privacy check's 2 m) but its own guard's; a moved clue no other
  clue or spot within CLUE_GAP_M (not stacked); a moved guard or act only its own guard's acts within ACT_GAP_M (the
  guard stands at one), and a moved act keeps no other spot busy (landslide_audit.privacy: within 5 m of a supplier
  or in his private volume, within 2 m of a safe or a stash);
- a re-pointed slot's new spot: floor on the main island within SEALED_ON_M, and no prop of the option on it.
- a moved crate (a stash) is not in the way (placement.crate_in_the_way: no part of it within 1.5 m of a door's line
  across its opening, no passage narrowed below 0.8 m of navmesh; below 1 m a line says it is tight); nor is a
  re-pointed slot's crate, unless it is so in Freelancer's own scene (landslide_audit.crate_way).

variant_audit.py runs its every-map audit with the fix brick too (FIX_SPECS: audit_brick builds the same overrides
on an empty brick), so a run shows each option as appended and with its fix.
"""
import math

import brick_edit as be
import placement as pl

# The root of IOI's empty bricks (vr_overrides_*, difficulty_easy_*): a ZSpatialEntity named "Scene".
ROOT_TYPE = 0x00A0A2026E9090BC   # [modules:/zspatialentity.class].pc_entitytype
ROOT_BLUEPRINT = 0x00B09C0C1A885059  # [modules:/zspatialentity.class].pc_entityblueprint
ROOT_ID = 0xFFFFFFFFFFFFFFFE

KINDS = {"stash": "setpiece_evergreen_stash_unique", "safe": "setpiece_evergreen_container_items_safe",
         "supplier": "helper_evergreen_supplierpackage", "mule": "setpiece_evergreen_moneymulesetup"}
# What a moved safe brings (build_bricks.KIND_TYPES'): its clues, its guard (a safeEnabled pin) and the guard's acts.
MEMBER_KINDS = {"clue": "setpiece_evergreen_scannable_safeclue", "guard": "setpiece_evergreen_safeactor",
                "act": "act_"}
# A moved spot must stand this close to its spec place (the snapped floor point, in plan and height).
MOVE_TOLERANCE_M = 0.3
# A re-pointed slot's new spot needs main-island floor this close (build_bricks.KEPT_FLOOR_M; variant_audit's 3 m
# lets a floor behind a wall count, as for Garden Show's Safe04).
SEALED_ON_M = 1.5
# A moved spot stands on the walking floor (placement.Placement.floor_z), within this.
FLOOR_TOLERANCE_M = 0.1
# The navmesh clearance a moved spot needs: placement.CLEARANCE, but a stash stands by a wall as Freelancer's own do.
CLEARANCE = dict(pl.CLEARANCE, stash=0.3)
# A moved clue: no other clue (of a slot's safe) or spot this close in plan (ACT_GAP_DZ in height): not stacked.
CLUE_GAP_M = 1.0
# An option navmesh with fewer areas than this share of Freelancer's is an extra mesh, not the map's (as in
# variant_audit.EXTRA_NAV_SHARE).
EXTRA_NAV_SHARE = 0.5


class SpotFixError(Exception):
    pass


def leaf(path):
    return path.split("?/")[-1].split("/")[-1].split("]")[0].lower() if path else ""


def type_leaf(b, i, names):
    return leaf(names.get(b.temp_refs[b.temp["subEntities"][i]["entityTypeResourceIndex"]][1], ""))


def kind_of_spot(b, i, names, kinds=KINDS):
    t = type_leaf(b, i, names)
    return next((k for k, p in kinds.items() if t.startswith(p)), None)


def kind_of(b, i, names):
    """A spot's kind (KINDS) or a safe member's (MEMBER_KINDS), or None."""
    return kind_of_spot(b, i, names) or kind_of_spot(b, i, names, MEMBER_KINDS)


def props(e):
    return {p["nPropertyID"]: p["value"]["$val"] for k in ("propertyValues", "postInitPropertyValues") for p in e[k]}


def safe_members(ov, i, names):
    """[(index, kind)] of what goes with the safe at index i of the override brick: its clues (its Clue* properties),
    its guards (its safeEnabled pins) and their acts (act entities whose m_pActor is the guard; the "Acts" group
    entity names the guard too, but has no place), as build_bricks.apply_spot_moves groups them."""
    P = [props(e) for e in ov.temp["subEntities"]]
    out = [(v["entityIndex"], "clue") for k, v in sorted(P[i].items())
           if k.startswith("Clue") and be.is_ref(v) and v["externalSceneIndex"] < 0 and v["entityIndex"] >= 0]
    guards = [p["toID"] for p in ov.tblu["pinConnections"] if p["fromID"] == i and p["fromPinName"] == "safeEnabled"]
    for g in guards:
        out.append((g, "guard"))
        out += [(j, "act") for j in range(len(P)) if be.is_ref(P[j].get("m_pActor")) and
                P[j]["m_pActor"]["externalSceneIndex"] < 0 and P[j]["m_pActor"]["entityIndex"] == g and
                kind_of_spot(ov, j, names, MEMBER_KINDS) == "act"]
    return out


def owners(spec, ov, names):
    """{moved entity id: the moved safe it belongs to} for the spec's moved clues, guards and acts; raises SpotFixError
    when a group is not whole: a moved safe without one of its members (or with one moved as another kind), or a
    moved clue, guard or act of no moved safe."""
    moves = getattr(spec, "MOVES", {})
    owner = {}
    for eid, m in sorted(moves.items()):
        if m["kind"] != "safe":
            continue
        i = ov.index_of(eid)
        for j, kind in safe_members(ov, i, names):
            jid = ov.ids()[j]
            if moves.get(jid, {}).get("kind") != kind:
                raise SpotFixError("%s: MOVES %s but not its %s %s (as a %s)" % (spec.NAME, ov.describe(i), kind,
                                                                                ov.describe(j), kind))
            owner[jid] = eid
    for eid, m in sorted(moves.items()):
        if m["kind"] in MEMBER_KINDS and eid not in owner:
            raise SpotFixError("%s: MOVES %s (%s) belongs to no moved safe" % (
                spec.NAME, ov.describe(ov.index_of(eid)), m["kind"]))
    return owner


def territory(ov):
    """(index, {slot property: local reference}) of the override brick's territory randomizer: the entity with
    available* slots naming local entities (one per override brick)."""
    hits = []
    for i, e in enumerate(ov.temp["subEntities"]):
        slots = {p["nPropertyID"]: p["value"]["$val"] for k in ("propertyValues", "postInitPropertyValues")
                 for p in e[k] if p["nPropertyID"].startswith("available") and be.is_ref(p["value"]["$val"])}
        if slots:
            hits.append((i, slots))
    if len(hits) != 1:
        raise SpotFixError("%s has %d entities with available* slots, not 1" % (ov.name, len(hits)))
    return hits[0]


def inverse(w):
    """The inverse of a rigid world matrix (3x4 rows)."""
    r = [[w[c][r] for c in range(3)] for r in range(3)]
    t = [-sum(r[i][k] * w[k][3] for k in range(3)) for i in range(3)]
    return [r[i] + [t[i]] for i in range(3)]


def apply_to_point(w, p):
    return tuple(sum(w[i][k] * p[k] for k in range(3)) + w[i][3] for i in range(3))


def skeleton(spec):
    """An empty fix brick (its root only), as build_bricks.build_fix makes from FIX_SOURCE, for the audit."""
    root = {"logicalParent": be.local_ref(-1), "entityTypeResourceIndex": 0, "propertyValues": [],
            "postInitPropertyValues": [], "platformSpecificPropertyValues": []}
    root["logicalParent"]["entityIndex"] = -1
    temp = {"subType": 2, "blueprintIndexInResourceHeader": 1, "rootEntityIndex": 0, "subEntities": [root],
            "propertyOverrides": [], "externalSceneTypeIndicesInResourceHeader": []}
    troot = {"logicalParent": dict(root["logicalParent"]), "entityTypeResourceIndex": 0, "entityId": ROOT_ID,
             "editorOnly": False, "entityName": "Scene", "propertyAliases": [], "exposedEntities": [],
             "exposedInterfaces": [], "entitySubsets": []}
    tblu = {"subType": 2, "rootEntityIndex": 0, "subEntities": [troot], "externalSceneTypeIndicesInResourceHeader": [],
            "pinConnections": [], "inputPinForwardings": [], "outputPinForwardings": [], "overrideDeletes": [],
            "pinConnectionOverrides": [], "pinConnectionOverrideDeletes": []}
    return be.Brick(spec.FIX_PATH.rsplit("/", 1)[-1], temp, tblu,
                    [(be.FLAG_NORMAL, ROOT_TYPE), (be.FLAG_NORMAL, spec.FIX_IDS[1])], [(be.FLAG_NORMAL, ROOT_BLUEPRINT)])


def apply(spec, b, ov, scene, names):
    """Adds the spec's moves, re-points and deletes to the fix brick b (its external scenes: the override brick, then
    the bricks of the deletes). ov: Freelancer's override brick (read, with .hash); scene: a landslide_audit.Scene of
    the bricks loaded before the fix (the moved entities' parents' places). Returns audit lines; raises SpotFixError
    on a spec that does not match the bricks."""
    lines = []
    ovh = ov.hash
    if ovh != spec.OVERRIDE[0]:
        raise SpotFixError("%s: the override brick is %016X, not the spec's %016X" % (spec.NAME, ovh, spec.OVERRIDE[0]))
    for h in [ovh] + [h for h, _, _, _ in getattr(spec, "DELETES", [])]:
        if h not in b.scenes:
            b.add_external_scene(h)
    ti, slots = territory(ov)
    tid = ov.tblu["subEntities"][ti]["entityId"]
    for eid, m in sorted(getattr(spec, "MOVES", {}).items()):
        i = ov.index_of(eid)
        if kind_of(ov, i, names) != m["kind"]:
            raise SpotFixError("%s: MOVES %s is a %s, not a %s" % (spec.NAME, ov.describe(i), type_leaf(ov, i, names),
                                                                   m["kind"]))
    owners(spec, ov, names)
    for eid, m in sorted(getattr(spec, "MOVES", {}).items()):
        i = ov.index_of(eid)
        w = scene.world(ovh, i)
        local, _ = scene.prop(ovh, i, "m_mTransform")
        if w is None or not isinstance(local, dict) or "Trans" not in local:
            raise SpotFixError("%s: MOVES %s has no place" % (spec.NAME, ov.describe(i)))
        lm = matrix(local)
        pw = mul(w, inverse(lm))  # its parent's world
        t = apply_to_point(inverse(pw), m["to"])
        value = {k: dict(v) for k, v in local.items()}
        value["Trans"] = {"x": be.f32(t[0]), "y": be.f32(t[1]), "z": be.f32(t[2])}
        turned = ""
        if m.get("yaw") is not None:
            # A turn about world up, level (a supply crate's open side to the room: placement.crate_yaw), in its
            # parent's axes: local axis k = the parent's rotation transposed times the world axis.
            c, sn = math.cos(math.radians(m["yaw"])), math.sin(math.radians(m["yaw"]))
            for key, ax in (("XAxis", (c, sn, 0.0)), ("YAxis", (-sn, c, 0.0)), ("ZAxis", (0.0, 0.0, 1.0))):
                lx = [sum(pw[r][k] * ax[r] for r in range(3)) for k in range(3)]
                value[key] = {"x": be.f32(lx[0]), "y": be.f32(lx[1]), "z": be.f32(lx[2])}
            turned = ", turned to yaw %.0f" % m["yaw"]
        b.add_property_override((ovh, eid), "m_mTransform", "SMatrix43", value)
        old = (w[0][3], w[1][3], w[2][3])
        lines.append("move %s (%s) (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f), %.1f m%s: %s" % (
            ov.describe(i), m["kind"], old[0], old[1], old[2], m["to"][0], m["to"][1], m["to"][2],
            math.dist(old, m["to"]), turned, m["why"]))
    for slot, (old, new, why) in sorted(getattr(spec, "REPOINTS", {}).items()):
        if slot not in slots:
            raise SpotFixError("%s: REPOINTS %s is not a slot of %s" % (spec.NAME, slot, ov.describe(ti)))
        r = slots[slot]
        if r["externalSceneIndex"] >= 0 or ov.ids()[r["entityIndex"]] != old:
            raise SpotFixError("%s: %s names %s, not %016x" % (
                spec.NAME, slot, ov.describe(r["entityIndex"]) if r["externalSceneIndex"] < 0 else "another brick",
                old))
        ki, kn = kind_of_spot(ov, ov.index_of(old), names), kind_of_spot(ov, ov.index_of(new), names)
        if ki is None or ki != kn:
            raise SpotFixError("%s: %s re-points a %s to a %s (%s)" % (spec.NAME, slot, ki, kn,
                                                                     ov.describe(ov.index_of(new))))
        b.add_property_override((ovh, tid), slot, "SEntityTemplateReference", (ovh, new))
        b.temp["propertyOverrides"][-1]["propertyValue"]["value"]["$val"]["exposedEntity"] = r["exposedEntity"]
        lines.append("re-point %s %s -> %s: %s" % (slot, ov.describe(ov.index_of(old)), ov.describe(ov.index_of(new)),
                                                  why))
    for h, eid, name, why in getattr(spec, "DELETES", []):
        b.add_override_delete(h, eid)
        lines.append("delete %016X %016x %s: %s" % (h, eid, name, why))
    return lines


def matrix(m):
    X, Y, Z, T = m["XAxis"], m["YAxis"], m["ZAxis"], m["Trans"]
    return [[X["x"], Y["x"], Z["x"], T["x"]], [X["y"], Y["y"], Z["y"], T["y"]], [X["z"], Y["z"], Z["z"], T["z"]]]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0.0) for j in range(4)]
            for i in range(3)]


def audit_brick(spec, bricks, names, scene_cls):
    """{fix TEMP hash: the fix brick} for variant_audit: skeleton() with apply(), on the bricks read for the option's
    scene ({hash: Brick}, the override among them). scene_cls: landslide_audit.Scene."""
    b = skeleton(spec)
    apply(spec, b, bricks[spec.OVERRIDE[0]], scene_cls(bricks), names)
    b.hash = spec.FIX_IDS[0]
    return {spec.FIX_IDS[0]: b}


def main_island(mesh):
    isl = mesh.islands()
    return max(isl, key=isl.get) if isl else None


def check(spec, label, scene, freelancer, own, mesh, la, names, freelancer_mesh=None):
    """(lines, problems) of the checks on one of the spec's scenes: scene: the option's scene with the fix brick
    (landslide_audit.Scene), freelancer: Freelancer's own scene (no option), own: the option's bricks' hashes, mesh:
    the navmesh the scene loads (navp.NavMesh), la: the landslide_audit module, freelancer_mesh: Freelancer's own
    navmesh (the sealed check runs when it is given and is not mesh)."""
    lines, problems = [], []
    ovh = spec.OVERRIDE[0]
    ov = scene.B[ovh]
    ti, slots = territory(ov)
    moves = getattr(spec, "MOVES", {})
    island = main_island(mesh)
    # Where each slot points now (the last override of the slot property wins).
    now = {}
    for slot, r in slots.items():
        v, ctx = scene.prop(ovh, ti, slot)
        k = scene.resolve(ctx, v) if be.is_ref(v) else None
        if k is None or k[0] != ovh:
            problems.append("%s: %s names no entity of %s" % (label, slot, ov.name))
            continue
        now[slot] = ov.ids()[k[1]]
    for slot, eid in sorted(now.items()):
        if eid in getattr(spec, "BROKEN", {}) and eid not in moves:
            problems.append("%s: %s still names %s, which the audit found spoiled (%s)" % (
                label, slot, ov.describe(ov.index_of(eid)), spec.BROKEN[eid]))
    for eid, m in sorted(moves.items()):
        if m["kind"] in KINDS and eid not in now.values():
            problems.append("%s: MOVES %s is no slot's spot" % (label, ov.describe(ov.index_of(eid))))

    acts = [(h, i, p) for h, i, p in la.scene_acts(scene, names)]
    judge = pl.Placement(mesh, None, [p for _, _, p in acts])
    spot_pos = {eid: scene.pos(ovh, ov.index_of(eid)) for eid in set(now.values())}
    # The clues of the safes the slots name (under the fix), and each safe's guards.
    clue_pos, guards_of = {}, {}
    for eid in set(now.values()):
        i = ov.index_of(eid)
        if kind_of_spot(ov, i, names) == "safe":
            for j, kind in safe_members(ov, i, names):
                if kind == "clue":
                    clue_pos[ov.ids()[j]] = (scene.pos(ovh, j), eid)
                elif kind == "guard":
                    guards_of.setdefault(eid, set()).add((ovh, j))
    doors, fl_doors = la.scene_doors(scene, names), la.scene_doors(freelancer, names)
    everything = la.new_content(scene, la.Scene({}), names, {spec.FIX_IDS[0]})
    new = la.new_content(scene, freelancer, names, {spec.FIX_IDS[0]})
    for eid, m in sorted(moves.items()):
        i = ov.index_of(eid)
        q = scene.pos(ovh, i)
        desc = "%s %s" % (m["kind"], ov.describe(i))
        if q is None or math.dist(q, m["to"]) > 0.01:
            problems.append("%s: %s is at %s under the fix, not %s" % (label, desc, q, m["to"]))
            continue
        need = CLEARANCE[m["kind"]]
        hit = mesh.snap(q, min_clear=need, radius=MOVE_TOLERANCE_M, up=MOVE_TOLERANCE_M, down=MOVE_TOLERANCE_M)
        if hit is None:
            problems.append("%s: %s at %s: no navmesh floor with %.1f m clearance within %.1f m" % (
                label, desc, pl.fmt(q), need, MOVE_TOLERANCE_M))
            continue
        if hit[1].island != island:
            problems.append("%s: %s at %s: its floor is on island %d, not the main one %d" % (
                label, desc, pl.fmt(q), hit[1].island, island))
        floor = judge.floor_z(hit[0])
        if abs(q[2] - floor) > FLOOR_TOLERANCE_M:
            problems.append("%s: %s at %s: %.2f m %s the floor (%.2f; the navmesh is at %.2f)" % (
                label, desc, pl.fmt(q), abs(q[2] - floor), "above" if q[2] > floor else "below", floor, hit[0][2]))
        # The NPCs whose acts may stand by it: a supplier's own, a safe's guards (and a guard's, his own acts).
        own_npc = set()
        if m["kind"] == "supplier":
            for r, ctx in [scene.prop(ovh, i, "m_rActorProvider")]:
                k = scene.resolve(ctx, r) if be.is_ref(r) else None
                if k:
                    own_npc.add(k)
        elif m["kind"] == "safe":
            own_npc |= guards_of.get(eid, set())
        elif m["kind"] == "guard":
            own_npc.add((ovh, i))
        near = sorted((pl.plan_d(q, p), h, j, p) for h, j, p in acts if abs(p[2] - q[2]) < pl.ACT_GAP_DZ and
                      pl.plan_d(q, p) < pl.ACT_GAP_M and (h, j) != (ovh, i) and la.actor_of(scene, h, j) not in own_npc)
        if near:
            problems.append("%s: %s at %s: an act %.2f m away (at least %.1f m)" % (label, desc, pl.fmt(q), near[0][0],
                                                                                    pl.ACT_GAP_M))
        if m["kind"] in KINDS:
            for other, r in spot_pos.items():
                if other != eid and r is not None and abs(r[2] - q[2]) < pl.SPOT_GAP_DZ and \
                        pl.plan_d(q, r) < pl.SPOT_GAP_M:
                    problems.append("%s: %s at %s is %.2f m from the spot %s" % (
                        label, desc, pl.fmt(q), pl.plan_d(q, r), ov.describe(ov.index_of(other))))
        if m["kind"] == "safe":
            busy = sorted((pl.plan_d(q, p), h, j) for h, j, p in acts if abs(p[2] - q[2]) < la.SPOT_PRIVACY_DZ and
                          pl.plan_d(q, p) < la.SPOT_PRIVACY_M and la.actor_of(scene, h, j) not in own_npc)
            if busy:
                d, h, j = busy[0]
                problems.append("%s: %s at %s: %d act(s) within %.1f m, the nearest %.2f m away (%s %s): someone "
                                "stands at the safe" % (label, desc, pl.fmt(q), len(busy), la.SPOT_PRIVACY_M, d,
                                                        scene.B[h].name, scene.B[h].describe(j)))
        if m["kind"] == "clue":
            others = [(c, r) for c, (r, _) in clue_pos.items() if c != eid] + list(spot_pos.items())
            for other, r in others:
                if r is not None and abs(r[2] - q[2]) < pl.ACT_GAP_DZ and pl.plan_d(q, r) < CLUE_GAP_M:
                    problems.append("%s: %s at %s is %.2f m from %s (at least %.1f m: not stacked)" % (
                        label, desc, pl.fmt(q), pl.plan_d(q, r), ov.describe(ov.index_of(other)), CLUE_GAP_M))
        if m["kind"] == "supplier":
            busy = sorted((pl.plan_d(q, p), h, j) for h, j, p in acts if abs(p[2] - q[2]) < pl.SPOT_GAP_DZ and
                          pl.plan_d(q, p) < pl.SUPPLIER_GAP_M and la.actor_of(scene, h, j) not in own_npc)
            if busy:
                d, h, j = busy[0]
                problems.append("%s: %s at %s: %d act(s) within %.1f m, the nearest %.2f m away (%s %s): a supplier "
                                "will not deal while someone is about" % (
                                    label, desc, pl.fmt(q), len(busy), pl.SUPPLIER_GAP_M, d, scene.B[h].name,
                                    scene.B[h].describe(j)))
            vanilla = set()
            for _, sid, _, _, own_f, vols in la.privacy_places(freelancer.B[ovh], names, freelancer):
                if sid == eid:
                    for h, j, p in la.scene_acts(freelancer, names):
                        if any(la.in_volume(p, w, size)[0] for w, size in vols):
                            vanilla.add((h, freelancer.B[h].tblu["subEntities"][j]["entityId"]))
            for _, sid, _, _, own_s, vols in la.privacy_places(ov, names, scene):
                if sid != eid:
                    continue
                inside = [(h, j) for h, j, p in acts if any(la.in_volume(p, w, size)[0] for w, size in vols) and
                          la.actor_of(scene, h, j) not in own_s and
                          (h, scene.B[h].tblu["subEntities"][j]["entityId"]) not in vanilla]
                for h, j in inside:
                    problems.append("%s: %s at %s: %s %s is inside its private volume (not so under Freelancer)" % (
                        label, desc, pl.fmt(q), scene.B[h].name, scene.B[h].describe(j)))
            # His NPC (a child of the spot) faces open floor, not a wall a step away (placement.supplier_problem).
            spot_yaw = pl.yaw_of((scene.world(ovh, i)[0][0], scene.world(ovh, i)[1][0], scene.world(ovh, i)[2][0]))
            for k in sorted(own_npc):
                nw, np_ = scene.world(*k), scene.pos(*k)
                if nw is None:
                    continue
                npc_yaw = pl.yaw_of((nw[0][0], nw[1][0], nw[2][0]))
                bad = pl.supplier_problem(mesh, np_, npc_yaw)
                if bad:
                    problems.append("%s: %s at %s: its NPC %s %s %s (the spot is at yaw %.0f: give it a yaw in the "
                                    "spec, turned as above)" % (label, desc, pl.fmt(q), scene.B[k[0]].name,
                                                                scene.B[k[0]].describe(k[1]), bad, spot_yaw))
                lines.append("%s: %s at %s: its NPC faces (yaw %.0f) %.2f m of navmesh" % (
                    label, desc, pl.fmt(q), npc_yaw, pl.crate_room(mesh, np_, npc_yaw)))
        if m["kind"] == "stash":
            w = scene.world(ovh, i)
            yaw = pl.yaw_of((w[0][0], w[1][0], w[2][0]))
            bad = pl.crate_problem(mesh, q, yaw)
            if bad:
                problems.append("%s: %s at %s: %s (give it the yaw placement.crate_yaw finds)" % (
                    label, desc, pl.fmt(q), bad))
            way, way_notes = la.crate_way(mesh, q, yaw, doors)
            problems += ["%s: %s at %s: %s" % (label, desc, pl.fmt(q), why) for why in way]
            lines += ["%s: %s at %s: %s" % (label, desc, pl.fmt(q), why) for why in way_notes]
            lines.append("%s: %s at %s: open side (yaw %.0f) %.2f m of navmesh in front" % (
                label, desc, pl.fmt(q), yaw, pl.crate_room(mesh, q, yaw)))
        clashes, _ = la.audit(scene, [c for c in everything if not (c[0] == ovh and c[1] in ov.subtree(i))],
                              [(desc, q)])
        problems += ["%s: %s" % (label, c) for c in clashes]
        others = [(pl.plan_d(q, p), la.actor_of(scene, h, j) in own_npc) for h, j, p in acts
                  if abs(p[2] - q[2]) < pl.SPOT_GAP_DZ and (h, j) != (ovh, i)]
        lines.append("%s: %s at %s: navmesh floor %.2f m away, %+.2f m from the walking floor, clearance %.2f m, "
                     "island %d%s" % (label, desc, pl.fmt(q), hit[3], q[2] - floor, hit[2], hit[1].island,
            "; nearest act %.1f m%s" % (min(others)[0], " (its own NPC's)" if min(others)[1] else "")
            if others else ""))
    # A moved act (a safe guard's) keeps no other spot busy: not within the privacy check's distance of a supplier, a
    # safe or a stash, nor inside a supplier's private volume (landslide_audit.privacy; the spot's own NPC's acts are
    # its own).
    moved_acts = [(ovh, ov.index_of(eid)) for eid, m in sorted(moves.items()) if m["kind"] == "act"]
    moved_acts = [(h, j, scene.pos(h, j)) for h, j in moved_acts if scene.pos(h, j) is not None]
    if moved_acts:
        for line in sorted(la.privacy(scene, la.privacy_places(ov, names, scene), moved_acts, names).values()):
            problems.append("%s: a moved act keeps a spot busy: %s" % (label, line))
    if freelancer_mesh is not None and freelancer_mesh is not mesh:
        places = [(kind_of_spot(ov, ov.index_of(eid), names), eid, q) for eid, q in sorted(spot_pos.items())] + \
            [("clue", c, q) for c, (q, _) in sorted(clue_pos.items())]
        n = 0
        for kind, eid, q in places:
            if kind not in ("safe", "stash", "supplier", "clue") or q is None:
                continue
            n += 1
            f = freelancer_mesh.snap(q, min_clear=0.0, radius=SEALED_ON_M)
            if f is None:
                continue  # no floor by it under Freelancer either (high furniture): not the option's doing
            v = mesh.snap(q, min_clear=0.0, radius=SEALED_ON_M)
            if v is None or v[1].island != island:
                what = ("of %s" % ov.describe(ov.index_of(clue_pos[eid][1]))) if kind == "clue" else \
                    ", ".join(k for k, x in sorted(now.items()) if x == eid)
                problems.append("%s: %s %s (%s) at %s: sealed off: floor %.2f m away under Freelancer's navmesh, "
                                "none within %.1f m on the option's main island" % (
                                    label, kind, ov.describe(ov.index_of(eid)), what, pl.fmt(q), f[3], SEALED_ON_M))
        lines.append("%s: sealed check: %d safes, stashes, suppliers and clues the slots name" % (label, n))
    for slot, (old, new_eid, _) in sorted(getattr(spec, "REPOINTS", {}).items()):
        if now.get(slot) != new_eid:
            problems.append("%s: %s names %016x under the fix, not %016x" % (label, slot, now.get(slot, 0), new_eid))
            continue
        i = ov.index_of(new_eid)
        q = scene.pos(ovh, i)
        desc = "%s %s (%s)" % (kind_of_spot(ov, i, names), ov.describe(i), slot)
        hit = mesh.snap(q, min_clear=0.0, radius=SEALED_ON_M) if q else None
        if hit is None or hit[1].island != island:
            problems.append("%s: %s at %s: no floor on the main island within %.1f m (sealed off)" % (
                label, desc, q, SEALED_ON_M))
        clashes, _ = la.audit(scene, [c for c in new if c[0] in own], [(desc, q)])
        problems += ["%s: %s" % (label, c) for c in clashes]
        room = ""
        if q and kind_of_spot(ov, i, names) == "stash":
            # The vanilla crate the slot names now opens toward the room on the option's navmesh, as
            # landslide_audit.crate_faces asks of every vanilla crate: under CRATE_FRONT_M is a problem when
            # Freelancer's navmesh gives it that much (the option takes the room away), as under Freelancer otherwise
            # (the navmesh is cut round some vanilla crates).
            w = scene.world(ovh, i)
            yaw = pl.yaw_of((w[0][0], w[1][0], w[2][0]))
            r = pl.crate_room(mesh, q, yaw)
            before = pl.crate_room(freelancer_mesh, q, yaw) \
                if freelancer_mesh is not None and freelancer_mesh is not mesh else None
            if r < pl.CRATE_FRONT_M and before is not None and before >= pl.CRATE_FRONT_M:
                problems.append("%s: %s at %s: its open side (yaw %.0f) has %.2f m of navmesh in front, %.2f m under "
                                "Freelancer's navmesh" % (label, desc, pl.fmt(q), yaw, r, before))
            for why in la.crate_way(mesh, q, yaw, doors, freelancer_mesh or mesh, fl_doors)[0]:
                problems.append("%s: %s at %s: %s" % (label, desc, pl.fmt(q), why))
            room = ", open side (yaw %.0f) %.2f m of navmesh in front%s" % (
                yaw, r, " (as under Freelancer)" if r < pl.CRATE_FRONT_M else "")
        lines.append("%s: %s -> %s at %s%s" % (label, slot, ov.describe(i), pl.fmt(q) if q else "?", room))
    return lines, problems


def navmesh_of(newest, fl, var, read_navp):
    """The navmesh the option's scene walks on: the option's own NAVP when it has one that replaces Freelancer's,
    else Freelancer's. read_navp(hash): a navp.NavMesh or None. Returns (hash, mesh, Freelancer's mesh)."""
    def navps(closure):
        return {r for h in closure for _, r in newest[h]["refs"] if newest.get(r, {}).get("type") == "NAVP"}
    fn, vn = navps(fl), navps(var) - navps(fl)
    if len(fn) != 1:
        raise SpotFixError("Freelancer's scene loads %d navmeshes, not 1" % len(fn))
    fh = next(iter(fn))
    fm = read_navp(fh)
    if fm is None:
        raise SpotFixError("Freelancer's navmesh %016X.NAVP is not extracted" % fh)
    if len(vn) == 1:
        vh = next(iter(vn))
        vm = read_navp(vh)
        if vm is None:
            raise SpotFixError("the option's navmesh %016X.NAVP is not extracted" % vh)
        if len(vm.areas) >= EXTRA_NAV_SHARE * len(fm.areas):
            return vh, vm, fm
    elif len(vn) > 1:
        raise SpotFixError("the option loads %d navmeshes" % len(vn))
    return fh, fm, fm
