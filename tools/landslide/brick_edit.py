"""Edits a brick (a TEMP and its TBLU, as ZHMTools ResourceLib JSON) and its two reference tables.

Used by build_bricks.py. Every operation names entities by their sub-entity id (the 64-bit id the TBLU gives each
entity, the one the game's logs and other bricks use), never by index, and fails loudly (BrickError) when an id is
missing, appears twice, or an edit would leave a reference dangling. That is what makes a re-base onto a newer game
build mechanical: the same spec either applies cleanly or says which id moved.

The JSON is index based:
- a local entity reference is {"entityID": 0xFFFFFFFFFFFFFFFF, "externalSceneIndex": -1, "entityIndex": i};
- a reference into another brick is {"entityID": <its id>, "externalSceneIndex": k, "entityIndex": -2}, where k
  indexes externalSceneTypeIndicesInResourceHeader, whose values index the reference table (the rpkg's, not in the
  JSON);
- entityTypeResourceIndex and blueprintIndexInResourceHeader index the reference tables too.

The reference tables of IOI's bricks are laid out [entity types...] [other dependencies...] [external scenes...]:
the TEMP's types are its TEMP-side templates, then 0x5F-flagged dependencies (behavior trees, the body bag), then its
own blueprint, then the external scenes; the TBLU's are the types' blueprints, then the blueprint closure, then the
external scenes (as TEMP hashes). The external scenes always come last; this module checks that and rebuilds only
that tail (and the TEMP's blueprint reference).
"""
import copy
import math
import struct

LOCAL = 0xFFFFFFFFFFFFFFFF
# The TBLU's lists of (fromID, toID) index pairs: pin connections, and the pin forwardings of composite entities.
PIN_LISTS = ("pinConnections", "inputPinForwardings", "outputPinForwardings")
REF_KEYS = {"entityID", "externalSceneIndex", "entityIndex", "exposedEntity"}
FLAG_NORMAL = 0x1F


class BrickError(Exception):
    pass


def f32(x):
    """x as the 32-bit float the binary resource stores (so a generate round trip gives it back exactly)."""
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def is_ref(v):
    return isinstance(v, dict) and REF_KEYS <= set(v.keys())


def walk_refs(v, out):
    """Every SEntityTemplateReference dict inside v (property values, nested arrays and structs)."""
    if is_ref(v):
        out.append(v)
        return
    if isinstance(v, dict):
        for x in v.values():
            walk_refs(x, out)
    elif isinstance(v, list):
        for x in v:
            walk_refs(x, out)


def _walk_resource_ids(v, out):
    """Every ZRuntimeResourceID value ({"m_IDHigh", "m_IDLow"}: m_IDLow indexes the reference table when m_IDHigh is
    0) inside v."""
    if isinstance(v, dict):
        if set(v.keys()) == {"m_IDHigh", "m_IDLow"}:
            out.append(v)
            return
        for x in v.values():
            _walk_resource_ids(x, out)
    elif isinstance(v, list):
        for x in v:
            _walk_resource_ids(x, out)


def local_ref(index, exposed=""):
    return {"entityID": LOCAL, "externalSceneIndex": -1, "entityIndex": index, "exposedEntity": exposed}


def ext_ref(scene, entity_id, exposed=""):
    return {"entityID": entity_id, "externalSceneIndex": scene, "entityIndex": -2, "exposedEntity": exposed}


class Brick:
    """One brick: temp/tblu are the ResourceLib JSON dicts; temp_refs/tblu_refs are [(flags, hash)]."""

    def __init__(self, name, temp, tblu, temp_refs, tblu_refs, editable=True):
        """editable False: read only (the JSON is not copied)."""
        self.name = name
        self.temp = copy.deepcopy(temp) if editable else temp
        self.tblu = copy.deepcopy(tblu) if editable else tblu
        self.temp_refs = list(temp_refs)
        self.tblu_refs = list(tblu_refs)
        te, be = self.temp["subEntities"], self.tblu["subEntities"]
        if len(te) != len(be):
            raise BrickError("%s: TEMP has %d entities, TBLU %d" % (name, len(te), len(be)))
        if self.temp["rootEntityIndex"] != self.tblu["rootEntityIndex"]:
            raise BrickError("%s: TEMP and TBLU roots differ" % name)
        self._check_tail()
        self.scenes = self.external_scenes()

    # --- reference tables -------------------------------------------------------------------------------------

    def _check_tail(self):
        for what, js, refs in (("TEMP", self.temp, self.temp_refs), ("TBLU", self.tblu, self.tblu_refs)):
            ext = js["externalSceneTypeIndicesInResourceHeader"]
            first = len(refs) - len(ext)
            if ext != list(range(first, len(refs))):
                raise BrickError("%s %s: external scenes are not the reference table's tail (%s of %d)" %
                                 (self.name, what, ext, len(refs)))
        if [self.temp_refs[i][1] for i in self.temp["externalSceneTypeIndicesInResourceHeader"]] != \
                [self.tblu_refs[i][1] for i in self.tblu["externalSceneTypeIndicesInResourceHeader"]]:
            raise BrickError("%s: TEMP and TBLU list different external scenes" % self.name)

    def external_scenes(self):
        return [self.temp_refs[i][1] for i in self.temp["externalSceneTypeIndicesInResourceHeader"]]

    def scene_index(self, scene_hash):
        if scene_hash not in self.scenes:
            raise BrickError("%s: %016X is not one of its external scenes" % (self.name, scene_hash))
        return self.scenes.index(scene_hash)

    def blueprint(self):
        return self.temp_refs[self.temp["blueprintIndexInResourceHeader"]][1]

    def set_blueprint(self, tblu_hash):
        i = self.temp["blueprintIndexInResourceHeader"]
        self.temp_refs[i] = (self.temp_refs[i][0], tblu_hash)

    def replace_scene(self, old, new):
        """Swaps one external scene for another in place (same index), so its references now point into new."""
        i = self.scene_index(old)
        if new in self.scenes:
            raise BrickError("%s: %016X is already an external scene" % (self.name, new))
        for js, attr in ((self.temp, "temp_refs"), (self.tblu, "tblu_refs")):
            refs = getattr(self, attr)
            k = js["externalSceneTypeIndicesInResourceHeader"][i]
            refs[k] = (refs[k][0], new)
        self.scenes[i] = new

    def set_external_scenes(self, scenes):
        """Replaces the external scene list (TEMP hashes, in order). A scene that stays keeps its references (they
        are re-indexed). Deletes, property overrides and pin connection overrides into a scene that goes go with it (they only act on
        that brick); any other reference into it is an error (delete or re-point it first). Returns
        {dropped scene: number of deletes and pin overrides dropped with it}."""
        if len(set(scenes)) != len(scenes):
            raise BrickError("%s: an external scene is listed twice" % self.name)
        old = self.scenes
        remap = {i: scenes.index(h) for i, h in enumerate(old) if h in scenes}
        dropped = self.drop_overrides_into([h for h in old if h not in scenes])
        dangling = []
        for owner, where, r in self.all_refs():
            k = r["externalSceneIndex"]
            if k >= 0:
                if k not in remap:
                    dangling.append("%s %s -> %016X entity %016x" % (owner, where, old[k], r["entityID"]))
                else:
                    r["externalSceneIndex"] = remap[k]
        if dangling:
            raise BrickError("%s: references into a dropped scene:\n  %s" % (self.name, "\n  ".join(dangling)))
        for what, js, attr in (("TEMP", self.temp, "temp_refs"), ("TBLU", self.tblu, "tblu_refs")):
            refs = getattr(self, attr)
            first = len(refs) - len(js["externalSceneTypeIndicesInResourceHeader"])
            refs = refs[:first] + [(FLAG_NORMAL, h) for h in scenes]
            setattr(self, attr, refs)
            js["externalSceneTypeIndicesInResourceHeader"] = list(range(first, first + len(scenes)))
        self.scenes = list(scenes)
        return dropped

    def drop_overrides_into(self, scene_hashes):
        """Removes the deletes, property overrides and pin connection overrides into these external scenes (they only
        act on those bricks); the scenes stay in the list. Returns {scene: number removed}."""
        gone = {self.scene_index(h) for h in scene_hashes}
        dropped = {}

        def into_dropped(refs):
            hits = [r["externalSceneIndex"] for r in refs if r["externalSceneIndex"] in gone]
            for k in hits[:1]:
                dropped[self.scenes[k]] = dropped.get(self.scenes[k], 0) + 1
            return bool(hits)

        self.tblu["overrideDeletes"] = [r for r in self.tblu["overrideDeletes"] if not into_dropped([r])]
        self.temp["propertyOverrides"] = [p for p in self.temp["propertyOverrides"]
                                          if not into_dropped([p["propertyOwner"]])]
        for k in ("pinConnectionOverrides", "pinConnectionOverrideDeletes"):
            self.tblu[k] = [p for p in self.tblu[k] if not into_dropped([p["fromEntity"], p["toEntity"]])]
        return dropped

    # --- entities ---------------------------------------------------------------------------------------------

    def ids(self):
        return [e["entityId"] for e in self.tblu["subEntities"]]

    def index_of(self, entity_id):
        found = [i for i, e in enumerate(self.tblu["subEntities"]) if e["entityId"] == entity_id]
        if len(found) != 1:
            raise BrickError("%s: entity %016x %s" % (self.name, entity_id,
                                                      "is missing" if not found else "appears %d times" % len(found)))
        return found[0]

    def entity_name(self, index):
        return self.tblu["subEntities"][index]["entityName"]

    def describe(self, index):
        return "%016x %s" % (self.tblu["subEntities"][index]["entityId"], self.entity_name(index))

    def all_refs(self):
        """(owner, where, ref dict) for every entity reference in both files (local and external), so they can be
        re-indexed in place. Pin connections and aliases hold bare indices and are handled apart."""
        out = []
        for i, (e, b) in enumerate(zip(self.temp["subEntities"], self.tblu["subEntities"])):
            owner = self.describe(i)
            out.append((owner, "logicalParent (TEMP)", e["logicalParent"]))
            out.append((owner, "logicalParent (TBLU)", b["logicalParent"]))
            for kind in ("propertyValues", "postInitPropertyValues", "platformSpecificPropertyValues"):
                for p in e[kind]:
                    found = []
                    walk_refs(p, found)
                    name = p.get("nPropertyID", kind)
                    out.extend((owner, "%s %s" % (kind, name), r) for r in found)
            for x in b["exposedEntities"]:
                out.extend((owner, "exposed entity %s" % x["sName"], r) for r in x["aTargets"])
        for x in self.temp["propertyOverrides"]:
            found = []
            walk_refs(x, found)
            out.extend(("propertyOverrides", "", r) for r in found)
        for r in self.tblu["overrideDeletes"]:
            out.append(("overrideDeletes", "", r))
        for k in ("pinConnectionOverrides", "pinConnectionOverrideDeletes"):
            for p in self.tblu[k]:
                where = "%s -> %s" % (p["fromPinName"], p["toPinName"])
                out.append((k, where, p["fromEntity"]))
                out.append((k, where, p["toEntity"]))
        return out

    def pins(self):
        return [p for k in PIN_LISTS for p in self.tblu[k]]

    def children(self, index):
        out = []
        for i, e in enumerate(self.temp["subEntities"]):
            lp = e["logicalParent"]
            if lp["externalSceneIndex"] < 0 and lp["entityIndex"] == index:
                out.append(i)
        return out

    def subtree(self, index):
        out, stack = set(), [index]
        while stack:
            i = stack.pop()
            if i in out:
                continue
            out.add(i)
            stack.extend(self.children(i))
        return out

    def delete(self, entity_ids, subtree=True):
        """Deletes the entities (and, with subtree, every entity logically under them). Pin connections between two
        deleted entities go with them; one from or to a kept entity, or any other reference to a deleted entity, is
        an error naming it. Returns the deleted entity ids."""
        gone = set()
        for eid in entity_ids:
            i = self.index_of(eid)
            gone |= self.subtree(i) if subtree else {i}
        if self.temp["rootEntityIndex"] in gone:
            raise BrickError("%s: cannot delete the root" % self.name)
        problems = []
        for owner, where, r in self.all_refs():
            if r["externalSceneIndex"] < 0 and r["entityIndex"] in gone and not owner_in(owner, self, gone):
                problems.append("%s %s -> %s" % (owner, where, self.describe(r["entityIndex"])))
        for p in self.pins():
            a, b = p["fromID"] in gone, p["toID"] in gone
            if a != b:
                problems.append("pin %s.%s -> %s.%s" % (self.describe(p["fromID"]), p["fromPinName"],
                                                        self.describe(p["toID"]), p["toPinName"]))
        for i, b in enumerate(self.tblu["subEntities"]):
            if i in gone:
                continue
            for a in b["propertyAliases"]:
                if a["entityID"] in gone:
                    problems.append("%s alias %s -> %s" % (self.describe(i), a["sAliasName"],
                                                           self.describe(a["entityID"])))
            for name, iface in b["exposedInterfaces"]:
                if iface in gone:
                    problems.append("%s interface %s -> %s" % (self.describe(i), name, self.describe(iface)))
        if problems:
            raise BrickError("%s: deleting %d entities leaves references to them:\n  %s" %
                             (self.name, len(gone), "\n  ".join(problems)))
        deleted_ids = [self.tblu["subEntities"][i]["entityId"] for i in sorted(gone)]
        keep = [i for i in range(len(self.temp["subEntities"])) if i not in gone]
        new_index = {old: new for new, old in enumerate(keep)}
        for k in PIN_LISTS:
            self.tblu[k] = [p for p in self.tblu[k] if p["fromID"] not in gone]
        self.temp["subEntities"] = [self.temp["subEntities"][i] for i in keep]
        self.tblu["subEntities"] = [self.tblu["subEntities"][i] for i in keep]
        for _, _, r in self.all_refs():
            if r["externalSceneIndex"] < 0 and r["entityIndex"] >= 0:
                r["entityIndex"] = new_index[r["entityIndex"]]
        for p in self.pins():
            p["fromID"] = new_index[p["fromID"]]
            p["toID"] = new_index[p["toID"]]
        for b in self.tblu["subEntities"]:
            for a in b["propertyAliases"]:
                a["entityID"] = new_index[a["entityID"]]
            b["exposedInterfaces"] = [[n, new_index[x]] for n, x in b["exposedInterfaces"]]
            for _, subset in b["entitySubsets"]:
                subset["entities"] = [new_index[x] for x in subset["entities"] if x not in gone]
        root = new_index[self.temp["rootEntityIndex"]]
        self.temp["rootEntityIndex"] = root
        self.tblu["rootEntityIndex"] = root
        return deleted_ids

    # --- properties -------------------------------------------------------------------------------------------

    def find_property(self, entity_id, prop):
        e = self.temp["subEntities"][self.index_of(entity_id)]
        found = [p for kind in ("propertyValues", "postInitPropertyValues") for p in e[kind]
                 if p["nPropertyID"] == prop]
        if len(found) != 1:
            raise BrickError("%s: %s has %s property %s" % (self.name, self.describe(self.index_of(entity_id)),
                                                            "no" if not found else "%d copies of the" % len(found),
                                                            prop))
        return found[0]

    def get(self, entity_id, prop):
        return self.find_property(entity_id, prop)["value"]["$val"]

    def set(self, entity_id, prop, value, expect=None):
        """Sets an existing property's value (its type stays); with expect, the old value must equal it."""
        p = self.find_property(entity_id, prop)
        if expect is not None and p["value"]["$val"] != expect:
            raise BrickError("%s: %s.%s is %r, expected %r" % (self.name, self.describe(self.index_of(entity_id)),
                                                               prop, p["value"]["$val"], expect))
        p["value"]["$val"] = value

    def remove_property(self, entity_id, prop):
        i = self.index_of(entity_id)
        e = self.temp["subEntities"][i]
        n = 0
        for kind in ("propertyValues", "postInitPropertyValues"):
            before = len(e[kind])
            e[kind] = [p for p in e[kind] if p["nPropertyID"] != prop]
            n += before - len(e[kind])
        if n != 1:
            raise BrickError("%s: %s has %d %s properties to remove" % (self.name, self.describe(i), n, prop))

    def set_translation(self, entity_id, xyz, yaw=None, parents=()):
        """Moves an entity: replaces the translation of its m_mTransform (and, with yaw in degrees, its rotation by
        a turn about the up axis). Its transform must be a world one: no m_eidParent, or the brick root, or one of
        parents (local entity ids that move with it, a rigid group). Returns the old translation."""
        i = self.index_of(entity_id)
        e = self.temp["subEntities"][i]
        found = [p for kind in ("propertyValues", "postInitPropertyValues") for p in e[kind]
                 if p["nPropertyID"] == "m_eidParent"]
        if found:
            par = found[0]["value"]["$val"]
            allowed = {self.temp["rootEntityIndex"]} | {self.index_of(x) for x in parents}
            if par["externalSceneIndex"] >= 0 or (par["entityIndex"] >= 0 and par["entityIndex"] not in allowed):
                raise BrickError("%s: %s hangs off %s, not the brick root: its transform is not a world one" %
                                 (self.name, self.describe(i), "another brick's entity" if
                                  par["externalSceneIndex"] >= 0 else self.describe(par["entityIndex"])))
        m = self.find_property(entity_id, "m_mTransform")["value"]["$val"]
        old = (m["Trans"]["x"], m["Trans"]["y"], m["Trans"]["z"])
        m["Trans"] = {"x": f32(xyz[0]), "y": f32(xyz[1]), "z": f32(xyz[2])}
        if yaw is not None:
            c, s = f32(math.cos(math.radians(yaw))), f32(math.sin(math.radians(yaw)))
            m["XAxis"] = {"x": c, "y": s, "z": 0.0}
            m["YAxis"] = {"x": f32(-s), "y": c, "z": 0.0}
            m["ZAxis"] = {"x": 0.0, "y": 0.0, "z": 1.0}
        return old

    def retype(self, entity_id, like_id):
        """Gives an entity the type of another one of the brick (entityTypeResourceIndex in the TEMP and the TBLU),
        keeping its id, properties and references, and the like entity's entity subset memberships: the game's
        ZTemplateEntityBlueprintFactory reads, for every member of a subset, the same-named subset of the member's
        type and crashes when that type has none (HITMAN3.exe+0xF2DE19: the full Icon, whose acts retyped from
        act_mr_sit_phone_text_lean_forward to act_mr_stand_think_100cm stayed in override_bingo's Replicable
        subset; act_mr_stand_think_100cm's blueprint has no subsets). Returns the old (TEMP, TBLU) type indices."""
        i, j = self.index_of(entity_id), self.index_of(like_id)
        if i == j:
            raise BrickError("%s: %s cannot take its own type" % (self.name, self.describe(i)))
        t, u = self.temp["subEntities"], self.tblu["subEntities"]
        old = (t[i]["entityTypeResourceIndex"], u[i]["entityTypeResourceIndex"])
        t[i]["entityTypeResourceIndex"] = t[j]["entityTypeResourceIndex"]
        u[i]["entityTypeResourceIndex"] = u[j]["entityTypeResourceIndex"]
        for owner in u:
            for _, subset in owner["entitySubsets"]:
                members = subset["entities"]
                if j in members and i not in members:
                    members.append(i)
                elif i in members and j not in members:
                    members.remove(i)
        return old

    def subset_members(self):
        """(owner index, subset name, member index, the member's type as the TBLU's reference: its blueprint) for
        every member of every entity subset of the brick."""
        u = self.tblu["subEntities"]
        return [(o, name, m, self.tblu_refs[u[m]["entityTypeResourceIndex"]][1])
                for o, e in enumerate(u) for name, subset in e["entitySubsets"] for m in subset["entities"]]

    def local(self, entity_id, exposed=""):
        return local_ref(self.index_of(entity_id), exposed)

    def add_override_delete(self, scene_hash, entity_id):
        r = ext_ref(self.scene_index(scene_hash), entity_id)
        if any(x["externalSceneIndex"] == r["externalSceneIndex"] and x["entityID"] == entity_id
               for x in self.tblu["overrideDeletes"]):
            raise BrickError("%s: %016x is already deleted" % (self.name, entity_id))
        self.tblu["overrideDeletes"].append(r)

    def remove_property_overrides(self, scene_hash, entity_id):
        """Removes every property override on an entity of another brick (before deleting it); returns how many."""
        k = self.scene_index(scene_hash)
        keep = [p for p in self.temp["propertyOverrides"] if not (p["propertyOwner"]["externalSceneIndex"] == k and
                                                                  p["propertyOwner"]["entityID"] == entity_id)]
        n = len(self.temp["propertyOverrides"]) - len(keep)
        self.temp["propertyOverrides"] = keep
        return n

    def remove_property_override(self, scene_hash, entity_id, prop):
        """Removes the one property override of prop on an entity of another brick; returns its value."""
        k = self.scene_index(scene_hash)
        hits = [p for p in self.temp["propertyOverrides"] if p["propertyOwner"]["externalSceneIndex"] == k and
                p["propertyOwner"]["entityID"] == entity_id and p["propertyValue"]["nPropertyID"] == prop]
        if len(hits) != 1:
            raise BrickError("%s: %d property overrides of %s on %016X:%016x" % (self.name, len(hits), prop,
                                                                                  scene_hash, entity_id))
        self.temp["propertyOverrides"].remove(hits[0])
        return hits[0]["propertyValue"]["value"]["$val"]

    def add_property_override(self, owner, prop, type_name, value):
        """Adds a property override: prop of owner (as for ref(); in practice another brick's entity, (scene hash,
        entity id)) set to value, a property's JSON $val of type type_name. For an SEntityTemplateReference, value is
        the target as for ref(). There must be no override of prop on owner yet. Returns the override."""
        o = self.ref(owner)
        if any(self._same(p["propertyOwner"], o) and p["propertyValue"]["nPropertyID"] == prop
               for p in self.temp["propertyOverrides"]):
            raise BrickError("%s: %r.%s is already overridden" % (self.name, owner, prop))
        if type_name == "SEntityTemplateReference":
            value = self.ref(value)
        p = {"propertyOwner": o, "propertyValue": {"nPropertyID": prop, "value": {"$type": type_name, "$val": value}}}
        self.temp["propertyOverrides"].append(p)
        return p

    def add_external_scene(self, scene_hash):
        """Appends an external scene (so references can be re-pointed into it before others are dropped)."""
        if scene_hash in self.scenes:
            raise BrickError("%s: %016X is already an external scene" % (self.name, scene_hash))
        self.set_external_scenes(self.scenes + [scene_hash])

    def repoint(self, entity_id, prop, old, new):
        """Re-points the references to another brick's entity old = (scene hash, entity id) inside one property of a
        local entity to new = (scene hash, entity id), both external scenes of this brick. Returns how many (at least
        one)."""
        p = self.find_property(entity_id, prop)
        k_old, k_new = self.scene_index(old[0]), self.scene_index(new[0])
        found = []
        walk_refs(p["value"]["$val"], found)
        hits = [r for r in found if r["externalSceneIndex"] == k_old and r["entityID"] == old[1]]
        if not hits:
            raise BrickError("%s: %s.%s does not name %016X:%016x" % (self.name, self.describe(self.index_of(entity_id)),
                                                                       prop, old[0], old[1]))
        for r in hits:
            r["externalSceneIndex"] = k_new
            r["entityID"] = new[1]
        return len(hits)

    def set_logical_parent_root(self, entity_id):
        """Moves an entity under the brick root in the entity tree (its logicalParent, TEMP and TBLU); its world
        place (m_eidParent, m_mTransform) is untouched. Returns the old parent reference."""
        i = self.index_of(entity_id)
        old = copy.deepcopy(self.temp["subEntities"][i]["logicalParent"])
        for js in (self.temp, self.tblu):
            js["subEntities"][i]["logicalParent"] = local_ref(self.temp["rootEntityIndex"])
        return old

    def drop_temp_refs(self, hashes):
        """Removes resources from the TEMP's reference table (each listed once, none an external scene); none may
        still be used by an entity type, the blueprint or a ZRuntimeResourceID value (the indices after them shift
        down)."""
        tail = len(self.temp_refs) - len(self.temp["externalSceneTypeIndicesInResourceHeader"])
        gone = []
        for h in hashes:
            at = [k for k, (_, x) in enumerate(self.temp_refs[:tail]) if x == h]
            if len(at) != 1:
                raise BrickError("%s: %016X is %d times in the TEMP's reference table (before its scenes)" %
                                 (self.name, h, len(at)))
            gone.append(at[0])
        ids = []
        _walk_resource_ids(self.temp["subEntities"], ids)
        _walk_resource_ids(self.temp["propertyOverrides"], ids)
        used = {e["entityTypeResourceIndex"] for e in self.temp["subEntities"]}
        used.add(self.temp["blueprintIndexInResourceHeader"])
        used |= {r["m_IDLow"] for r in ids if r["m_IDHigh"] == 0}
        for k in gone:
            if k in used:
                raise BrickError("%s: %016X (reference %d) is still used" % (self.name, self.temp_refs[k][1], k))

        def remap(k):
            return k - sum(1 for g in gone if g < k)
        for e in self.temp["subEntities"]:
            e["entityTypeResourceIndex"] = remap(e["entityTypeResourceIndex"])
        self.temp["blueprintIndexInResourceHeader"] = remap(self.temp["blueprintIndexInResourceHeader"])
        for r in ids:
            if r["m_IDHigh"] == 0:
                r["m_IDLow"] = remap(r["m_IDLow"])
        self.temp["externalSceneTypeIndicesInResourceHeader"] = [
            remap(k) for k in self.temp["externalSceneTypeIndicesInResourceHeader"]]
        self.temp_refs = [r for k, r in enumerate(self.temp_refs) if k not in gone]
        self._check_tail()

    def ref(self, target, exposed=""):
        """target: a local entity id, or (scene hash, entity id)."""
        if isinstance(target, tuple):
            return ext_ref(self.scene_index(target[0]), target[1], exposed)
        return self.local(target, exposed)

    def _same(self, a, b):
        return a["externalSceneIndex"] == b["externalSceneIndex"] and \
            (a["entityIndex"] == b["entityIndex"] if a["externalSceneIndex"] < 0 else a["entityID"] == b["entityID"])

    def remove_pin_override(self, src, src_pin, dst, dst_pin):
        """Removes one pin connection override (src/dst as for ref()); it must exist exactly once."""
        a, b = self.ref(src), self.ref(dst)
        hits = [p for p in self.tblu["pinConnectionOverrides"] if self._same(p["fromEntity"], a) and
                self._same(p["toEntity"], b) and p["fromPinName"] == src_pin and p["toPinName"] == dst_pin]
        if len(hits) != 1:
            raise BrickError("%s: %d pin overrides %r.%s -> %r.%s" % (self.name, len(hits), src, src_pin, dst, dst_pin))
        self.tblu["pinConnectionOverrides"].remove(hits[0])

    def remove_pin_overrides_of(self, entity_id):
        """Removes every pin connection override from or to a local entity (before deleting it); returns how many."""
        i = self.index_of(entity_id)

        def mine(r):
            return r["externalSceneIndex"] < 0 and r["entityIndex"] == i
        keep = [p for p in self.tblu["pinConnectionOverrides"] if not (mine(p["fromEntity"]) or mine(p["toEntity"]))]
        n = len(self.tblu["pinConnectionOverrides"]) - len(keep)
        self.tblu["pinConnectionOverrides"] = keep
        return n

    def add_pin_override(self, src, src_pin, dst, dst_pin):
        self.tblu["pinConnectionOverrides"].append({
            "fromEntity": self.ref(src), "toEntity": self.ref(dst), "fromPinName": src_pin, "toPinName": dst_pin,
            "constantPinValue": {"$type": "void", "$val": None}})

    # --- checks -----------------------------------------------------------------------------------------------

    def validate(self):
        n = len(self.temp["subEntities"])
        problems = []
        for owner, where, r in self.all_refs():
            k = r["externalSceneIndex"]
            if k < 0:
                if r["entityID"] != LOCAL or not (r["entityIndex"] == -1 or 0 <= r["entityIndex"] < n):
                    problems.append("%s %s: bad local reference %r" % (owner, where, r))
            elif k >= len(self.scenes) or r["entityIndex"] != -2:
                problems.append("%s %s: bad external reference %r" % (owner, where, r))
        for p in self.pins():
            if not (0 <= p["fromID"] < n and 0 <= p["toID"] < n):
                problems.append("pin connection %r out of range" % p)
        ntypes_t, ntypes_b = len(self.temp_refs), len(self.tblu_refs)
        for i, (e, b) in enumerate(zip(self.temp["subEntities"], self.tblu["subEntities"])):
            if not 0 <= e["entityTypeResourceIndex"] < ntypes_t or not 0 <= b["entityTypeResourceIndex"] < ntypes_b:
                problems.append("%s: type index out of range" % self.describe(i))
        ids = self.ids()
        if len(set(ids)) != len(ids):
            problems.append("duplicate entity ids")
        if problems:
            raise BrickError("%s: invalid:\n  %s" % (self.name, "\n  ".join(problems)))


def owner_in(owner, brick, gone):
    """Whether a reference's owner (as all_refs describes it) is itself being deleted."""
    if owner in ("propertyOverrides", "overrideDeletes", "pinConnectionOverrides", "pinConnectionOverrideDeletes"):
        return False
    eid = int(owner.split(" ", 1)[0], 16)
    return brick.index_of(eid) in gone


# --- entity subsets and the types of their members ----------------------------------------------------------------
#
# A TBLU sub-entity's entitySubsets ({name: member indices}; IOI's bricks have them on their root: AudioEmitters,
# Replicable, Rooms, Gates, ...) are built into the brick's entity type by ZTemplateEntityBlueprintFactory's
# constructor (HITMAN3.exe+0xF2CBA0 in the 3.x Steam build): for every member it looks up the subset of the same name
# in the member's own type (+0xF3CC70, ZEntityType::m_pSubsets by name) and reads its flags at +0x10 with no null
# check (+0xF2DE19). So every member's type, the blueprint its TBLU reference names, must itself have a subset of that
# name (a template's root lists it, a C++ class declares it); a member of any other type crashes the scene load
# (an access violation reading 0x10, the full Icon). Which types have which subsets is not in the
# bricks we edit; what is known is what vanilla bricks do: a (subset name, member type) pair a vanilla brick uses is
# safe, and a template blueprint whose root has no such subset is not.


def subset_type_evidence(bricks):
    """{(subset name, member type blueprint hash)} that the given (vanilla, read-only) bricks use: each pair is a
    member type the game builds into that subset without crashing."""
    return {(name, t) for b in bricks if b is not None for _, name, _, t in b.subset_members()}


def subset_type_problems(brick, known, root_subsets=None):
    """Members of brick's entity subsets whose type is not known to carry the subset: (owner, name, member, type)
    pairs not in known (subset_type_evidence of vanilla bricks), unless root_subsets(type hash) (the subset names of
    that blueprint's root, or None when it cannot be read) lists the name. Returns [(text, definite)]: definite when
    the member's blueprint was read and its root has no such subset (the game crashes), else no evidence either way."""
    out = []
    for o, name, m, t in brick.subset_members():
        if (name, t) in known:
            continue
        have = root_subsets(t) if root_subsets is not None else None
        if have is not None and name in have:
            continue
        out.append(("%s: %s is in %s's subset %s, but its type %016X %s" % (
            brick.name, brick.describe(m), brick.describe(o), name, t,
            "has no %s subset (its blueprint's root has %s)" % (name, ", ".join(have) or "none")
            if have is not None else "is in no vanilla brick's %s subset" % name), have is not None))
    return out
