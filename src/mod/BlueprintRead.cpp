#include "BlueprintRead.h"

#include "../core/MissionStories.h"
#include "../core/VariantComments.h"
#include "../core/VariantExits.h"
#include "ActorReads.h" // SafeOwning
#include "EngineText.h"
#include "Guard.h"

// EntityFactory.h needs ZEntity.h (and through it ZObject.h) first.
#include <Glacier/ZEntity.h>

#include <Glacier/CompileReflection.h>
#include <Glacier/EntityFactory.h>

#include <fmt/format.h>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace {

// More sub-entities or connections than this in one blueprint means the read is garbage.
constexpr size_t kMaxElements = 100'000;
constexpr size_t kMaxConstants = 64;

std::string FaultWhy(const char* p_Step, DWORD p_Code) {
    return fmt::format("{}: {}", p_Step, FaultText(p_Code));
}

// Workers and guards follow the pattern in Guard.h.

__declspec(noinline) void FactoryOf(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase** p_Out) {
    *p_Out = ZEntityRef(p_Entity).GetBlueprintFactory();
}

__declspec(noinline) DWORD SafeFactory(ZEntityType** p_Entity, ZEntityBlueprintFactoryBase** p_Out) {
    DWORD s_Code = 0;

    __try {
        FactoryOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct BlueprintCheck {
    const STemplateEntityBlueprint* blueprint;
    size_t blueprintSubs;
    size_t factorySubs;
    int32_t blueprintRoot;
    int32_t factoryRoot;
};

__declspec(noinline) void BlueprintCheckOf(ZEntityBlueprintFactoryBase* p_Factory, BlueprintCheck* p_Out) {
    *p_Out = {};
    const auto* s_Template = static_cast<const ZTemplateEntityBlueprintFactory*>(p_Factory);
    p_Out->blueprint = s_Template->m_pTemplateEntityBlueprint;
    p_Out->factorySubs = s_Template->m_aSubEntityOffsets.size();
    p_Out->factoryRoot = s_Template->m_rootEntityIndex;

    if (!p_Out->blueprint)
        return;

    p_Out->blueprintSubs = p_Out->blueprint->subEntities.size();
    p_Out->blueprintRoot = p_Out->blueprint->rootEntityIndex;
}

__declspec(noinline) DWORD SafeBlueprintCheck(ZEntityBlueprintFactoryBase* p_Factory, BlueprintCheck* p_Out) {
    DWORD s_Code = 0;

    __try {
        BlueprintCheckOf(p_Factory, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void IndexOf(ZEntityBlueprintFactoryBase* p_Factory, uint64_t p_Id, int* p_Out) {
    *p_Out = p_Factory->GetSubEntityIndex(p_Id);
}

__declspec(noinline) DWORD SafeIndex(ZEntityBlueprintFactoryBase* p_Factory, uint64_t p_Id, int* p_Out) {
    DWORD s_Code = 0;

    __try {
        IndexOf(p_Factory, p_Id, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct SubNameOut {
    uint64_t id;
    char name[160];
};

__declspec(noinline) void SubNameOf(const STemplateEntityBlueprint* p_Blueprint, size_t p_Index, SubNameOut* p_Out) {
    const STemplateBlueprintSubEntity& s_Sub = p_Blueprint->subEntities[p_Index];
    p_Out->id = s_Sub.entityId;
    const size_t s_Length = std::min<size_t>(s_Sub.entityName.size(), sizeof(p_Out->name) - 1);

    if (s_Length > 0)
        std::memcpy(p_Out->name, s_Sub.entityName.c_str(), s_Length);

    p_Out->name[s_Length] = '\0';
}

__declspec(noinline) DWORD SafeSubName(const STemplateEntityBlueprint* p_Blueprint, size_t p_Index, SubNameOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        SubNameOf(p_Blueprint, p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ConstantsOut {
    size_t count;
    int32_t values[kMaxConstants];
};

// The int32 constants of the pin connections (and pin forwardings) from or to p_Index.
__declspec(noinline) void ConstantsOf(const STemplateEntityBlueprint* p_Blueprint, int32_t p_Index,
                                      ConstantsOut* p_Out) {
    p_Out->count = 0;
    const STypeID* s_Int32 = nullptr;

    for (const TArray<SEntityTemplatePinConnection>* s_Array :
         {&p_Blueprint->pinConnections, &p_Blueprint->inputPinForwardings, &p_Blueprint->outputPinForwardings}) {
        const size_t s_Size = s_Array->size();

        if (s_Size > kMaxElements)
            continue;

        for (size_t i = 0; i < s_Size && p_Out->count < kMaxConstants; ++i) {
            const SEntityTemplatePinConnection& s_Connection = (*s_Array)[i];

            if (s_Connection.fromID != p_Index && s_Connection.toID != p_Index)
                continue;

            const STypeID* s_Type = s_Connection.constantPinValue.GetTypeID();

            if (!s_Type)
                continue;

            if (!s_Int32) {
                const IType* s_Info = s_Type->GetTypeInfo();
                const char* s_Name = s_Info ? s_Info->pszTypeName : nullptr;

                if (!s_Name || std::strcmp(s_Name, "int32") != 0)
                    continue;

                s_Int32 = s_Type;
            }
            else if (s_Type != s_Int32) {
                continue;
            }

            const void* s_Data = s_Connection.constantPinValue.GetData();

            if (s_Data)
                std::memcpy(&p_Out->values[p_Out->count++], s_Data, sizeof(int32_t));
        }
    }
}

__declspec(noinline) DWORD SafeConstants(const STemplateEntityBlueprint* p_Blueprint, int32_t p_Index,
                                         ConstantsOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ConstantsOf(p_Blueprint, p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct LinkCounts {
    size_t counts[3];
};

__declspec(noinline) void LinkCountsOf(const STemplateEntityBlueprint* p_Blueprint, LinkCounts* p_Out) {
    p_Out->counts[0] = p_Blueprint->pinConnections.size();
    p_Out->counts[1] = p_Blueprint->inputPinForwardings.size();
    p_Out->counts[2] = p_Blueprint->outputPinForwardings.size();
}

__declspec(noinline) DWORD SafeLinkCounts(const STemplateEntityBlueprint* p_Blueprint, LinkCounts* p_Out) {
    DWORD s_Code = 0;

    __try {
        LinkCountsOf(p_Blueprint, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

void CopyPin(const ZString& p_Name, char* p_Out, size_t p_Size) {
    const size_t s_Length = std::min<size_t>(p_Name.size(), p_Size - 1);

    if (s_Length > 0)
        std::memcpy(p_Out, p_Name.c_str(), s_Length);

    p_Out[s_Length] = '\0';
}

__declspec(noinline) void LinkOf(const STemplateEntityBlueprint* p_Blueprint, int p_Array, size_t p_Index,
                                 BlueprintLink* p_Out) {
    const TArray<SEntityTemplatePinConnection>& s_Array = p_Array == 0   ? p_Blueprint->pinConnections
                                                          : p_Array == 1 ? p_Blueprint->inputPinForwardings
                                                                         : p_Blueprint->outputPinForwardings;
    const SEntityTemplatePinConnection& s_Link = s_Array[p_Index];
    p_Out->from = s_Link.fromID;
    p_Out->to = s_Link.toID;
    CopyPin(s_Link.fromPinName, p_Out->fromPin, sizeof(p_Out->fromPin));
    CopyPin(s_Link.toPinName, p_Out->toPin, sizeof(p_Out->toPin));
}

__declspec(noinline) DWORD SafeLink(const STemplateEntityBlueprint* p_Blueprint, int p_Array, size_t p_Index,
                                    BlueprintLink* p_Out) {
    DWORD s_Code = 0;

    __try {
        LinkOf(p_Blueprint, p_Array, p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void SubCountOf(const STemplateEntityBlueprint* p_Blueprint, size_t* p_Out) {
    *p_Out = p_Blueprint->subEntities.size();
}

__declspec(noinline) DWORD SafeSubCount(const STemplateEntityBlueprint* p_Blueprint, size_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        SubCountOf(p_Blueprint, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct PinScan {
    bool eventOccurredOut;
    bool triggerIn;
    bool storyPinIn;
    bool exitPinIn;
    bool hintPinIn;
};

bool PinIs(const ZString& p_Name, std::string_view p_Want) {
    return p_Name.size() == p_Want.size() && std::memcmp(p_Name.c_str(), p_Want.data(), p_Want.size()) == 0;
}

__declspec(noinline) void PinScanOf(const STemplateEntityBlueprint* p_Blueprint, PinScan* p_Out) {
    *p_Out = {};
    const size_t s_Size = p_Blueprint->pinConnections.size();

    if (s_Size > kMaxElements)
        return;

    for (size_t i = 0; i < s_Size && !(p_Out->eventOccurredOut && p_Out->triggerIn && p_Out->storyPinIn &&
                                       p_Out->exitPinIn && p_Out->hintPinIn);
         ++i) {
        const SEntityTemplatePinConnection& s_Link = p_Blueprint->pinConnections[i];

        if (!p_Out->eventOccurredOut && PinIs(s_Link.fromPinName, fr::kEventOccurredPin))
            p_Out->eventOccurredOut = true;

        if (!p_Out->triggerIn && PinIs(s_Link.toPinName, fr::kDianaLinePin))
            p_Out->triggerIn = true;

        const std::string_view s_To(s_Link.toPinName.c_str(), s_Link.toPinName.size());

        if (!p_Out->storyPinIn && fr::IsStoryInputPin(s_To))
            p_Out->storyPinIn = true;

        if (!p_Out->exitPinIn && PinIs(s_Link.toPinName, fr::kHijackerActivatePin))
            p_Out->exitPinIn = true;

        if (!p_Out->hintPinIn && fr::IsHintQueuePin(s_To))
            p_Out->hintPinIn = true;
    }
}

__declspec(noinline) DWORD SafePinScan(const STemplateEntityBlueprint* p_Blueprint, PinScan* p_Out) {
    DWORD s_Code = 0;

    __try {
        PinScanOf(p_Blueprint, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

const STemplateEntityBlueprint* TemplateBlueprintOfFactory(ZEntityBlueprintFactoryBase* p_Factory, std::string* p_Why) {
    BlueprintCheck s_Check {};

    if (!p_Factory) {
        *p_Why = "no blueprint factory";
        return nullptr;
    }

    if (const DWORD s_Code = SafeBlueprintCheck(p_Factory, &s_Check)) {
        *p_Why = FaultWhy("reading the template blueprint", s_Code);
        return nullptr;
    }

    if (!s_Check.blueprint) {
        *p_Why = "no template blueprint";
        return nullptr;
    }

    if (s_Check.blueprintSubs != s_Check.factorySubs || s_Check.blueprintSubs > kMaxElements ||
        s_Check.blueprintRoot != s_Check.factoryRoot) {
        *p_Why = fmt::format("not a template factory (blueprint {} sub-entities, root {}; factory {}, root {})",
                             s_Check.blueprintSubs, s_Check.blueprintRoot, s_Check.factorySubs, s_Check.factoryRoot);
        return nullptr;
    }

    return s_Check.blueprint;
}

const STemplateEntityBlueprint* TemplateBlueprintOf(ZEntityType** p_Root, std::string* p_Why) {
    ZEntityBlueprintFactoryBase* s_Factory = nullptr;

    if (const DWORD s_Code = SafeFactory(p_Root, &s_Factory)) {
        *p_Why = FaultWhy("reading the blueprint factory", s_Code);
        return nullptr;
    }

    if (!s_Factory) {
        *p_Why = "not a template instance root (no blueprint factory)";
        return nullptr;
    }

    return TemplateBlueprintOfFactory(s_Factory, p_Why);
}

std::optional<OwnedSubEntity> ReadOwnedSubEntity(ZEntityType** p_Entity, uint64_t p_Id, std::string* p_Why) {
    OwnedSubEntity s_Out;

    if (const DWORD s_Code = SafeOwning(p_Entity, &s_Out.owner)) {
        *p_Why = FaultWhy("reading the owning entity", s_Code);
        return std::nullopt;
    }

    if (!s_Out.owner || s_Out.owner == p_Entity) {
        *p_Why = "no owning entity";
        return std::nullopt;
    }

    ZEntityBlueprintFactoryBase* s_Factory = nullptr;

    if (const DWORD s_Code = SafeFactory(s_Out.owner, &s_Factory)) {
        *p_Why = FaultWhy("reading the owning entity's factory", s_Code);
        return std::nullopt;
    }

    if (!s_Factory) {
        *p_Why = "the owning entity has no blueprint factory";
        return std::nullopt;
    }

    s_Out.blueprint = TemplateBlueprintOfFactory(s_Factory, p_Why);

    if (!s_Out.blueprint)
        return std::nullopt;

    if (const DWORD s_Code = SafeIndex(s_Factory, p_Id, &s_Out.index)) {
        *p_Why = FaultWhy("looking up the sub-entity index", s_Code);
        return std::nullopt;
    }

    if (s_Out.index < 0) {
        *p_Why = fmt::format("{:016x} is not a sub-entity of the owning template", p_Id);
        return std::nullopt;
    }

    SubNameOut s_Name {};

    if (const DWORD s_Code = SafeSubName(s_Out.blueprint, static_cast<size_t>(s_Out.index), &s_Name)) {
        *p_Why = FaultWhy("reading the sub-entity's name", s_Code);
        return std::nullopt;
    }

    if (s_Name.id != p_Id) {
        *p_Why = fmt::format("blueprint entry {} has id {:016x}, not {:016x}", s_Out.index, s_Name.id, p_Id);
        return std::nullopt;
    }

    s_Out.name = s_Name.name;
    ConstantsOut s_Constants {};

    if (!SafeConstants(s_Out.blueprint, s_Out.index, &s_Constants))
        s_Out.connectionConstants.assign(s_Constants.values, s_Constants.values + s_Constants.count);

    return s_Out;
}

std::optional<std::vector<BlueprintLink>> ReadBlueprintLinks(const STemplateEntityBlueprint* p_Blueprint,
                                                             std::string* p_Why) {
    if (!p_Blueprint) {
        *p_Why = "no template blueprint";
        return std::nullopt;
    }

    LinkCounts s_Counts {};

    if (const DWORD s_Code = SafeLinkCounts(p_Blueprint, &s_Counts)) {
        *p_Why = FaultWhy("reading the pin connection counts", s_Code);
        return std::nullopt;
    }

    for (const size_t s_Count : s_Counts.counts) {
        if (s_Count > kMaxElements) {
            *p_Why = fmt::format("implausible pin connection count {}", s_Count);
            return std::nullopt;
        }
    }

    constexpr BlueprintLink::Kind kKinds[3] {BlueprintLink::Kind::Connection, BlueprintLink::Kind::InputForwarding,
                                             BlueprintLink::Kind::OutputForwarding};
    std::vector<BlueprintLink> s_Links;
    s_Links.reserve(s_Counts.counts[0] + s_Counts.counts[1] + s_Counts.counts[2]);

    for (int a = 0; a < 3; ++a) {
        for (size_t i = 0; i < s_Counts.counts[a]; ++i) {
            BlueprintLink s_Link {};

            if (const DWORD s_Code = SafeLink(p_Blueprint, a, i, &s_Link)) {
                *p_Why = FaultWhy("reading a pin connection", s_Code);
                return std::nullopt;
            }

            s_Link.kind = kKinds[a];
            s_Links.push_back(s_Link);
        }
    }

    return s_Links;
}

uint32_t ScanBriefingPins(const STemplateEntityBlueprint* p_Blueprint, bool* p_EventOccurredOut, bool* p_TriggerIn,
                          bool* p_StoryPinIn, bool* p_ExitPinIn, bool* p_HintPinIn) {
    *p_EventOccurredOut = false;
    *p_TriggerIn = false;
    *p_StoryPinIn = false;

    if (p_ExitPinIn)
        *p_ExitPinIn = false;

    if (p_HintPinIn)
        *p_HintPinIn = false;

    if (!p_Blueprint)
        return 0;

    PinScan s_Scan {};

    if (const DWORD s_Code = SafePinScan(p_Blueprint, &s_Scan))
        return static_cast<uint32_t>(s_Code);

    *p_EventOccurredOut = s_Scan.eventOccurredOut;
    *p_TriggerIn = s_Scan.triggerIn;
    *p_StoryPinIn = s_Scan.storyPinIn;

    if (p_ExitPinIn)
        *p_ExitPinIn = s_Scan.exitPinIn;

    if (p_HintPinIn)
        *p_HintPinIn = s_Scan.hintPinIn;

    return 0;
}

std::optional<fr::BriefingGraph> CopyBriefingGraph(const STemplateEntityBlueprint* p_Blueprint, std::string* p_Why) {
    const std::optional<std::vector<BlueprintLink>> s_Links = ReadBlueprintLinks(p_Blueprint, p_Why);

    if (!s_Links)
        return std::nullopt;

    size_t s_Count = 0;

    if (const DWORD s_Code = SafeSubCount(p_Blueprint, &s_Count)) {
        *p_Why = FaultWhy("reading the sub-entity count", s_Code);
        return std::nullopt;
    }

    if (s_Count == 0 || s_Count > kMaxElements) {
        *p_Why = fmt::format("implausible sub-entity count {}", s_Count);
        return std::nullopt;
    }

    fr::BriefingGraph s_Graph;
    s_Graph.ids.resize(s_Count);
    s_Graph.names.resize(s_Count);

    for (size_t i = 0; i < s_Count; ++i) {
        SubNameOut s_Name {};

        if (const DWORD s_Code = SafeSubName(p_Blueprint, i, &s_Name)) {
            *p_Why = FaultWhy("reading a sub-entity name", s_Code);
            return std::nullopt;
        }

        s_Graph.ids[i] = s_Name.id;
        s_Graph.names[i] = s_Name.name;
    }

    s_Graph.links.reserve(s_Links->size());

    for (const BlueprintLink& s_Link : *s_Links) {
        const fr::BriefingLinkKind s_Kind =
            s_Link.kind == BlueprintLink::Kind::Connection         ? fr::BriefingLinkKind::Connection
            : s_Link.kind == BlueprintLink::Kind::OutputForwarding ? fr::BriefingLinkKind::OutputForwarding
                                                                   : fr::BriefingLinkKind::InputForwarding;
        s_Graph.links.push_back({s_Link.from, s_Link.fromPin, s_Link.to, s_Link.toPin, s_Kind});
    }

    return s_Graph;
}
