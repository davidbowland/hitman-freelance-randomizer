// Prestige card shuffle (see PrestigeShuffle.h). Every raw engine read and
// write follows the guard pattern in Guard.h.

#include "PrestigeShuffle.h"

#include "BlueprintRead.h"
#include "ActorReads.h" // SafeOwning
#include "Collections.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "ObjectValue.h"
#include "Guard.h"
#include "EntityReads.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Glacier/ZObject.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <memory>
#include <set>
#include <span>
#include <string_view>

namespace {

using fr::kPinGetValue; // a ZValueInt_Poll's value
using fr::kPinIn;
using fr::kPinObjectiveId;
using fr::kPinOnPickDone;
using fr::kPinOnPickFailed; // the selector had nothing to pick
using fr::kPinOut;
using fr::kPinOut02;
using fr::kPinOut06;
using fr::kPinPick;
using fr::kPinPoll;

// In each choice entry: the ZValueInt_Poll that emits the entry's objective
// number when the picker picks it (the card shows that entry), and the one
// that emits it when the card is taken.
constexpr uint64_t kDisplayPollId = 0x9a109f047f0cc34dull;
constexpr uint64_t kTakePollId = 0x8e5a39a7f607be3aull;
constexpr size_t kMaxChoices = 256;
constexpr size_t kMaxListedProperties = 48;

struct PinName {
    uint32_t id;
    const char* name;
};

const std::array<PinName, 19> kPinNames {{
    {fr::kPinOut, "Out"},
    {fr::kPinOut01, "Out01"},
    {fr::kPinOut02, "Out02"},
    {fr::kPinOut03, "Out03"},
    {fr::kPinOut04, "Out04"},
    {fr::kPinOut05, "Out05"},
    {fr::kPinOut06, "Out06"},
    {fr::kPinIn, "In"},
    {fr::kPinPick, "Pick"},
    {fr::kPinPoll, "Poll"},
    {fr::kPinObjectiveId, "ObjectiveId"},
    {fr::kPinOnPickDone, "OnPickDone"},
    {fr::kPinOnPickFailed, "OnPickFailed"},
    {fr::kPinSetValue, "SetValue"},
    {fr::kPinOnValue, "OnValue"},
    {fr::kPinOnTrue, "OnTrue"},
    {fr::kPinOnFalse, "OnFalse"},
    {fr::kPinCompleted, "Completed"},
    {fr::kPinOnSelected, "OnSelected"},
}};

std::string PinText(uint32_t p_Pin) {
    for (const PinName& s_Name : kPinNames) {
        if (s_Name.id == p_Pin)
            return s_Name.name;
    }

    return fmt::format("pin {:08x}", p_Pin);
}

const char* RoleName(size_t p_Role) {
    static constexpr std::array<const char*, 14> s_Names {
        "SignalFork_Void_04",
        "SignalFork_Void",
        "SignalPipe_Conditional_Void",
        "SignalFork_Void_01 (card A)",
        "SignalFork_Void_02 (card B)",
        "SignalFork_Void_03 (card C)",
        "Setpiece_evergreen_PrestigeObjective_Unique",
        "SetCPD_OptinPayoutObjective",
        "Picker1",
        "Picker2",
        "Picker3",
        "Picker1's RandomSelector",
        "Picker2's RandomSelector",
        "Picker3's RandomSelector",
    };
    return p_Role < s_Names.size() ? s_Names[p_Role] : "?";
}

constexpr char kCardLetters[] = {'A', 'B', 'C'};

constexpr size_t kMaxChain = 6;           // owner chain steps followed
constexpr size_t kMaxListedChildren = 48; // picker sub-entities listed when its selector is missing

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string LookupText(const SubEntityLookup& p_Lookup) {
    if (p_Lookup.faultStep)
        return fmt::format("{} {}", p_Lookup.faultStep, FaultText(p_Lookup.faultCode));

    return p_Lookup.entity ? "found" : "MISSING";
}

__declspec(noinline) void TypeIdOf(const ZEntityType* p_Type, uint64_t* p_Out) {
    *p_Out = p_Type ? p_Type->m_nEntityID : 0;
}

__declspec(noinline) DWORD SafeTypeId(const ZEntityType* p_Type, uint64_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        TypeIdOf(p_Type, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void EntityIdOf(ZEntityType** p_Entity, uint64_t* p_Out) {
    const ZEntityImpl* s_Entity = ZEntityRef(p_Entity).GetEntity();
    const ZEntityType* s_Type = s_Entity ? s_Entity->GetType() : nullptr;
    *p_Out = s_Type ? s_Type->m_nEntityID : 0;
}

__declspec(noinline) DWORD SafeEntityId(ZEntityType** p_Entity, uint64_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntityIdOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The owning entity (p_Owning) or logical parent, read guarded.
uint32_t ReadRelative(ZEntityType** p_Entity, bool p_Owning, ZEntityType*** p_Out) {
    return p_Owning ? static_cast<uint32_t>(SafeOwning(p_Entity, p_Out)) : ReadLogicalParent(p_Entity, p_Out);
}

// The sub-entity ids of p_Entity and its owning entities (p_Out[0] its own),
// up to kMaxChain steps; the rest stays 0.
std::array<uint64_t, kMaxChain + 1> OwnerChainIds(ZEntityType** p_Entity) {
    std::array<uint64_t, kMaxChain + 1> s_Ids {};
    ZEntityType** s_At = p_Entity;

    for (size_t i = 0; i < s_Ids.size() && s_At; ++i) {
        if (SafeEntityId(s_At, &s_Ids[i]) || s_Ids[i] == 0) {
            s_Ids[i] = 0;
            break;
        }

        ZEntityType** s_Next = nullptr;

        if (ReadRelative(s_At, true, &s_Next) || s_Next == s_At)
            break;

        s_At = s_Next;
    }

    return s_Ids;
}

// "<id> <class> -> <owner id> <class> [own factory] -> ...": the owning
// (p_Owning) or logical-parent chain of an entity.
std::string ChainText(ZEntityType** p_Entity, bool p_Owning) {
    std::string s_Text = EntityText(p_Entity);
    ZEntityType** s_At = p_Entity;

    for (size_t i = 0; i < kMaxChain && s_At; ++i) {
        ZEntityType** s_Next = nullptr;

        if (const DWORD s_Code = ReadRelative(s_At, p_Owning, &s_Next)) {
            s_Text += " -> " + FaultText(s_Code);
            return s_Text;
        }

        if (!s_Next || s_Next == s_At) {
            s_Text += " -> (none)";
            return s_Text;
        }

        ZEntityBlueprintFactoryBase* s_Factory = nullptr;
        const uint32_t s_Code = ReadOwnBlueprintFactory(s_Next, &s_Factory);
        s_Text += fmt::format(" -> {}{}", EntityText(s_Next),
                              s_Code      ? " [factory " + FaultText(s_Code) + "]"
                              : s_Factory ? " [own factory]"
                                          : "");
        s_At = s_Next;
    }

    return s_Text;
}

// Writes the payload of an int32 pin signal in place (the receivers get it);
// the caller checked that it is an int32.
__declspec(noinline) void WriteInt32PayloadOf(const ZObjectRef* p_Data, int32_t p_Value) {
    void* s_Bytes = p_Data->GetData();

    if (s_Bytes)
        std::memcpy(s_Bytes, &p_Value, sizeof(p_Value));
}

__declspec(noinline) DWORD SafeWriteInt32Payload(const ZObjectRef* p_Data, int32_t p_Value) {
    DWORD s_Code = 0;

    __try {
        WriteInt32PayloadOf(p_Data, p_Value);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

std::optional<float> ReadFloat(uintptr_t p_Address) {
    float s_Value = 0.0f;

    if (p_Address == 0 || ReadEngineBytes(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value;
}

// "name [type] = value" for every property: 4-byte numbers and bools as
// values, arrays by their element count, the rest by type only.
std::string PropertiesText(ZEntityType** p_Entity) {
    const EntityPropertyTable s_List = ListEntityProperties(p_Entity, kMaxListedProperties);
    std::string s_Text;

    for (const TableProperty& e : s_List.properties) {
        const std::string_view s_Type(e.typeName);
        std::string s_Value;
        int32_t s_I32 = 0;
        float s_F32 = 0.0f;
        std::memcpy(&s_I32, e.bytes, 4);
        std::memcpy(&s_F32, e.bytes, 4);

        if (e.hasGetter)
            s_Value = "<getter>";
        else if (s_Type == "bool")
            s_Value = e.bytes[0] ? "true" : "false";
        else if (s_Type == "float32")
            s_Value = fmt::format("{}", s_F32);
        else if (e.typeSize == 4)
            s_Value = std::to_string(s_I32);
        else if (s_Type.starts_with("TArray<")) {
            uintptr_t s_Begin = 0;
            std::memcpy(&s_Begin, e.bytes, 8);
            s_Value = fmt::format("<array at 0x{:X}>", s_Begin);
        }
        else
            s_Value = "...";

        s_Text += fmt::format("{}{} [{}] = {}", s_Text.empty() ? "" : "; ",
                              e.hasName ? e.name : fmt::format("{:08x}", e.id), e.typeName, s_Value);
    }

    if (s_List.faultCode)
        s_Text += "; " + FaultText(s_List.faultCode);

    return fmt::format("{} of {} properties: {}", s_List.properties.size(), s_List.total,
                       s_Text.empty() ? "none" : s_Text);
}

std::optional<int32_t> Int32Payload(const ZObjectRef& p_Data) {
    ObjectValue s_Value {};

    if (SafeReadObjectValue(&p_Data, &s_Value) || s_Value.kind != ObjectValue::Kind::Int32)
        return std::nullopt;

    return static_cast<int32_t>(s_Value.bits);
}

std::string CardsText(const std::array<int32_t, fr::kPrestigeCards>& p_Cards) {
    std::string s_Text;

    for (size_t i = 0; i < p_Cards.size(); ++i)
        s_Text += fmt::format("{}card {}: {}", i ? ", " : "", kCardLetters[i], fr::PrestigeChoiceText(p_Cards[i]));

    return s_Text;
}

// A choice's m_Condition (the selector skips a choice whose condition is
// false): its value when the condition entity has a readable m_bValue, and
// what was read, for the log.
struct ChoiceCondition {
    std::optional<bool> value;
    std::string text;
};

ChoiceCondition ReadChoiceCondition(ZEntityType** p_Choice) {
    const RefRead s_Ref = ReadEntityRefProperty(p_Choice, "m_Condition");

    if (!s_Ref.why.empty())
        return {std::nullopt, "m_Condition " + s_Ref.why};

    if (!s_Ref.entity)
        return {std::nullopt, "no m_Condition (none set: the choice is never ruled out by a condition)"};

    const PropertyLocation s_Location = LocateProperty(s_Ref.entity, "m_bValue");
    uint8_t s_Byte = 0;

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != 1 ||
        ReadEngineBytes(s_Location.address, &s_Byte, sizeof(s_Byte)))
        return {std::nullopt, fmt::format("m_Condition {}: m_bValue not readable raw; {}", EntityText(s_Ref.entity),
                                          PropertiesText(s_Ref.entity))};

    return {s_Byte != 0, fmt::format("m_Condition {}: m_bValue {}", EntityText(s_Ref.entity), s_Byte != 0)};
}

constexpr std::string_view kTag = "prestige shuffle";

} // namespace

void PrestigeShuffle::Configure(bool p_On, std::set<int32_t> p_Exclude, std::optional<fr::PrestigePickState> p_State,
                                SeedSource p_Seed, StateSaver p_Save) {
    m_On = p_On;
    m_Exclude = std::move(p_Exclude);
    m_State = p_State.value_or(fr::PrestigePickState {});
    m_Seed = std::move(p_Seed);
    m_Save = std::move(p_Save);
}

void PrestigeShuffle::SaveState() {
    if (m_Save)
        m_Save(fr::FormatPrestigePickState(m_State));
}

void PrestigeShuffle::RekeyCampaign(int32_t p_From, int32_t p_To) {
    std::lock_guard s_Lock(m_Mutex);
    const fr::PrestigePickState s_Next = fr::RekeyPrestigePick(m_State, p_From, p_To);

    if (s_Next == m_State)
        return;

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: the pick state of campaign seed {} (pick #{}) now belongs to its mission seed {}; the "
          "cards go on from there",
          p_From, s_Next.counter, p_To);
    m_State = s_Next;
    SaveState();
}

// A Freelancer mission is loading: a card taken in the safehouse is played,
// so the next pick draws new cards (once: a restart finds nothing taken).
void PrestigeShuffle::Played(const std::string& p_Scene) {
    std::lock_guard s_Lock(m_Mutex);
    const fr::PrestigePickState s_Next = fr::ConsumePrestigePick(m_State);

    if (s_Next == m_State)
        return;

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: mission {}: the card taken in the safehouse is played; the next safehouse visit shows new "
          "cards (campaign seed {}, pick #{})",
          p_Scene, s_Next.seed, s_Next.counter);
    m_State = s_Next;
    SaveState();
}

void PrestigeShuffle::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (m_On)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context); });
}

void PrestigeShuffle::Reset() {
    m_PinsOn.store(false, std::memory_order_release);
    std::lock_guard s_Lock(m_Mutex);
    ClearWatch();
    m_Pickers = {};
    m_Done = false;
    m_IsSafehouse = false;
    m_Scene.clear();
    m_Ready = false;
    m_GaveUp = false;
    m_FromReturn = false;
    m_Cards.reset();
    m_Shown = {};
    m_Taking.reset();
    m_Redraws = 0;
    m_Rejected = {}; // a rejection holds for this visit only (the game's conditions can change between visits)
    m_LastTrigger.clear();
    m_ReportPending = false;
    m_ReportIn = 0.0;
    m_Emitted.clear();
    m_PinLines = 0;
    m_WarnedNotReady = false;
    m_Picked = false;
    m_Redrawing = false;
}

void PrestigeShuffle::OnSceneClear(std::optional<int32_t> p_Optin) noexcept {
    if (!m_On)
        return;

    Contained(kTag, "scene clear", [&] {
        std::lock_guard s_Lock(m_Mutex);

        // The pickers were read, but SignalFork_Void_04 never fired: the game
        // did not pick cards in this visit (a campaign step not advanced, the
        // safehouse after a showdown).
        if (m_IsSafehouse && m_Ready && !m_Picked)
            LogTo(fr::LogTopic::PrestigeCards,
                  "prestige shuffle: no pick this visit (game kept objective {}); the game never picked the cards",
                  p_Optin ? fr::PrestigeChoiceText(*p_Optin) : std::string("not seen in a CpdSet"));

        if (m_IsSafehouse)
            RestoreAll("the scene is cleared");
    });

    Reset();
}

void PrestigeShuffle::Restore(const char* p_Why) noexcept {
    Contained(kTag, p_Why, [&] {
        std::lock_guard s_Lock(m_Mutex);
        // Off first, so no draw runs after the weights go back.
        m_On = false;
        m_PinsOn.store(false, std::memory_order_relaxed);
        RestoreAll(p_Why);
    });
}

void PrestigeShuffle::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    if (!p_Context)
        return;

    const char* s_Stage = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded: Reset(); return;
        case ESceneLoadingStage::eLoading_SceneAllocated: s_Stage = "eLoading_SceneAllocated"; break;
        case ESceneLoadingStage::eLoading_SceneStarted: s_Stage = "eLoading_SceneStarted"; break;
        default: return;
    }

    if (m_Done)
        return;

    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene
    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();

    if (!fr::IsFreelancerScene(s_Type)) {
        m_Done = true;
        return;
    }

    if (fr::IsFreelancerMission(s_Type, s_Scene)) {
        m_Done = true;
        Played(s_Scene);
        return;
    }

    m_IsSafehouse = true;
    m_Scene = s_Scene;
    m_Done = Find(p_Context, s_Stage);
}

bool PrestigeShuffle::Find(ZEntitySceneContext* p_Context, const char* p_Stage) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const bool s_LastChance = std::string_view(p_Stage) == "eLoading_SceneStarted";
    ZEntityType** s_Root = nullptr;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_Bricks[i].m_RuntimeResourceID.GetID() == fr::kPrestigeBrickRid)
            s_Root = s_Bricks[i].m_EntityRef.m_pObj;
    }

    if (!s_Root) {
        LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: {} at {}: safehouse brick {:016X} not loaded{}", m_Scene,
              p_Stage, fr::kPrestigeBrickRid,
              s_LastChance ? "; the game picks the cards (vanilla)" : "; trying again at eLoading_SceneStarted");
        return s_LastChance;
    }

    std::lock_guard s_Lock(m_Mutex);
    const std::array<uint64_t, 8> s_ChainIds {
        fr::kPrestigePickForkId,     fr::kPrestigeRepickForkId,   fr::kPrestigeReturnPipeId,
        fr::kPrestigeTakeForkIds[0], fr::kPrestigeTakeForkIds[1], fr::kPrestigeTakeForkIds[2],
        fr::kPrestigeUniqueId,       fr::kPrestigeSetCpdId,
    };
    std::string s_Found;

    // p_ParentFactory null: p_Parent's own factory (the brick root).
    const auto s_Look = [&](ZEntityType** p_Parent, ZEntityBlueprintFactoryBase* p_ParentFactory, uint64_t p_Id,
                            size_t p_Role) {
        const SubEntityLookup s_Lookup = LookUpSubEntity(p_Parent, p_ParentFactory, p_Id);
        m_Watch[p_Role].store(s_Lookup.entity, std::memory_order_relaxed);
        s_Found += fmt::format("{}{} {} {}", s_Found.empty() ? "" : ", ", RoleName(p_Role), IdText(p_Id),
                               LookupText(s_Lookup));
        return s_Lookup;
    };

    for (size_t i = 0; i < s_ChainIds.size(); ++i)
        s_Look(s_Root, nullptr, s_ChainIds[i], i);

    size_t s_Pickers = 0;
    bool s_SelectorMissing = false;

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        Picker& s_Picker = m_Pickers[i];
        const SubEntityLookup s_PickerLookup = s_Look(s_Root, nullptr, fr::kPrestigePickerIds[i], kPicker1 + i);
        s_Picker.root = s_PickerLookup.entity;
        s_Picker.factory = s_PickerLookup.factory;

        if (!s_Picker.root)
            continue;

        ++s_Pickers;

        // The picker is a nested template instance: its sub-entities come from the
        // picker template's factory (as the entity walk reaches them), not from a
        // factory of its own (GetBlueprintFactory is null for it).
        if (s_Picker.factory) {
            s_Picker.selector = s_Look(s_Picker.root, s_Picker.factory, fr::kPrestigeSelectorId, kSelector1 + i).entity;
            s_Picker.selectorFrom = "the picker template's factory";
        }

        if (!s_Picker.selector) {
            s_Picker.selector = s_Look(s_Picker.root, nullptr, fr::kPrestigeSelectorId, kSelector1 + i).entity;
            s_Picker.selectorFrom = "the picker's own factory";
        }

        if (!s_Picker.selector) {
            s_Picker.selectorFrom.clear();
            s_SelectorMissing = true;
        }
    }

    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: {} at {}: {}", m_Scene, p_Stage, s_Found);

    // Last resort: the entity walk of the brick, which reaches the pickers' sub-entities.
    if (s_SelectorMissing && s_Pickers > 0)
        FindSelectorsByWalk(s_Root);

    for (size_t i = 0; i < fr::kPrestigeCards; ++i)
        LogPickerShape(i);

    if (s_Pickers == 0 && !s_LastChance) {
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: no card picker yet; trying again at eLoading_SceneStarted");
        ClearWatch();
        return false;
    }

    // Watch the chain whatever else happens: the log shows the game's own picks too.
    m_PinsOn.store(true, std::memory_order_release);

    std::string s_Why;
    bool s_Ready = true;

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        if (!ReadPicker(i, &s_Why)) {
            LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: picker {} (card {}): {}", i + 1, kCardLetters[i],
                  s_Why);
            s_Ready = false;
        }
    }

    m_Ready = s_Ready;
    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: {}",
          s_Ready ? "ready: the cards are drawn right before Picker1 picks"
                  : "NOT ready: the game picks the cards (vanilla) in this visit");
    return true;
}

// The walk of the safehouse brick (as the merces prestige wall finds the
// display polls under the choice entries): any entity with the selector's id
// whose parent is a picker still without one. Also counts what the walk sees
// under each picker, so the log shows the shape when it fails too.
void PrestigeShuffle::FindSelectorsByWalk(ZEntityType** p_BrickRoot) {
    std::array<uintptr_t, fr::kPrestigeCards> s_Wanted {};

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        if (m_Pickers[i].root && !m_Pickers[i].selector)
            s_Wanted[i] = reinterpret_cast<uintptr_t>(m_Pickers[i].root);
    }

    std::array<size_t, fr::kPrestigeCards> s_Children {};
    std::array<std::string, fr::kPrestigeCards> s_Ids;
    size_t s_Elsewhere = 0;
    std::string s_ElsewhereText;
    const double s_Start = NowMs();

    const WalkStats s_Stats = VisitEntityTree(
        p_BrickRoot, fr::kPrestigeBrickRid,
        [&](const VisitedEntity& p_Visited) {
            uint64_t s_Id = 0;

            if (SafeTypeId(p_Visited.type, &s_Id))
                return false;

            const uintptr_t s_Parent = reinterpret_cast<uintptr_t>(p_Visited.parent);

            for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
                if (s_Wanted[i] != 0 && s_Wanted[i] == s_Parent) {
                    ++s_Children[i];

                    if (s_Children[i] <= 8)
                        s_Ids[i] += fmt::format("{}{}", s_Ids[i].empty() ? "" : ", ", IdText(s_Id));
                }
            }

            if (s_Id == fr::kPrestigeSelectorId && !fr::PickerOfSelector(s_Id, s_Parent, s_Wanted) &&
                ++s_Elsewhere <= 4) {
                s_ElsewhereText +=
                    fmt::format("{}{} under {}", s_ElsewhereText.empty() ? "" : "; ",
                                static_cast<const void*>(p_Visited.entity), EntityText(p_Visited.parent));
            }

            const std::optional<size_t> s_Picker = fr::PickerOfSelector(s_Id, s_Parent, s_Wanted);

            if (!s_Picker || m_Pickers[*s_Picker].selector)
                return false;

            Picker& s_Found = m_Pickers[*s_Picker];
            s_Found.selector = p_Visited.entity;
            s_Found.selectorFrom = "the entity walk of the brick";
            m_Watch[kSelector1 + *s_Picker].store(p_Visited.entity, std::memory_order_relaxed);

            if (!s_Found.factory)
                s_Found.factory = p_Visited.parentFactory;

            return true;
        },
        kWalkEntityCap);

    std::string s_Text;

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        if (s_Wanted[i] == 0)
            continue;

        s_Text += fmt::format("{}picker {}: {} ({} sub-entities seen under it{}{})", s_Text.empty() ? "" : "; ", i + 1,
                              m_Pickers[i].selector ? "selector FOUND" : "selector not found", s_Children[i],
                              s_Ids[i].empty() ? "" : ": ", s_Ids[i]);
    }

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: entity walk of the brick for the selectors: {} entities, {} faults, {}, {:.1f} ms: {}; "
          "selector id under other parents: {}",
          s_Stats.visited, s_Stats.faults, s_Stats.capped ? "capped" : "complete", NowMs() - s_Start, s_Text,
          s_Elsewhere ? fmt::format("{} ({})", s_Elsewhere, s_ElsewhereText) : std::string("none"));
}

// One line per picker: what it is and how its sub-entities are reached; when
// its selector is missing, its properties and the sub-entities its template
// factory lists (so the next log pins where m_Choices lives).
void PrestigeShuffle::LogPickerShape(size_t p_Index) {
    const Picker& s_Picker = m_Pickers[p_Index];

    if (!s_Picker.root)
        return;

    ZEntityBlueprintFactoryBase* s_Own = nullptr;
    const uint32_t s_OwnCode = ReadOwnBlueprintFactory(s_Picker.root, &s_Own);
    int64_t s_Count = 0;
    const uint32_t s_CountCode = ReadSubEntityCount(s_Picker.factory, &s_Count);

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: picker {} (card {}) {} instance {}: own blueprint factory {}; template factory {} ({}); "
          "selector {}",
          p_Index + 1, kCardLetters[p_Index], EntityText(s_Picker.root), static_cast<const void*>(s_Picker.root),
          s_OwnCode ? FaultText(s_OwnCode)
          : s_Own   ? fmt::format("{}", static_cast<const void*>(s_Own))
                    : "none",
          static_cast<const void*>(s_Picker.factory),
          s_CountCode ? FaultText(s_CountCode) : fmt::format("{} sub-entities", s_Count),
          s_Picker.selector ? fmt::format("{} instance {} via {}", EntityText(s_Picker.selector),
                                          static_cast<const void*>(s_Picker.selector), s_Picker.selectorFrom)
                            : std::string("MISSING"));

    if (s_Picker.selector)
        return;

    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: picker {} (card {}) properties: {}", p_Index + 1,
          kCardLetters[p_Index], PropertiesText(s_Picker.root));

    ZEntityBlueprintFactoryBase* s_Factory = s_Picker.factory ? s_Picker.factory : s_Own;

    if (!s_Factory || s_CountCode)
        return;

    std::string s_Text;
    const int s_Listed = static_cast<int>(std::min<int64_t>(s_Count, kMaxListedChildren));

    for (int i = 0; i < s_Listed; ++i) {
        ZEntityType** s_Sub = nullptr;
        ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;
        const uint32_t s_Code = ReadSubEntityAt(s_Factory, s_Picker.root, i, &s_Sub, &s_SubFactory);
        s_Text += fmt::format("{}#{} {}{}", s_Text.empty() ? "" : "; ", i,
                              s_Code ? FaultText(s_Code) : EntityText(s_Sub), s_SubFactory ? " [template]" : "");
    }

    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: picker {} (card {}) sub-entities ({} of {}): {}", p_Index + 1,
          kCardLetters[p_Index], s_Listed, s_Count, s_Text.empty() ? std::string("none") : s_Text);
}

// The selector's m_Choices of picker p_Index: its elements, each named by its
// choice entry. False (with *p_Why) when it cannot be used.
bool PrestigeShuffle::ReadPicker(size_t p_Index, std::string* p_Why) {
    Picker& s_Picker = m_Pickers[p_Index];

    if (!s_Picker.root || !s_Picker.selector) {
        *p_Why = s_Picker.root ? "no RandomSelector 1a029cfb6fe05b78 in it (not in the picker template's factory, "
                                 "its own, or the entity walk)"
                               : "picker not found";
        return false;
    }

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: picker {} (card {}) RandomSelector {} instance {} (via {}): {}", p_Index + 1,
          kCardLetters[p_Index], EntityText(s_Picker.selector), static_cast<const void*>(s_Picker.selector),
          s_Picker.selectorFrom, PropertiesText(s_Picker.selector));

    const PropertyLocation s_Location = LocateProperty(s_Picker.selector, "m_Choices");

    switch (s_Location.status) {
        case PropertyLocation::Status::Ok: break;
        case PropertyLocation::Status::Missing: *p_Why = "the selector has no m_Choices"; return false;
        case PropertyLocation::Status::HasGetter: *p_Why = "m_Choices has a getter"; return false;
        case PropertyLocation::Status::Fault: *p_Why = "m_Choices " + FaultText(s_Location.faultCode); return false;
    }

    if (!s_Location.isArray || s_Location.elementSize != kElementSize) {
        *p_Why = fmt::format("m_Choices is {} (element {}, {} bytes), not an array of {}-byte entity references",
                             s_Location.typeName[0] ? s_Location.typeName : "<unknown>",
                             s_Location.elementTypeName[0] ? s_Location.elementTypeName : "<unknown>",
                             s_Location.elementSize, kElementSize);
        return false;
    }

    struct {
        uintptr_t begin;
        uintptr_t end;
        uint64_t flags;
    } s_Array {};

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Array, sizeof(s_Array))) {
        *p_Why = "m_Choices array " + FaultText(s_Code);
        return false;
    }

    const auto s_Span =
        fr::ArrayElements(s_Location.address, s_Array.begin, s_Array.end, s_Array.flags, kElementSize, kMaxChoices);

    if (!s_Span || s_Span->isInline || s_Span->count == 0) {
        *p_Why = fmt::format("m_Choices implausible or inline (begin 0x{:X}, end 0x{:X}, flags 0x{:X})", s_Array.begin,
                             s_Array.end, s_Array.flags);
        return false;
    }

    std::vector<Bytes> s_Raw(s_Span->count);

    for (size_t i = 0; i < s_Span->count; ++i) {
        const uintptr_t s_Address = s_Span->begin + i * kElementSize;

        if (const uint32_t s_Code = ReadEngineBytes(s_Address, s_Raw[i].data(), kElementSize)) {
            *p_Why = fmt::format("m_Choices element {} {}", i, FaultText(s_Code));
            return false;
        }
    }

    s_Picker.property = s_Location.address;
    s_Picker.begin = s_Span->begin;
    s_Picker.count = s_Span->count;
    s_Picker.original.clear();
    std::string s_Named;
    std::string s_Unnamed;
    std::string s_NoWeight;
    std::set<int32_t> s_Seen;
    size_t s_Own = 0;        // elements whose owner chain passes through this picker
    size_t s_FromMemory = 0; // weights still 0 from an earlier write of this session

    for (size_t i = 0; i < s_Raw.size(); ++i) {
        Element s_Element;
        s_Element.bytes = s_Raw[i];
        std::memcpy(&s_Element.entity, s_Raw[i].data(), sizeof(s_Element.entity));

        if (!s_Element.entity) {
            s_Element.how = "null entity";
        }
        else {
            // The element is the entry's exposed RandomSelectorEntityChoice: the
            // entry itself, or an entity owned by it (some steps up).
            const auto s_Chain = OwnerChainIds(s_Element.entity);

            if (std::find(s_Chain.begin(), s_Chain.end(), fr::kPrestigePickerIds[p_Index]) != s_Chain.end())
                ++s_Own;

            if (!m_LoggedChain) {
                m_LoggedChain = true;
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle: picker {} (card {}) m_Choices element {} {}: owning chain {}; logical parent "
                      "chain {}",
                      p_Index + 1, kCardLetters[p_Index], i, static_cast<const void*>(s_Element.entity),
                      ChainText(s_Element.entity, true), ChainText(s_Element.entity, false));
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle: picker {} (card {}) m_Choices element {} {} properties: {}", p_Index + 1,
                      kCardLetters[p_Index], i, EntityText(s_Element.entity), PropertiesText(s_Element.entity));
            }

            s_Element.id = s_Chain[0];

            if (const auto s_Found = fr::PrestigeChoiceInChain(s_Chain)) {
                s_Element.objective = s_Found->choice->objective;
                s_Element.depth = s_Found->depth;
                s_Element.how = s_Found->depth == 0
                                    ? std::string("its own id")
                                    : fmt::format("its owning entity {} (element {}, {} up)",
                                                  IdText(s_Chain[s_Found->depth]), IdText(s_Chain[0]), s_Found->depth);
            }
            else {
                std::string s_Ids;

                for (const uint64_t s_Id : s_Chain) {
                    if (s_Id == 0)
                        break;

                    s_Ids += (s_Ids.empty() ? "" : " -> ") + IdText(s_Id);
                }

                s_Element.how =
                    fmt::format("not named (owner chain {})", s_Ids.empty() ? std::string("unreadable") : s_Ids);
            }

            // m_Weight's storage: the shuffle writes it, so it must be a plain float32.
            const PropertyLocation s_Weight = LocateProperty(s_Element.entity, "m_Weight");
            const bool s_Plain = s_Weight.status == PropertyLocation::Status::Ok && !s_Weight.isArray &&
                                 s_Weight.typeSize == sizeof(float);

            if (const auto s_Value = s_Plain ? ReadFloat(s_Weight.address) : std::nullopt) {
                s_Element.weightAddress = s_Weight.address;
                s_Element.weight = *s_Value;

                // A brick kept loaded across a scene clear may still hold an earlier write.
                if (const auto s_It = m_SessionWeights.find(s_Weight.address); s_It != m_SessionWeights.end()) {
                    if (*s_Value == 0.0f) {
                        s_Element.weight = s_It->second;
                        ++s_FromMemory;

                        // Written back with the next restore, and read as the original until then.
                        if (std::none_of(s_Picker.zeroed.begin(), s_Picker.zeroed.end(),
                                         [&](const Zeroed& z) { return z.address == s_Weight.address; }))
                            s_Picker.zeroed.push_back(Zeroed {s_Weight.address, s_It->second});
                    }
                    else {
                        m_SessionWeights.erase(s_It);
                    }
                }
            }
            else if (s_NoWeight.empty()) {
                const char* s_What = s_Weight.status == PropertyLocation::Status::Missing     ? "missing"
                                     : s_Weight.status == PropertyLocation::Status::HasGetter ? "has a getter"
                                     : s_Weight.status == PropertyLocation::Status::Fault     ? "faulted"
                                                                                          : "not a readable float32";
                s_NoWeight =
                    fmt::format("element {} m_Weight {} ({}, {} bytes)", i, s_What,
                                s_Weight.typeName[0] ? s_Weight.typeName : "<unknown type>", s_Weight.typeSize);
            }
        }

        if (!s_Element.entity && s_NoWeight.empty())
            s_NoWeight = fmt::format("element {} is a null entity", i);

        const std::string s_WeightText = s_Element.weight ? fmt::format(" w{}", *s_Element.weight) : " w?";

        if (s_Element.objective) {
            s_Seen.insert(*s_Element.objective);
            s_Named += fmt::format("{}#{} {}{}", s_Named.empty() ? "" : "; ", i,
                                   fr::PrestigeChoiceText(*s_Element.objective), s_WeightText);
        }
        else {
            s_Unnamed += fmt::format("{}#{} {}{}", s_Unnamed.empty() ? "" : "; ", i, s_Element.how, s_WeightText);
        }

        s_Picker.original.push_back(std::move(s_Element));
    }

    // Table entries the array does not offer: present in the template or not.
    std::string s_Absent;

    for (const fr::PrestigeChoice& s_Choice : fr::kPrestigeChoices) {
        if (s_Seen.contains(s_Choice.objective))
            continue;

        const SubEntityLookup s_Entry = LookUpSubEntity(s_Picker.root, s_Picker.factory, s_Choice.entryId);
        s_Absent += fmt::format("{}{} ({})", s_Absent.empty() ? "" : ", ", s_Choice.objective,
                                s_Entry.faultStep ? LookupText(s_Entry)
                                : s_Entry.entity  ? "entry exists"
                                                  : "no such entry");
    }

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: picker {} (card {}): m_Choices ({} of {}) at 0x{:X}: {} elements; named by the first "
          "element: {}; {} of {} elements are choices of this picker's own instance (owner chain through {}){}",
          p_Index + 1, kCardLetters[p_Index], s_Location.typeName,
          s_Location.elementTypeName[0] ? s_Location.elementTypeName : "?", s_Span->begin, s_Span->count,
          s_Picker.original.empty() ? std::string("-") : s_Picker.original.front().how, s_Own, s_Raw.size(),
          IdText(fr::kPrestigePickerIds[p_Index]),
          s_FromMemory ? fmt::format("; {} weights still held this session's earlier write (originals from memory)",
                                     s_FromMemory)
                       : std::string());
    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: picker {} (card {}) offers {} objectives: {}", p_Index + 1,
          kCardLetters[p_Index], s_Seen.size(), s_Named.empty() ? std::string("none") : s_Named);

    if (!s_Unnamed.empty())
        LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: picker {} (card {}) elements not named: {}", p_Index + 1,
              kCardLetters[p_Index], s_Unnamed);

    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: picker {} (card {}) does not offer: {}", p_Index + 1,
          kCardLetters[p_Index], s_Absent.empty() ? std::string("-") : s_Absent);

    if (s_Seen.empty()) {
        *p_Why = "no element of m_Choices could be named";
        return false;
    }

    if (!s_NoWeight.empty()) {
        *p_Why = "the weights cannot be forced: " + s_NoWeight;
        return false;
    }

    return true;
}

bool PrestigeShuffle::ArrayUnmoved(const Picker& p_Picker, std::string* p_Why) const {
    struct {
        uintptr_t begin;
        uintptr_t end;
        uint64_t flags;
    } s_Array {};

    if (const uint32_t s_Code = ReadEngineBytes(p_Picker.property, &s_Array, sizeof(s_Array))) {
        *p_Why = "m_Choices " + FaultText(s_Code);
        return false;
    }

    const auto s_Span =
        fr::ArrayElements(p_Picker.property, s_Array.begin, s_Array.end, s_Array.flags, kElementSize, kMaxChoices);

    if (!s_Span || s_Span->begin != p_Picker.begin || s_Span->count != p_Picker.count) {
        *p_Why = fmt::format("m_Choices moved or resized (now begin 0x{:X} end 0x{:X}; read at 0x{:X} with {} "
                             "elements)",
                             s_Array.begin, s_Array.end, p_Picker.begin, p_Picker.count);
        return false;
    }

    return true;
}

// Picker p_Index's elements with their weights now: the original for a weight
// the mod set to 0 that still reads 0.
bool PrestigeShuffle::LiveSlots(size_t p_Index, std::vector<fr::PrestigeSlot>* p_Out, std::string* p_Why) const {
    const Picker& s_Picker = m_Pickers[p_Index];
    p_Out->clear();

    for (size_t k = 0; k < s_Picker.original.size(); ++k) {
        const Element& s_Element = s_Picker.original[k];
        const std::optional<float> s_Now = ReadFloat(s_Element.weightAddress);

        if (!s_Now) {
            *p_Why = fmt::format("picker {} element {}: m_Weight at 0x{:X} not readable", p_Index + 1, k,
                                 s_Element.weightAddress);
            return false;
        }

        float s_Weight = *s_Now;

        if (s_Weight == 0.0f) {
            const auto s_It = std::find_if(s_Picker.zeroed.begin(), s_Picker.zeroed.end(),
                                           [&](const Zeroed& z) { return z.address == s_Element.weightAddress; });

            if (s_It != s_Picker.zeroed.end())
                s_Weight = s_It->original;
        }

        p_Out->push_back(fr::PrestigeSlot {s_Element.objective, s_Weight});
    }

    return true;
}

void PrestigeShuffle::Apply(const char* p_Trigger) {
    m_LastTrigger = p_Trigger;
    m_Picked = true;

    if (!m_Ready) {
        if (!m_WarnedNotReady) {
            m_WarnedNotReady = true;
            LogTo(fr::LogTopic::PrestigeCards,
                  "prestige shuffle: {}: the pickers were not read; the game picks the cards (vanilla)", p_Trigger);
        }

        return;
    }

    if (m_GaveUp) {
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: {}: the guard ran in this visit; the game picks the cards (vanilla)", p_Trigger);
        return;
    }

    const std::optional<int32_t> s_Seed = m_Seed ? m_Seed() : std::nullopt;

    if (!s_Seed) {
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: {}: campaign seed unknown; the game picks the cards (vanilla)", p_Trigger);
        RestoreAll("no campaign seed");
        return;
    }

    // Every picker's choices and their weights as the selectors see them now.
    std::array<std::vector<fr::PrestigeSlot>, fr::kPrestigeCards> s_Slots;
    std::string s_Changed;
    size_t s_ChangedCount = 0;

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        std::string s_Why;

        if (!ArrayUnmoved(m_Pickers[i], &s_Why) || !LiveSlots(i, &s_Slots[i], &s_Why)) {
            LogTo(fr::LogTopic::PrestigeCards,
                  "prestige shuffle: {}: picker {} (card {}): {}; the game picks the cards (vanilla)", p_Trigger, i + 1,
                  kCardLetters[i], s_Why);
            RestoreAll("the pickers changed since they were read");
            return;
        }

        for (size_t k = 0; k < s_Slots[i].size(); ++k) {
            const std::optional<float> s_Load = m_Pickers[i].original[k].weight;

            if (s_Load && *s_Load != s_Slots[i][k].weight && ++s_ChangedCount <= 24) {
                s_Changed += fmt::format("{}picker {} #{} {} w{} -> w{}", s_Changed.empty() ? "" : "; ", i + 1, k,
                                         s_Slots[i][k].objective ? fr::PrestigeChoiceText(*s_Slots[i][k].objective)
                                                                 : std::string("(not named)"),
                                         *s_Load, s_Slots[i][k].weight);
            }
        }
    }

    // Evidence for how the game rules choices out: weights it changed since the load.
    LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: {}: weights changed by the game since the load: {}",
          p_Trigger, s_ChangedCount ? fmt::format("{} ({})", s_ChangedCount, s_Changed) : std::string("none"));

    const fr::PrestigePickState s_Before = m_State;
    m_State = fr::AdvancePrestigePick(m_State, *s_Seed);
    const std::vector<int32_t> s_Available = fr::PrestigeObjectivesOffered(s_Slots);

    // Objectives every picker lists but not all with a weight above 0 now.
    std::string s_ZeroWeight;

    for (const fr::PrestigeChoice& s_Choice : fr::kPrestigeChoices) {
        const bool s_Listed = std::all_of(s_Slots.begin(), s_Slots.end(), [&](const std::vector<fr::PrestigeSlot>& s) {
            return std::any_of(s.begin(), s.end(),
                               [&](const fr::PrestigeSlot& e) { return e.objective == s_Choice.objective; });
        });

        if (s_Listed && std::find(s_Available.begin(), s_Available.end(), s_Choice.objective) == s_Available.end())
            s_ZeroWeight += fmt::format("{}{}", s_ZeroWeight.empty() ? "" : ", ", s_Choice.objective);
    }

    const auto s_Cards = fr::ChoosePrestigeCards(m_State, s_Available, m_Rejected, m_Exclude);

    if (!s_Cards) {
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: {}: campaign seed {}, pick #{}: no three of the {} objectives all pickers offer fit "
              "the cards, even with objectives from prestige_exclude (card A takes only a Silent Assassin or Timed "
              "objective; none that its card's picker rejected in this visit); the game picks the cards (vanilla)",
              p_Trigger, *s_Seed, m_State.counter, s_Available.size());

        if (!(m_State == s_Before))
            SaveState();

        RestoreAll("nothing to draw");
        return;
    }

    const char* s_Reason = s_Before.seed != *s_Seed                      ? "a new campaign"
                           : m_State.cards && *m_State.cards == *s_Cards ? "the cards kept for this pick"
                           : m_State.cards ? "drawn again: a kept card is no longer offered or "
                                             "its picker rejected it"
                                           : "drawn for this pick";
    m_State.cards = s_Cards;

    if (!(m_State == s_Before))
        SaveState();

    std::string s_RejectedText;

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        for (const int32_t s_Objective : m_Rejected[i])
            s_RejectedText +=
                fmt::format("{}card {} {}", s_RejectedText.empty() ? "" : ", ", kCardLetters[i], s_Objective);
    }

    std::string s_Excluded;

    for (const int32_t s_Objective : s_Available) {
        if (const fr::PrestigeChoice* s_Choice = fr::PrestigeChoiceByObjective(s_Objective);
            !fr::PrestigeChoiceDrawable(*s_Choice, m_Exclude))
            s_Excluded += fmt::format("{}{} ({})", s_Excluded.empty() ? "" : ", ", s_Objective,
                                      s_Choice->excluded ? s_Choice->excluded : fr::kExcludedSetting);
    }

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: {}: campaign seed {}, pick #{}{} ({}): {}; from the {} objectives all pickers offer "
          "with a weight above 0 (never drawn: {}; weight 0 in a picker now: {}; rejected by a card's picker in this "
          "visit: {})",
          p_Trigger, *s_Seed, m_State.counter, m_State.taken ? ", a card taken" : "", s_Reason, CardsText(*s_Cards),
          s_Available.size(), s_Excluded.empty() ? std::string("-") : s_Excluded,
          s_ZeroWeight.empty() ? std::string("-") : s_ZeroWeight,
          s_RejectedText.empty() ? std::string("-") : s_RejectedText);

    // Cards filled from prestige_exclude: the allowed ones could not fill every card.
    if (const size_t s_Outside = fr::PrestigeCardsOutside(*s_Cards, m_Exclude)) {
        std::string s_Fills;

        for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
            if (m_Exclude.contains((*s_Cards)[i]))
                s_Fills += fmt::format("{}card {} {}", s_Fills.empty() ? "" : ", ", kCardLetters[i],
                                       fr::PrestigeChoiceText((*s_Cards)[i]));
        }

        LogTo(
            fr::LogTopic::PrestigeCards,
            "prestige shuffle: OUTSIDE THE LIST: {} of the three cards hold an objective from prestige_exclude ({}): "
            "every placement of the allowed objectives was tried, and no allowed objective fits {} (card A takes only "
            "a Silent Assassin or Timed objective, at most one card per category{}); the rest are from the list",
            s_Outside, s_Fills, s_Outside == 1 ? "that card" : "those cards",
            s_RejectedText.empty() ? "" : ", minus what a card's picker rejected in this visit");
    }

    if (m_Cards && *m_Cards == *s_Cards && WeightsHold(*s_Cards)) {
        LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: the weights already hold this draw");
    }
    else {
        RestoreAll("a new draw");
        std::string s_Why;

        if (!WriteAll(*s_Cards, s_Slots, &s_Why)) {
            LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: write failed: {}; the game picks the cards (vanilla)",
                  s_Why);
            RestoreAll("the write failed");
            return;
        }
    }

    m_Cards = s_Cards;
    m_Shown = {};
    m_Emitted.clear();
    m_ReportPending = true;
    m_ReportIn = 1.0;
    m_GuardPending = true;
    m_GuardElapsed = 0.0;

    for (Picker& s_Picker : m_Pickers) {
        s_Picker.selectorPicks = 0;
        s_Picker.selectorOutputs = 0;
        s_Picker.pickerOutputs = 0;
        s_Picker.pickDone = false;
        s_Picker.pickFailed = false;
    }
}

// Every picker forced to its card, every weight the mod set to 0 still 0.
bool PrestigeShuffle::WeightsHold(const std::array<int32_t, fr::kPrestigeCards>& p_Cards) const {
    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        const Picker& s_Picker = m_Pickers[i];

        if (s_Picker.forced != p_Cards[i])
            return false;

        for (const Zeroed& s_Zeroed : s_Picker.zeroed) {
            if (ReadFloat(s_Zeroed.address) != 0.0f)
                return false;
        }
    }

    return true;
}

// Sets m_Weight 0 on every choice of picker N but card N's objective's.
// m_Choices is not touched.
bool PrestigeShuffle::WriteAll(const std::array<int32_t, fr::kPrestigeCards>& p_Cards,
                               const std::array<std::vector<fr::PrestigeSlot>, fr::kPrestigeCards>& p_Slots,
                               std::string* p_Why) {
    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        Picker& s_Picker = m_Pickers[i];
        const auto s_Target = fr::ForcedPrestigeWeights(p_Slots[i], p_Cards[i]);

        if (!s_Target) {
            *p_Why = fmt::format("picker {} has no element of {} with a weight above 0", i + 1,
                                 fr::PrestigeChoiceText(p_Cards[i]));
            return false;
        }

        s_Picker.forced = p_Cards[i];
        size_t s_Kept = 0;
        size_t s_AlreadyZero = 0;
        size_t s_Holding = 0;

        for (size_t k = 0; k < s_Target->size(); ++k) {
            const uintptr_t s_Address = s_Picker.original[k].weightAddress;
            const float s_Now = p_Slots[i][k].weight;

            if ((*s_Target)[k] == s_Now) {
                (*s_Target)[k] > 0.0f ? ++s_Kept : ++s_AlreadyZero;
                continue;
            }

            // An entity listed twice is written once.
            if (std::any_of(s_Picker.zeroed.begin(), s_Picker.zeroed.end(),
                            [&](const Zeroed& z) { return z.address == s_Address; }))
                continue;

            const float s_Zero = 0.0f;

            if (const DWORD s_Code = WriteEngineBytes(s_Address, &s_Zero, sizeof(s_Zero))) {
                *p_Why = fmt::format("picker {} element {} m_Weight {}", i + 1, k, FaultText(s_Code));
                return false;
            }

            s_Picker.zeroed.push_back(Zeroed {s_Address, s_Now});
            m_SessionWeights[s_Address] = s_Now;

            if (ReadFloat(s_Address) == 0.0f)
                ++s_Holding;
        }

        LogTo(
            fr::LogTopic::PrestigeCards,
            "prestige shuffle: picker {} (card {}): forced to {}: m_Weight 0 written on {} choices (read back {}), {} "
            "already 0, {} element(s) of the drawn objective kept; m_Choices untouched",
            i + 1, kCardLetters[i], fr::PrestigeChoiceText(p_Cards[i]), s_Picker.zeroed.size(), s_Holding,
            s_AlreadyZero, s_Kept);

        if (s_Holding != s_Picker.zeroed.size()) {
            *p_Why =
                fmt::format("picker {}: only {} of {} weights read back 0", i + 1, s_Holding, s_Picker.zeroed.size());
            return false;
        }
    }

    return true;
}

// Writes back every weight the mod set to 0 that still reads 0 (one the game
// has set since is left as the game set it).
void PrestigeShuffle::RestoreAll(const char* p_Why) {
    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        Picker& s_Picker = m_Pickers[i];

        if (!s_Picker.forced && s_Picker.zeroed.empty())
            continue;

        size_t s_Restored = 0;
        size_t s_Changed = 0;
        size_t s_Faults = 0;

        for (const Zeroed& s_Zeroed : s_Picker.zeroed) {
            const std::optional<float> s_Now = ReadFloat(s_Zeroed.address);

            if (!s_Now) {
                ++s_Faults;
                continue;
            }

            if (*s_Now != 0.0f)
                ++s_Changed;
            else if (WriteEngineBytes(s_Zeroed.address, &s_Zeroed.original, sizeof(s_Zeroed.original)))
                ++s_Faults;
            else
                ++s_Restored;

            m_SessionWeights.erase(s_Zeroed.address);
        }

        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: picker {} (card {}): {}: {} of {} weights written back{}{}", i + 1, kCardLetters[i],
              p_Why, s_Restored, s_Picker.zeroed.size(),
              s_Changed ? fmt::format(", {} set by the game since (left)", s_Changed) : std::string(),
              s_Faults ? fmt::format(", {} FAULTED", s_Faults) : std::string());
        s_Picker.zeroed.clear();
        s_Picker.forced.reset();
    }
}

bool PrestigeShuffle::TakeLine() {
    if (m_PinLines >= kMaxPinLines)
        return false;

    if (++m_PinLines == kMaxPinLines)
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: {} pin lines logged in this visit; further pins are not logged", kMaxPinLines);

    return true;
}

void PrestigeShuffle::NoteObjective(const char* p_Where, int32_t p_Value) {
    std::string s_Match;

    if (p_Value == 0) {
        s_Match = "reset";
    }
    else if (!m_Cards) {
        s_Match = "no draw by the mod in this visit";
    }
    else {
        const auto s_It = std::find(m_Cards->begin(), m_Cards->end(), p_Value);
        s_Match = s_It == m_Cards->end() ? std::string("NOT one of the drawn cards")
                                         : fmt::format("drawn card {}", kCardLetters[s_It - m_Cards->begin()]);

        if (m_Taking) {
            const std::optional<int32_t> s_Shown = m_Shown[*m_Taking];
            s_Match += !s_Shown ? fmt::format("; card {} taken, what it shows was not seen", kCardLetters[*m_Taking])
                       : *s_Shown == p_Value ? fmt::format("; card {} taken, as it shows", kCardLetters[*m_Taking])
                                             : fmt::format("; card {} taken, it SHOWS {}", kCardLetters[*m_Taking],
                                                           fr::PrestigeChoiceText(*s_Shown));
        }
    }

    const std::string s_Line =
        fmt::format("{} {} ({})", p_Where, p_Value == 0 ? std::string("0") : fr::PrestigeChoiceText(p_Value), s_Match);

    if (m_Emitted.size() < 32)
        m_Emitted.push_back(s_Line);

    if (TakeLine())
        LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle pin: {}", s_Line);
}

// A choice entry's display poll during a pick: the card of the picker that
// owns the entry shows that objective.
void PrestigeShuffle::NoteShown(ZEntityType** p_Poll, int32_t p_Value) {
    const auto s_Chain = OwnerChainIds(p_Poll);

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        if (std::find(s_Chain.begin(), s_Chain.end(), fr::kPrestigePickerIds[i]) == s_Chain.end())
            continue;

        m_Shown[i] = p_Value;

        if (TakeLine())
            LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle pin: card {} shows {}{}", kCardLetters[i],
                  fr::PrestigeChoiceText(p_Value),
                  !m_Pickers[i].forced              ? std::string()
                  : *m_Pickers[i].forced == p_Value ? " (the forced card)"
                                                    : fmt::format(" (NOT the forced {}: its picker rejected it)",
                                                                  fr::PrestigeChoiceText(*m_Pickers[i].forced)));

        return;
    }
}

// An ObjectiveId while a card is taken: what the card shows is sent on (to
// Setpiece_evergreen_PrestigeObjective_Unique and the CPD), whatever entry the
// picker's Poll reached.
void PrestigeShuffle::CorrectTake(const ZObjectRef& p_Data, uint64_t p_Id, int32_t p_Value) {
    int32_t s_Value = p_Value;

    if (m_Taking) {
        if (const std::optional<int32_t> s_Shown = fr::PrestigeTakeCorrection(m_Shown[*m_Taking], p_Value)) {
            if (const DWORD s_Code = SafeWriteInt32Payload(&p_Data, *s_Shown)) {
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle: card {} taken: ObjectiveId of {} {} but the card shows {}; rewriting it: {}",
                      kCardLetters[*m_Taking], IdText(p_Id), fr::PrestigeChoiceText(p_Value),
                      fr::PrestigeChoiceText(*s_Shown), FaultText(s_Code));
            }
            else {
                s_Value = *s_Shown;
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle: card {} taken: ObjectiveId of {} {} -> sent as {} (what the card shows)",
                      kCardLetters[*m_Taking], IdText(p_Id), fr::PrestigeChoiceText(p_Value),
                      fr::PrestigeChoiceText(*s_Shown));
            }
        }
    }

    NoteObjective(fmt::format("ObjectiveId of {}", IdText(p_Id)).c_str(), s_Value);
}

void PrestigeShuffle::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    if (!m_PinsOn.load(std::memory_order_acquire) || !p_Entity.m_pObj)
        return;

    Contained(kTag, "output pin", [&] {
        if (p_PinId == kPinObjectiveId) {
            if (const auto s_Value = Int32Payload(p_Data)) {
                uint64_t s_Id = 0;
                SafeEntityId(p_Entity.m_pObj, &s_Id);
                std::lock_guard s_Lock(m_Mutex);
                CorrectTake(p_Data, s_Id, *s_Value);
            }
        }
        else if (p_PinId == kPinGetValue) {
            uint64_t s_Id = 0;

            if (!SafeEntityId(p_Entity.m_pObj, &s_Id) && (s_Id == kDisplayPollId || s_Id == kTakePollId)) {
                if (const auto s_Value = Int32Payload(p_Data)) {
                    std::lock_guard s_Lock(m_Mutex);

                    if (s_Id == kDisplayPollId)
                        NoteShown(p_Entity.m_pObj, *s_Value);
                    else if (m_Taking && TakeLine())
                        LogTo(fr::LogTopic::PrestigeCards,
                              "prestige shuffle pin: card {} taken: its picker's Poll reached the entry of {}{}",
                              kCardLetters[*m_Taking], fr::PrestigeChoiceText(*s_Value),
                              m_Shown[*m_Taking] && *m_Shown[*m_Taking] != *s_Value
                                  ? fmt::format(", NOT the shown {} (corrected below)",
                                                fr::PrestigeChoiceText(*m_Shown[*m_Taking]))
                                  : std::string());
                }
            }
        }

        if (const std::optional<size_t> s_Role = WatchedRole(p_Entity.m_pObj)) {
            std::lock_guard s_Lock(m_Mutex);

            // A reset between the scan and the lock forgot it.
            if (m_Watch[*s_Role].load(std::memory_order_relaxed) == p_Entity.m_pObj)
                Handle(*s_Role, p_PinId, p_Data, false);
        }
    });
}

void PrestigeShuffle::OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    if (!m_PinsOn.load(std::memory_order_acquire) || !p_Entity.m_pObj)
        return;

    Contained(kTag, "input pin", [&] {
        if (const std::optional<size_t> s_Role = WatchedRole(p_Entity.m_pObj)) {
            std::lock_guard s_Lock(m_Mutex);

            if (m_Watch[*s_Role].load(std::memory_order_relaxed) == p_Entity.m_pObj)
                Handle(*s_Role, p_PinId, p_Data, true);
        }
    });
}

void PrestigeShuffle::ClearWatch() {
    for (auto& s_Entity : m_Watch)
        s_Entity.store(nullptr, std::memory_order_relaxed);
}

std::optional<size_t> PrestigeShuffle::WatchedRole(ZEntityType** p_Entity) const {
    for (size_t i = 0; i < kRoleCount; ++i) {
        if (m_Watch[i].load(std::memory_order_relaxed) == p_Entity)
            return i;
    }

    return std::nullopt;
}

void PrestigeShuffle::Handle(size_t p_Role, uint32_t p_PinId, const ZObjectRef& p_Data, bool p_Input) {
    const std::optional<int32_t> s_Int = Int32Payload(p_Data);
    const char* s_Way = p_Input ? "input" : "output";

    switch (p_Role) {
        case kReturnPipe:
            if (!p_Input && p_PinId == kPinOut) {
                m_FromReturn = true;
                LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle pin: {} .Out (a return to the safehouse)",
                      RoleName(p_Role));
            }
            return;

        case kRepickFork:
            if (!p_Input && p_PinId == kPinOut06) {
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle pin: {} .Out06 -> SignalFork_Void_04: {}; the game resets its pick and the "
                      "pickers run again{}",
                      RoleName(p_Role),
                      m_Redrawing    ? "the mod's redraw"
                      : m_FromReturn ? "after a return to the safehouse"
                                     : "the laptop opened",
                      m_Redrawing ? "" : ": the same cards");
                m_FromReturn = false;
                m_Redrawing = false;
            }
            return;

        case kPickFork:
            if (!p_Input && p_PinId == kPinOut02)
                Apply("SignalFork_Void_04 .Out02 (-> Picker1 .Pick)");
            return;

        case kTakeFork1:
        case kTakeFork2:
        case kTakeFork3:
            if (!p_Input && p_PinId == kPinOut02) {
                const size_t s_Card = p_Role - kTakeFork1;
                m_Taking = s_Card; // until the next frame: the Poll runs within this signal

                if (m_State.seed != 0 && !m_State.taken) {
                    m_State.taken = true;
                    SaveState();
                }

                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle pin: card {} taken ({} .Out02 -> Picker{} .Poll); the mod drew {}, the card "
                      "shows "
                      "{}; the cards stay until a mission starts with it",
                      kCardLetters[s_Card], RoleName(p_Role), s_Card + 1,
                      m_Cards ? fr::PrestigeChoiceText((*m_Cards)[s_Card]) : std::string("nothing in this visit"),
                      m_Shown[s_Card] ? fr::PrestigeChoiceText(*m_Shown[s_Card]) : std::string("(not seen)"));
            }
            return;

        case kUnique:
        case kSetCpd:
            if (s_Int)
                NoteObjective(fmt::format("{} {} {}", RoleName(p_Role), s_Way, PinText(p_PinId)).c_str(), *s_Int);

            // The game's pick reset (the laptop opened again): no card taken now.
            if (p_Role == kUnique && !p_Input && s_Int == 0 && m_State.taken) {
                m_State.taken = false;
                SaveState();
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle: the game reset its pick: no card taken now (the cards stay)");
            }
            return;

        case kPicker1:
        case kPicker2:
        case kPicker3: {
            Picker& s_Picker = m_Pickers[p_Role - kPicker1];

            if (p_Input && p_PinId == kPinPick && p_Role == kPicker1)
                Apply("Picker1 .Pick (input)");

            if (!p_Input) {
                ++s_Picker.pickerOutputs;
                s_Picker.pickDone = s_Picker.pickDone || p_PinId == kPinOnPickDone;
            }

            if (s_Int && p_PinId != kPinObjectiveId)
                NoteObjective(fmt::format("{} {} {}", RoleName(p_Role), s_Way, PinText(p_PinId)).c_str(), *s_Int);
            else if (!s_Int && TakeLine())
                LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle pin: {} {} {}", RoleName(p_Role), s_Way,
                      PinText(p_PinId));

            return;
        }

        case kSelector1:
        case kSelector2:
        case kSelector3: {
            Picker& s_Picker = m_Pickers[p_Role - kSelector1];

            if (p_Input && p_PinId == kPinPick)
                ++s_Picker.selectorPicks;
            else if (!p_Input)
                ++s_Picker.selectorOutputs;

            if (TakeLine())
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle pin: {} {} {}{} (since the draw: {} picks, {} outputs)", RoleName(p_Role),
                      s_Way, PinText(p_PinId), s_Int ? fmt::format(" int32 {}", *s_Int) : std::string(),
                      s_Picker.selectorPicks, s_Picker.selectorOutputs);

            // Acted on by the guard at the next tick, outside the game's pick.
            if (!p_Input && p_PinId == kPinOnPickFailed && m_GuardPending && !s_Picker.pickFailed) {
                s_Picker.pickFailed = true;
                LogTo(
                    fr::LogTopic::PrestigeCards,
                    "prestige shuffle: PICK FAILED: {} signaled OnPickFailed after the draw (forced to {}): it had no "
                    "choice with a weight above 0 it could pick; the guard writes the weights back and lets the game "
                    "pick",
                    RoleName(p_Role),
                    s_Picker.forced ? fr::PrestigeChoiceText(*s_Picker.forced) : std::string("nothing"));
            }

            if (m_GuardPending && !s_Picker.pickFailed &&
                (s_Picker.selectorPicks > kMaxSelectorPicks || s_Picker.selectorOutputs > kMaxSelectorOutputs)) {
                s_Picker.pickFailed = true;
                LogTo(fr::LogTopic::PrestigeCards,
                      "prestige shuffle: LOOP GUARD: {} picked {} times / signaled {} outputs since the draw; the game "
                      "may be rejecting the forced choice",
                      RoleName(p_Role), s_Picker.selectorPicks, s_Picker.selectorOutputs);
            }

            return;
        }

        default: return;
    }
}

// After a forced draw: once every picker is done, the original weights go
// back; if a selector failed, or not every picker is done within
// kGuardSeconds, they go back too, the rest of the visit is vanilla and
// Picker1 .Pick is signaled once so the game picks the vanilla cards.
void PrestigeShuffle::Guard(double p_Dt) {
    if (!m_GuardPending)
        return;

    m_GuardElapsed += p_Dt;
    std::array<fr::PrestigePickerProgress, fr::kPrestigeCards> s_Progress {};
    std::string s_State;

    for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
        s_Progress[i] = {m_Pickers[i].pickDone, m_Pickers[i].pickFailed};
        s_State += fmt::format("{}picker {}: {}", i ? ", " : "", i + 1,
                               m_Pickers[i].pickFailed ? "PICK FAILED"
                               : m_Pickers[i].pickDone ? "done"
                                                       : "no OnPickDone");
    }

    switch (fr::PrestigeGuardVerdict(s_Progress, m_GuardElapsed, kGuardSeconds)) {
        case fr::PrestigeGuard::Wait: return;

        case fr::PrestigeGuard::Done: {
            m_GuardPending = false;
            std::string s_Shown;
            bool s_Rejected = false;

            for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
                const Picker& s_Picker = m_Pickers[i];
                s_Shown += fmt::format("{}card {} shows {}", i ? ", " : "", kCardLetters[i],
                                       m_Shown[i] ? fr::PrestigeChoiceText(*m_Shown[i]) : std::string("(not seen)"));

                if (!fr::PrestigeCardRejected(s_Picker.forced, m_Shown[i]))
                    continue;

                s_Rejected = true;
                s_Shown += fmt::format(" (REJECTED the forced {}", fr::PrestigeChoiceText(*s_Picker.forced));

                // Why: the forced choice's condition in this picker, read now.
                for (const Element& s_Element : s_Picker.original) {
                    if (s_Element.objective == s_Picker.forced && s_Element.entity) {
                        s_Shown += "; its " + ReadChoiceCondition(s_Element.entity).text;
                        break;
                    }
                }

                s_Shown += ")";

                // A picker's condition can depend on what the earlier cards
                // show (card A falling back to 101 Silent Assassin keeps card C
                // from showing a forced 101). Counted only when
                // every earlier card showed the objective forced on it.
                bool s_EarlierAsForced = true;

                for (size_t j = 0; j < i; ++j)
                    s_EarlierAsForced = s_EarlierAsForced && m_Pickers[j].forced && m_Shown[j] == m_Pickers[j].forced;

                if (!s_EarlierAsForced) {
                    s_Shown += " (not counted against this card: an earlier card did not show its forced objective)";
                    continue;
                }

                std::vector<int32_t>& s_List = m_Rejected[i];

                if (std::find(s_List.begin(), s_List.end(), *s_Picker.forced) == s_List.end())
                    s_List.push_back(*s_Picker.forced);
            }

            LogTo(fr::LogTopic::PrestigeCards,
                  "prestige shuffle: guard: all three pickers picked ({:.2f} s): {}; {}; the weights stay forced until "
                  "the "
                  "scene is cleared",
                  m_GuardElapsed, s_State, s_Shown);

            if (s_Rejected)
                Redraw();

            return;
        }

        case fr::PrestigeGuard::Failed: break;
    }

    m_GuardPending = false;
    m_GaveUp = true;
    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: GUARD TRIPPED {:.2f} s after the draw ({}): writing the original weights back and "
          "letting the game pick the vanilla cards; vanilla for the rest of this visit",
          m_GuardElapsed, s_State);
    RestoreAll("guard");

    // Through the game's reset, so the cards' text and takes follow the vanilla pick too.
    if (ZEntityType** const s_Reset = m_Watch[kRepickFork].load(std::memory_order_relaxed)) {
        m_Redrawing = true;

        if (const DWORD s_Code = SendInputPin(s_Reset, kPinIn)) {
            m_Redrawing = false;
            Log("prestige shuffle: GUARD: signaling SignalFork_Void .In {}", FaultText(s_Code));
        }
        else {
            LogTo(fr::LogTopic::PrestigeCards,
                  "prestige shuffle: GUARD: SignalFork_Void .In signaled (the game's reset, then its pick)");
            return;
        }
    }

    ZEntityType** const s_Picker1 = m_Pickers[0].root;

    if (!s_Picker1) {
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: GUARD: Picker1 not found; the cards stay as they are until the next pick");
        return;
    }

    if (const DWORD s_Code = SendInputPin(s_Picker1, kPinPick))
        Log("prestige shuffle: GUARD: signaling Picker1 .Pick {}", FaultText(s_Code));
    else
        LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle: GUARD: Picker1 .Pick signaled; picker outputs follow as "
                                           "\"prestige shuffle pin:\" lines");
}

// A card's picker rejected its forced card: the cards are picked again
// through the game's reset (SignalFork_Void .In, as the laptop opening does),
// and the draw at SignalFork_Void_04 .Out02 leaves the rejected objective off
// that card. Not through SignalFork_Void_04 .In alone: that re-pick moves the
// laptop screen but not the card's text or the entry its take reaches (the
// screen can show one objective while the text and the take give another).
bool PrestigeShuffle::Redraw() {
    ZEntityType** const s_Reset = m_Watch[kRepickFork].load(std::memory_order_relaxed);

    if (fr::PrestigeRedrawPath(s_Reset != nullptr, m_Redraws, kMaxRedraws) == fr::PrestigeRedraw::Keep) {
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: {}; the cards stay as shown (their text and takes agree: no pick since the game's "
              "reset)",
              !s_Reset
                  ? std::string("SignalFork_Void not found, so no redraw through the game's reset")
                  : fmt::format("a card's picker rejected its card again after {} new picks in this visit", m_Redraws));
        return false;
    }

    ++m_Redraws;
    m_Redrawing = true;

    if (const DWORD s_Code = SendInputPin(s_Reset, kPinIn)) {
        m_Redrawing = false;
        LogTo(fr::LogTopic::PrestigeCards,
              "prestige shuffle: picking again (#{} in this visit): signaling SignalFork_Void .In {}; the cards stay "
              "as shown",
              m_Redraws, FaultText(s_Code));
        return false;
    }

    LogTo(fr::LogTopic::PrestigeCards,
          "prestige shuffle: picked again (#{} in this visit) through SignalFork_Void .In (the game's reset), without "
          "the rejected card",
          m_Redraws);
    return true;
}

void PrestigeShuffle::Tick(double p_Dt) noexcept {
    if (!m_PinsOn.load(std::memory_order_relaxed))
        return;

    Contained(kTag, "tick", [&] {
        std::lock_guard s_Lock(m_Mutex);
        m_Taking.reset(); // a take's Poll runs within its signal
        Guard(p_Dt);

        if (!m_ReportPending)
            return;

        m_ReportIn -= p_Dt;

        if (m_ReportIn > 0.0)
            return;

        m_ReportPending = false;

        for (size_t i = 0; i < fr::kPrestigeCards; ++i) {
            const Picker& s_Picker = m_Pickers[i];

            LogTo(
                fr::LogTopic::PrestigeCards,
                "prestige shuffle report ({}): picker {} (card {}): drew {}; {}; selector {} picks, {} outputs; picker "
                "{} outputs{}{}",
                m_LastTrigger, i + 1, kCardLetters[i],
                m_Cards ? fr::PrestigeChoiceText((*m_Cards)[i]) : std::string("nothing"),
                s_Picker.forced ? fmt::format("weights still forced ({} at 0)", s_Picker.zeroed.size())
                                : std::string("weights written back"),
                s_Picker.selectorPicks, s_Picker.selectorOutputs, s_Picker.pickerOutputs,
                s_Picker.pickDone ? ", OnPickDone seen" : ", NO OnPickDone",
                s_Picker.pickFailed ? ", PICK FAILED" : "");
        }

        std::string s_Emitted;

        for (const std::string& s_Line : m_Emitted)
            s_Emitted += (s_Emitted.empty() ? "" : "; ") + s_Line;

        LogTo(fr::LogTopic::PrestigeCards, "prestige shuffle report: objective values seen since the draw: {}",
              s_Emitted.empty() ? std::string("none yet (they come when a card is taken)") : s_Emitted);
    });
}
