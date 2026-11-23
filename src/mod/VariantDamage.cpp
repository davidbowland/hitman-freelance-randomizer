// A map variant's own timed damage switched off (see VariantDamage.h). Every
// raw engine step follows the guard pattern in Guard.h.

#include "VariantDamage.h"

#include "EngineText.h"
#include "EntityWalk.h" // ReadPropertyRaw32
#include "Guard.h"
#include "ModLog.h"
#include "PinIds.h"

#include <Glacier/Hash.h>
#include <Glacier/ZEntity.h>
#include <Glacier/ZObject.h>

#include <cstring>
#include <optional>
#include <string_view>

namespace variant_damage {

namespace {

static_assert(Hash::Crc32(fr::kDamageProperty.data(), fr::kDamageProperty.size()) == fr::kPropDamage);
static_assert(Hash::Crc32(fr::kIntervalProperty.data(), fr::kIntervalProperty.size()) == fr::kPropInterval);

// A float property's storage, read raw; empty when not readable.
std::optional<float> ReadFloat(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);

    if (s_Read.status != RawRead32::Status::Ok)
        return std::nullopt;

    float s_Value = 0.0f;
    std::memcpy(&s_Value, &s_Read.bits, sizeof(s_Value));
    return s_Value;
}

struct SetArgs {
    ZEntityType** entity;
    bool damageOk;
    bool intervalOk;
};

__declspec(noinline) void SetOf(SetArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->damageOk = s_Entity.SetProperty(fr::kPropDamage, ZObjectRef::From<float>(fr::kDamageOff));
    p_Args->intervalOk = s_Entity.SetProperty(fr::kPropInterval, ZObjectRef::From<float>(fr::kIntervalOff));
}

__declspec(noinline) DWORD SafeSet(SetArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

fr::DamageBefore Read(ZEntityType** p_Entity) {
    if (!p_Entity)
        return {};

    return {ReadFloat(p_Entity, fr::kDamageProperty), ReadFloat(p_Entity, fr::kIntervalProperty)};
}

std::string SwitchOff(ZEntityType** p_Entity) {
    if (!p_Entity)
        return "no entity";

    SetArgs s_Args {p_Entity, false, false};

    if (const DWORD s_Code = SafeSet(&s_Args))
        return fmt::format("SetProperty {}", FaultText(s_Code));

    std::string s_Why;
    const auto s_Check = [&](std::string_view p_Name, bool p_Set, float p_Expected) {
        std::string s_Problem;

        if (!p_Set)
            s_Problem = "SetProperty returned false";
        else if (!fr::DamageReadBackOk(ReadFloat(p_Entity, p_Name), p_Expected))
            s_Problem = "read back a different value";

        if (!s_Problem.empty())
            s_Why += fmt::format("{}{} {}", s_Why.empty() ? "" : ", ", p_Name, s_Problem);
    };

    s_Check(fr::kDamageProperty, s_Args.damageOk, fr::kDamageOff);
    s_Check(fr::kIntervalProperty, s_Args.intervalOk, fr::kIntervalOff);
    return s_Why;
}

} // namespace variant_damage
