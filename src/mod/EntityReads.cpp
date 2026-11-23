#include "EntityReads.h"

#include "Collections.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "ModLog.h"

#include <Functions.h>
#include <Globals.h>
#include <IModSDK.h>

#include <Glacier/EntityFactory.h>
#include <Glacier/Enums.h>
#include <Glacier/Hash.h>
#include <Glacier/Reflection.h>
#include <Glacier/ZGeomEntity.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/SExternalReferences.h>
#include <Glacier/ZSpatialEntity.h>
#include <Glacier/ZTypeRegistry.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>

namespace {

// Raw-pointer and engine-call steps follow the guard pattern in Guard.h.

struct SpawnArgs {
    IEntityFactory* factory;
    const ZEntityRef* parent;
    ZEntityRef* out;
};

__declspec(noinline) void SpawnOf(SpawnArgs* p_Args) {
    SExternalReferences s_ExternalRefs;
    Functions::ZEntityManager_NewEntity->Call(Globals::EntityManager, *p_Args->out, "", p_Args->factory,
                                              *p_Args->parent, s_ExternalRefs, -1);
}

__declspec(noinline) DWORD SafeSpawn(SpawnArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SpawnOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct SetEventTypeArgs {
    ZEntityType** entity;
    uint32_t propertyId;
    int32_t value;
    bool ok;
};

__declspec(noinline) void SetEventTypeOf(SetEventTypeArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok = s_Entity.SetProperty(p_Args->propertyId,
                                      ZObjectRef::From<EAIEventType>(static_cast<EAIEventType>(p_Args->value)));
}

__declspec(noinline) DWORD SafeSetEventType(SetEventTypeArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetEventTypeOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct SetFloatArgs {
    ZEntityType** entity;
    uint32_t propertyId;
    float value;
    bool ok;
};

__declspec(noinline) void SetFloatOf(SetFloatArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok = s_Entity.SetProperty(p_Args->propertyId, ZObjectRef::From<float>(p_Args->value));
}

__declspec(noinline) DWORD SafeSetFloat(SetFloatArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetFloatOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A transform from rows X, Y, Z and the position (12 floats).
SMatrix43 MatrixOf(const std::array<float, 12>& p_Rows) {
    SMatrix43 s_M;
    s_M.XAxis = SVector3(p_Rows[0], p_Rows[1], p_Rows[2]);
    s_M.YAxis = SVector3(p_Rows[3], p_Rows[4], p_Rows[5]);
    s_M.ZAxis = SVector3(p_Rows[6], p_Rows[7], p_Rows[8]);
    s_M.Trans = SVector3(p_Rows[9], p_Rows[10], p_Rows[11]);
    return s_M;
}

struct SetTransformArgs {
    ZEntityType** entity;
    const std::array<float, 12>* rows;
    const STypeID* spatial; // world: the spatial interface's type
    bool ok;                // local: SetProperty's result; world: the entity has the spatial interface
};

__declspec(noinline) void SetLocalTransformOf(SetTransformArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok =
        s_Entity.SetProperty(Hash::Crc32("m_mTransform"), ZObjectRef::From<SMatrix43>(MatrixOf(*p_Args->rows)));
}

__declspec(noinline) DWORD SafeSetLocalTransform(SetTransformArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetLocalTransformOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SetWorldTransformOf(SetTransformArgs* p_Args) {
    auto* s_Spatial = static_cast<ZSpatialEntity*>(ZEntityRef(p_Args->entity).QueryInterface(p_Args->spatial));
    p_Args->ok = s_Spatial != nullptr;

    if (s_Spatial)
        s_Spatial->SetObjectToWorldMatrixFromEditor(SMatrix(MatrixOf(*p_Args->rows)));
}

__declspec(noinline) DWORD SafeSetWorldTransform(SetTransformArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetWorldTransformOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The float32 properties of one entity, filled in as they are read, so a fault
// keeps what came before it.
constexpr size_t kMaxListedProperties = 256;

struct FloatListOut {
    uint32_t total;      // properties looked at
    uint32_t withGetter; // float32 with a getter
    uint32_t count;      // entries filled
    struct Entry {
        uint32_t id;
        uint32_t bits;
        bool hasGetter; // bits not read: read through the getter afterwards
        char name[64];
    } entries[kMaxListedProperties];
};

// Copies at most p_Capacity - 1 printable ASCII characters (stopping at the
// first other one) and terminates p_Out.
void CopyName(const char* p_Text, char* p_Out, size_t p_Capacity) {
    size_t i = 0;

    for (; p_Text && p_Text[i] >= 0x20 && p_Text[i] < 0x7F && i + 1 < p_Capacity; ++i)
        p_Out[i] = p_Text[i];

    p_Out[i] = '\0';
}

__declspec(noinline) void FloatListOf(ZEntityType** p_Entity, uint32_t p_Max, FloatListOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    const TArray<SPropertyData>* s_Properties = s_Type ? s_Type->m_pPropertyData : nullptr;

    if (!s_Properties)
        return;

    const size_t s_Size = s_Properties->size();

    for (size_t i = 0; i < s_Size && i < p_Max && p_Out->count < kMaxListedProperties; ++i) {
        const SPropertyData& s_Property = (*s_Properties)[i];
        ++p_Out->total;
        const SPropertyInfo* s_Info = s_Property.m_pPropertyInfo;
        const IType* s_TypeInfo = s_Info && s_Info->m_Type ? s_Info->m_Type->GetTypeInfo() : nullptr;

        if (!s_TypeInfo || !s_TypeInfo->pszTypeName || std::strcmp(s_TypeInfo->pszTypeName, "float32") != 0)
            continue;

        FloatListOut::Entry& s_Entry = p_Out->entries[p_Out->count];
        s_Entry.id = s_Property.m_nPropertyID;
        s_Entry.hasGetter = (s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0;

        if (s_Entry.hasGetter) {
            ++p_Out->withGetter;
        }
        else {
            std::memcpy(
                &s_Entry.bits,
                reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(p_Entity) + s_Property.m_nPropertyOffset),
                sizeof(s_Entry.bits));
        }

        s_Entry.name[0] = '\0';
        ++p_Out->count;
        // The name last, so a bad name pointer faults after the value is kept;
        // only when the info sits inside an SNamedPropertyInfo of this id.
        const SNamedPropertyInfo* s_Named = s_Property.GetPropertyInfo();

        if (s_Named->m_nPropertyID == s_Property.m_nPropertyID)
            CopyName(s_Named->m_pszPropertyName, s_Entry.name, sizeof(s_Entry.name));
    }
}

__declspec(noinline) DWORD SafeFloatList(ZEntityType** p_Entity, uint32_t p_Max, FloatListOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        FloatListOf(p_Entity, p_Max, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct GetterFloatOut {
    bool ok;
    float value;
};

// GetProperty<float> calls the property's getter into an engine allocation.
__declspec(noinline) void GetterFloatOf(ZEntityType** p_Entity, uint32_t p_Id, GetterFloatOut* p_Out) {
    *p_Out = {false, 0.0f};
    const auto s_Variant = ZEntityRef(p_Entity).GetProperty<float>(p_Id);

    if (const float* s_Value = s_Variant.As<float>()) {
        p_Out->ok = true;
        p_Out->value = *s_Value;
    }
}

__declspec(noinline) DWORD SafeGetterFloat(ZEntityType** p_Entity, uint32_t p_Id, GetterFloatOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        GetterFloatOf(p_Entity, p_Id, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

constexpr size_t kMaxStringChars = 128;

struct StringListOut {
    uint32_t count; // entries filled
    struct Entry {
        uint32_t id;
        bool hasGetter;
        bool read;
        char name[64];
        char value[kMaxStringChars];
    } entries[kMaxListedProperties];
};

// ZString's layout (SDK Glacier/ZString.h): length with flag bits, then the characters.
struct RawZString {
    uint32_t length;
    uint32_t pad;
    const char* chars;
};

// Copies at most p_Capacity - 1 bytes of p_Text (p_Length long) and terminates p_Out.
void CopyChars(const char* p_Text, uint32_t p_Length, char* p_Out, size_t p_Capacity) {
    const size_t s_Count = std::min<size_t>(p_Length, p_Capacity - 1);

    if (p_Text && s_Count)
        std::memcpy(p_Out, p_Text, s_Count);

    p_Out[p_Text ? s_Count : 0] = '\0';
}

__declspec(noinline) void StringListOf(ZEntityType** p_Entity, uint32_t p_Max, StringListOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    const TArray<SPropertyData>* s_Properties = s_Type ? s_Type->m_pPropertyData : nullptr;

    if (!s_Properties)
        return;

    const size_t s_Size = s_Properties->size();

    for (size_t i = 0; i < s_Size && i < p_Max && p_Out->count < kMaxListedProperties; ++i) {
        const SPropertyData& s_Property = (*s_Properties)[i];
        const SPropertyInfo* s_Info = s_Property.m_pPropertyInfo;
        const IType* s_TypeInfo = s_Info && s_Info->m_Type ? s_Info->m_Type->GetTypeInfo() : nullptr;

        if (!s_TypeInfo || !s_TypeInfo->pszTypeName || std::strcmp(s_TypeInfo->pszTypeName, "ZString") != 0)
            continue;

        StringListOut::Entry& s_Entry = p_Out->entries[p_Out->count];
        s_Entry.id = s_Property.m_nPropertyID;
        s_Entry.hasGetter = (s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0;
        s_Entry.read = false;
        s_Entry.name[0] = '\0';
        s_Entry.value[0] = '\0';
        ++p_Out->count;

        if (!s_Entry.hasGetter) {
            RawZString s_Raw {};
            std::memcpy(
                &s_Raw,
                reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(p_Entity) + s_Property.m_nPropertyOffset),
                sizeof(s_Raw));
            CopyChars(s_Raw.chars, s_Raw.length & 0x3FFFFFFF, s_Entry.value, sizeof(s_Entry.value));
            s_Entry.read = true;
        }

        // The name last, as in FloatListOf.
        const SNamedPropertyInfo* s_Named = s_Property.GetPropertyInfo();

        if (s_Named->m_nPropertyID == s_Property.m_nPropertyID)
            CopyName(s_Named->m_pszPropertyName, s_Entry.name, sizeof(s_Entry.name));
    }
}

__declspec(noinline) DWORD SafeStringList(ZEntityType** p_Entity, uint32_t p_Max, StringListOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        StringListOf(p_Entity, p_Max, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct GetterStringOut {
    bool ok;
    char value[kMaxStringChars];
};

// GetProperty<ZString> calls the property's getter into an engine allocation.
__declspec(noinline) void GetterStringOf(ZEntityType** p_Entity, uint32_t p_Id, GetterStringOut* p_Out) {
    p_Out->ok = false;
    p_Out->value[0] = '\0';
    const auto s_Variant = ZEntityRef(p_Entity).GetProperty<ZString>(p_Id);

    if (const ZString* s_Value = s_Variant.As<ZString>()) {
        CopyChars(s_Value->c_str(), s_Value->size(), p_Out->value, sizeof(p_Out->value));
        p_Out->ok = true;
    }
}

__declspec(noinline) DWORD SafeGetterString(ZEntityType** p_Entity, uint32_t p_Id, GetterStringOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        GetterStringOf(p_Entity, p_Id, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

RefRead ReadEntityRefProperty(ZEntityType** p_Entity, std::string_view p_Name) {
    RefRead s_Out;
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    switch (s_Location.status) {
        case PropertyLocation::Status::Missing: s_Out.why = "no such property"; return s_Out;
        case PropertyLocation::Status::HasGetter: s_Out.why = "read through a getter"; return s_Out;
        case PropertyLocation::Status::Fault:
            s_Out.why = fmt::format("fault (exception 0x{:08X})", s_Location.faultCode);
            return s_Out;
        case PropertyLocation::Status::Ok: break;
    }

    if (!fr::EntityRefSize(s_Location.typeName)) {
        s_Out.why = fmt::format("type {} is not an entity reference",
                                s_Location.typeName[0] ? s_Location.typeName : "<unknown>");
        return s_Out;
    }

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Out.entity, sizeof(s_Out.entity))) {
        s_Out.entity = nullptr;
        s_Out.why = fmt::format("fault (exception 0x{:08X})", s_Code);
    }

    return s_Out;
}

RefArrayRead ReadEntityRefArray(ZEntityType** p_Entity, std::string_view p_Name, size_t p_MaxCount) {
    RefArrayRead s_Out;
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    if (s_Location.status != PropertyLocation::Status::Ok) {
        s_Out.why = s_Location.status == PropertyLocation::Status::Missing ? "no such property"
                    : s_Location.status == PropertyLocation::Status::HasGetter
                        ? "read through a getter"
                        : fmt::format("fault (exception 0x{:08X})", s_Location.faultCode);
        return s_Out;
    }

    // The element type's own size when it is an entity reference; else from the array's type name.
    std::optional<size_t> s_ElementSize = fr::EntityRefSize(s_Location.elementTypeName);

    if (s_ElementSize && s_Location.elementSize && *s_ElementSize != s_Location.elementSize)
        s_ElementSize = std::nullopt;

    if (!s_ElementSize)
        s_ElementSize = fr::EntityRefArrayElementSize(s_Location.typeName);

    if (!s_Location.isArray || !s_ElementSize) {
        s_Out.why = fmt::format("type {} (element {}) is not an array of entity references",
                                s_Location.typeName[0] ? s_Location.typeName : "<unknown>",
                                s_Location.elementTypeName[0] ? s_Location.elementTypeName : "<unknown>");
        return s_Out;
    }

    struct RawArray {
        uintptr_t begin;
        uintptr_t end;
        uint64_t allocationEndOrFlags;
    } s_Array {};

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Array, sizeof(s_Array))) {
        s_Out.why = fmt::format("fault reading the array (exception 0x{:08X})", s_Code);
        return s_Out;
    }

    // The SDK's TArray may hold up to 16 bytes of elements inline (bit 62 of
    // the third qword); they are then at the array's own address.
    const auto s_Span = fr::ArrayElements(s_Location.address, s_Array.begin, s_Array.end, s_Array.allocationEndOrFlags,
                                          *s_ElementSize, p_MaxCount);

    if (!s_Span) {
        s_Out.why = fmt::format("implausible array (begin 0x{:X}, end 0x{:X}, flags 0x{:X}, element size {})",
                                s_Array.begin, s_Array.end, s_Array.allocationEndOrFlags, *s_ElementSize);
        return s_Out;
    }

    for (size_t i = 0; i < s_Span->count; ++i) {
        ZEntityType** s_Element = nullptr;

        if (const uint32_t s_Code =
                ReadEngineBytes(s_Span->begin + i * *s_ElementSize, &s_Element, sizeof(s_Element))) {
            s_Out.why = fmt::format("fault reading element {} (exception 0x{:08X})", i, s_Code);
            return s_Out;
        }

        s_Out.entities.push_back(s_Element);
    }

    return s_Out;
}

uint32_t SpawnEntity(IEntityFactory* p_Factory, const ZEntityRef& p_Parent, ZEntityRef* p_Out) {
    SpawnArgs s_Args {p_Factory, &p_Parent, p_Out};
    return static_cast<uint32_t>(SafeSpawn(&s_Args));
}

uint32_t WriteFloatProperty(ZEntityType** p_Entity, std::string_view p_Name, float p_Value, bool* p_Ok) {
    *p_Ok = false;

    if (!p_Entity)
        return 0xC0000005; // as the write would fault

    SetFloatArgs s_Args {p_Entity, Hash::Crc32(p_Name.data(), p_Name.size()), p_Value, false};
    const DWORD s_Code = SafeSetFloat(&s_Args);
    *p_Ok = !s_Code && s_Args.ok;
    return static_cast<uint32_t>(s_Code);
}

uint32_t WriteAIEventTypeProperty(ZEntityType** p_Entity, std::string_view p_Name, int32_t p_Value, bool* p_Ok) {
    *p_Ok = false;

    if (!p_Entity)
        return 0xC0000005; // as the write would fault

    SetEventTypeArgs s_Args {p_Entity, Hash::Crc32(p_Name.data(), p_Name.size()), p_Value, false};
    const DWORD s_Code = SafeSetEventType(&s_Args);
    *p_Ok = !s_Code && s_Args.ok;
    return static_cast<uint32_t>(s_Code);
}

uint32_t WriteLocalTransform(ZEntityType** p_Entity, const std::array<float, 12>& p_Rows, bool* p_Ok) {
    *p_Ok = false;

    if (!p_Entity)
        return 0xC0000005; // as the write would fault

    SetTransformArgs s_Args {p_Entity, &p_Rows, nullptr, false};
    const DWORD s_Code = SafeSetLocalTransform(&s_Args);
    *p_Ok = !s_Code && s_Args.ok;
    return static_cast<uint32_t>(s_Code);
}

uint32_t WriteWorldTransform(ZEntityType** p_Entity, const std::array<float, 12>& p_Rows, bool* p_Found) {
    static const STypeID* s_Spatial = nullptr;
    *p_Found = false;

    if (!s_Spatial && *Globals::TypeRegistry)
        s_Spatial = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZSpatialEntity")));

    if (!p_Entity || !s_Spatial)
        return 0xC0000005; // as the write would fault

    SetTransformArgs s_Args {p_Entity, &p_Rows, s_Spatial, false};
    const DWORD s_Code = SafeSetWorldTransform(&s_Args);
    *p_Found = !s_Code && s_Args.ok;
    return static_cast<uint32_t>(s_Code);
}

FloatProperties ReadFloatProperties(ZEntityType** p_Entity, size_t p_Max) {
    FloatProperties s_Out;

    if (!p_Entity)
        return s_Out;

    auto s_List = std::make_unique<FloatListOut>();
    std::memset(s_List.get(), 0, sizeof(FloatListOut));
    s_Out.faultCode = static_cast<uint32_t>(
        SafeFloatList(p_Entity, static_cast<uint32_t>(std::min<size_t>(p_Max, 65'535)), s_List.get()));
    s_Out.total = s_List->total;
    s_Out.withGetter = s_List->withGetter;

    // GetProperty<float> default-constructs a ZVariant<float>, which needs float32 in the type registry.
    const bool s_CanGet = s_List->withGetter > 0 && IsTypeRegistered(ZHMTypeName<float>);

    for (uint32_t i = 0; i < s_List->count; ++i) {
        const FloatListOut::Entry& s_Entry = s_List->entries[i];
        std::optional<float> s_Value;

        if (!s_Entry.hasGetter) {
            float s_Raw = 0.0f;
            std::memcpy(&s_Raw, &s_Entry.bits, sizeof(s_Raw));
            s_Value = s_Raw;
        }
        else if (s_CanGet) {
            GetterFloatOut s_Get {};

            if (const DWORD s_Code = SafeGetterFloat(p_Entity, s_Entry.id, &s_Get)) {
                Log("float property {:08x}: getter faulted (exception 0x{:08X})", s_Entry.id,
                    static_cast<uint32_t>(s_Code));
            }
            else if (s_Get.ok) {
                s_Value = s_Get.value;
            }
        }

        s_Out.floats.push_back({s_Entry.name[0] ? std::string(s_Entry.name) : std::string("<no name>"), s_Entry.id,
                                s_Value, s_Entry.hasGetter});
    }

    return s_Out;
}

StringProperties ReadStringProperties(ZEntityType** p_Entity, size_t p_Max) {
    StringProperties s_Out;

    if (!p_Entity)
        return s_Out;

    auto s_List = std::make_unique<StringListOut>();
    std::memset(s_List.get(), 0, sizeof(StringListOut));
    s_Out.faultCode = static_cast<uint32_t>(
        SafeStringList(p_Entity, static_cast<uint32_t>(std::min<size_t>(p_Max, 65'535)), s_List.get()));

    bool s_CanGet = false;
    bool s_CheckedGet = false;

    for (uint32_t i = 0; i < s_List->count; ++i) {
        const StringListOut::Entry& s_Entry = s_List->entries[i];
        NamedString s_String {s_Entry.name[0] ? std::string(s_Entry.name) : std::string("<no name>"), s_Entry.id,
                              std::string(), s_Entry.read, s_Entry.hasGetter};

        if (s_Entry.read) {
            s_String.value = s_Entry.value;
        }
        else if (s_Entry.hasGetter) {
            // GetProperty<ZString> default-constructs a ZVariant<ZString>, which needs ZString in the type registry.
            if (!s_CheckedGet) {
                s_CanGet = IsTypeRegistered(ZHMTypeName<ZString>);
                s_CheckedGet = true;
            }

            GetterStringOut s_Get {};

            if (s_CanGet && SafeGetterString(p_Entity, s_Entry.id, &s_Get) == 0 && s_Get.ok) {
                s_String.value = s_Get.value;
                s_String.read = true;
            }
        }

        s_Out.strings.push_back(std::move(s_String));
    }

    return s_Out;
}

namespace {

struct PositionOut {
    bool found; // the entity has the spatial interface
    bool dirty; // the cached world transform is stale
    float x;
    float y;
    float z;
    float axes[9]; // the world X, Y and Z axes (m_mTransform's rotation rows)
};

__declspec(noinline) void AxesOf(const ZSpatialEntity* p_Spatial, PositionOut* p_Out) {
    const SMatrix43& s_M = p_Spatial->m_mTransform;
    const float s_Axes[9] {s_M.XAxis.x, s_M.XAxis.y, s_M.XAxis.z, s_M.YAxis.x, s_M.YAxis.y,
                           s_M.YAxis.z, s_M.ZAxis.x, s_M.ZAxis.y, s_M.ZAxis.z};

    for (int i = 0; i < 9; ++i)
        p_Out->axes[i] = s_Axes[i];
}

// The cached world transform, read raw (no engine call).
__declspec(noinline) void PositionOf(ZEntityType** p_Entity, const STypeID* p_Spatial, PositionOut* p_Out) {
    const auto* s_Spatial = static_cast<const ZSpatialEntity*>(ZEntityRef(p_Entity).QueryInterface(p_Spatial));
    p_Out->found = s_Spatial != nullptr;

    if (!s_Spatial)
        return;

    p_Out->dirty = s_Spatial->m_bWorldTransformDirty;
    p_Out->x = s_Spatial->m_mTransform.Trans.x;
    p_Out->y = s_Spatial->m_mTransform.Trans.y;
    p_Out->z = s_Spatial->m_mTransform.Trans.z;
    AxesOf(s_Spatial, p_Out);
}

__declspec(noinline) DWORD SafePosition(ZEntityType** p_Entity, const STypeID* p_Spatial, PositionOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        PositionOf(p_Entity, p_Spatial, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The world transform as ZSpatialEntity::GetObjectToWorldMatrix gives it: a
// stale (dirty) cache is first refreshed by the engine's own
// ZSpatialEntity_UpdateCachedWorldMat (it composes the parent chain into the
// cache and clears the flag; nothing else changes). *p_Refreshed when it ran.
__declspec(noinline) void RefreshedPositionOf(ZEntityType** p_Entity, const STypeID* p_Spatial, PositionOut* p_Out,
                                              bool* p_Refreshed) {
    const auto* s_Spatial = static_cast<const ZSpatialEntity*>(ZEntityRef(p_Entity).QueryInterface(p_Spatial));
    p_Out->found = s_Spatial != nullptr;

    if (!s_Spatial)
        return;

    if (s_Spatial->m_bWorldTransformDirty && Functions::ZSpatialEntity_UpdateCachedWorldMat &&
        Functions::ZSpatialEntity_UpdateCachedWorldMat->Exists()) {
        Functions::ZSpatialEntity_UpdateCachedWorldMat->Call(s_Spatial);
        *p_Refreshed = true;
    }

    p_Out->dirty = s_Spatial->m_bWorldTransformDirty;
    p_Out->x = s_Spatial->m_mTransform.Trans.x;
    p_Out->y = s_Spatial->m_mTransform.Trans.y;
    p_Out->z = s_Spatial->m_mTransform.Trans.z;
    AxesOf(s_Spatial, p_Out);
}

__declspec(noinline) DWORD SafeRefreshedPosition(ZEntityType** p_Entity, const STypeID* p_Spatial, PositionOut* p_Out,
                                                 bool* p_Refreshed) {
    DWORD s_Code = 0;

    __try {
        RefreshedPositionOf(p_Entity, p_Spatial, p_Out, p_Refreshed);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct PropertyListOut {
    uint32_t total;
    uint32_t count;
    struct Entry {
        uint32_t id;
        uint16_t typeSize;
        bool hasGetter;
        uint8_t byteCount;
        uint8_t bytes[16];
        int64_t offset;
        char typeName[96];
        char name[64];
    } entries[kMaxListedProperties];
};

__declspec(noinline) void PropertyListOf(ZEntityType** p_Entity, uint32_t p_Max, PropertyListOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    const TArray<SPropertyData>* s_Properties = s_Type ? s_Type->m_pPropertyData : nullptr;

    if (!s_Properties)
        return;

    const size_t s_Size = s_Properties->size();

    for (size_t i = 0; i < s_Size && i < p_Max && p_Out->count < kMaxListedProperties; ++i) {
        const SPropertyData& s_Property = (*s_Properties)[i];
        ++p_Out->total;
        PropertyListOut::Entry& s_Entry = p_Out->entries[p_Out->count++];
        s_Entry.id = s_Property.m_nPropertyID;
        s_Entry.offset = s_Property.m_nPropertyOffset;
        const SPropertyInfo* s_Info = s_Property.m_pPropertyInfo;

        if (!s_Info)
            continue;

        s_Entry.hasGetter = (s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0;
        const IType* s_TypeInfo = s_Info->m_Type ? s_Info->m_Type->GetTypeInfo() : nullptr;

        if (s_TypeInfo) {
            s_Entry.typeSize = s_TypeInfo->m_nTypeSize;
            CopyName(s_TypeInfo->pszTypeName, s_Entry.typeName, sizeof(s_Entry.typeName));
        }

        if (!s_Entry.hasGetter && s_Entry.typeSize > 0) {
            const size_t s_Bytes = std::min<size_t>(s_Entry.typeSize, sizeof(s_Entry.bytes));
            std::memcpy(
                s_Entry.bytes,
                reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(p_Entity) + s_Property.m_nPropertyOffset),
                s_Bytes);
            s_Entry.byteCount = static_cast<uint8_t>(s_Bytes);
        }

        // The name last (see FloatListOf).
        const SNamedPropertyInfo* s_Named = s_Property.GetPropertyInfo();

        if (s_Named->m_nPropertyID == s_Property.m_nPropertyID)
            CopyName(s_Named->m_pszPropertyName, s_Entry.name, sizeof(s_Entry.name));
    }
}

__declspec(noinline) DWORD SafePropertyList(ZEntityType** p_Entity, uint32_t p_Max, PropertyListOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        PropertyListOf(p_Entity, p_Max, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct HeroOut {
    ZEntityType** entity;
};

__declspec(noinline) void HeroOf(HeroOut* p_Out) {
    p_Out->entity = SDK()->GetLocalPlayer().m_entityRef.m_pObj;
}

__declspec(noinline) DWORD SafeHero(HeroOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        HeroOf(p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A model's bounds (ReadGeomBounds), read raw.
struct GeomBoundsOut {
    bool geom;
    bool loaded;
    uint32_t type;
    float min[3];
    float max[3];
    float scale[3];
};

__declspec(noinline) void GeomBoundsOf(ZEntityType** p_Entity, const STypeID* p_Geom, GeomBoundsOut* p_Out) {
    const auto* s_Geom = static_cast<const ZGeomEntity*>(ZEntityRef(p_Entity).QueryInterface(p_Geom));
    p_Out->geom = s_Geom != nullptr;

    if (!s_Geom || s_Geom->m_ResourceID.m_nResourceIndex.val < 0)
        return;

    const auto& s_Info = (*Globals::ResourceContainer)->m_resources[s_Geom->m_ResourceID.m_nResourceIndex.val];
    const auto* s_Prim = static_cast<const ZRenderPrimitiveResource*>(s_Info.resourceData);
    p_Out->loaded = s_Prim != nullptr;
    p_Out->type = s_Info.resourceType;

    if (!s_Prim)
        return;

    const SVector3& s_Min = s_Prim->m_vMin;
    const SVector3& s_Max = s_Prim->m_vMax;
    const SVector3& s_Scale = s_Geom->m_PrimitiveScale;
    const float s_Mins[3] {s_Min.x, s_Min.y, s_Min.z};
    const float s_Maxes[3] {s_Max.x, s_Max.y, s_Max.z};
    const float s_Scales[3] {s_Scale.x, s_Scale.y, s_Scale.z};

    for (int i = 0; i < 3; ++i) {
        p_Out->min[i] = s_Mins[i];
        p_Out->max[i] = s_Maxes[i];
        p_Out->scale[i] = s_Scales[i];
    }
}

__declspec(noinline) DWORD SafeGeomBounds(ZEntityType** p_Entity, const STypeID* p_Geom, GeomBoundsOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        GeomBoundsOf(p_Entity, p_Geom, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

GeomBoundsRead ReadGeomBounds(ZEntityType** p_Entity) {
    static const STypeID* s_GeomType = nullptr;
    GeomBoundsRead s_Read;

    if (!s_GeomType && *Globals::TypeRegistry)
        s_GeomType = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZGeomEntity")));

    if (!p_Entity || !s_GeomType || !Globals::ResourceContainer || !*Globals::ResourceContainer)
        return s_Read;

    GeomBoundsOut s_Out {};
    s_Read.faultCode = static_cast<uint32_t>(SafeGeomBounds(p_Entity, s_GeomType, &s_Out));
    s_Read.geom = s_Out.geom;
    s_Read.loaded = s_Out.loaded;
    s_Read.resourceType = s_Out.type;

    // The type is a FourCC: PRIM, read either way round.
    constexpr uint32_t kPrim = 0x5052494D;    // 'P' 'R' 'I' 'M', the first in the high byte
    constexpr uint32_t kPrimLow = 0x4D495250; // the first in the low byte

    if (s_Read.faultCode || !s_Out.loaded || (s_Out.type != kPrim && s_Out.type != kPrimLow))
        return s_Read;

    s_Read.bounds.emplace();

    for (size_t i = 0; i < 3; ++i) {
        const float s_Scale = s_Out.scale[i] > 0.01f && s_Out.scale[i] < 100.0f ? s_Out.scale[i] : 1.0f;
        (*s_Read.bounds)[i] = s_Out.min[i] * s_Scale;
        (*s_Read.bounds)[3 + i] = s_Out.max[i] * s_Scale;
    }

    return s_Read;
}

ZEntityType** ReadHeroEntity() {
    HeroOut s_Hero {};

    if (SafeHero(&s_Hero))
        return nullptr;

    return s_Hero.entity;
}

std::string PositionText(ZEntityType** p_Entity) {
    static const STypeID* s_Spatial = nullptr;

    if (!s_Spatial && *Globals::TypeRegistry)
        s_Spatial = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZSpatialEntity")));

    if (!p_Entity)
        return "no entity";

    if (!s_Spatial)
        return "position unreadable (ZSpatialEntity not in the type registry)";

    PositionOut s_Out {};

    if (const DWORD s_Code = SafePosition(p_Entity, s_Spatial, &s_Out))
        return fmt::format("position <fault 0x{:08X}>", s_Code);

    if (!s_Out.found)
        return "not spatial";

    return fmt::format("at ({:.3f}, {:.3f}, {:.3f}){}", s_Out.x, s_Out.y, s_Out.z, s_Out.dirty ? " (dirty)" : "");
}

std::optional<WorldPosition> ReadWorldPosition(ZEntityType** p_Entity) {
    static const STypeID* s_Spatial = nullptr;

    if (!s_Spatial && *Globals::TypeRegistry)
        s_Spatial = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZSpatialEntity")));

    if (!p_Entity || !s_Spatial)
        return std::nullopt;

    PositionOut s_Out {};

    if (SafePosition(p_Entity, s_Spatial, &s_Out) || !s_Out.found || s_Out.dirty)
        return std::nullopt;

    return WorldPosition {s_Out.x, s_Out.y, s_Out.z};
}

PositionRead ReadPosition(ZEntityType** p_Entity, bool p_Refresh) {
    static const STypeID* s_Spatial = nullptr;
    PositionRead s_Read;

    if (!s_Spatial && *Globals::TypeRegistry)
        s_Spatial = (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZSpatialEntity")));

    if (!p_Entity || !s_Spatial)
        return s_Read;

    PositionOut s_Out {};
    s_Read.faultCode =
        static_cast<uint32_t>(p_Refresh ? SafeRefreshedPosition(p_Entity, s_Spatial, &s_Out, &s_Read.refreshed)
                                        : SafePosition(p_Entity, s_Spatial, &s_Out));
    s_Read.spatial = s_Out.found;

    if (s_Read.faultCode || !s_Out.found)
        return s_Read;

    s_Read.stale = s_Out.dirty;

    if (!s_Out.dirty) {
        s_Read.at = WorldPosition {s_Out.x, s_Out.y, s_Out.z};
        s_Read.axes.emplace();

        for (size_t i = 0; i < 9; ++i)
            (*s_Read.axes)[i] = s_Out.axes[i];
    }

    return s_Read;
}

EntityPropertyTable ListEntityProperties(ZEntityType** p_Entity, size_t p_Max) {
    EntityPropertyTable s_Out;

    if (!p_Entity)
        return s_Out;

    auto s_List = std::make_unique<PropertyListOut>();
    std::memset(s_List.get(), 0, sizeof(PropertyListOut));
    s_Out.faultCode = static_cast<uint32_t>(
        SafePropertyList(p_Entity, static_cast<uint32_t>(std::min<size_t>(p_Max, 65'535)), s_List.get()));
    s_Out.total = s_List->total;

    for (uint32_t i = 0; i < s_List->count; ++i) {
        const PropertyListOut::Entry& s_Entry = s_List->entries[i];
        TableProperty s_Property;
        s_Property.hasName = s_Entry.name[0] != '\0';
        s_Property.name = s_Property.hasName ? std::string(s_Entry.name) : std::string("<no name>");
        s_Property.id = s_Entry.id;
        s_Property.typeName = s_Entry.typeName[0] ? std::string(s_Entry.typeName) : std::string("<unknown type>");
        s_Property.typeSize = s_Entry.typeSize;
        s_Property.hasGetter = s_Entry.hasGetter;
        s_Property.byteCount = s_Entry.byteCount;
        s_Property.offset = s_Entry.offset;
        std::memcpy(s_Property.bytes, s_Entry.bytes, sizeof(s_Property.bytes));
        s_Out.properties.push_back(std::move(s_Property));
    }

    return s_Out;
}
