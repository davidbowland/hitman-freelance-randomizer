// Legal sniper rifles (see LegalSniperKits.h). Raw-pointer steps follow the
// guard pattern in Guard.h.

#include "LegalSniperKits.h"

#include "ActorReads.h"
#include "Collections.h" // fr::ArrayElements, fr::EntityRefSize
#include "EngineText.h"
#include "EntityWalk.h" // LocateProperty, ReadEngineBytes
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "EntityReads.h" // ReadStringProperties, ListEntityProperties
#include "Repository.h"

#include <Globals.h>

#include <Glacier/ZActor.h>
#include <Glacier/ZGameKeywordManager.h>
#include <Glacier/ZOutfit.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <string_view>

namespace {

constexpr double kFirstPassSeconds = 2.0;
constexpr double kPassIntervalSeconds = 15.0;
constexpr size_t kRefSize = 16;         // TEntityRef: the entity, then the interface
constexpr size_t kMaxListEntries = 256; // a weapon list longer than this is implausible

__declspec(noinline) void KitOf(const ZActor* p_Actor, ZEntityType*** p_Out) {
    *p_Out = p_Actor->m_rOutfit.m_entityRef.m_pObj;
}

__declspec(noinline) DWORD SafeKitOf(const ZActor* p_Actor, ZEntityType*** p_Out) {
    DWORD s_Code = 0;

    __try {
        KitOf(p_Actor, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

std::string LocateWhy(const PropertyLocation& p_Location, std::string_view p_Name) {
    switch (p_Location.status) {
        case PropertyLocation::Status::Ok: return {};
        case PropertyLocation::Status::Missing: return fmt::format("no {}", p_Name);
        case PropertyLocation::Status::HasGetter: return fmt::format("{} has a getter", p_Name);
        case PropertyLocation::Status::Fault: return fmt::format("{}: {}", p_Name, FaultText(p_Location.faultCode));
    }

    return fmt::format("{}: ?", p_Name);
}

// A kit's TArray<TEntityRef<...>> property: where it is, its engine type, its
// header, its elements' bytes and entities.
struct RefArray {
    std::string why; // empty when read
    std::string typeName;
    uintptr_t address = 0;
    fr::RawArrayHeader header;
    uintptr_t elements = 0;
    std::vector<uint8_t> bytes;
    std::vector<ZEntityType**> entities;
};

RefArray ReadRefArray(ZEntityType** p_Kit, std::string_view p_Name) {
    RefArray s_Out;
    const PropertyLocation s_Location = LocateProperty(p_Kit, p_Name);

    if (s_Location.status != PropertyLocation::Status::Ok) {
        s_Out.why = LocateWhy(s_Location, p_Name);
        return s_Out;
    }

    const std::optional<size_t> s_Element = fr::EntityRefSize(s_Location.elementTypeName);
    s_Out.typeName = s_Location.typeName[0] ? s_Location.typeName : "<unknown type>";

    if (!s_Location.isArray || s_Element != kRefSize) {
        s_Out.why = fmt::format("{} is {} (element {}), not an array of TEntityRef", p_Name,
                                s_Location.typeName[0] ? s_Location.typeName : "<unknown>",
                                s_Location.elementTypeName[0] ? s_Location.elementTypeName : "<unknown>");
        return s_Out;
    }

    s_Out.address = s_Location.address;

    if (const uint32_t s_Code = ReadEngineBytes(s_Out.address, &s_Out.header, sizeof(s_Out.header))) {
        s_Out.why = fmt::format("{}: reading the array {}", p_Name, FaultText(s_Code));
        return s_Out;
    }

    const auto s_Span = fr::ArrayElements(s_Out.address, s_Out.header.begin, s_Out.header.end, s_Out.header.third,
                                          kRefSize, kMaxListEntries);

    if (!s_Span) {
        s_Out.why = fmt::format("{}: implausible array (begin 0x{:X}, end 0x{:X}, third 0x{:X})", p_Name,
                                s_Out.header.begin, s_Out.header.end, s_Out.header.third);
        return s_Out;
    }

    s_Out.elements = s_Span->begin;
    s_Out.bytes.resize(s_Span->count * kRefSize);

    if (s_Span->count) {
        if (const uint32_t s_Code = ReadEngineBytes(s_Out.elements, s_Out.bytes.data(), s_Out.bytes.size())) {
            s_Out.why = fmt::format("{}: reading the elements {}", p_Name, FaultText(s_Code));
            s_Out.bytes.clear();
            return s_Out;
        }
    }

    for (size_t i = 0; i < s_Span->count; ++i) {
        ZEntityType** s_Entity = nullptr;
        std::memcpy(&s_Entity, s_Out.bytes.data() + i * kRefSize, sizeof(s_Entity));
        s_Out.entities.push_back(s_Entity);
    }

    return s_Out;
}

std::optional<bool> ReadBool(ZEntityType** p_Entity, std::string_view p_Name) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != 1)
        return std::nullopt;

    uint8_t s_Value = 0;

    if (ReadEngineBytes(s_Location.address, &s_Value, 1))
        return std::nullopt;

    return s_Value != 0;
}

// An item key's m_RepositoryId, read only when the entity has that property
// (a list's entity is not checked to be a key).
std::optional<fr::Guid> ReadKeyItem(ZEntityType** p_Key) {
    if (!p_Key)
        return std::nullopt;

    const PropertyLocation s_Location = LocateProperty(p_Key, "m_RepositoryId");

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(fr::Guid))
        return std::nullopt;

    fr::Guid s_Id;

    if (ReadEngineBytes(s_Location.address, &s_Id, sizeof(s_Id)))
        return std::nullopt;

    return s_Id;
}

// The kit's m_sCommonName (else m_sTitle), quoted, or "<unnamed>".
std::string KitName(ZEntityType** p_Kit) {
    const StringProperties s_Strings = ReadStringProperties(p_Kit);
    std::string s_Title;

    for (const NamedString& s_String : s_Strings.strings) {
        if (!s_String.read || s_String.value.empty())
            continue;

        if (s_String.name == "m_sCommonName")
            return fmt::format("\"{}\"", s_String.value);

        if (s_String.name == "m_sTitle")
            s_Title = s_String.value;
    }

    return s_Title.empty() ? std::string("<unnamed>") : fmt::format("\"{}\"", s_Title);
}

std::string JoinList(const std::vector<std::string>& p_Items) {
    std::string s_Out;

    for (const std::string& s_Item : p_Items)
        s_Out += (s_Out.empty() ? "" : ", ") + s_Item;

    return "[" + s_Out + "]";
}

// --- The game's keywords (ZGameKeywordManager), read raw and guarded ---

constexpr size_t kMaxKeywordNames = 8'192;
constexpr size_t kMaxMapNodes = 400'000;   // nodes one map walk visits at most
constexpr uint32_t kMaxBuckets = 1u << 22; // a bucket count above this is implausible
constexpr size_t kKeywordChars = 100;
constexpr size_t kMaxHolderIds = 16;
constexpr size_t kEntityHeader = 0x18;     // ZEntityImpl: vtable, m_pEntityType, m_nEntityPtrIndex, m_nEntityFlags
constexpr size_t kMaxEntityWindow = 0x400; // bytes of a keyword entity looked at, at most

void CopyZStringText(const ZString& p_String, char* p_Out, size_t p_Capacity) {
    const size_t s_Length = std::min<size_t>(p_String.size(), p_Capacity - 1);

    if (s_Length > 0)
        std::memcpy(p_Out, p_String.c_str(), s_Length);

    p_Out[s_Length] = '\0';
}

// Every node of a THashMap / THashSet (SHashMapInfo / SHashSetInfo: buckets of
// node indices, each node chaining to the next), visiting p_Visit(node data).
// False when the walk was cut short (implausible bucket count, too many nodes).
template <class TInfo, class TVisit>
bool WalkNodes(const TInfo& p_Info, TVisit&& p_Visit) {
    if (p_Info.m_nBuckets > kMaxBuckets)
        return false;

    size_t s_Steps = 0;

    for (uint32_t b = 0; b < p_Info.m_nBuckets; ++b) {
        uint32_t s_Index = p_Info.m_pBuckets[b];

        while (s_Index != UINT32_MAX) {
            if (++s_Steps > kMaxMapNodes)
                return false;

            const auto& s_Node = p_Info.m_pNodes[s_Index];
            p_Visit(s_Node.m_data);
            s_Index = static_cast<uint32_t>(s_Node.m_iNext);
        }
    }

    return true;
}

struct KeywordNamesOut {
    bool noManager;
    bool capped;
    uint32_t counter; // m_nKeywordIDCounter
    uint32_t size;    // m_KeywordIDtoStringMap's size
    uint32_t count;
    struct Entry {
        int32_t id;
        char text[kKeywordChars];
    } entries[kMaxKeywordNames];
};

__declspec(noinline) void KeywordNamesOf(KeywordNamesOut* p_Out) {
    const ZGameKeywordManager* s_Manager = Globals::GameKeywordManager;

    if (!s_Manager) {
        p_Out->noManager = true;
        return;
    }

    p_Out->counter = static_cast<uint32_t>(s_Manager->m_nKeywordIDCounter);
    p_Out->size = s_Manager->m_KeywordIDtoStringMap.m_nSize;
    p_Out->capped = !WalkNodes(s_Manager->m_KeywordIDtoStringMap.m_info, [p_Out](const auto& p_Pair) {
        if (p_Out->count >= kMaxKeywordNames) {
            p_Out->capped = true;
            return;
        }

        KeywordNamesOut::Entry& s_Entry = p_Out->entries[p_Out->count++];
        s_Entry.id = p_Pair.first;
        s_Entry.text[0] = '\0';
        CopyZStringText(p_Pair.second, s_Entry.text, sizeof(s_Entry.text));
    }) || p_Out->capped;
}

__declspec(noinline) DWORD SafeKeywordNames(KeywordNamesOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        KeywordNamesOf(p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// The keyword entity as the manager knows it: as a keyword holder (the ids it
// holds) and as a keyword channel (the channel's name).
struct KeywordHolderOut {
    bool noManager;
    uint32_t idCount;
    int32_t ids[kMaxHolderIds];
    bool channelFound;
    char channel[kKeywordChars];
};

__declspec(noinline) void KeywordHolderOf(ZEntityType** p_Entity, KeywordHolderOut* p_Out) {
    const ZGameKeywordManager* s_Manager = Globals::GameKeywordManager;

    if (!s_Manager) {
        p_Out->noManager = true;
        return;
    }

    WalkNodes(s_Manager->m_HolderToKeywordMap.m_info, [p_Entity, p_Out](const auto& p_Pair) {
        if (p_Pair.first.m_pObj != p_Entity)
            return;

        WalkNodes(p_Pair.second.m_info, [p_Out](const int32_t& p_Id) {
            if (p_Out->idCount < kMaxHolderIds)
                p_Out->ids[p_Out->idCount++] = p_Id;
        });
    });
    WalkNodes(s_Manager->m_KeywordChannelMap.m_info, [p_Entity, p_Out](const auto& p_Pair) {
        if (p_Out->channelFound || p_Pair.second.m_pObj != p_Entity)
            return;

        p_Out->channelFound = true;
        CopyZStringText(p_Pair.first, p_Out->channel, sizeof(p_Out->channel));
    });
}

__declspec(noinline) DWORD SafeKeywordHolder(ZEntityType** p_Entity, KeywordHolderOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        KeywordHolderOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

std::string IdList(const std::vector<int32_t>& p_Ids) {
    std::vector<std::string> s_Parts;

    for (const int32_t s_Id : p_Ids)
        s_Parts.push_back(std::to_string(s_Id));

    return JoinList(s_Parts);
}

} // namespace

// p_Missed: a lookup in this book found nothing.
const LegalSniperKits::KeywordBook& LegalSniperKits::Book(bool p_Missed) {
    if (m_Book.read && (m_Book.pass == m_Passes || (m_Book.why.empty() && !p_Missed)))
        return m_Book;

    m_Book = {};
    m_Book.read = true;
    m_Book.pass = m_Passes;
    auto s_Out = std::make_unique<KeywordNamesOut>();
    std::memset(s_Out.get(), 0, sizeof(KeywordNamesOut));

    if (const DWORD s_Code = SafeKeywordNames(s_Out.get()))
        m_Book.why = fmt::format("reading the keyword manager {}", FaultText(s_Code));
    else if (s_Out->noManager)
        m_Book.why = "no keyword manager";
    else if (s_Out->capped)
        m_Book.why = fmt::format("implausible keyword map (size {}, {} entries read before stopping)", s_Out->size,
                                 s_Out->count);

    m_Book.counter = s_Out->counter;
    m_Book.size = s_Out->size;

    if (m_Book.why.empty()) {
        for (uint32_t i = 0; i < s_Out->count; ++i) {
            const KeywordNamesOut::Entry& s_Entry = s_Out->entries[i];
            m_Book.names[s_Entry.id] = s_Entry.text;

            if (fr::IsSniperKeyword(s_Entry.text))
                m_Book.sniperIds.push_back(s_Entry.id);
        }

        if (m_Book.names.empty())
            m_Book.why = "the keyword manager has no keywords";
    }

    if (m_Book.why.empty())
        LogTo(fr::LogTopic::Weapons, "legal sniper rifles: keyword manager: {} keyword(s) (id counter {}); {} is id {}",
              m_Book.names.size(), m_Book.counter, fr::kSniperKeyword,
              m_Book.sniperIds.empty() ? std::string("<not registered>") : IdList(m_Book.sniperIds));
    else
        LogTo(fr::LogTopic::Weapons,
              "legal sniper rifles: keyword manager: {}; keyword entities are read from their "
              "properties only",
              m_Book.why);

    return m_Book;
}

// A keyword entity's keyword, first found of: its ZString properties; the
// keyword manager's ids for it as a holder, or its name as a keyword channel;
// a ZString inside its own bytes whose text is a registered keyword; the one
// registered keyword id inside its own bytes (fr::KeywordIdSlots). Logged once
// per scene with how it was found.
const LegalSniperKits::Keyword& LegalSniperKits::ReadKeyword(ZEntityType** p_Entity) {
    if (const auto s_It = m_Keywords.find(p_Entity); s_It != m_Keywords.end())
        return s_It->second;

    Keyword s_Keyword;

    for (const NamedString& s_String : ReadStringProperties(p_Entity).strings) {
        if (s_String.read && !s_String.value.empty()) {
            s_Keyword.texts.push_back(s_String.value);
            s_Keyword.how = "property " + s_String.name;
        }
    }

    std::string s_Class;
    uint64_t s_Id = 0;
    ReadEntityClassName(p_Entity, s_Class, &s_Id);

    // The property table, with offsets from the entity's start (ZEntityImpl,
    // the entity pointer minus 8): its own bytes end with its last property.
    const EntityPropertyTable s_Table = ListEntityProperties(p_Entity, 64);
    std::vector<fr::ByteRange> s_Skip {{0, kEntityHeader}};
    size_t s_OwnEnd = kEntityHeader;

    for (const TableProperty& s_Property : s_Table.properties) {
        const int64_t s_Start = s_Property.offset + 8;

        if (s_Start >= 0 && static_cast<size_t>(s_Start) + s_Property.typeSize <= kMaxEntityWindow) {
            s_Skip.push_back({static_cast<size_t>(s_Start), static_cast<size_t>(s_Start) + s_Property.typeSize});
            s_OwnEnd = std::max(s_OwnEnd, static_cast<size_t>(s_Start) + s_Property.typeSize);
        }
    }

    std::vector<uint8_t> s_Own(s_OwnEnd);

    if (ReadEngineBytes(reinterpret_cast<uintptr_t>(p_Entity) - 8, s_Own.data(), s_Own.size()))
        s_Own.clear();

    // The keyword manager's names; empty texts when not found.
    const auto s_Search = [&](const KeywordBook& p_Book) {
        if (!p_Book.why.empty()) {
            s_Keyword.how = p_Book.why;
        }
        else {
            const auto s_NameOf = [&p_Book](int32_t p_KeywordId) -> std::optional<std::string> {
                const auto s_Name = p_Book.names.find(p_KeywordId);
                return s_Name == p_Book.names.end() ? std::nullopt : std::optional<std::string>(s_Name->second);
            };

            // As a holder or a channel.
            KeywordHolderOut s_Holder {};

            if (!SafeKeywordHolder(p_Entity, &s_Holder)) {
                for (uint32_t i = 0; i < s_Holder.idCount; ++i) {
                    if (const auto s_Name = s_NameOf(s_Holder.ids[i])) {
                        s_Keyword.texts.push_back(*s_Name);
                        s_Keyword.how = fmt::format("keyword manager: the entity holds keyword id {}", s_Holder.ids[i]);
                    }
                }

                if (s_Keyword.texts.empty() && s_Holder.channelFound && s_Holder.channel[0]) {
                    s_Keyword.texts.push_back(s_Holder.channel);
                    s_Keyword.how = "keyword manager: the entity's keyword channel";
                }
            }

            // A ZString inside its own bytes (length, then the characters' pointer) naming a registered keyword.
            for (size_t o = kEntityHeader; s_Keyword.texts.empty() && o + 16 <= s_Own.size(); o += 8) {
                uint32_t s_Length = 0;
                uint64_t s_Chars = 0;
                std::memcpy(&s_Length, s_Own.data() + o, sizeof(s_Length));
                std::memcpy(&s_Chars, s_Own.data() + o + 8, sizeof(s_Chars));
                s_Length &= 0x3FFFFFFF;

                if (s_Length < 2 || s_Length > 96 || !fr::LooksLikePointer(s_Chars))
                    continue;

                std::string s_Text(s_Length, '\0');

                if (ReadEngineBytes(static_cast<uintptr_t>(s_Chars), s_Text.data(), s_Length) ||
                    !fr::IsKeywordLikeText(s_Text))
                    continue;

                if (std::any_of(p_Book.names.begin(), p_Book.names.end(),
                                [&s_Text](const auto& p_Name) { return p_Name.second == s_Text; })) {
                    s_Keyword.texts.push_back(s_Text);
                    s_Keyword.how = fmt::format("a ZString in the entity at +0x{:X}", o);
                }
            }

            // The one registered keyword id inside its own bytes.
            if (s_Keyword.texts.empty()) {
                std::vector<int32_t> s_Registered;

                for (const auto& [s_KeywordId, s_Name] : p_Book.names)
                    s_Registered.push_back(s_KeywordId);

                const std::vector<fr::KeywordIdSlot> s_Slots = fr::KeywordIdSlots(s_Own, s_Skip, s_Registered);

                if (const auto s_Pick = fr::PickKeywordId(s_Slots); s_Pick && s_NameOf(*s_Pick)) {
                    s_Keyword.texts.push_back(*s_NameOf(*s_Pick));
                    s_Keyword.how = fmt::format("keyword id {} in the entity's own bytes", *s_Pick);
                }
                else if (s_Slots.size() > 1) {
                    s_Keyword.how = "several different keyword ids in its bytes: ambiguous, not used";
                }
            }
        }
    };

    if (s_Keyword.texts.empty()) {
        // An earlier pass's book may miss keywords registered since: read it again once.
        const bool s_Earlier = m_Book.read && m_Book.why.empty() && m_Book.pass != m_Passes;
        s_Search(Book());

        if (s_Keyword.texts.empty() && s_Earlier) {
            s_Keyword.how.clear();
            s_Search(Book(true));
        }
    }

    if (s_Keyword.texts.empty() && s_Keyword.how.empty())
        s_Keyword.how = "keyword not found";

    std::string s_Texts;

    for (const std::string& s_Text : s_Keyword.texts)
        s_Texts += (s_Texts.empty() ? "" : "/") + s_Text;

    LogTo(fr::LogTopic::Weapons, "legal sniper rifles: keyword entity 0x{:X} ({}, id {:016x}): {} ({})",
          reinterpret_cast<uintptr_t>(p_Entity), s_Class.empty() ? std::string("<no class>") : s_Class, s_Id,
          s_Texts.empty() ? std::string("UNREADABLE") : s_Texts, s_Keyword.how);

    return m_Keywords.emplace(p_Entity, std::move(s_Keyword)).first->second;
}

void LegalSniperKits::OnLoadingStage(ESceneLoadingStage p_Stage, bool p_Freelancer) noexcept {
    if (p_Stage != ESceneLoadingStage::eLoading_ScenePlaying)
        return;

    m_Armed = m_On && p_Freelancer;
    m_Clock = 0.0;
    m_NextPass = kFirstPassSeconds;
    m_Passes = 0;

    if (!m_On && p_Freelancer)
        LogTo(fr::LogTopic::Weapons, "legal sniper rifles: off; outfits left vanilla");
}

void LegalSniperKits::Tick(double p_Seconds) noexcept {
    if (!m_Armed)
        return;

    m_Clock += std::max(0.0, p_Seconds);

    if (m_Clock < m_NextPass)
        return;

    m_NextPass = m_Clock + kPassIntervalSeconds;
    Pass(m_Passes == 0 ? "scene start" : "new actors");
    ++m_Passes;
}

bool LegalSniperKits::EnsureItems() {
    if (m_ItemsBuilt)
        return true;

    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::Weapons, "legal sniper rifles: pro.repo: {}; item keys are not named", s_Why);
        return false;
    }

    std::vector<RepoItemEntry> s_Entries;
    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanItems(s_Entries, &s_Overflow, &s_Scanned)) {
        LogTo(fr::LogTopic::Weapons,
              "legal sniper rifles: the pro.repo item scan faulted ({}) after {} entries; item keys are not named",
              FaultText(s_Code), s_Scanned);
        return false;
    }

    for (const RepoItemEntry& s_Entry : s_Entries)
        m_Items[s_Entry.guid] = Item {s_Entry.icon, s_Entry.itemType, s_Entry.title};

    m_ItemsBuilt = true;
    return true;
}

void LegalSniperKits::Pass(const char* p_When) {
    try {
        if (!Globals::ActorManager) {
            LogTo(fr::LogTopic::Weapons, "legal sniper rifles: {}: no actor manager; nothing changed", p_When);
            return;
        }

        std::vector<ActorRef> s_Actors;
        size_t s_Total = 0;

        if (const uint32_t s_Code = ReadActivatedActors(Globals::ActorManager, s_Actors, &s_Total))
            LogTo(fr::LogTopic::Weapons, "legal sniper rifles: {}: reading the activated actors faulted ({})", p_When,
                  FaultText(s_Code));

        std::vector<ActorRef> s_Spawned;

        if (ReadSpawnedActors(Globals::ActorManager, s_Spawned, &s_Total) == 0)
            s_Actors.insert(s_Actors.end(), s_Spawned.begin(), s_Spawned.end());

        std::vector<ZEntityType**> s_NewKits;

        for (const ActorRef& s_Actor : s_Actors) {
            ZEntityType** s_Kit = nullptr;

            if (!s_Actor.second || SafeKitOf(s_Actor.second, &s_Kit) || !s_Kit)
                continue;

            if (m_SeenKits.insert(s_Kit).second)
                s_NewKits.push_back(s_Kit);
        }

        if (s_NewKits.empty())
            return;

        size_t s_Edited = 0;
        size_t s_Removed = 0;
        size_t s_Failed = 0;

        for (ZEntityType** s_Kit : s_NewKits) {
            const std::string s_Name = KitName(s_Kit);
            const std::optional<bool> s_WeaponsAllowed = ReadBool(s_Kit, "m_bWeaponsAllowed");

            if (!s_WeaponsAllowed) {
                LogTo(fr::LogTopic::Weapons, "legal sniper rifles: kit {}: m_bWeaponsAllowed unreadable; left alone",
                      s_Name);
                continue;
            }

            // The four lists: [0] exception keys, [1] exception keywords, [2] allowed keys, [3] allowed keywords.
            static constexpr std::array<std::string_view, 4> kLists = {
                "m_aWeaponsAllowedExceptionKeys", "m_aWeaponsAllowedExceptionKeywords", "m_aAllowedWeaponKeys",
                "m_aAllowedWeaponKeywords"};
            std::array<RefArray, 4> s_Arrays;
            std::array<std::vector<bool>, 4> s_Sniper;
            std::array<std::vector<std::string>, 4> s_Texts;
            fr::KitSniperFacts s_Facts;
            s_Facts.weaponsAllowed = *s_WeaponsAllowed;

            for (size_t l = 0; l < kLists.size(); ++l) {
                s_Arrays[l] = ReadRefArray(s_Kit, kLists[l]);
                const bool s_Keywords = l == 1 || l == 3;
                const bool s_Exceptions = l < 2;

                // An exception list that cannot be read may hold the sniper rifles.
                if (s_Exceptions && !s_Arrays[l].why.empty())
                    ++s_Facts.unknownExceptions;

                if (!s_Keywords && !s_Arrays[l].entities.empty())
                    EnsureItems();

                for (ZEntityType** s_Entity : s_Arrays[l].entities) {
                    fr::EntryMatch s_Match = fr::EntryMatch::Unknown;
                    std::string s_Text;

                    if (s_Keywords) {
                        const Keyword& s_Keyword = ReadKeyword(s_Entity);
                        s_Match = fr::MatchKeywordTexts(s_Keyword.texts);

                        for (const std::string& s_Part : s_Keyword.texts)
                            s_Text += (s_Text.empty() ? "" : "/") + s_Part;

                        s_Text = s_Keyword.texts.empty() ? fmt::format("<keyword entity 0x{:X} unreadable>",
                                                                       reinterpret_cast<uintptr_t>(s_Entity))
                                                         : fmt::format("{} ({})", s_Text, s_Keyword.how);
                    }
                    else {
                        const std::optional<fr::Guid> s_Id = ReadKeyItem(s_Entity);

                        if (!s_Id) {
                            s_Text = "<unreadable key>";
                        }
                        else if (const auto s_Item = m_Items.find(*s_Id); s_Item != m_Items.end()) {
                            s_Match = fr::IsSniperItem(s_Item->second.icon, s_Item->second.itemType)
                                          ? fr::EntryMatch::Sniper
                                          : fr::EntryMatch::Other;
                            s_Text = fmt::format("{} ({})", s_Item->second.title, s_Item->second.icon);
                        }
                        else {
                            s_Text = fmt::format("{} (not a pro.repo item)", fr::FormatGuid(*s_Id));
                        }
                    }

                    s_Sniper[l].push_back(s_Match == fr::EntryMatch::Sniper);
                    s_Texts[l].push_back(s_Match == fr::EntryMatch::Sniper    ? s_Text + " *sniper*"
                                         : s_Match == fr::EntryMatch::Unknown ? s_Text + " *unknown*"
                                                                              : s_Text);

                    if (s_Match == fr::EntryMatch::Sniper)
                        ++(s_Exceptions ? s_Facts.sniperExceptions : s_Facts.sniperAllowed);
                    else if (s_Match == fr::EntryMatch::Unknown && s_Exceptions)
                        ++s_Facts.unknownExceptions;
                }
            }

            const fr::KitSniper s_Verdict = fr::ClassifyKit(s_Facts);
            ++m_Verdicts[static_cast<size_t>(s_Verdict)];
            std::string s_Lists;

            for (size_t l = 0; l < kLists.size(); ++l)
                s_Lists += s_Arrays[l].why.empty()
                               ? fmt::format("; {} <{}> {}", kLists[l], s_Arrays[l].typeName, JoinList(s_Texts[l]))
                               : fmt::format("; {} ({})", kLists[l], s_Arrays[l].why);

            std::string s_Done;

            if (s_Verdict == fr::KitSniper::Legalize) {
                size_t s_KitRemoved = 0;

                for (size_t l = 0; l < 2; ++l) {
                    RefArray& s_Array = s_Arrays[l];
                    const fr::ExceptionEdit s_Plan = fr::PlanExceptionEdit(s_Sniper[l]);

                    if (!s_Array.why.empty() || !s_Plan.Changes())
                        continue;

                    const auto s_Header = fr::ShrinkArrayHeader(s_Array.header, kRefSize, s_Plan.kept);

                    if (!s_Header) {
                        ++s_Failed;
                        s_Done += fmt::format("; {}: header not shrinkable, not changed", kLists[l]);
                        continue;
                    }

                    Edit s_Edit;
                    s_Edit.kit = s_Name;
                    s_Edit.list = std::string(kLists[l]);
                    s_Edit.address = s_Array.address;
                    s_Edit.vanilla = s_Array.header;
                    s_Edit.written = *s_Header;
                    s_Edit.elements = s_Array.elements;

                    std::vector<uint8_t> s_Ordered(s_Array.bytes.size());

                    for (size_t i = 0; i < s_Plan.order.size(); ++i)
                        std::memcpy(s_Ordered.data() + i * kRefSize, s_Array.bytes.data() + s_Plan.order[i] * kRefSize,
                                    kRefSize);

                    // The same set of entries in a new order first (a reader in between sees the vanilla set), then the count.
                    if (s_Ordered != s_Array.bytes) {
                        if (const uint32_t s_Code =
                                WriteEngineBytes(s_Edit.elements, s_Ordered.data(), s_Ordered.size())) {
                            ++s_Failed;
                            s_Done += fmt::format("; {}: reordering FAULTED ({})", kLists[l], FaultText(s_Code));
                            continue;
                        }

                        s_Edit.vanillaBytes = s_Array.bytes;
                    }

                    if (const uint32_t s_Code =
                            WriteEngineBytes(s_Edit.address, &s_Edit.written, sizeof(s_Edit.written))) {
                        ++s_Failed;
                        s_Done += fmt::format("; {}: shrinking FAULTED ({})", kLists[l], FaultText(s_Code));

                        if (!s_Edit.vanillaBytes.empty())
                            WriteEngineBytes(s_Edit.elements, s_Edit.vanillaBytes.data(), s_Edit.vanillaBytes.size());

                        continue;
                    }

                    const size_t s_Dropped = s_Plan.order.size() - s_Plan.kept;
                    s_KitRemoved += s_Dropped;
                    s_Done += fmt::format("; {}: {} -> {} entries", kLists[l], s_Plan.order.size(), s_Plan.kept);
                    m_Edits.push_back(std::move(s_Edit));
                }

                if (s_KitRemoved) {
                    ++s_Edited;
                    s_Removed += s_KitRemoved;
                    s_Done =
                        fmt::format(" -> sniper rifles made legal (removed {} exception(s){})", s_KitRemoved, s_Done);
                }
                else {
                    s_Done = fmt::format(" -> NOT changed{}", s_Done);
                }
            }

            LogTo(fr::LogTopic::Weapons, "legal sniper rifles: kit {} (weapons allowed: {}): {}{}{}", s_Name,
                  *s_WeaponsAllowed ? "yes" : "no", fr::KitSniperText(s_Verdict), s_Lists, s_Done);
        }

        LogTo(fr::LogTopic::Weapons,
              "legal sniper rifles: {}: {} new outfit kit(s) of {} actors: {} made legal ({} exception(s) removed{}); "
              "so far this scene: {} without visible firearms, {} without firearms but with a sniper allowed, {} "
              "already legal, {} made legal, {} with an unreadable exception (left alone)",
              p_When, s_NewKits.size(), s_Actors.size(), s_Edited, s_Removed,
              s_Failed ? fmt::format(", {} FAILED", s_Failed) : std::string(), m_Verdicts[0], m_Verdicts[1],
              m_Verdicts[2], m_Verdicts[3], m_Verdicts[4]);
    }
    catch (const std::exception& e) {
        Log("legal sniper rifles: {}: failed: {}", p_When, e.what());
    }
    catch (...) {
        Log("legal sniper rifles: {}: failed: unknown exception", p_When);
    }
}

void LegalSniperKits::Restore(const char* p_When) noexcept {
    try {
        size_t s_Restored = 0;
        size_t s_Left = 0;
        size_t s_Failed = 0;

        // Latest first, so a list edited twice (never expected) unwinds in order.
        for (auto s_It = m_Edits.rbegin(); s_It != m_Edits.rend(); ++s_It) {
            const Edit& s_Edit = *s_It;
            fr::RawArrayHeader s_Now;

            if (ReadEngineBytes(s_Edit.address, &s_Now, sizeof(s_Now)) || !(s_Now == s_Edit.written)) {
                ++s_Left;
                LogTo(fr::LogTopic::Weapons, "legal sniper rifles: {}: kit {} {} changed since the edit; left alone",
                      p_When, s_Edit.kit, s_Edit.list);
                continue;
            }

            if (!s_Edit.vanillaBytes.empty() &&
                WriteEngineBytes(s_Edit.elements, s_Edit.vanillaBytes.data(), s_Edit.vanillaBytes.size())) {
                ++s_Failed;
                continue;
            }

            if (WriteEngineBytes(s_Edit.address, &s_Edit.vanilla, sizeof(s_Edit.vanilla))) {
                ++s_Failed;
                continue;
            }

            ++s_Restored;
        }

        if (!m_Edits.empty())
            LogTo(fr::LogTopic::Weapons, "legal sniper rifles: {}: {} list(s) put back to vanilla{}{}", p_When,
                  s_Restored, s_Left ? fmt::format(", {} changed by the game and left alone", s_Left) : std::string(),
                  s_Failed ? fmt::format(", {} writes FAULTED", s_Failed) : std::string());
    }
    catch (const std::exception& e) {
        Log("legal sniper rifles: {}: restore failed: {}", p_When, e.what());
    }
    catch (...) {
        Log("legal sniper rifles: {}: restore failed: unknown exception", p_When);
    }

    m_Edits.clear();
    m_SeenKits.clear();
    m_Keywords.clear();
    m_Book = {};
    m_Verdicts = {};
    m_Armed = false;
}
