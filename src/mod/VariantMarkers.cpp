// A map variant's own map markers switched off (see VariantMarkers.h). Every
// raw engine step follows the guard pattern in Guard.h.

#include "VariantMarkers.h"

#include "EngineText.h"
#include "EntityWalk.h" // LocateProperty, ReadEngineBytes, ReadPropertyRaw32
#include "Guard.h"
#include "ModLog.h"

#include <Glacier/Hash.h>
#include <Glacier/Reflection.h>
#include <Glacier/ZEntity.h>
#include <Glacier/ZObject.h>

#include <cstring>
#include <iterator>
#include <string_view>

namespace variant_markers {

namespace {

constexpr size_t kMaxInterfaces = 32;
constexpr size_t kNameSize = 96;
constexpr size_t kShowCount = std::size(fr::kMarkerShowProperties);

struct InterfacesOut {
    size_t count;
    bool tracker;
    char names[kMaxInterfaces][kNameSize];
};

void CopyTypeName(const char* p_Text, char* p_Out) {
    size_t i = 0;

    for (; p_Text && p_Text[i] && i + 1 < kNameSize; ++i)
        p_Out[i] = p_Text[i];

    p_Out[i] = '\0';
}

const char* TypeNameOf(const SInterfaceData& p_Interface) {
    const IType* s_Info = p_Interface.m_Type ? p_Interface.m_Type->GetTypeInfo() : nullptr;
    return s_Info ? s_Info->pszTypeName : nullptr;
}

// Its type's interface names (an aspect entity lists every aspect's), copied
// only when one is a map tracker class (most entities are not).
__declspec(noinline) void InterfacesOf(ZEntityType** p_Entity, InterfacesOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;

    if (!s_Type || !s_Type->m_pInterfaceData)
        return;

    for (const SInterfaceData& s_Interface : *s_Type->m_pInterfaceData) {
        const char* s_Name = TypeNameOf(s_Interface);

        if (s_Name && std::strstr(s_Name, "TrackerAspect"))
            p_Out->tracker = true;
    }

    if (!p_Out->tracker)
        return;

    for (const SInterfaceData& s_Interface : *s_Type->m_pInterfaceData) {
        if (p_Out->count >= kMaxInterfaces)
            break;

        CopyTypeName(TypeNameOf(s_Interface), p_Out->names[p_Out->count++]);
    }
}

__declspec(noinline) DWORD SafeInterfaces(ZEntityType** p_Entity, InterfacesOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        InterfacesOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The m_bShowOn* properties through the engine (their change handlers run).
struct ShowOffArgs {
    ZEntityType** entity;
    bool ok[kShowCount];
};

__declspec(noinline) void ShowOffOf(ShowOffArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);

    for (size_t i = 0; i < kShowCount; ++i) {
        const std::string_view s_Name = fr::kMarkerShowProperties[i];
        p_Args->ok[i] = s_Entity.SetProperty(Hash::Crc32(s_Name.data(), s_Name.size()), ZObjectRef::From<bool>(false));
    }
}

__declspec(noinline) DWORD SafeShowOff(ShowOffArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        ShowOffOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

Seen Read(ZEntityType** p_Entity) {
    Seen s_Seen;

    if (!p_Entity)
        return s_Seen;

    InterfacesOut s_Out; // the names are written only up to count
    s_Out.count = 0;
    s_Out.tracker = false;

    if (const DWORD s_Code = SafeInterfaces(p_Entity, &s_Out)) {
        s_Seen.fault = static_cast<uint32_t>(s_Code);
        return s_Seen;
    }

    if (!s_Out.tracker)
        return s_Seen;

    s_Seen.tracker = true;

    for (size_t i = 0; i < s_Out.count; ++i)
        s_Seen.interfaces.emplace_back(s_Out.names[i]);

    const RawRead32 s_Layer = ReadPropertyRaw32(ZEntityRef(p_Entity), "m_eLayer");

    if (s_Layer.status == RawRead32::Status::Ok)
        s_Seen.layer = static_cast<int32_t>(s_Layer.bits);

    return s_Seen;
}

std::string SwitchOff(ZEntityType** p_Entity) {
    ShowOffArgs s_Args {p_Entity, {}};

    if (const DWORD s_Code = SafeShowOff(&s_Args))
        return fmt::format("SetProperty {}", FaultText(s_Code));

    std::string s_Why;

    for (size_t i = 0; i < kShowCount; ++i) {
        const std::string_view s_Name = fr::kMarkerShowProperties[i];
        const PropertyLocation s_At = LocateProperty(p_Entity, s_Name);
        std::string s_Problem;

        if (!s_Args.ok[i])
            s_Problem = "SetProperty returned false";
        else if (s_At.status == PropertyLocation::Status::Ok && s_At.typeSize == 1) {
            uint8_t s_Value = 1;

            if (const uint32_t s_Code = ReadEngineBytes(s_At.address, &s_Value, 1))
                s_Problem = fmt::format("read back: {}", FaultText(s_Code));
            else if (s_Value != 0)
                s_Problem = "read back true";
        }
        else if (s_At.status != PropertyLocation::Status::HasGetter)
            s_Problem = "not read back";

        if (!s_Problem.empty())
            s_Why += fmt::format("{}{} {}", s_Why.empty() ? "" : ", ", s_Name, s_Problem);
    }

    return s_Why;
}

} // namespace variant_markers
