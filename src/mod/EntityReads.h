#pragma once

// Guarded engine reads and calls shared by many features: entity-reference
// properties, positions, spawning an entity, property writes and listings. Each returns 0 or the structured-exception code of a fault.

#include <Glacier/ZEntity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class IEntityFactory;
class ZObjectRef;

// An entity-reference property (ZEntityRef or TEntityRef<...>) read raw: its
// entity, or why not (missing, other type, fault).
struct RefRead {
    ZEntityType** entity = nullptr;
    std::string why; // empty when read
};

RefRead ReadEntityRefProperty(ZEntityType** p_Entity, std::string_view p_Name);

// The entities of a TArray of entity references, read raw, or why not.
struct RefArrayRead {
    std::vector<ZEntityType**> entities;
    std::string why; // empty when read
};

// At most p_MaxCount elements (more is implausible and reported as such).
RefArrayRead ReadEntityRefArray(ZEntityType** p_Entity, std::string_view p_Name, size_t p_MaxCount = 4'096);

// ZEntityManager::NewEntity under p_Parent, as the SDK's QuickSave mod creates its level-control entity.
uint32_t SpawnEntity(IEntityFactory* p_Factory, const ZEntityRef& p_Parent, ZEntityRef* p_Out);

// Writes a float32 property through the engine property setter (change handlers run).
uint32_t WriteFloatProperty(ZEntityType** p_Entity, std::string_view p_Name, float p_Value, bool* p_Ok);

// Writes an EAIEventType property (a ZAISoundEventDescriptor's m_eType) through the engine property setter.
uint32_t WriteAIEventTypeProperty(ZEntityType** p_Entity, std::string_view p_Name, int32_t p_Value, bool* p_Ok);

// A spatial entity's transform under its parent (m_eidParent): the
// m_mTransform property (SMatrix43: rows X, Y, Z, then the position) through
// the engine property setter (change handlers run); *p_Ok is SetProperty's
// result. Read the world transform back (ReadPosition) to see it took.
uint32_t WriteLocalTransform(ZEntityType** p_Entity, const std::array<float, 12>& p_Rows, bool* p_Ok);

// A spatial entity's world transform (rows X, Y, Z, then the position)
// through ZSpatialEntity::SetObjectToWorldMatrixFromEditor (the SDK editor's
// way to move an entity; the engine stores it under the parent); *p_Found:
// the entity has the spatial interface.
uint32_t WriteWorldTransform(ZEntityType** p_Entity, const std::array<float, 12>& p_Rows, bool* p_Found);

// One float32 property of an entity: read raw, or through its getter.
struct NamedFloat {
    std::string name; // "<no name>" when unreadable
    uint32_t id;
    std::optional<float> value; // empty when the getter read failed
    bool throughGetter;
};

// An entity's float32 properties, from its type's property table (at most
// p_Max entries looked at). Properties with a getter are read through it
// (ZEntityRef::GetProperty<float>, guarded). On a fault while listing, the
// properties read before it are kept.
struct FloatProperties {
    size_t total = 0;      // properties in the table (looked at: at most p_Max)
    size_t withGetter = 0; // float32 properties read through a getter
    std::vector<NamedFloat> floats;
    uint32_t faultCode = 0;
};

FloatProperties ReadFloatProperties(ZEntityType** p_Entity, size_t p_Max = 256);

// One ZString property of an entity, read raw, or through its getter.
struct NamedString {
    std::string name; // "<no name>" when unreadable
    uint32_t id;
    std::string value; // at most 127 characters; empty when not read
    bool read;         // false when the value could not be read
    bool throughGetter;
};

// An entity's ZString properties, from its type's property table (at most
// p_Max entries looked at), read guarded: raw storage, or the getter
// (ZEntityRef::GetProperty<ZString>) when the property has one. On a fault
// while listing, the properties read before it are kept.
struct StringProperties {
    std::vector<NamedString> strings;
    uint32_t faultCode = 0;
};

StringProperties ReadStringProperties(ZEntityType** p_Entity, size_t p_Max = 256);

// An entity's cached world position, read raw and guarded (no engine call):
// "at (x, y, z)" (with " (dirty)" when the cache is stale), "not spatial", or
// why not.
std::string PositionText(ZEntityType** p_Entity);

// The same position as numbers: empty for no entity, not spatial, a stale
// (dirty) cache or a fault.
struct WorldPosition {
    float x;
    float y;
    float z;
};

std::optional<WorldPosition> ReadWorldPosition(ZEntityType** p_Entity);

// An entity's world position, read guarded. A stale (dirty) cached world
// transform is never returned: with p_Refresh it is first refreshed as
// ZSpatialEntity::GetObjectToWorldMatrix does (the engine's
// ZSpatialEntity_UpdateCachedWorldMat composes the parent chain into the cache
// and clears the flag, nothing else; game thread only); without, it is left
// stale and reported so. Static set dressing can keep a dirty, all-zero cache
// well into play.
struct PositionRead {
    std::optional<WorldPosition> at; // empty: no entity, not spatial, stale or a fault
    // With at: the world rotation, the X, Y and Z axes (x, y, z each; Z is world up).
    std::optional<std::array<float, 9>> axes;
    bool spatial = false;   // the entity has the spatial interface
    bool stale = false;     // the cache is stale (not refreshed, or still dirty after)
    bool refreshed = false; // the engine refreshed a stale cache for this read
    uint32_t faultCode = 0;
};

PositionRead ReadPosition(ZEntityType** p_Entity, bool p_Refresh);

// A model's bounds, read guarded: a ZGeomEntity's primitive resource
// (m_ResourceID, its ZRenderPrimitiveResource m_vMin and m_vMax: the model's
// own bounds in its axes) times its m_PrimitiveScale (left out when not a
// plain positive scale). Only a resource of type PRIM is read.
struct GeomBoundsRead {
    std::optional<std::array<float, 6>> bounds; // min x, y, z, then max x, y, z (m); empty: unread
    bool geom = false;                          // the entity has the ZGeomEntity interface
    bool loaded = false;                        // its primitive resource has data
    uint32_t resourceType = 0;                  // that resource's type (with data)
    uint32_t faultCode = 0;
};

GeomBoundsRead ReadGeomBounds(ZEntityType** p_Entity);

// 47's entity (null before he exists, or on a fault), read guarded.
ZEntityType** ReadHeroEntity();

// One property of an entity's type table: name, engine type, size, whether it
// has a getter, and its first (up to 16) bytes of storage when it has none.
struct TableProperty {
    std::string name;     // "<no name>" when unreadable
    bool hasName = false; // the name was read
    uint32_t id = 0;
    std::string typeName;
    uint16_t typeSize = 0;
    bool hasGetter = false;
    uint8_t bytes[16] {};
    uint8_t byteCount = 0; // bytes read (0 with a getter)
    int64_t offset = 0;    // the storage's offset from the entity pointer (ZEntityRef::m_pObj)
};

// Every property of an entity's type table (at most p_Max), read guarded. On a
// fault while listing, the properties read before it are kept.
struct EntityPropertyTable {
    size_t total = 0;
    std::vector<TableProperty> properties;
    uint32_t faultCode = 0;
};

EntityPropertyTable ListEntityProperties(ZEntityType** p_Entity, size_t p_Max = 256);
