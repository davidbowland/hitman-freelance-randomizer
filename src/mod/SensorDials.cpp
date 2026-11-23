// Sensor dials (see SensorDials.h). Every raw engine read and write follows
// the guard pattern in Guard.h.

#include "SensorDials.h"

#include "Collections.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PatchBook.h"
#include "PinIds.h"
#include "EntityReads.h"

#include <Glacier/Hash.h>
#include <Glacier/Reflection.h>
#include <Glacier/ZEntity.h>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace {

constexpr uint64_t kGlobalDataBrick = 0x00097AA87B144150;
constexpr uint64_t kKnownSensorId = 0x6b6889480b488495; // the professional shared sensor
constexpr const char* kCollectionClass = "ZDifficultyCollectionEntity";
constexpr const char* kConditionClass = "ZDifficultyCondition";
constexpr const char* kSensorClass = "ZSharedSensorDef";
constexpr std::string_view kVisibilityProperty = "m_VisibilitySetting";
constexpr size_t kMaxEnumItems = 32;
constexpr size_t kMaxVisibility = 32;   // elements (7 in vanilla)
constexpr float kMaxSaneCone = 1000.0f; // meters; anything above means the layout is wrong
constexpr std::string_view kTag = "sensor dials";

double MsSince(std::chrono::steady_clock::time_point p_Start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - p_Start).count();
}

// Copies at most p_Capacity - 1 characters and terminates p_Out.
void CopyName(const char* p_Text, char* p_Out, size_t p_Capacity) {
    size_t i = 0;

    for (; p_Text && p_Text[i] && i + 1 < p_Capacity; ++i)
        p_Out[i] = p_Text[i];

    p_Out[i] = '\0';
}

// One field of the visibility element type, from its reflection.
struct FieldOut {
    bool found;
    uint32_t offset; // SPropertyInfo::m_nExtraData: the member's offset in a class type
    uint16_t size;
    bool isEnum;
    char type[64];
};

struct EnumItemOut {
    char name[48];
    int32_t value;
};

// What reflection says about m_VisibilitySetting: a TArray of a class with
// m_eSensitivity (an enum) and m_fFocusConeRange (float32).
struct LayoutOut {
    bool hasProperty;
    bool hasGetter;
    bool isArray;
    bool elementIsClass;
    uint16_t elementSize;
    char elementType[64];
    FieldOut sensitivity;
    FieldOut cone;
    uint32_t enumCount;
    EnumItemOut items[kMaxEnumItems];
};

void FillField(const SNamedPropertyInfo& p_Field, FieldOut* p_Out) {
    const IType* s_Type = p_Field.m_propertyInfo.m_Type ? p_Field.m_propertyInfo.m_Type->GetTypeInfo() : nullptr;
    p_Out->found = true;
    p_Out->offset = static_cast<uint32_t>(p_Field.m_propertyInfo.m_nExtraData);

    if (!s_Type)
        return;

    p_Out->size = s_Type->m_nTypeSize;
    p_Out->isEnum = s_Type->IsEnum();
    CopyName(s_Type->pszTypeName, p_Out->type, sizeof(p_Out->type));
}

__declspec(noinline) void VisibilityLayoutOf(ZEntityType** p_Entity, uint32_t p_PropertyId, LayoutOut* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    const SPropertyData* s_Property = s_Type ? s_Type->FindProperty(p_PropertyId) : nullptr;
    const SPropertyInfo* s_Info = s_Property ? s_Property->m_pPropertyInfo : nullptr;

    if (!s_Info)
        return;

    p_Out->hasProperty = true;

    if ((s_Info->m_Flags & EPropertyInfoFlags::E_HAS_GETTER_SETTER) != 0) {
        p_Out->hasGetter = true;
        return;
    }

    const IType* s_TypeInfo = s_Info->m_Type ? s_Info->m_Type->GetTypeInfo() : nullptr;

    if (!s_TypeInfo || !s_TypeInfo->IsArray())
        return;

    p_Out->isArray = true;
    const STypeID* s_ElementId = reinterpret_cast<const IContainerType*>(s_TypeInfo)->elementType;
    const IType* s_Element = s_ElementId ? s_ElementId->GetTypeInfo() : nullptr;

    if (!s_Element)
        return;

    p_Out->elementSize = s_Element->m_nTypeSize;
    CopyName(s_Element->pszTypeName, p_Out->elementType, sizeof(p_Out->elementType));

    if (!s_Element->IsClass())
        return;

    p_Out->elementIsClass = true;
    const auto* s_Class = reinterpret_cast<const IClassType*>(s_Element);

    for (uint16_t i = 0; i < s_Class->m_nProperties; ++i) {
        const SNamedPropertyInfo& s_Field = s_Class->m_pProperties[i];
        const std::string_view s_Name = s_Field.m_pszPropertyName ? s_Field.m_pszPropertyName : "";

        if (s_Name == "m_fFocusConeRange") {
            FillField(s_Field, &p_Out->cone);
            continue;
        }

        if (s_Name != "m_eSensitivity")
            continue;

        FillField(s_Field, &p_Out->sensitivity);
        const IType* s_EnumType =
            s_Field.m_propertyInfo.m_Type ? s_Field.m_propertyInfo.m_Type->GetTypeInfo() : nullptr;

        if (!s_EnumType || !s_EnumType->IsEnum())
            continue;

        const auto* s_Enum = reinterpret_cast<const IEnumType*>(s_EnumType);

        for (size_t k = 0; k < s_Enum->items.size() && p_Out->enumCount < kMaxEnumItems; ++k) {
            EnumItemOut& s_Item = p_Out->items[p_Out->enumCount++];
            CopyName(s_Enum->items[k].szName, s_Item.name, sizeof(s_Item.name));
            s_Item.value = s_Enum->items[k].nValue;
        }
    }
}

__declspec(noinline) DWORD SafeVisibilityLayout(ZEntityType** p_Entity, uint32_t p_PropertyId, LayoutOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        VisibilityLayoutOf(p_Entity, p_PropertyId, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// Writes a raw float and reads it back: true when it holds.
bool WriteRawFloat(uintptr_t p_Address, float p_Value) {
    float s_Back = 0.0f;
    return p_Address && !WriteEngineBytes(p_Address, &p_Value, sizeof(p_Value)) &&
           !ReadEngineBytes(p_Address, &s_Back, sizeof(s_Back)) && s_Back == p_Value;
}

std::optional<float> ReadRawFloat(uintptr_t p_Address) {
    float s_Value = 0.0f;

    if (!p_Address || ReadEngineBytes(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value;
}

std::optional<float> ReadSensorFloat(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Raw = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);
    return s_Raw.status == RawRead32::Status::Ok ? std::optional(std::bit_cast<float>(s_Raw.bits)) : std::nullopt;
}

std::optional<int32_t> RawInt(ZEntityType** p_Entity, std::string_view p_Name) {
    const RawRead32 s_Raw = ReadPropertyRaw32(ZEntityRef(p_Entity), p_Name);
    return s_Raw.status == RawRead32::Status::Ok ? std::optional(std::bit_cast<int32_t>(s_Raw.bits)) : std::nullopt;
}

struct SensorFound {
    ZEntityType** sensor = nullptr;
    uint64_t sensorId = 0;
    uint64_t collectionId = 0;
    size_t collections = 0;
    std::string why; // when not found
};

// The professional collection's shared sensor in the globaldata brick.
SensorFound FindProfessionalSensor(const TArray<SBrickAllocationInfo>& p_Bricks) {
    SensorFound s_Out;
    bool s_Loaded = false;

    for (size_t i = 0; i < p_Bricks.size() && !s_Loaded; ++i)
        s_Loaded = p_Bricks[i].m_RuntimeResourceID.GetID() == kGlobalDataBrick;

    if (!s_Loaded) {
        s_Out.why = fmt::format("the globaldata brick {:016X} is not loaded yet", kGlobalDataBrick);
        return s_Out;
    }

    std::vector<ClassEntity> s_Collections;
    FindEntitiesOfClass(p_Bricks, {kGlobalDataBrick}, kCollectionClass, s_Collections);
    s_Out.collections = s_Collections.size();

    for (const ClassEntity& s_Collection : s_Collections) {
        const RefRead s_Condition = ReadEntityRefProperty(s_Collection.entity, "m_rEnableCondition");
        std::string s_Class;
        uint64_t s_Id = 0;
        std::optional<int32_t> s_Level;

        if (s_Condition.entity && !ReadEntityClassName(s_Condition.entity, s_Class, &s_Id) &&
            s_Class == kConditionClass) {
            s_Level = RawInt(s_Condition.entity, "m_eDifficulty");
        }

        if (fr::CollectionRoleOf(s_Class, s_Level) != fr::CollectionRole::Professional)
            continue;

        std::string s_Unused;
        ReadEntityClassName(s_Collection.entity, s_Unused, &s_Out.collectionId);
        const RefRead s_Sensor = ReadEntityRefProperty(s_Collection.entity, "m_rSharedSensor");

        if (!s_Sensor.entity) {
            s_Out.why = fmt::format("the professional collection {:016x} has no shared sensor ({})", s_Out.collectionId,
                                    s_Sensor.why.empty() ? "none" : s_Sensor.why);
            return s_Out;
        }

        std::string s_SensorClass;

        if (const uint32_t s_Code = ReadEntityClassName(s_Sensor.entity, s_SensorClass, &s_Out.sensorId)) {
            s_Out.why = "the professional collection's shared sensor " + FaultText(s_Code);
            return s_Out;
        }

        if (s_SensorClass != kSensorClass) {
            s_Out.why = fmt::format("the professional collection's shared sensor {:016x} is a {}, not a {}",
                                    s_Out.sensorId, s_SensorClass.empty() ? "<no class>" : s_SensorClass, kSensorClass);
            return s_Out;
        }

        s_Out.sensor = s_Sensor.entity;
        return s_Out;
    }

    s_Out.why = fmt::format("no professional collection (ZDifficultyCondition eDL_NORMAL) among the {} difficulty "
                            "collections in the globaldata brick",
                            s_Out.collections);
    return s_Out;
}

// One visibility element: its perception state and where its cone range is.
struct ConeElement {
    std::string sensitivity;
    uintptr_t coneAddress;
    float cone;
};

// Reads m_VisibilitySetting through reflection; empty on success, else why
// the cone ranges cannot be written safely. p_Layout gets a description.
std::string ReadVisibility(ZEntityType** p_Sensor, std::vector<ConeElement>& p_Out, std::string* p_Layout) {
    auto s_Layout = std::make_unique<LayoutOut>();
    std::memset(s_Layout.get(), 0, sizeof(LayoutOut));
    if (const DWORD s_Code = SafeVisibilityLayout(p_Sensor, fr::kPropVisibilitySetting, s_Layout.get()))
        return "reading its type " + FaultText(static_cast<uint32_t>(s_Code));

    const LayoutOut& L = *s_Layout;

    if (!L.hasProperty)
        return "the sensor has no m_VisibilitySetting";
    if (L.hasGetter)
        return "m_VisibilitySetting has a getter";
    if (!L.isArray || !L.elementIsClass)
        return fmt::format("m_VisibilitySetting is not an array of a class (element {})",
                           L.elementType[0] ? L.elementType : "<unknown>");
    if (!L.sensitivity.found || !L.cone.found)
        return fmt::format("{} has no {}", L.elementType,
                           !L.sensitivity.found ? "m_eSensitivity" : "m_fFocusConeRange");
    if (!L.sensitivity.isEnum || L.sensitivity.size != 4 || L.enumCount == 0)
        return fmt::format("m_eSensitivity is {} ({} bytes, {} names), not a 4-byte enum", L.sensitivity.type,
                           L.sensitivity.size, L.enumCount);
    if (std::string_view(L.cone.type) != "float32" || L.cone.size != 4)
        return fmt::format("m_fFocusConeRange is {} ({} bytes), not float32", L.cone.type, L.cone.size);
    if (L.sensitivity.offset == L.cone.offset || L.sensitivity.offset + 4 > L.elementSize ||
        L.cone.offset + 4 > L.elementSize || L.cone.offset % 4 != 0 || L.sensitivity.offset % 4 != 0)
        return fmt::format("implausible offsets (m_eSensitivity {}, m_fFocusConeRange {}, element {} bytes)",
                           L.sensitivity.offset, L.cone.offset, L.elementSize);

    *p_Layout = fmt::format("{} ({} bytes; m_eSensitivity {} at {}, m_fFocusConeRange at {})", L.elementType,
                            L.elementSize, L.sensitivity.type, L.sensitivity.offset, L.cone.offset);

    const PropertyLocation s_Location = LocateProperty(p_Sensor, kVisibilityProperty);

    if (s_Location.status != PropertyLocation::Status::Ok)
        return s_Location.status == PropertyLocation::Status::Fault ? "locating it " + FaultText(s_Location.faultCode)
                                                                    : std::string("cannot locate it");

    struct {
        uintptr_t begin;
        uintptr_t end;
        uint64_t flags;
    } s_Array {};

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Array, sizeof(s_Array)))
        return "the array " + FaultText(s_Code);

    const auto s_Span =
        fr::ArrayElements(s_Location.address, s_Array.begin, s_Array.end, s_Array.flags, L.elementSize, kMaxVisibility);

    if (!s_Span || s_Span->isInline || s_Span->count == 0)
        return fmt::format("the array is implausible (begin 0x{:X}, end 0x{:X}, flags 0x{:X})", s_Array.begin,
                           s_Array.end, s_Array.flags);

    for (size_t i = 0; i < s_Span->count; ++i) {
        const uintptr_t s_Element = s_Span->begin + i * L.elementSize;
        int32_t s_Sensitivity = 0;
        float s_Cone = 0.0f;

        if (const uint32_t s_Code = ReadEngineBytes(s_Element + L.sensitivity.offset, &s_Sensitivity, 4))
            return fmt::format("element {} {}", i, FaultText(s_Code));
        if (const uint32_t s_Code = ReadEngineBytes(s_Element + L.cone.offset, &s_Cone, 4))
            return fmt::format("element {} {}", i, FaultText(s_Code));

        const char* s_Name = nullptr;

        for (uint32_t k = 0; k < L.enumCount && !s_Name; ++k) {
            if (L.items[k].value == s_Sensitivity)
                s_Name = L.items[k].name;
        }

        // Both checks guard against a wrong layout: a write lands only where these read sanely.
        if (!s_Name)
            return fmt::format("element {}: m_eSensitivity {} is no {} value", i, s_Sensitivity, L.sensitivity.type);
        if (!std::isfinite(s_Cone) || s_Cone < 0.0f || s_Cone > kMaxSaneCone)
            return fmt::format("element {} ({}): m_fFocusConeRange {} is implausible", i, s_Name, s_Cone);

        p_Out.push_back({s_Name, s_Element + L.cone.offset, s_Cone});
    }

    return {};
}

} // namespace

bool SensorDials::Apply(ZEntitySceneContext* p_Context, const Plan& p_Plan, const char* p_Stage) {
    const auto s_Start = std::chrono::steady_clock::now();
    const SensorFound s_Found = FindProfessionalSensor(p_Context->m_aLoadedBricks);

    if (!s_Found.sensor) {
        if (std::string_view(p_Stage) != "eLoading_SceneStarted") {
            LogTo(fr::LogTopic::AiDifficulty, "sensor dials at {}: {}; tried again at scene start", p_Stage,
                  s_Found.why);
            return false;
        }

        m_Done = true;
        m_Summary = "Guard senses: not changed (" + s_Found.why + ").";
        LogTo(fr::LogTopic::AiDifficulty, "sensor dials at {}: {}; nothing written this mission", p_Stage, s_Found.why);
        return true;
    }

    m_Done = true;
    m_Sensor = s_Found.sensor;
    LogTo(fr::LogTopic::AiDifficulty, "sensor dials at {}: professional sensor {} {:016x} (collection {:016x}{}){}",
          p_Stage, kSensorClass, s_Found.sensorId, s_Found.collectionId,
          s_Found.sensorId == kKnownSensorId ? "" : fmt::format("; NOT the known {:016x}", kKnownSensorId));

    // The cone elements, located once (a wrong layout writes none of them).
    std::vector<ConeElement> s_Cones;
    std::string s_Layout;
    const std::string s_ConeWhy = ReadVisibility(s_Found.sensor, s_Cones, &s_Layout);

    if (s_ConeWhy.empty()) {
        std::string s_Text;

        for (const ConeElement& s_Cone : s_Cones)
            s_Text += fmt::format("{}{} {:.9g}", s_Text.empty() ? "" : ", ", s_Cone.sensitivity, s_Cone.cone);

        LogTo(fr::LogTopic::AiDifficulty, "sensor dials: m_VisibilitySetting: {} elements of {}: {}", s_Cones.size(),
              s_Layout, s_Text);
    }
    else {
        LogTo(fr::LogTopic::AiDifficulty, "sensor dials: m_VisibilitySetting: {}; no focus-cone range is written",
              s_ConeWhy);
    }

    size_t s_Written = 0, s_Failed = 0, s_Off = 0;

    for (const fr::SensorDial& s_Dial : fr::SensorDialTable()) {
        const fr::SensorDecision s_Decision =
            fr::DecideSensorDial(p_Plan.seed, p_Plan.scene, s_Dial, p_Plan.roll, p_Plan.off);
        Row s_Row {&s_Dial,
                   std::numeric_limits<double>::quiet_NaN(),
                   s_Decision.base,
                   s_Decision.multiplier,
                   s_Decision.value,
                   false,
                   s_Decision.attempts,
                   s_Decision.fellBack,
                   ""};
        const std::string s_Label = fr::SensorDialLabel(s_Dial);

        if (!s_Decision.apply) {
            ++s_Off;
            LogTo(fr::LogTopic::AiDifficulty, "sensor dial {} ({}): in sensor_dials_off; left alone", s_Dial.key,
                  s_Label);
            continue;
        }

        const size_t s_RowIndex = m_Rows.size();
        std::vector<Written> s_Writes;
        std::string s_Why;

        if (s_Dial.sensitivity.empty()) {
            if (const std::optional<float> s_Before = ReadSensorFloat(s_Found.sensor, s_Dial.property)) {
                bool s_Ok = false;
                const uint32_t s_Code = WriteFloatProperty(s_Found.sensor, s_Dial.property, s_Decision.value, &s_Ok);
                const std::optional<float> s_After = ReadSensorFloat(s_Found.sensor, s_Dial.property);
                s_Row.found = *s_Before;

                // Holding the value counts even if the setter reported failure (restored at scene clear either way).
                if (!s_Code && s_After && *s_After == s_Decision.value)
                    s_Writes.push_back({s_RowIndex, s_Found.sensor, 0, *s_Before, s_Decision.value});
                else
                    s_Why = s_Code ? "write " + FaultText(s_Code)
                                   : fmt::format("write failed (reads {})", s_After ? fmt::format("{:.9g}", *s_After)
                                                                                    : std::string("<unreadable>"));
            }
            else {
                s_Why = "not readable as a raw float32";
            }
        }
        else if (!s_ConeWhy.empty()) {
            s_Why = "visibility array not usable";
        }
        else {
            for (const ConeElement& s_Cone : s_Cones) {
                if (s_Cone.sensitivity != s_Dial.sensitivity)
                    continue;

                s_Row.found = s_Cone.cone;

                if (WriteRawFloat(s_Cone.coneAddress, s_Decision.value))
                    s_Writes.push_back({s_RowIndex, nullptr, s_Cone.coneAddress, s_Cone.cone, s_Decision.value});
                else
                    s_Why = "raw write did not hold";
            }

            if (std::isnan(s_Row.found))
                s_Why = "no element with this m_eSensitivity";
        }

        s_Row.ok = !s_Writes.empty() && s_Why.empty();
        s_Row.status = s_Row.ok ? "written" : (s_Why.empty() ? "write failed" : s_Why);
        (s_Row.ok ? s_Written : s_Failed) += 1;

        // A partly written dial (several elements, one failed) is still restored.
        m_Written.insert(m_Written.end(), s_Writes.begin(), s_Writes.end());

        const fr::RollResult s_Roll {s_Decision.multiplier, s_Decision.attempts, s_Decision.fellBack};
        const std::string s_Note = fr::RollNote(s_Roll);
        LogTo(fr::LogTopic::AiDifficulty, "sensor dial {} ({}) {}: found {}, vanilla {:.9g} x{:.4f} -> {:.9g}: {}{}{}",
              s_Dial.key, s_Dial.info.name, s_Label,
              std::isnan(s_Row.found) ? std::string("<none>") : fmt::format("{:.9g}", s_Row.found), s_Decision.base,
              s_Decision.multiplier, s_Decision.value, s_Row.status,
              !std::isnan(s_Row.found) && static_cast<float>(s_Row.found) != static_cast<float>(s_Decision.base)
                  ? " (the sensor did not hold vanilla: another mod? rolled from vanilla anyway)"
                  : "",
              s_Note.empty() ? std::string() : "; " + s_Note);

        m_Rows.push_back(std::move(s_Row));
    }

    m_Summary = fmt::format("Guard senses changed at {}: {} of {} ({} failed, {} off).", p_Stage, s_Written,
                            fr::SensorDialTable().size(), s_Failed, s_Off);
    LogTo(fr::LogTopic::AiDifficulty,
          "sensor dials at {}: {} dials written, {} failed, {} off, {:.1f} ms; checks follow at scene start, scene "
          "playing, 5 s and 60 s of play",
          p_Stage, s_Written, s_Failed, s_Off, MsSince(s_Start));
    return true;
}

bool SensorDials::ReadCurrent(const Written& p_Written, float* p_Out) const {
    const std::optional<float> s_Value = p_Written.address
                                             ? ReadRawFloat(p_Written.address)
                                             : ReadSensorFloat(p_Written.entity, m_Rows[p_Written.row].dial->property);

    if (!s_Value)
        return false;

    *p_Out = *s_Value;
    return true;
}

void SensorDials::Check(const char* p_When) {
    size_t s_Held = 0;
    std::map<size_t, std::string> s_Status;
    std::map<size_t, bool> s_RowHeld; // a row is held when every value written for it reads back

    for (const Written& s_Written : m_Written) {
        float s_Now = 0.0f;
        const bool s_Read = ReadCurrent(s_Written, &s_Now);
        const Row& s_Row = m_Rows[s_Written.row];

        if (s_Read && s_Now == s_Written.written) {
            ++s_Held;
            s_Status.try_emplace(s_Written.row, fmt::format("held at {}", p_When));
            s_RowHeld.try_emplace(s_Written.row, true);
            continue;
        }

        const std::string s_Text = s_Read ? fmt::format("{:.9g}", s_Now) : std::string("<unreadable>");
        s_Status[s_Written.row] = fmt::format("NOT HELD at {}: {}", p_When, s_Text);
        s_RowHeld[s_Written.row] = false;
        LogTo(fr::LogTopic::AiDifficulty,
              "sensor dials check {}: {} ({}) reads {} (wrote {:.9g}, original {:.9g}): the game {}", p_When,
              s_Row.dial->key, fr::SensorDialLabel(*s_Row.dial), s_Text, s_Written.written, s_Written.original,
              s_Read && s_Now == s_Written.original ? "put the original back" : "changed it");
    }

    for (auto& [s_Index, s_Text] : s_Status)
        m_Rows[s_Index].status = std::move(s_Text);

    for (const auto& [s_Index, s_RowIsHeld] : s_RowHeld)
        m_Rows[s_Index].held = s_RowIsHeld;

    LogTo(fr::LogTopic::AiDifficulty, "sensor dials check {}: {} of {} written values hold (sensor {:016x})", p_When,
          s_Held, m_Written.size(), reinterpret_cast<uintptr_t>(m_Sensor));
}

bool SensorDials::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                 const Plan& p_Plan) noexcept {
    bool s_Changed = false;
    Contained(kTag, "loading stage", [&] { s_Changed = LoadingStage(p_Stage, p_Context, p_Plan); });
    return s_Changed;
}

bool SensorDials::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context, const Plan& p_Plan) {
    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_SceneAllocated:
        case ESceneLoadingStage::eLoading_SceneStarted: {
            const char* s_Stage = p_Stage == ESceneLoadingStage::eLoading_SceneAllocated ? "eLoading_SceneAllocated"
                                                                                         : "eLoading_SceneStarted";

            if (m_Done) {
                if (!m_Written.empty()) {
                    Check("scene start");
                    return true;
                }

                return false;
            }

            if (!p_Plan.write || !p_Context)
                return false;

            return Apply(p_Context, p_Plan, s_Stage);
        }
        case ESceneLoadingStage::eLoading_ScenePlaying:
            if (m_Written.empty())
                return false;

            Check("scene playing");
            m_Playing = true;
            m_Play = 0.0;
            m_Did5 = m_Did60 = false;
            return true;
        default: return false;
    }
}

bool SensorDials::Tick(double p_Seconds) noexcept {
    bool s_Changed = false;
    Contained(kTag, "tick", [&] { s_Changed = PlayChecks(p_Seconds); });
    return s_Changed;
}

bool SensorDials::PlayChecks(double p_Seconds) {
    if (!m_Playing || m_Written.empty())
        return false;

    m_Play += p_Seconds;

    if (!m_Did5 && m_Play >= 5.0) {
        m_Did5 = true;
        Check("5 s of play");
        return true;
    }

    if (!m_Did60 && m_Play >= 60.0) {
        m_Did60 = true;
        m_Playing = false;
        Check("60 s of play");
        return true;
    }

    return false;
}

void SensorDials::Restore(const char* p_When) noexcept {
    Contained(kTag, "restore", [&] {
        if (!m_Written.empty()) {
            size_t s_Restored = 0, s_Already = 0, s_Elsewhere = 0, s_Failed = 0;

            for (const Written& s_Written : m_Written) {
                const fr::SensorDial& s_Dial = *m_Rows[s_Written.row].dial;
                float s_Now = 0.0f;

                if (!ReadCurrent(s_Written, &s_Now)) {
                    ++s_Failed;
                    Log("sensor dials restore: {} unreadable; left alone", s_Dial.key);
                    continue;
                }

                switch (fr::DecideRestore(s_Now, {0, s_Written.original, s_Written.written})) {
                    case fr::RestoreDecision::Restore: {
                        bool s_Ok = false;

                        if (s_Written.address) {
                            s_Ok = WriteRawFloat(s_Written.address, s_Written.original);
                        }
                        else {
                            const uint32_t s_Code =
                                WriteFloatProperty(s_Written.entity, s_Dial.property, s_Written.original, &s_Ok);
                            const std::optional<float> s_After = ReadSensorFloat(s_Written.entity, s_Dial.property);
                            s_Ok = !s_Code && s_After && *s_After == s_Written.original;
                        }

                        (s_Ok ? s_Restored : s_Failed) += 1;

                        if (!s_Ok)
                            Log("sensor dials restore: {} {:.9g} -> {:.9g}: write failed", s_Dial.key, s_Now,
                                s_Written.original);
                        break;
                    }
                    case fr::RestoreDecision::AlreadyOriginal: ++s_Already; break;
                    case fr::RestoreDecision::ChangedElsewhere:
                        ++s_Elsewhere;
                        LogTo(fr::LogTopic::AiDifficulty,
                              "sensor dials restore: {} is {:.9g} (wrote {:.9g}, original {:.9g}): changed elsewhere, "
                              "left alone",
                              s_Dial.key, s_Now, s_Written.written, s_Written.original);
                        break;
                }
            }

            LogTo(fr::LogTopic::AiDifficulty,
                  "sensor dials restore ({}): {} restored, {} already original, {} changed elsewhere, {} failed",
                  p_When, s_Restored, s_Already, s_Elsewhere, s_Failed);
        }
    });

    Reset();
}

void SensorDials::OnLoadScene() noexcept {
    if (!m_Written.empty())
        LogTo(fr::LogTopic::AiDifficulty,
              "sensor dials: forgetting {} written values without restoring them (no scene clear came)",
              m_Written.size());

    Reset();
}

void SensorDials::Reset() noexcept {
    m_Done = false;
    m_Playing = false;
    m_Play = 0.0;
    m_Did5 = m_Did60 = false;
    m_Sensor = nullptr;
    m_Rows.clear();
    m_Written.clear();
    m_Summary.clear();
}
