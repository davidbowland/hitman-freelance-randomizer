// Random loadout (see RandomLoadout.h). Engine reads and pin
// signals go through guarded steps (Guard.h).

#include "RandomLoadout.h"

#include "ClientReqs.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "FreelancerRandomizer.h"
#include "Guard.h"
#include "LogTopics.h"
#include "LootMerces.h"
#include "Merces.h"
#include "ModLog.h"
#include "PinIds.h"
#include "Scene.h"
#include "WeaponRarity.h"

#include <Functions.h>
#include <Globals.h>

#include <IModSDK.h>

#include <Glacier/ZHitman5.h>
#include <Glacier/ZInventory.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZObject.h>
#include <Glacier/ZPrimitives.h>
#include <Glacier/ZString.h>
#include <Glacier/ZTypeRegistry.h>
#include <Glacier/ZBitArray.h>
#include <Glacier/ZWorldInventory.h>
#include <Glacier/ZDelegate.h>
#include <Glacier/EDynamicEntityType.h>
#include <Glacier/Hash.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>

namespace {

constexpr uint64_t kInventoryItemsId = 0xedcffb1b8d7c1847;  // "Inventory items" (InventoryItems)
constexpr uint64_t kPersistentItemsId = 0xd651a1ce428435be; // "Persistent items" (the gear wall)
constexpr uint64_t kTransientItemsId = 0x5c76b8b6f24f80ba;  // "Transient items" (the Freelancer tools)

constexpr uint32_t kMaxPinLines = 200;    // per scene
constexpr uint32_t kMaxCreateLines = 100; // per scene
constexpr double kListWaitSeconds = 20.0; // game time after eLoading_ScenePlaying without the list's OnDone
constexpr double kHeroWaitSeconds = 10.0; // game time to wait for 47 and his inventory
constexpr double kConfirmSeconds = 15.0;  // game time to watch for the created items
constexpr double kScanEverySeconds = 0.5;
constexpr double kHandsWatchSeconds = 20.0; // game time after the delivery to report a delivered item in 47's hands
constexpr double kGrantDelaySeconds = fr::kGrantDelaySeconds; // the grants: game time after scene playing, at least
// The exit rounds (the grants' RemoveRepositoryId and the wall return): one this often,
constexpr double kExitRemoveEverySeconds = 0.5;
constexpr uint32_t kExitRemoveEveryTicks = 30; // ... or this many frames (game time may stand still at the exit)
constexpr size_t kExitRemoveMax = 120;         // ... at most this many
constexpr uint32_t kMaxLedgerLines = 200;      // per scene
constexpr uint32_t kMaxKindCensusLines = 60;   // item grants: census lines per kind and scene
constexpr const char* kStateFileName = "freelancerrandomizer-loadout.txt";
constexpr size_t kIdsPerLine = 8;
constexpr double kLoanRepairDelaySeconds = 3.0; // wall loans: game time after scene playing before the push back
constexpr double kHoldLogSeconds = 30.0;        // the blend hold: a "still held" line this often
constexpr uint32_t kHoldChangeLines = 40;       // the blend hold: "47's state changed" lines per hold

// 47's state bits and movement for the log.
std::string HeroStateLogText(const std::optional<uint64_t>& p_Mask, const std::optional<int32_t>& p_Move) {
    return fmt::format("state bits {} ({}), movement {}",
                       p_Mask ? fmt::format("0x{:012X}", *p_Mask) : std::string("not readable"),
                       p_Mask ? fr::HeroStateText(*p_Mask) : std::string("-"),
                       p_Move ? fr::MovementTypeName(*p_Move) : std::string("none or not readable"));
}

using PairArray = TArray<SDynamicObjectKeyValuePair>;

// What happens to a confirmed item's source entry: a Freelancer tool is taken
// off the tools; a gear wall item stays on the wall (fr::WallLoans); a spawned
// safehouse pickup has none.
std::string AfterConfirm(fr::LoadoutSource p_Source) {
    if (p_Source == fr::LoadoutSource::Spawned)
        return "spawned, not owned: in no list, nothing to remove or put back";
    return fr::RemovesFromSource(p_Source)
               ? fmt::format("removed from the {} at the next frame", fr::LoadoutSourceName(p_Source))
               : fmt::format("left on the {} (lent: it is there again after the mission)",
                             fr::LoadoutSourceName(p_Source));
}

constexpr std::string_view kTag = "loadout";

int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// --- guarded steps -------------------------------------------------------------

struct EventNameOut {
    char name[64];
};

// The event's "Name" string (top-level key), without serializing the event.
__declspec(noinline) void EventNameOf(const ZDynamicObject* p_Event, EventNameOut* p_Out) {
    p_Out->name[0] = '\0';
    const PairArray* s_Pairs = p_Event->As<PairArray>();

    if (!s_Pairs)
        return;

    for (size_t i = 0; i < s_Pairs->size(); ++i) {
        const SDynamicObjectKeyValuePair& s_Pair = (*s_Pairs)[i];

        if (s_Pair.sKey.ToStringView() != "Name")
            continue;

        if (const ZString* s_Text = s_Pair.value.As<ZString>()) {
            const std::string_view s_View = s_Text->ToStringView();
            const size_t s_Count = std::min(s_View.size(), sizeof(p_Out->name) - 1);
            std::memcpy(p_Out->name, s_View.data(), s_Count);
            p_Out->name[s_Count] = '\0';
        }

        return;
    }
}

__declspec(noinline) DWORD SafeEventName(const ZDynamicObject* p_Event, EventNameOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        EventNameOf(p_Event, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// An event as JSON (empty on a fault).
std::string EventJson(const ZDynamicObject& p_Event) {
    std::string s_Json;
    ReadDynamicObjectJson(p_Event, &s_Json);
    return s_Json;
}

struct RepoIdSignal {
    ZEntityType** entity;
    uint32_t pin;
    fr::Guid id;
};

// The pin with a ZRepositoryID payload, as the game's "Add inventory item"
// channel sends it.
__declspec(noinline) void SignalRepoIdOf(const RepoIdSignal* p_Args) {
    ZRepositoryID s_Id;
    std::memcpy(static_cast<void*>(&s_Id), &p_Args->id, sizeof(p_Args->id));
    ZEntityRef(p_Args->entity).SignalInputPin(p_Args->pin, ZObjectRef::From<ZRepositoryID>(s_Id));
}

__declspec(noinline) DWORD SafeSignalRepoId(const RepoIdSignal* p_Args) {
    DWORD s_Code = 0;

    __try {
        SignalRepoIdOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// A ZRepositoryID pin payload's id (p_Type: ZRepositoryID's STypeID), or
// empty with *p_Why.
std::optional<fr::Guid> RepoIdPayload(const ZObjectRef& p_Data, const void* p_Type, std::string* p_Why) {
    PinPayload s_Payload;
    fr::Guid s_Id;

    if (const uint32_t s_Code = ReadPinPayload(p_Data, &s_Payload)) {
        *p_Why = FaultText(s_Code);
        return std::nullopt;
    }

    if (!s_Payload.type || s_Payload.type != p_Type || !s_Payload.data) {
        *p_Why = "not a ZRepositoryID";
        return std::nullopt;
    }

    if (const uint32_t s_Code = ReadEngineBytes(reinterpret_cast<uintptr_t>(s_Payload.data), &s_Id, sizeof(s_Id))) {
        *p_Why = FaultText(s_Code);
        return std::nullopt;
    }

    return s_Id;
}

// --- 47 and the world inventory ------------------------------------------------------

struct HeroOut {
    ZEntityType** entity;
    ZHitman5* hitman;
};

__declspec(noinline) void HeroOf(HeroOut* p_Out) {
    const TEntityRef<ZHitman5> s_Hero = SDK()->GetLocalPlayer();
    p_Out->entity = s_Hero.m_entityRef.m_pObj;
    p_Out->hitman = s_Hero.m_pInterfaceRef;
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

constexpr size_t kMaxControllers = 32;

// 47's ZCharacter subcontrollers (ZCharacterSubcontrollerContainer::
// m_aReferencedControllers; the SDK's Assets and Player mods take index 6 as
// the inventory).
struct ControllersOut {
    size_t count;
    ZEntityType** entity[kMaxControllers];
    void* iface[kMaxControllers];
};

__declspec(noinline) void ControllersOf(ZHitman5* p_Hero, ControllersOut* p_Out) {
    p_Out->count = 0;
    ZCharacter* const s_Character = p_Hero->m_pCharacter.m_pInterfaceRef;

    if (!s_Character)
        return;

    ZCharacterSubcontrollerContainer* const s_Container = s_Character->m_rSubcontrollerContainer.m_pInterfaceRef;

    if (!s_Container)
        return;

    const auto& s_List = s_Container->m_aReferencedControllers;
    p_Out->count = s_List.size();

    for (size_t i = 0; i < std::min(s_List.size(), kMaxControllers); ++i) {
        p_Out->entity[i] = s_List[i].m_entityRef.m_pObj;
        p_Out->iface[i] = s_List[i].m_pInterfaceRef;
    }
}

__declspec(noinline) DWORD SafeControllers(ZHitman5* p_Hero, ControllersOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ControllersOf(p_Hero, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct CreateArgs {
    void* inventory;
    fr::Guid id;
    int32_t type;
    void* result;
};

// ZCharacterSubcontrollerInventory::CreateItem(id, "", {}, type), as the SDK's
// Assets mod calls it for "Add to inventory".
__declspec(noinline) void CreateItemOf(CreateArgs* p_Args) {
    ZRepositoryID s_Id;
    std::memcpy(static_cast<void*>(&s_Id), &p_Args->id, sizeof(p_Args->id));
    const ZString s_Instance("");
    const TArray<ZRepositoryID> s_Modifiers;
    p_Args->result = Functions::ZCharacterSubcontrollerInventory_CreateItem->Call(
        static_cast<ZCharacterSubcontrollerInventory*>(p_Args->inventory), s_Id, s_Instance, s_Modifiers,
        static_cast<ZCharacterSubcontrollerInventory::ECreateItemType>(p_Args->type));
}

__declspec(noinline) DWORD SafeCreateItem(CreateArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        CreateItemOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

constexpr size_t kMaxWorldItems = 8192;

struct ScanRow {
    ZEntityType** entity;
    ZEntityType** owner;
    uint8_t id[16];
    uint8_t hasId;
};

struct ScanArgs {
    const void* itemType; // STypeID of ZHM5Item
    ScanRow* rows;
    size_t capacity;
    size_t count;
    size_t entries;
    bool noInventory;
    ZWorldInventory* world; // the resolved world inventory (RandomLoadout::ResolveWorldInventory)
};

// Every ZHM5Item of ZWorldInventory::m_aItems: its entity, owner
// (m_pOwner, 47 for his inventory) and repository id
// (m_pItemConfigDescriptor->m_ItemID).
__declspec(noinline) void ScanOf(ScanArgs* p_Args) {
    p_Args->count = 0;
    p_Args->entries = 0;
    p_Args->noInventory = false;
    ZWorldInventory* const s_World = p_Args->world;

    if (!s_World) {
        p_Args->noInventory = true;
        return;
    }

    const auto& s_Items = s_World->m_aItems;
    p_Args->entries = s_Items.size();

    for (size_t i = 0; i < s_Items.size() && p_Args->count < p_Args->capacity; ++i) {
        const ZEntityRef& s_Ref = s_Items[i].m_entityRef;

        if (!s_Ref.m_pObj)
            continue;

        auto* const s_Item =
            static_cast<ZHM5Item*>(s_Ref.QueryInterface(static_cast<const STypeID*>(p_Args->itemType)));

        if (!s_Item)
            continue;

        ScanRow& s_Row = p_Args->rows[p_Args->count++];
        s_Row.entity = s_Ref.m_pObj;
        s_Row.owner = s_Item->m_pOwner.m_pObj;
        s_Row.hasId = 0;

        if (const ZItemConfigDescriptor* s_Config = s_Item->m_pItemConfigDescriptor) {
            std::memcpy(s_Row.id, static_cast<const void*>(&s_Config->m_ItemID), sizeof(s_Row.id));
            s_Row.hasId = 1;
        }
    }
}

__declspec(noinline) DWORD SafeScan(ScanArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        ScanOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// --- the briefcase path (evergreen_setup) -----------------------------------------
//
// Sub-entity ids of the evergreen_setup entities on the game's briefcase and
// pocket paths, in RandomLoadout::Flow order.

struct FlowEntity {
    uint64_t id;
    const char* name;
};

constexpr std::array<FlowEntity, 9> kFlowEntities {{
    {fr::kPurchasedActionsId, "Purchased item actions"},
    {0xd1c25638f27943c5, "Briefcase"},
    {0x58a882e3958458a1, "Briefcase container actions"},
    {0x1bdef8a37f428ec5, "Briefcase actions"},
    {0x8ab0dfbd897de3f6, "Existing briefcase container actions"},
    {0x67ddd7e8ef0ff589, "Existing briefcase actions"},
    {0x841529469c77e1e9, "EvergreenGearPersistenceHelper"},
    {0x6d07147f642a3aeb, "Loaded item actions"},
    // The pocket items' path: SetItem -> OnItemChanged -> PickupIntoPocket.
    {0xe15f202bdc5860fd, "HeroItemAction"},
}};

// A pin payload: its type and, when it holds an entity reference (ZEntityRef
// or TEntityRef<...>, the entity first), the entity.
struct EntityPayloadOut {
    const STypeID* type;
    char typeName[96];
    uint16_t size;
    ZEntityType** entity;
};

__declspec(noinline) void EntityPayloadOf(const ZObjectRef* p_Data, EntityPayloadOut* p_Out) {
    p_Out->type = p_Data->GetTypeID();
    p_Out->typeName[0] = '\0';
    p_Out->size = 0;
    p_Out->entity = nullptr;

    if (!p_Out->type)
        return;

    const IType* s_Info = p_Out->type->GetTypeInfo();

    if (!s_Info)
        return;

    p_Out->size = s_Info->m_nTypeSize;

    if (s_Info->pszTypeName) {
        const std::string_view s_Name = s_Info->pszTypeName;
        const size_t s_Count = std::min(s_Name.size(), sizeof(p_Out->typeName) - 1);
        std::memcpy(p_Out->typeName, s_Name.data(), s_Count);
        p_Out->typeName[s_Count] = '\0';
    }

    const std::string_view s_Name = p_Out->typeName;
    const void* s_Bytes = p_Data->GetData();

    if (s_Bytes && p_Out->size >= 8 && (s_Name == "ZEntityRef" || s_Name.starts_with("TEntityRef<")))
        std::memcpy(&p_Out->entity, s_Bytes, sizeof(p_Out->entity));
}

__declspec(noinline) DWORD SafeEntityPayload(const ZObjectRef* p_Data, EntityPayloadOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        EntityPayloadOf(p_Data, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// ZWorldInventory::RequestNewItem(id, delegate, a new dynamic id of 47's,
// not loading, no parent, created by 47), as the SDK's Editor mod spawns a
// weapon for an actor. The item comes unowned, through the delegate.
struct RequestArgs {
    RandomLoadout* self;
    ZEntityType** hero;
    fr::Guid id;
    ZWorldInventory* world;
    uint32_t ticket;
    bool invalid;
    bool noInventory;
    int stage;         // the last step begun (RequestStageName), for a fault
    uint64_t entityId; // GenerateDynamicObjectID's
};

const char* RequestStageName(int p_Stage) {
    switch (p_Stage) {
        case 0: return "before any call";
        case 1: return "in ZEntityManager::GenerateDynamicObjectID";
        case 2: return "building the delegate";
        case 3: return "in ZWorldInventory::RequestNewItem";
        case 4: return "reading the invalid ticket";
        case 5: return "after RequestNewItem returned";
        default: return "?";
    }
}

__declspec(noinline) void RequestNewItemOf(RequestArgs* p_Args) {
    p_Args->ticket = 0;
    p_Args->invalid = false;
    p_Args->stage = 0;
    p_Args->entityId = 0;
    p_Args->noInventory = !p_Args->world || !Globals::EntityManager;

    if (p_Args->noInventory)
        return;

    ZRepositoryID s_Id;
    std::memcpy(static_cast<void*>(&s_Id), &p_Args->id, sizeof(p_Args->id));
    ZEntityRef s_Hero(p_Args->hero);
    p_Args->stage = 1;
    const uint64_t s_EntityId = Functions::ZEntityManager_GenerateDynamicObjectID->Call(
        Globals::EntityManager, s_Hero, EDynamicEntityType::eDET_CharacterInventoryItem, 0);
    p_Args->entityId = s_EntityId;
    p_Args->stage = 2;
    const ZMemberDelegate<RandomLoadout, void(uint32_t, TEntityRef<IItemBase>)> s_Delegate(
        p_Args->self, &RandomLoadout::OnBriefcaseItemCreated);
    const ZEntityRef s_Creator(p_Args->hero);
    p_Args->stage = 3;
    p_Args->ticket = Functions::ZWorldInventory_RequestNewItem->Call(p_Args->world, s_Id, s_Delegate, s_EntityId, false,
                                                                     {}, s_Creator);
    p_Args->stage = 4;
    p_Args->invalid = Globals::WorldInventory_InvalidTicket && p_Args->ticket == *Globals::WorldInventory_InvalidTicket;
    p_Args->stage = 5;
}

__declspec(noinline) DWORD SafeRequestNewItem(RequestArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        RequestNewItemOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// An input pin with an item as its payload, in the type the game's own
// SetItem got (p_Type: ZEntityRef, or TEntityRef<I> with the item's I).
struct ItemSignal {
    ZEntityType** target;
    uint32_t pin;
    const STypeID* type;
    ZEntityType** item;
    bool sent;
    char why[96];
};

__declspec(noinline) void SignalItemOf(ItemSignal* p_Args) {
    p_Args->sent = false;
    p_Args->why[0] = '\0';
    const auto s_Why = [&](const char* p_Text) { std::snprintf(p_Args->why, sizeof(p_Args->why), "%s", p_Text); };

    const IType* s_Info = p_Args->type ? p_Args->type->GetTypeInfo() : nullptr;

    if (!s_Info || !s_Info->pszTypeName) {
        s_Why("no payload type");
        return;
    }

    const std::string_view s_Name = s_Info->pszTypeName;
    const ZEntityRef s_Item(p_Args->item);
    alignas(16) uint8_t s_Bytes[32] {};

    if (s_Name == "ZEntityRef" && s_Info->m_nTypeSize == sizeof(ZEntityRef)) {
        std::memcpy(s_Bytes, &s_Item, sizeof(s_Item));
    }
    else if (s_Name.starts_with("TEntityRef<") && s_Name.ends_with(">") && s_Info->m_nTypeSize == 16) {
        const std::string s_Inner(s_Name.substr(11, s_Name.size() - 12));
        STypeID* const s_InnerType = (*Globals::TypeRegistry)->GetTypeID(ZString(s_Inner));

        if (!s_InnerType) {
            std::snprintf(p_Args->why, sizeof(p_Args->why), "interface type %s not registered", s_Inner.c_str());
            return;
        }

        void* const s_Interface = s_Item.QueryInterface(s_InnerType);

        if (!s_Interface) {
            std::snprintf(p_Args->why, sizeof(p_Args->why), "the item has no %s interface", s_Inner.c_str());
            return;
        }

        std::memcpy(s_Bytes, &s_Item, sizeof(s_Item));
        std::memcpy(s_Bytes + 8, &s_Interface, sizeof(s_Interface));
    }
    else {
        s_Why("payload type is neither ZEntityRef nor TEntityRef<...>");
        return;
    }

    ZObjectRef s_Payload;
    s_Payload.Assign(const_cast<STypeID*>(p_Args->type), s_Bytes);
    ZEntityRef(p_Args->target).SignalInputPin(p_Args->pin, s_Payload);
    p_Args->sent = true;
}

__declspec(noinline) DWORD SafeSignalItem(ItemSignal* p_Args) {
    DWORD s_Code = 0;

    __try {
        SignalItemOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct VoidSignal {
    ZEntityType** target;
    uint32_t pin;
};

__declspec(noinline) void SignalVoidOf(const VoidSignal* p_Args) {
    ZEntityRef(p_Args->target).SignalInputPin(p_Args->pin, ZObjectRef());
}

__declspec(noinline) DWORD SafeSignalVoid(const VoidSignal* p_Args) {
    DWORD s_Code = 0;

    __try {
        SignalVoidOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// An item entity's ZHM5Item (by the registered type), owner and repository id.
struct ItemOut {
    ZHM5Item* item;
    ZEntityType** owner;
    uint8_t id[16];
    bool hasId;
};

__declspec(noinline) void ItemOf(ZEntityType** p_Entity, ItemOut* p_Out) {
    p_Out->item = nullptr;
    p_Out->owner = nullptr;
    p_Out->hasId = false;

    if (!p_Entity || !Globals::TypeRegistry || !*Globals::TypeRegistry)
        return;

    STypeID* const s_Type = (*Globals::TypeRegistry)->GetTypeID(ZString("ZHM5Item"));

    if (!s_Type)
        return;

    p_Out->item = static_cast<ZHM5Item*>(ZEntityRef(p_Entity).QueryInterface(static_cast<const STypeID*>(s_Type)));

    if (!p_Out->item)
        return;

    p_Out->owner = p_Out->item->m_pOwner.m_pObj;

    if (const ZItemConfigDescriptor* s_Config = p_Out->item->m_pItemConfigDescriptor) {
        std::memcpy(p_Out->id, static_cast<const void*>(&s_Config->m_ItemID), sizeof(p_Out->id));
        p_Out->hasId = true;
    }
}

__declspec(noinline) DWORD SafeItem(ZEntityType** p_Entity, ItemOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ItemOf(p_Entity, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// ZWorldInventory::DestroyItem, as the SDK's Editor mod removes an item
// (only for an item 47 does not hold).
__declspec(noinline) void DestroyItemOf(ZWorldInventory* p_World, ZEntityType** p_Entity, ZHM5Item* p_Item) {
    TEntityRef<IItemBase> s_Ref;
    s_Ref.m_entityRef = ZEntityRef(p_Entity);
    s_Ref.m_pInterfaceRef = static_cast<IItemBase*>(p_Item);
    Functions::ZWorldInventory_DestroyItem->Call(p_World, s_Ref);
}

__declspec(noinline) DWORD SafeDestroyItem(ZWorldInventory* p_World, ZEntityType** p_Entity, ZHM5Item* p_Item) {
    DWORD s_Code = 0;

    __try {
        DestroyItemOf(p_World, p_Entity, p_Item);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct InstanceOut {
    char text[64];
};

__declspec(noinline) void InstanceOf(const ZString* p_Text, InstanceOut* p_Out) {
    const std::string_view s_View = p_Text->ToStringView();
    const size_t s_Count = std::min(s_View.size(), sizeof(p_Out->text) - 1);
    std::memcpy(p_Out->text, s_View.data(), s_Count);
    p_Out->text[s_Count] = '\0';
}

__declspec(noinline) DWORD SafeInstance(const ZString* p_Text, InstanceOut* p_Out) {
    DWORD s_Code = 0;
    p_Out->text[0] = '\0';

    __try {
        InstanceOf(p_Text, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// --- items ------------------------------------------------------------------------

std::vector<std::string> SplitTraits(std::string_view p_Text) {
    std::vector<std::string> s_Out;
    size_t s_At = 0;

    while (s_At <= p_Text.size()) {
        const size_t s_Comma = p_Text.find(',', s_At);
        const std::string_view s_Part =
            p_Text.substr(s_At, s_Comma == std::string_view::npos ? std::string_view::npos : s_Comma - s_At);

        if (!s_Part.empty())
            s_Out.push_back(fr::ToLower(s_Part));

        if (s_Comma == std::string_view::npos)
            break;

        s_At = s_Comma + 1;
    }

    return s_Out;
}

using EntryMap = std::map<fr::Guid, const RepoLoadoutEntry*>;

// How briefcase items are recognized (fr::IsBriefcaseItem): trait "case" (the
// ICA Briefcase itself); else the HolsterAbility alone when the item has one;
// only without one, pro.repo ItemSize containing "large" when any of the
// player's items has a string ItemSize (else the icon guess).
struct BriefcaseRule {
    bool itemSize = false;
};

fr::LoadoutItem MakeItem(const fr::Guid& p_Id, fr::LoadoutSource p_Source, const EntryMap& p_Entries,
                         const BriefcaseRule& p_Rule) {
    fr::LoadoutItem s_Item;
    s_Item.id = p_Id;
    s_Item.source = p_Source;
    const auto s_It = p_Entries.find(p_Id);

    if (s_It == p_Entries.end()) {
        s_Item.known = false;
        s_Item.name = fr::FormatGuid(p_Id);
        return s_Item;
    }

    const RepoLoadoutEntry& s_Entry = *s_It->second;
    s_Item.name = s_Entry.title[0] ? s_Entry.title : fr::FormatGuid(p_Id);

    if (s_Entry.commonName[0] && std::strcmp(s_Entry.commonName, s_Entry.title) != 0)
        s_Item.name += std::string(" / ") + s_Entry.commonName;

    s_Item.kind = s_Entry.itemType;
    s_Item.icon = fr::ToLower(s_Entry.icon);
    s_Item.traits = SplitTraits(s_Entry.traits);
    s_Item.perks = SplitTraits(s_Entry.perks);
    s_Item.capacity = s_Entry.hasCapacity ? static_cast<int32_t>(s_Entry.capacity + 0.5) : 0;
    s_Item.rarity = s_Entry.hasRarity ? static_cast<int32_t>(s_Entry.rarity + 0.5) : 0;
    s_Item.silenced = fr::IsSilencedAudio(s_Entry.audioHead, s_Entry.audioTail);
    s_Item.dartGun = fr::IsFirearmItem(s_Item) && fr::IsDartGun(s_Entry.audioExotic, s_Item.name);
    s_Item.itemSize = s_Entry.itemSize;
    s_Item.holster = s_Entry.holster;
    s_Item.briefcase = fr::IsBriefcaseItem(s_Item, p_Rule.itemSize);
    return s_Item;
}

std::string ItemText(const fr::LoadoutItem& p_Item) {
    if (!p_Item.known)
        return fmt::format("{} (not in pro.repo)", fr::FormatGuid(p_Item.id));

    std::string s_Traits;
    for (const std::string& s_Trait : p_Item.traits)
        s_Traits += (s_Traits.empty() ? "" : ",") + s_Trait;

    std::string s_Perks;
    for (const std::string& s_Perk : p_Item.perks)
        s_Perks += (s_Perks.empty() ? "" : ",") + s_Perk;

    return fmt::format("{} \"{}\" [{}{}{}] cost {}{}{}{}{}{}", fr::FormatGuid(p_Item.id), p_Item.name, p_Item.icon,
                       s_Traits.empty() ? "" : "; ", s_Traits, fr::SlotCost(p_Item),
                       p_Item.rarity ? fmt::format(", {}", fr::RarityName(p_Item.rarity)) : std::string(),
                       p_Item.silenced ? ", silenced" : "", p_Item.dartGun ? ", dart gun" : "",
                       p_Item.briefcase ? ", BRIEFCASE" : "",
                       fr::IsFirearmItem(p_Item) ? fmt::format(" (ItemSize \"{}\", HolsterAbility \"{}\", Perks [{}])",
                                                               p_Item.itemSize, p_Item.holster, s_Perks)
                                                 : std::string());
}

// --- the game's exe: the SDK's world inventory pointer and patterns --------------------------
//
// The SDK's Globals::WorldInventory can be an empty inventory on which
// RequestNewItem faults. So the pointer is checked by its RTTI before any
// call, the live ZWorldInventory is looked for by its class, and the SDK's
// RequestNewItem address must be its pattern's only match in the exe.

// The SDK's RequestNewItem pattern (ZHMModSDK Functions.cpp).
const std::string kRequestNewItemPattern("\x4C\x8B\xDC\x49\x89\x6B\x08", 7);
constexpr std::string_view kRequestNewItemMask = "xxxxxxx";

fr::MemReader GameMemory() {
    return [](uint64_t p_Address, void* p_Out, size_t p_Size) {
        if (p_Address < 0x10000 || p_Address + p_Size < p_Address)
            return false;
        return ReadEngineBytes(static_cast<uintptr_t>(p_Address), p_Out, p_Size) == 0;
    };
}

std::optional<uint64_t> ReadQword(uint64_t p_Address) {
    uint64_t s_Value = 0;
    if (!GameMemory()(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;
    return s_Value;
}

// --- 47's state bits (the blend hold) ------------------------------------------------------
//
// ZHitman5 keeps its EHeroGameState bits as a uint64 at +0xE28 and its current
// movement at +0xAA0 (EBaseMovementType at +0x28 of it); see fr::JudgeBlendHold
// in core/Loadout.h for how that was found. Both offsets are trusted only if
// the two code sites that use them are found once each in the exe, with these
// offsets in them: ZHeroStateConditionEntity's update (mov rdx, [rcx+0x20]
// (its 47) ... test [rdx+0xE28], rax with rax = 1 << m_eState) and the start
// of ZHitman5's movement-bits update (mov rax, [rcx+0xAA0] ... and
// [rcx+0xE28] ... mov r11d, [rax+0x28]).
constexpr size_t kHeroStateMaskOffset = 0xE28;
constexpr size_t kHeroMovementOffset = 0xAA0;
constexpr size_t kMovementTypeOffset = 0x28;

const std::string kHeroStateReadPattern("\x48\x8B\x51\x20\x48\x8B\xD9\x48\x85\xD2\x0F\x84\x00\x00\x00\x00\x0F\xB6\x49"
                                        "\x50\xB8\x01\x00\x00\x00\x48\xD3\xE0\x48\x85\x82\x28\x0E\x00\x00\x0F\x97"
                                        "\xC0",
                                        38);
constexpr std::string_view kHeroStateReadMask = "xxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxx";
const std::string kHeroStateWritePattern(
    "\x48\x83\xEC\x28\x48\x8B\x81\xA0\x0A\x00\x00\x4C\x8B\xD1\x48\x85\xC0\x75\x00\x48\xB8\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x48\x21\x81\x28\x0E\x00\x00\x48\xB9\x00\x00\x00\x00\x00\x00\x00\x00\x49\x21\x8A\x28"
    "\x0E\x00\x00\x48\x83\xC4\x28\xC3\x44\x8B\x58\x28",
    62);
constexpr std::string_view kHeroStateWriteMask = "xxxxxxxxxxxxxxxxxx?xx????????xxxxxxxxx????????xxxxxxxxxxxxxxxx";

// The address an EngineFunction calls (its first member, m_Address).
uint64_t EngineFunctionAddress(const void* p_Function) {
    return p_Function ? ReadQword(reinterpret_cast<uint64_t>(p_Function)).value_or(0) : 0;
}

// Committed, readable memory (VirtualQuery), cached per region.
struct RegionCache {
    uint64_t start = 0;
    uint64_t end = 0;
    bool readable = false;
};

bool Readable(uint64_t p_Address, RegionCache& p_Cache) {
    if (p_Address >= p_Cache.start && p_Address < p_Cache.end)
        return p_Cache.readable;

    MEMORY_BASIC_INFORMATION s_Info {};
    if (!VirtualQuery(reinterpret_cast<const void*>(p_Address), &s_Info, sizeof(s_Info))) {
        p_Cache = {p_Address, p_Address + 1, false};
        return false;
    }

    p_Cache.start = reinterpret_cast<uint64_t>(s_Info.BaseAddress);
    p_Cache.end = p_Cache.start + s_Info.RegionSize;
    const DWORD s_Protect = s_Info.Protect & 0xFF;
    p_Cache.readable = s_Info.State == MEM_COMMIT && !(s_Info.Protect & PAGE_GUARD) &&
                       (s_Protect == PAGE_READONLY || s_Protect == PAGE_READWRITE || s_Protect == PAGE_WRITECOPY ||
                        s_Protect == PAGE_EXECUTE_READ || s_Protect == PAGE_EXECUTE_READWRITE);
    return p_Cache.readable;
}

// The exe's writable qwords, each called with (its address, its value).
template <class F>
void ForEachDataQword(const fr::ImageInfo& p_Image, F&& p_Body) {
    std::vector<uint64_t> s_Buffer;
    for (const fr::ImageSection& s_Section : p_Image.sections) {
        if (!s_Section.writable || s_Section.executable)
            continue;
        constexpr size_t kChunkQwords = 1 << 17;
        for (uint64_t s_At = s_Section.start; s_At + 8 <= s_Section.start + s_Section.size;) {
            const size_t s_Count =
                static_cast<size_t>(std::min<uint64_t>(kChunkQwords, (s_Section.start + s_Section.size - s_At) / 8));
            s_Buffer.resize(s_Count);
            if (GameMemory()(s_At, s_Buffer.data(), s_Count * 8)) {
                for (size_t i = 0; i < s_Count; ++i)
                    p_Body(s_At + i * 8, s_Buffer[i]);
            }
            s_At += s_Count * 8;
        }
    }
}

struct Holder {
    uint64_t holder; // the exe's qword holding the pointer
    uint64_t object;
};

// Objects outside the exe whose vtable is one of p_Vtables, reached from a
// pointer in the exe's writable sections (a heap singleton's global).
std::vector<Holder> HeapHolders(const fr::ImageInfo& p_Image, const std::vector<uint64_t>& p_Vtables, size_t p_Max,
                                size_t& p_Considered) {
    std::vector<Holder> s_Pointers;
    ForEachDataQword(p_Image, [&](uint64_t p_At, uint64_t p_Value) {
        if (p_Value < 0x10000 || p_Value >= 0x7FFFFFFF0000ull || (p_Value & 7) || p_Image.Contains(p_Value))
            return;
        if (s_Pointers.size() < (size_t(1) << 20))
            s_Pointers.push_back({p_At, p_Value});
    });
    p_Considered = s_Pointers.size();

    std::sort(s_Pointers.begin(), s_Pointers.end(),
              [](const Holder& a, const Holder& b) { return a.object < b.object; });

    std::vector<Holder> s_Found;
    RegionCache s_Cache;
    for (const Holder& s_Pointer : s_Pointers) {
        if (!Readable(s_Pointer.object, s_Cache) || s_Pointer.object + 8 > s_Cache.end)
            continue;
        const std::optional<uint64_t> s_Vtable = ReadQword(s_Pointer.object);
        if (!s_Vtable || std::find(p_Vtables.begin(), p_Vtables.end(), *s_Vtable) == p_Vtables.end())
            continue;
        s_Found.push_back(s_Pointer);
        if (s_Found.size() >= p_Max)
            break;
    }
    return s_Found;
}

} // namespace

// --- configuration and state file ---------------------------------------------------

void RandomLoadout::Configure(bool p_On, double p_DelaySeconds, bool p_Briefcase, bool p_LockpickFirst,
                              bool p_ObjectiveItems) {
    m_On = p_On;
    m_DelaySeconds = p_DelaySeconds;
    m_Briefcase = p_Briefcase;
    m_LockpickFirst = p_LockpickFirst;
    m_ObjectiveItems = p_ObjectiveItems;
}

std::filesystem::path RandomLoadout::StatePath() const {
    const std::filesystem::path s_Directory = ModDirectory();
    return s_Directory.empty() ? s_Directory : s_Directory / kStateFileName;
}

void RandomLoadout::LoadState() noexcept {
    Contained(kTag, "reading the state file", [&] {
        std::lock_guard s_Lock(m_Mutex);
        const std::filesystem::path s_Path = StatePath();
        std::error_code s_Error;

        if (s_Path.empty() || !std::filesystem::exists(s_Path, s_Error)) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout: no state file {} yet (written at the first CpdSet of a Freelancer scene)",
                  s_Path.empty() ? std::string(kStateFileName) : s_Path.string());
            return;
        }

        std::ifstream s_In(s_Path, std::ios::binary);
        std::stringstream s_Buffer;
        s_Buffer << s_In.rdbuf();
        const std::string s_Text = s_Buffer.str();

        if (const auto s_Live = fr::ParseLoadoutState(s_Text, "live"))
            m_Live = *s_Live;

        m_Departure = fr::ParseLoadoutState(s_Text, "departure");
        m_SafehouseCpdSinceFreeze = s_Text.find("meta.safehouse_cpd_since_freeze=1") != std::string::npos;
        m_Loans = fr::ParseWallLoans(s_Text);

        for (const fr::WallLoan& s_Loan : m_Loans.loans)
            LogTo(fr::LogTopic::Loadout,
                  "loadout: state file: \"{}\" ({}) is lent from your gear wall (put back {} time(s) so far); checked "
                  "at the next Freelancer scene",
                  s_Loan.name, fr::FormatGuid(s_Loan.id), s_Loan.repairs);

        const auto s_Count = [](const fr::LoadoutState& p_State, std::string_view p_Key) {
            const auto* s_List = p_State.List(p_Key);
            return s_List ? fmt::format("{}", s_List->size()) : std::string("-");
        };

        LogTo(fr::LogTopic::Loadout,
              "loadout: state file {}: live (updated {}): InventoryItems {}, PersistentItems {}, TransientItems {}, "
              "GearSlotsTotal {}; departure snapshot {}; safehouse CpdSet since the last freeze: {}",
              s_Path.string(), m_Live.updatedUnix, s_Count(m_Live, "InventoryItems"),
              s_Count(m_Live, "PersistentItems"), s_Count(m_Live, "TransientItems"),
              m_Live.slotsTotal ? fmt::format("{}", *m_Live.slotsTotal) : "-",
              m_Departure ? fmt::format("(updated {})", m_Departure->updatedUnix) : std::string("none"),
              m_SafehouseCpdSinceFreeze);
    });
}

void RandomLoadout::SaveState(const char* p_Why) {
    const std::filesystem::path s_Path = StatePath();

    if (s_Path.empty()) {
        Log("loadout: state not saved ({}): the mods folder is unknown", p_Why);
        return;
    }

    std::string s_Text = std::string(fr::LoadoutStateHeader()) + "\n";
    s_Text += fr::FormatLoadoutState(m_Live, "live");

    if (m_Departure)
        s_Text += fr::FormatLoadoutState(*m_Departure, "departure");

    s_Text += fmt::format("meta.safehouse_cpd_since_freeze={}\n", m_SafehouseCpdSinceFreeze ? 1 : 0);
    s_Text += fr::FormatWallLoans(m_Loans);

    std::filesystem::path s_Temp = s_Path;
    s_Temp += ".tmp";
    bool s_Written = false;
    {
        std::ofstream s_Out(s_Temp, std::ios::binary | std::ios::trunc);

        if (s_Out) {
            s_Out.write(s_Text.data(), static_cast<std::streamsize>(s_Text.size()));
            s_Out.close();
            s_Written = static_cast<bool>(s_Out);
        }
    }

    std::error_code s_Error;

    if (s_Written)
        std::filesystem::rename(s_Temp, s_Path, s_Error);

    if (!s_Written || s_Error) {
        std::filesystem::remove(s_Temp, s_Error);
        Log("loadout: could not write {} ({})", s_Path.string(), p_Why);
    }
}

// --- scene lifecycle ------------------------------------------------------------------

void RandomLoadout::OnSceneLoad(const std::string& p_Scene, const std::string& p_Type,
                                const std::optional<fr::SeedResolution>& p_Seed, const char* p_When) noexcept {
    Contained(kTag, "scene load", [&] { SceneLoad(p_Scene, p_Type, p_Seed, p_When); });
}

void RandomLoadout::SceneLoad(const std::string& p_Scene, const std::string& p_Type,
                              const std::optional<fr::SeedResolution>& p_Seed, const char* p_When) {
    std::lock_guard s_Lock(m_Mutex);

    m_InventoryList.store(nullptr, std::memory_order_release);
    m_Scene = p_Scene;
    m_Freelancer.store(fr::IsFreelancerScene(p_Type), std::memory_order_relaxed);
    m_Mission = fr::IsFreelancerMission(p_Type, p_Scene);
    m_Safehouse = m_Freelancer.load(std::memory_order_relaxed) && !m_Mission;
    m_Plan.clear();
    m_Names.clear();
    m_Packed.clear();
    m_Planned = false;
    m_PendingEntries.reset();
    m_PendingSeed.reset();
    m_EntitiesFound = false;
    m_PersistentList = nullptr;
    m_TransientList = nullptr;
    m_Emitted = 0;
    m_PinLines = 0;
    m_IterationDone = false;
    m_Phase = Phase::Idle;
    m_Timer = 0;
    m_ScanTimer = 0;
    m_HeroInventory = nullptr;
    m_Hero = nullptr;
    m_World = nullptr;
    m_WorldResolved = false;
    m_WorldWhy = "not resolved yet";
    m_PackedCreateType.reset();
    m_CreateLines = 0;
    m_ScanFaultLogged = false;
    m_FlowOn.store(false, std::memory_order_release);
    for (auto& s_Watch : m_FlowWatch)
        s_Watch.store(nullptr, std::memory_order_release);
    m_FlowEntities.fill(nullptr);

    if (m_Job.step == BriefcaseJob::Step::WaitItem)
        LogTo(fr::LogTopic::Loadout,
              "loadout briefcase: a new scene loads while the item was still awaited from the world inventory; a late "
              "item is ignored");
    m_Job = BriefcaseJob {};
    m_Hold = BlendHold {};
    m_GrantListOn.store(false, std::memory_order_release);
    m_Grant = LockpickGrant {};
    ResetItemGrants();
    m_Exit = GrantExit {};
    m_GuardedIds.clear();
    m_Ledger.Clear();
    m_LedgerLines = 0;
    m_LoanRepairs.clear();
    m_LoanRepairDone = false;
    m_Return = WallReturn {};
    m_ScenePlaying = false;
    m_PlayingSeconds = 0;

    if (!m_RepoIdType.load(std::memory_order_relaxed) && Globals::TypeRegistry && *Globals::TypeRegistry)
        m_RepoIdType.store((*Globals::TypeRegistry)->GetTypeID(ZString("ZRepositoryID")), std::memory_order_release);

    // The safehouse: where is each lent gear wall item now (the mission's CpdSet is merged)?
    if (m_Safehouse && !m_Loans.loans.empty())
        ReviewWallLoans(p_When, m_Live, nullptr);

    if (!m_Mission)
        return;

    // One pro.repo scan for the grants' censuses.
    if (m_GrantMode != fr::LockpickGrantMode::Off || AnyItemGrant()) {
        std::vector<RepoLoadoutEntry> s_Entries;
        std::string s_Why;
        const bool s_Scanned = GrantScan(s_Entries, s_Why);

        if (m_GrantMode != fr::LockpickGrantMode::Off)
            GrantCensus(p_When, s_Scanned ? &s_Entries : nullptr, s_Why);
        if (AnyItemGrant())
            ItemGrantCensus(p_When, s_Scanned ? &s_Entries : nullptr, s_Why);
    }

    // The departure snapshot: frozen from the live state when the safehouse
    // sent a CpdSet since the last freeze (or there is none yet); a restart
    // keeps it, so it plans the same loadout.
    bool s_Frozen = false;

    if (m_SafehouseCpdSinceFreeze || !m_Departure) {
        m_Departure = m_Live;
        m_SafehouseCpdSinceFreeze = false;
        s_Frozen = true;
        SaveState("departure frozen");
        LogTo(fr::LogTopic::Loadout, "loadout: {}: departure snapshot frozen from the merged CPD (updated {} s ago)",
              p_When, m_Live.updatedUnix ? UnixNow() - m_Live.updatedUnix : -1);
    }
    else {
        LogTo(fr::LogTopic::Loadout,
              "loadout: {}: no safehouse CpdSet since the last freeze; the departure snapshot is kept (a restart plans "
              "the same loadout)",
              p_When);
    }

    // Lent gear wall items: closed when back on the wall; one missing from the
    // wall and the loadout is pushed back onto the wall in this mission.
    if (!m_Loans.loans.empty())
        ReviewWallLoans(p_When, *m_Departure, &*m_Departure);

    // The item ledger: the packed items reach 47 at the start (their ItemPickedUp is not counted again).
    // m_Packed is filled now too, not only by the plan at scene start: a packed
    // item's CreateItem is told apart by it.
    if (const auto* s_Packed = m_Departure->List("InventoryItems")) {
        std::string s_Picks;
        for (const fr::Guid& s_Id : *s_Packed) {
            m_Ledger.Expect(s_Id);
            m_Packed.insert(s_Id);
            if (IsLockpickLike(s_Id))
                s_Picks += fmt::format("{}{}", s_Picks.empty() ? "" : ", ", fr::FormatGuid(s_Id));
        }
        if (m_GrantMode != fr::LockpickGrantMode::Off)
            LogTo(fr::LogTopic::Loadout,
                  "loadout lockpick ledger: {}: {} packed items expected; lockpicks among them: [{}]", p_When,
                  s_Packed->size(), s_Picks);
    }

    if (!m_On)
        return;

    if (m_Departure->Empty()) {
        LogTo(fr::LogTopic::Loadout,
              "loadout: {}: the departure snapshot is empty (no CpdSet seen yet, no state file); nothing to plan",
              p_When);
        return;
    }

    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        LogTo(fr::LogTopic::Loadout, "loadout: {}: pro.repo: {}; nothing planned", p_When, s_Why);
        return;
    }

    const auto s_Start = std::chrono::steady_clock::now();
    std::vector<RepoLoadoutEntry> s_Entries;
    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanLoadoutItems(s_Entries, &s_Overflow, &s_Scanned)) {
        LogTo(fr::LogTopic::Loadout, "loadout: {}: the pro.repo item scan faulted ({}); nothing planned", p_When,
              FaultText(s_Code));
        return;
    }

    LogTo(fr::LogTopic::Loadout, "loadout: {}: pro.repo: {} items of {} entries{}, {:.1f} ms", p_When, s_Entries.size(),
          s_Scanned, s_Overflow ? fmt::format(" ({} over the limit)", s_Overflow) : std::string(),
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count());

    if (s_Frozen)
        LogSnapshot(s_Entries);

    // The plan waits for eLoading_SceneStarted: the payout table is written
    // then (MercesPayouts, before this class's stage), and the client
    // requirements go highest payout first. Nothing uses the plan before
    // eLoading_SceneActivated.
    m_PendingEntries = std::move(s_Entries);
    m_PendingSeed = p_Seed;
    LogTo(fr::LogTopic::Loadout, "loadout: {}: planned at eLoading_SceneStarted, once the payouts are known", p_When);
}

void RandomLoadout::LogSnapshot(const std::vector<RepoLoadoutEntry>& p_Entries) {
    EntryMap s_Map;
    for (const auto& s_Entry : p_Entries)
        s_Map.emplace(s_Entry.guid, &s_Entry);

    const BriefcaseRule s_Rule; // the snapshot lines show the rule without ItemSize; the plan says which it used
    const fr::LoadoutState& s_State = *m_Departure;

    for (const auto& [s_Key, s_Ids] : s_State.lists) {
        LogTo(fr::LogTopic::Loadout, "loadout departure: {} ({} items):", s_Key, s_Ids.size());

        for (size_t i = 0; i < s_Ids.size(); i += kIdsPerLine) {
            std::string s_Line;

            for (size_t k = i; k < std::min(s_Ids.size(), i + kIdsPerLine); ++k) {
                const fr::LoadoutItem s_Item = MakeItem(s_Ids[k], fr::LoadoutSource::Persistent, s_Map, s_Rule);
                s_Line += (s_Line.empty() ? "" : "; ") + ItemText(s_Item);
            }

            LogTo(fr::LogTopic::Loadout, "loadout departure: {} {}-{}: {}", s_Key, i + 1,
                  std::min(s_Ids.size(), i + kIdsPerLine), s_Line);
        }
    }

    std::string s_Ints;
    for (const auto& [s_Key, s_Value] : s_State.ints)
        s_Ints += fmt::format("{}{} {}", s_Ints.empty() ? "" : ", ", s_Key, s_Value);

    const auto s_Optin = s_State.Int("OptinPayoutObjective");
    LogTo(fr::LogTopic::Loadout,
          "loadout departure: OptinPayoutObjective {}{}; GearSlotsTotal {}, GearSlotsUsed {}; CPD values: {}",
          s_Optin ? fmt::format("{}", *s_Optin) : std::string("not seen"),
          s_Optin && !fr::PrestigeName(*s_Optin).empty() ? fmt::format(" ({})", fr::PrestigeName(*s_Optin))
                                                         : std::string(),
          s_State.slotsTotal ? fmt::format("{}", *s_State.slotsTotal) : std::string("not seen"),
          s_State.slotsUsed ? fmt::format("{}", *s_State.slotsUsed) : std::string("not seen"),
          s_Ints.empty() ? std::string("none") : s_Ints);
}

void RandomLoadout::Plan(const std::vector<RepoLoadoutEntry>& p_Entries,
                         const std::optional<fr::SeedResolution>& p_Seed, const std::map<int32_t, int32_t>& p_Payouts,
                         const char* p_When) {
    EntryMap s_Map;
    for (const auto& s_Entry : p_Entries)
        s_Map.emplace(s_Entry.guid, &s_Entry);

    const fr::LoadoutState& s_State = *m_Departure;
    static const std::vector<fr::Guid> s_None;
    const auto s_List = [&](std::string_view p_Key) -> const std::vector<fr::Guid>& {
        const auto* s_Ids = s_State.List(p_Key);
        return s_Ids ? *s_Ids : s_None;
    };

    // Briefcase rule: ItemSize when the player's items have it as a string.
    BriefcaseRule s_Rule;
    size_t s_WithSize = 0;

    for (const char* s_Key : {"PersistentItems", "TransientItems", "InventoryItems"}) {
        for (const fr::Guid& s_Id : s_List(s_Key)) {
            const auto s_It = s_Map.find(s_Id);
            if (s_It != s_Map.end() && s_It->second->itemSize[0])
                ++s_WithSize;
        }
    }

    s_Rule.itemSize = s_WithSize > 0;

    fr::LoadoutInput s_Input;

    for (const fr::Guid& s_Id : s_List("InventoryItems"))
        s_Input.carried.push_back(MakeItem(s_Id, fr::LoadoutSource::Persistent, s_Map, s_Rule));
    for (const fr::Guid& s_Id : s_List("PersistentItems"))
        s_Input.owned.push_back(MakeItem(s_Id, fr::LoadoutSource::Persistent, s_Map, s_Rule));
    for (const fr::Guid& s_Id : s_List("TransientItems"))
        s_Input.owned.push_back(MakeItem(s_Id, fr::LoadoutSource::Transient, s_Map, s_Rule));

    // The safehouse pickups (grapes, a banana) for Make Target Slip: never
    // owned, so spawned; always pocketed, at their fixed cost (fr::kSpawnedItems).
    size_t s_Spawnable = 0;
    for (const fr::SpawnedItem& s_Spawned : fr::kSpawnedItems) {
        const fr::LoadoutItem s_Item =
            MakeItem(*fr::ParseGuid(s_Spawned.id), fr::LoadoutSource::Spawned, s_Map, s_Rule);
        if (!s_Item.known) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout plan: spawnable {} ({}) is not in pro.repo; Make Target Slip goes without it",
                  s_Spawned.name, s_Spawned.id);
            continue;
        }
        s_Input.owned.push_back(s_Item);
        s_Input.owned.back().briefcase = false;
        ++s_Spawnable;
    }

    s_Input.totalSlots = s_State.slotsTotal.value_or(0);
    s_Input.reportedUsed = s_State.slotsUsed;
    s_Input.prestige = s_State.Int("OptinPayoutObjective");
    s_Input.seed = p_Seed ? p_Seed->seed : 0;
    s_Input.scene = m_Scene;
    s_Input.startCount = s_State.Int("CPD_CampaignStep").value_or(0);
    // random_loadout_holsterable_only off: a briefcase item (at most one) is delivered
    // inside an ICA Briefcase through the game's purchase path; on, no rule
    // picks one (a need only briefcase items meet is skipped and noted).
    s_Input.briefcaseItems = m_Briefcase;
    s_Input.lockpickFirst = m_LockpickFirst;
    s_Input.objectiveItems = m_ObjectiveItems;

    for (const fr::LoadoutItem& s_Item : s_Input.carried) {
        m_Names[s_Item.id] = s_Item.name;
        m_Packed.insert(s_Item.id);
    }
    for (const fr::LoadoutItem& s_Item : s_Input.owned)
        m_Names.emplace(s_Item.id, s_Item.name);

    const std::optional<std::string> s_Prefix = fr::ClientReqPrefixForScene(m_Scene);
    std::string s_ReqText;

    if (s_Prefix) {
        for (int s_Slot = 1; s_Slot <= 3; ++s_Slot) {
            const std::string s_Key = fmt::format("{}_ClientReq{:02d}", *s_Prefix, s_Slot);
            const auto s_Req = s_State.Int(s_Key);
            const auto s_Pay = s_Req ? p_Payouts.find(*s_Req) : p_Payouts.end();
            s_ReqText += fmt::format("{}{} {}{}", s_ReqText.empty() ? "" : ", ", s_Key,
                                     s_Req ? fr::ClientReqLabel(*s_Req) : std::string("not seen"),
                                     !s_Req || !*s_Req          ? std::string()
                                     : s_Pay != p_Payouts.end() ? fmt::format(" (pays {})", s_Pay->second)
                                                                : std::string(" (payout unknown)"));
            if (s_Req && *s_Req) {
                s_Input.clientReqs.push_back(*s_Req);
                if (s_Pay != p_Payouts.end())
                    s_Input.clientReqPayouts.emplace(*s_Req, s_Pay->second);
            }
        }

        // Where the payouts come from: the payout table as this scene's start
        // left it (MercesPayouts). Not read when payouts is off, or the table was not found.
        if (s_Input.clientReqPayouts.empty() && !s_Input.clientReqs.empty())
            s_ReqText += " (no payout known: the payout table was not read at scene start, so the CPD's order is kept)";
    }
    else {
        s_ReqText = "unknown location (scene name not recognized)";
    }

    if (!s_State.List("PersistentItems"))
        LogTo(
            fr::LogTopic::Loadout,
            "loadout plan: no PersistentItems list seen yet (gear wall unknown); only the Freelancer tools are owned");

    if (!s_State.slotsTotal)
        LogTo(fr::LogTopic::Loadout,
              "loadout plan: GearSlotsTotal not seen yet (sent in the safehouse); 0 slots assumed: nothing is brought");

    const fr::LoadoutPlan s_Plan = fr::PlanLoadout(s_Input);

    LogTo(
        fr::LogTopic::Loadout,
        "loadout plan: {} (at {}): seed {} ({}), campaign step {}; slots {}: used {} (packed items' costs {}, "
        "GearSlotsUsed {}); briefcase items by {}; prestige {}; client requirements: {}; owned {} (wall {}, tools {}), "
        "packed {}, spawnable safehouse pickups {} (Make Target Slip only)",
        m_Scene, p_When, s_Input.seed, p_Seed ? fr::SeedSourceName(p_Seed->source) : "no seed", s_Input.startCount,
        s_Plan.total, s_Plan.usedBefore, s_Plan.computedUsed,
        s_Input.reportedUsed ? fmt::format("{}", *s_Input.reportedUsed) : std::string("not seen"),
        s_Rule.itemSize ? fmt::format("pro.repo HolsterAbility eHolsterTemporarilyOnly or eCanNotBeHolstered (not "
                                      "pocketable; any other HolsterAbility is pocketable whatever the ItemSize), "
                                      "or, without a HolsterAbility, ItemSize containing \"large\" ({} of the "
                                      "player's items have one)",
                                      s_WithSize)
                        : std::string("pro.repo HolsterAbility eHolsterTemporarilyOnly or eCanNotBeHolstered, else a "
                                      "GUESS: sniper rifle, assault rifle and shotgun icons (no ItemSize key)"),
        s_Input.prestige ? fmt::format("{} {}", *s_Input.prestige, fr::PrestigeName(*s_Input.prestige))
                         : std::string("not seen"),
        s_ReqText, s_Input.owned.size() - s_Spawnable, s_List("PersistentItems").size(),
        s_List("TransientItems").size(), s_Input.carried.size(), s_Spawnable);

    for (const fr::LoadoutItem& s_Item : s_Input.carried)
        LogTo(fr::LogTopic::Loadout, "loadout plan: packed: {}", ItemText(s_Item));

    for (const std::string& s_Note : s_Plan.notes)
        LogTo(fr::LogTopic::Loadout, "loadout plan: {}", s_Note);

    for (size_t i = 0; i < s_Plan.picks.size(); ++i) {
        const fr::LoadoutPick& s_Pick = s_Plan.picks[i];
        const fr::LoadoutItem& s_Item = s_Input.owned[s_Pick.owned];
        LogTo(fr::LogTopic::Loadout, "loadout plan: pick {}: {} from the {}: {}", i + 1, ItemText(s_Item),
              fr::LoadoutSourceName(s_Item.source), s_Pick.why);

        Planned s_Planned;
        s_Planned.id = s_Item.id;
        s_Planned.name = s_Item.name;
        s_Planned.source = s_Item.source;
        s_Planned.briefcase = s_Item.briefcase;
        s_Planned.pocket = fr::IsPocketDeliveryItem(s_Item);
        s_Planned.why = s_Pick.why;
        m_Plan.push_back(std::move(s_Planned));
    }

    LogTo(
        fr::LogTopic::Loadout,
        "loadout plan: {} items; slots {} -> {} of {}; briefcase items {} -> {} (random_loadout_holsterable_only {}){}",
        s_Plan.picks.size(), s_Plan.usedBefore, s_Plan.usedAfter, s_Plan.total, s_Plan.briefcasesBefore,
        s_Plan.briefcasesAfter, m_Briefcase ? "off" : "on",
        fmt::format("; given to 47 directly (CreateItem with the packed items' type; a briefcase item inside an ICA "
                    "Briefcase through the game's purchase path; a large holsterable item into the pockets through "
                    "the game's pocket call) {} s of game time after the packed items",
                    m_DelaySeconds));

    m_Planned = !m_Plan.empty();
}

void RandomLoadout::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context); });
}

void RandomLoadout::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    std::lock_guard s_Lock(m_Mutex);

    if (!m_Mission)
        return;

    const char* s_StageName = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_SceneAllocated: s_StageName = "eLoading_SceneAllocated"; break;
        case ESceneLoadingStage::eLoading_SceneStarted: s_StageName = "eLoading_SceneStarted"; break;
        case ESceneLoadingStage::eLoading_SceneActivated: s_StageName = "eLoading_SceneActivated"; break;
        case ESceneLoadingStage::eLoading_ScenePlaying: s_StageName = "eLoading_ScenePlaying"; break;
        default: return;
    }

    if (p_Context && !m_EntitiesFound)
        FindEntities(p_Context, s_StageName);

    // The plan the scene load prepared, now that the payout table was written
    // (MercesPayouts runs before this at eLoading_SceneStarted). A later stage
    // plans it too, should eLoading_SceneStarted not have reached this class.
    if (p_Stage != ESceneLoadingStage::eLoading_SceneAllocated && m_PendingEntries) {
        const std::vector<RepoLoadoutEntry> s_Entries = std::move(*m_PendingEntries);
        m_PendingEntries.reset();
        const std::map<int32_t, int32_t> s_Payouts = m_PayoutsFor ? m_PayoutsFor() : std::map<int32_t, int32_t> {};
        Plan(s_Entries, m_PendingSeed, s_Payouts, s_StageName);
    }

    // The exe scan and the world inventory lookup take about half a second:
    // done behind the loading screen, not as the items are given (a stutter).
    if (p_Stage == ESceneLoadingStage::eLoading_SceneActivated &&
        (m_Planned || m_GrantMode != fr::LockpickGrantMode::Off)) {
        ResolveWorldInventory();

        // The blend hold's exe check (once per process), for a briefcase item.
        if (m_Planned && m_Briefcase)
            CheckHeroState();
    }

    if (p_Stage == ESceneLoadingStage::eLoading_SceneActivated || p_Stage == ESceneLoadingStage::eLoading_ScenePlaying)
        LogTo(fr::LogTopic::Loadout, "loadout: {}: \"Inventory items\" gave {} ids so far{}", s_StageName, m_Emitted,
              m_IterationDone ? " (iteration done)" : "");

    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying)
        m_ScenePlaying = true;

    // The same check at scene playing when scene activated was missed, still before any delivery.
    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && m_Planned && m_Briefcase && !m_HeroStateChecked)
        CheckHeroState();

    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && m_Planned && m_Phase == Phase::Idle) {
        m_Phase = Phase::WaitList;
        m_Timer = 0;
        LogTo(fr::LogTopic::Loadout,
              "loadout deliver: eLoading_ScenePlaying: waiting for \"Inventory items\" to finish the packed items "
              "(its OnDone, at most {:.0f} s of game time), then {:.1f} s more",
              kListWaitSeconds, m_DelaySeconds);
    }

    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && m_GrantMode != fr::LockpickGrantMode::Off &&
        m_Grant.step == LockpickGrant::Step::Idle) {
        m_Grant.step = LockpickGrant::Step::Wait;
        LogTo(fr::LogTopic::Loadout,
              "loadout lockpick grant: eLoading_ScenePlaying: mode {}: once the packed items are in{} and {:.0f} s "
              "have passed, 47 gets {} {} if he carries no lockpick",
              fr::LockpickGrantModeName(m_GrantMode), m_Planned ? " and the random loadout has created its items" : "",
              kGrantDelaySeconds, fr::LockpickGrantCount(m_GrantMode, m_GrantNails), fr::FormatGuid(m_GrantId));
    }

    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && AnyItemGrant() &&
        m_Items.step == ItemGrants::Step::Idle) {
        m_Items.step = ItemGrants::Step::Wait;
        std::string s_Asked;
        for (const fr::GrantItem s_Item : fr::kGrantItems) {
            if (m_ItemGrantOn[fr::GrantItemIndex(s_Item)])
                s_Asked +=
                    fmt::format("{}{} {} (if he carries no {})", s_Asked.empty() ? "" : ", ", fr::GrantItemName(s_Item),
                                fr::FormatGuid(fr::GrantItemId(s_Item)), fr::GrantKindName(s_Item));
        }
        LogTo(fr::LogTopic::Loadout,
              "loadout item grant: eLoading_ScenePlaying: once the packed items are in{} and {:.0f} s have passed, 47 "
              "gets: {}",
              m_Planned ? " and the random loadout has created its items" : "", kGrantDelaySeconds, s_Asked);
    }
}

void RandomLoadout::FindEntities(ZEntitySceneContext* p_Context, const char* p_Stage) {
    std::vector<uint64_t> s_Ids {kInventoryItemsId, kPersistentItemsId, kTransientItemsId};
    for (const FlowEntity& s_Flow : kFlowEntities)
        s_Ids.push_back(s_Flow.id);
    std::vector<ZEntityType**> s_Found;
    const std::string s_Why = FindSetupBrickEntities(p_Context->m_aLoadedBricks, s_Ids, s_Found);

    if (!s_Why.empty()) {
        LogTo(fr::LogTopic::Loadout, "loadout: {}: {}; tried again at the next stage", p_Stage, s_Why);
        return;
    }

    m_EntitiesFound = true;
    m_PersistentList = s_Found[1];
    m_TransientList = s_Found[2];

    std::string s_Class;
    uint64_t s_Id = 0;

    if (s_Found[0])
        ReadEntityClassName(s_Found[0], s_Class, &s_Id);

    LogTo(fr::LogTopic::Loadout,
          "loadout: {}: evergreen_setup: \"Inventory items\" {:016x} {} ({}), \"Persistent items\" {:016x} {}, "
          "\"Transient items\" {:016x} {}",
          p_Stage, kInventoryItemsId, s_Found[0] ? "found" : "NOT FOUND", s_Class.empty() ? "?" : s_Class,
          kPersistentItemsId, s_Found[1] ? "found" : "NOT FOUND", kTransientItemsId,
          s_Found[2] ? "found" : "NOT FOUND");

    m_InventoryList.store(s_Found[0], std::memory_order_release);

    // The briefcase path (and the packed items' path, logged).
    std::string s_Flow;
    for (size_t i = 0; i < kFlowCount; ++i) {
        ZEntityType** const s_Entity = s_Found.size() > 3 + i ? s_Found[3 + i] : nullptr;
        m_FlowEntities[i] = s_Entity;
        m_FlowWatch[i].store(s_Entity, std::memory_order_release);
        std::string s_FlowClass;
        uint64_t s_FlowId = 0;
        if (s_Entity)
            ReadEntityClassName(s_Entity, s_FlowClass, &s_FlowId);
        s_Flow += fmt::format("{}\"{}\" {:016x} {}{}", s_Flow.empty() ? "" : ", ", kFlowEntities[i].name,
                              kFlowEntities[i].id, s_Entity ? "found" : "NOT FOUND",
                              s_FlowClass.empty() ? std::string() : fmt::format(" ({})", s_FlowClass));
    }

    m_FlowOn.store(true, std::memory_order_release);
    LogTo(fr::LogTopic::Loadout, "loadout: {}: evergreen_setup briefcase path: {}{}", p_Stage, s_Flow,
          m_FlowEntities[kFlowPurchased] && m_FlowEntities[kFlowBriefcase]
              ? ""
              : "; a briefcase item cannot be delivered in this scene (it stays owned)");
}

std::optional<int32_t> RandomLoadout::LiveOptinObjective() {
    std::lock_guard s_Lock(m_Mutex);
    return m_Live.Int("OptinPayoutObjective");
}

std::optional<std::vector<fr::Guid>> RandomLoadout::OwnedItemIds() {
    std::lock_guard s_Lock(m_Mutex);
    std::optional<std::vector<fr::Guid>> s_Out;

    // The gear wall, the Freelancer tools you keep (one of each; a packed tool moves to InventoryItems), the packed
    // items and the awarded items (collection and reward items, usually also on the wall: read anyway so an awarded
    // item is never offered by a crate).
    for (const char* s_Key : {"PersistentItems", "TransientItems", "InventoryItems", "AwardedItems"}) {
        if (const std::vector<fr::Guid>* s_List = m_Live.List(s_Key)) {
            if (!s_Out)
                s_Out.emplace();

            s_Out->insert(s_Out->end(), s_List->begin(), s_List->end());
        }
    }

    return s_Out;
}

void RandomLoadout::OnSceneClear() noexcept {
    Contained(kTag, "scene clear", [&] {
        std::lock_guard s_Lock(m_Mutex);
        m_InventoryList.store(nullptr, std::memory_order_release);
        m_FlowOn.store(false, std::memory_order_release);
        for (auto& s_Watch : m_FlowWatch)
            s_Watch.store(nullptr, std::memory_order_release);

        if (m_Job.step != BriefcaseJob::Step::Idle && m_Job.plan < m_Plan.size())
            LogTo(fr::LogTopic::Loadout, "loadout briefcase summary: {} \"{}\": {} ({}); briefcase {}, item entity {}",
                  fr::FormatGuid(m_Plan[m_Job.plan].id), m_Plan[m_Job.plan].name,
                  m_Job.step == BriefcaseJob::Step::Done ? fr::BriefcaseVerdictName(m_Job.verdict)
                                                         : "UNFINISHED at the scene clear",
                  fr::BriefcaseProgressText(m_Job.progress), fmt::ptr(m_Job.briefcase), fmt::ptr(m_Job.item));
        m_Job = BriefcaseJob {};

        if (m_Hold.plan && *m_Hold.plan < m_Plan.size()) {
            Planned& s_Held = m_Plan[*m_Hold.plan];
            s_Held.skipNote = fmt::format("held for {:.0f} s: 47 never left his blend spot or act ({}); never given",
                                          m_Hold.seconds, HeroStateLogText(m_Hold.lastMask, m_Hold.lastMove));
            LogTo(fr::LogTopic::Loadout, "loadout briefcase summary: {} \"{}\": {}; it stays on the {}",
                  fr::FormatGuid(s_Held.id), s_Held.name, s_Held.skipNote, fr::LoadoutSourceName(s_Held.source));
        }
        m_Hold = BlendHold {};

        for (const PocketJob& s_Pocket : m_Pockets) {
            if (s_Pocket.plan >= m_Plan.size())
                continue;
            LogTo(fr::LogTopic::Loadout, "loadout holster summary: {} \"{}\": {} ({}); item entity {}",
                  fr::FormatGuid(m_Plan[s_Pocket.plan].id), m_Plan[s_Pocket.plan].name,
                  s_Pocket.step == PocketJob::Step::Done ? fr::PocketVerdictName(s_Pocket.verdict)
                                                         : "UNFINISHED at the scene clear",
                  fr::PocketProgressText(s_Pocket.progress), fmt::ptr(s_Pocket.item));
        }
        m_Pockets.clear();
        m_PocketActive.reset();
        m_HandsWatch = 0;
        m_HandsItem.reset();

        if (m_Mission && m_Grant.step != LockpickGrant::Step::Idle)
            LogTo(
                fr::LogTopic::Loadout,
                "loadout lockpick grant summary: mode {}: {}; {} given items recorded; \"Inventory items\": {} pushes "
                "and {} removes of {} blocked",
                fr::LockpickGrantModeName(m_GrantMode),
                m_Grant.outcome.empty() ? std::string("not finished") : m_Grant.outcome, m_Grant.given.size(),
                m_Grant.pushesBlocked, m_Grant.removesBlocked, fr::FormatGuid(m_GrantId));

        if (m_Mission && m_Items.step != ItemGrants::Step::Idle) {
            for (const fr::GrantItem s_Item : fr::kGrantItems) {
                const ItemGrantJob& s_Job = m_Items.items[fr::GrantItemIndex(s_Item)];
                if (m_ItemGrantOn[fr::GrantItemIndex(s_Item)])
                    LogTo(fr::LogTopic::Loadout,
                          "loadout item grant summary: {} {}: {}; \"Inventory items\": {} pushes and {} removes of it "
                          "blocked",
                          fr::GrantItemName(s_Item), fr::FormatGuid(s_Job.id),
                          s_Job.outcome.empty() ? std::string("not finished") : s_Job.outcome, s_Job.pushesBlocked,
                          s_Job.removesBlocked);
            }
        }

        m_GrantListOn.store(false, std::memory_order_release);
        m_Grant = LockpickGrant {};
        ResetItemGrants();
        m_Exit = GrantExit {};
        m_GuardedIds.clear();

        if (m_Mission && !m_Return.items.empty() && !m_Return.checked)
            LogTo(fr::LogTopic::Loadout,
                  "loadout wall return: the mission ended without a save after {} rounds; the {} lent "
                  "item(s) are wherever the last save left them (checked at the next load)",
                  m_Return.sends, m_Return.items.size());
        m_Return = WallReturn {};

        if (m_Mission && !m_Plan.empty()) {
            for (const Planned& s_Planned : m_Plan)
                LogTo(
                    fr::LogTopic::Loadout,
                    "loadout summary: {} \"{}\"{}: requested {}{}, new item entities seen {}, in 47's inventory {}{}, "
                    "removed from the {} {}",
                    fr::FormatGuid(s_Planned.id), s_Planned.name, s_Planned.briefcase ? " (briefcase item)" : "",
                    s_Planned.requested ? "yes" : "no",
                    !s_Planned.skipNote.empty()      ? fmt::format(" ({})", s_Planned.skipNote)
                    : !s_Planned.requestNote.empty() ? fmt::format(" ({})", s_Planned.requestNote)
                                                     : std::string(),
                    s_Planned.spawned.size(), s_Planned.confirmed ? "yes" : "no",
                    s_Planned.confirmed ? fmt::format(" (by {})", s_Planned.confirmedBy) : std::string(),
                    fr::LoadoutSourceName(s_Planned.source), s_Planned.removed ? "yes" : "no");
        }

        m_EntitiesFound = false;
        m_PersistentList = nullptr;
        m_TransientList = nullptr;
        m_Phase = Phase::Idle;
        m_HeroInventory = nullptr;
        m_Hero = nullptr;
    });
}

// --- pins and CreateItem ---------------------------------------------------------------

void RandomLoadout::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    if (m_FlowOn.load(std::memory_order_relaxed) && p_Entity.m_pObj) {
        for (size_t i = 0; i < kFlowCount; ++i) {
            if (m_FlowWatch[i].load(std::memory_order_relaxed) == p_Entity.m_pObj) {
                Contained(kTag, "briefcase path pin", [&] {
                    std::lock_guard s_Lock(m_Mutex);
                    OnFlowPin(i, p_PinId, p_Data);
                });
                return;
            }
        }
    }

    ZEntityType** const s_List = m_InventoryList.load(std::memory_order_relaxed);

    if (!s_List || p_Entity.m_pObj != s_List)
        return;

    // (OnGuid, the same id again just before OnRepositoryId, is not logged.)
    if (p_PinId != fr::kPinOnRepositoryId && p_PinId != fr::kPinOnDone && p_PinId != fr::kPinNoValue)
        return;

    Contained(kTag, "pin", [&] {
        std::lock_guard s_Lock(m_Mutex);
        const bool s_Log = ++m_PinLines <= kMaxPinLines;

        if (m_PinLines == kMaxPinLines + 1)
            LogTo(fr::LogTopic::Loadout, "loadout pin: {} lines logged this scene; further pins are not logged",
                  kMaxPinLines);

        if (p_PinId == fr::kPinOnRepositoryId) {
            std::string s_Why;
            const std::optional<fr::Guid> s_Read =
                RepoIdPayload(p_Data, m_RepoIdType.load(std::memory_order_acquire), &s_Why);

            if (!s_Read) {
                if (s_Log)
                    LogTo(fr::LogTopic::Loadout,
                          "loadout pin: \"Inventory items\" OnRepositoryId: payload unreadable ({})", s_Why);
                return;
            }

            const fr::Guid s_Id = *s_Read;
            ++m_Emitted;

            if (s_Log)
                LogTo(fr::LogTopic::Loadout, "loadout pin: \"Inventory items\" OnRepositoryId #{} {} \"{}\"{}",
                      m_Emitted, fr::FormatGuid(s_Id), NameOf(s_Id), m_Packed.contains(s_Id) ? " (packed)" : "");
            return;
        }

        m_IterationDone = true;

        if (s_Log)
            LogTo(fr::LogTopic::Loadout,
                  "loadout pin: \"Inventory items\" {} after {} ids (the packed items are handed out)",
                  p_PinId == fr::kPinOnDone ? "OnDone" : "NoValue", m_Emitted);
    });
}

void RandomLoadout::OnCreateItem(const void* p_Inventory, const ZRepositoryID& p_Id, const ZString& p_InstanceId,
                                 int32_t p_Type) noexcept {
    Contained(kTag, "CreateItem", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Mission)
            return;

        fr::Guid s_Id;
        std::memcpy(&s_Id, static_cast<const void*>(&p_Id), sizeof(s_Id));
        const bool s_Packed = m_Packed.contains(s_Id);
        const bool s_Planned =
            std::any_of(m_Plan.begin(), m_Plan.end(), [&](const Planned& p_Planned) { return p_Planned.id == s_Id; });

        if (s_Packed && !m_InOwnCreate && !m_PackedCreateType)
            m_PackedCreateType = p_Type;

        if (++m_CreateLines > kMaxCreateLines) {
            if (m_CreateLines == kMaxCreateLines + 1)
                LogTo(fr::LogTopic::Loadout,
                      "loadout create: {} lines logged this scene; further CreateItem calls are not logged",
                      kMaxCreateLines);
            return;
        }

        InstanceOut s_Instance {};
        SafeInstance(&p_InstanceId, &s_Instance);

        LogTo(fr::LogTopic::Loadout,
              "loadout create #{}: ZCharacterSubcontrollerInventory::CreateItem {} \"{}\" type {} ({}), instance "
              "\"{}\", inventory {}{}: {}",
              m_CreateLines, fr::FormatGuid(s_Id), NameOf(s_Id), p_Type, fr::CreateItemTypeName(p_Type),
              s_Instance.text, fmt::ptr(p_Inventory),
              m_HeroInventory ? (p_Inventory == m_HeroInventory ? " (47's)" : " (NOT 47's)") : "",
              m_InOwnCreate ? "this mod's delivery"
              : s_Packed    ? "a packed item (the game's own path)"
              : s_Planned   ? "a planned item, NOT from this mod"
                            : "the game's");
    });
}

// --- delivery ----------------------------------------------------------------------------

std::string RandomLoadout::NameOf(const fr::Guid& p_Id) const {
    const auto s_It = m_Names.find(p_Id);
    return s_It == m_Names.end() ? std::string("?") : s_It->second;
}

void RandomLoadout::PrepareWorldInventory() noexcept {
    Contained(kTag, "world inventory for lockpick_grant", [&] {
        std::lock_guard s_Lock(m_Mutex);
        ResolveWorldInventory();
    });
}

std::optional<fr::LockpickCarry> RandomLoadout::HeroLockpicks(std::string* p_Why) noexcept {
    std::optional<fr::LockpickCarry> s_Out;

    Contained(kTag, "lockpick check", [&] {
        std::lock_guard s_Lock(m_Mutex);
        // The item ledger decides; the world inventory scan only adds what it sees (it reads no items in the current game).
        fr::LockpickCarry s_Carry = LedgerCarry();
        ResolveWorldInventory();
        const WorldScan s_Scan = ScanWorld();
        fr::LockpickCarry s_Seen;

        if (s_Scan.ok) {
            for (const WorldItem& s_Item : s_Scan.items) {
                if (s_Item.owner != s_Scan.hero || !s_Item.hasId)
                    continue;

                if (m_Grant.given.contains(s_Item.entity))
                    ++s_Seen.given;
                else if (IsLockpickLike(s_Item.id))
                    ++s_Seen.own;
            }

            s_Carry.own = std::max(s_Carry.own, s_Seen.own);
            s_Carry.given = std::max(s_Carry.given, s_Seen.given);
        }

        if (p_Why)
            *p_Why = s_Scan.ok && s_Scan.entries > 0
                         ? fmt::format("item events; world inventory scan: own {}, given {}", s_Seen.own, s_Seen.given)
                         : std::string("item events");
        s_Out = s_Carry;
    });

    if (!s_Out && p_Why && p_Why->empty())
        *p_Why = "the check failed";

    return s_Out;
}

bool RandomLoadout::IsLockpickLike(const fr::Guid& p_Id) const {
    return fr::IsLockpickId(p_Id) || m_LockpickIds.contains(p_Id);
}

fr::LockpickCarry RandomLoadout::LedgerCarry() const {
    return m_Ledger.Carry([this](const fr::Guid& p_Id) { return IsLockpickLike(p_Id); });
}

// ItemPickedUp, ItemRemovedFromInventory, ItemDropped, ItemThrown in a mission: the item ledger.
void RandomLoadout::LedgerEvent(fr::HeroItemEvent p_Event, uint32_t p_EventIndex, const std::string& p_Json) {
    const auto s_IdText = fr::JsonStringField(p_Json, "RepositoryId");
    const auto s_Id = s_IdText ? fr::ParseGuid(*s_IdText) : std::nullopt;

    if (!s_Id)
        return;

    const double s_At = fr::JsonNumberField(p_Json, "Timestamp").value_or(0.0);
    const fr::LockpickCarry s_Before = LedgerCarry();
    // The item grants: the asked kind of this id, if any.
    std::optional<fr::GrantItem> s_GrantKind;
    for (const fr::GrantItem s_Item : fr::kGrantItems) {
        if (!s_GrantKind && m_ItemGrantOn[fr::GrantItemIndex(s_Item)] && IsGrantKind(s_Item, *s_Id))
            s_GrantKind = s_Item;
    }
    const fr::LockpickCarry s_KindBefore = s_GrantKind ? KindCarry(*s_GrantKind) : fr::LockpickCarry {};
    m_Ledger.Event(p_Event, *s_Id, s_At);

    const bool s_Lockpick = m_GrantMode != fr::LockpickGrantMode::Off && IsLockpickLike(*s_Id);

    if ((!s_Lockpick && !s_GrantKind) || ++m_LedgerLines > kMaxLedgerLines)
        return;

    const auto s_Name = fr::JsonStringField(p_Json, "ItemName");
    const char* s_Kind = p_Event == fr::HeroItemEvent::PickedUp  ? "ItemPickedUp"
                         : p_Event == fr::HeroItemEvent::Removed ? "ItemRemovedFromInventory"
                         : p_Event == fr::HeroItemEvent::Dropped ? "ItemDropped"
                                                                 : "ItemThrown";

    if (s_Lockpick) {
        const fr::LockpickCarry s_After = LedgerCarry();
        LogTo(fr::LogTopic::Loadout,
              "loadout lockpick ledger: #{} {} {} \"{}\" at {:.2f} s{}: 47's lockpicks own {} -> {}, given {} -> {}",
              p_EventIndex, s_Kind, fr::FormatGuid(*s_Id), s_Name ? *s_Name : "", s_At,
              *s_Id == m_GrantId ? " (the given id)" : "", s_Before.own, s_After.own, s_Before.given, s_After.given);
    }

    if (s_GrantKind) {
        const fr::LockpickCarry s_After = KindCarry(*s_GrantKind);
        LogTo(fr::LogTopic::Loadout,
              "loadout item grant ledger: #{} {} {} \"{}\" at {:.2f} s{}: 47's {} items own {} -> {}, given {} -> {}",
              p_EventIndex, s_Kind, fr::FormatGuid(*s_Id), s_Name ? *s_Name : "", s_At,
              *s_Id == fr::GrantItemId(*s_GrantKind) ? " (the given id)" : "", fr::GrantKindName(*s_GrantKind),
              s_KindBefore.own, s_After.own, s_KindBefore.given, s_After.given);
    }
}

// --- lockpick_grant ----------------------------------------------------------------------

void RandomLoadout::ConfigureLockpickGrant(fr::LockpickGrantMode p_Mode, int32_t p_Nails) {
    std::lock_guard s_Lock(m_Mutex);
    m_GrantMode = p_Mode;
    m_GrantNails = fr::ClampLockpickNails(p_Nails);
    // The mode's own item: the classic lockpick (Tool_Lockpick) or the Rusty Old Nail.
    const std::string_view s_Item = fr::LockpickGrantItemId(p_Mode);
    m_GrantChosen = s_Item.empty() ? fr::Guid {} : fr::ParseGuid(s_Item).value_or(fr::Guid {});
    m_GrantId = m_GrantChosen;
}

// The pro.repo items for the grants' censuses (one scan for the lockpick
// grant and the item grants); false, with p_Why, when there are none.
bool RandomLoadout::GrantScan(std::vector<RepoLoadoutEntry>& p_Entries, std::string& p_Why) {
    std::string s_Why;

    if (!m_Repo.Ready(s_Why)) {
        p_Why = "pro.repo: " + s_Why;
        return false;
    }

    size_t s_Overflow = 0;
    size_t s_Scanned = 0;

    if (const uint32_t s_Code = m_Repo.ScanLoadoutItems(p_Entries, &s_Overflow, &s_Scanned)) {
        p_Why = fmt::format("the pro.repo item scan faulted ({})", FaultText(s_Code));
        return false;
    }

    return true;
}

// Where the player owns an item (per the last CpdSet), for the censuses.
std::string RandomLoadout::OwnedText(const fr::Guid& p_Id) const {
    std::string s_Where;

    for (const char* s_Key : {"InventoryItems", "PersistentItems", "TransientItems"}) {
        const auto* s_List = m_Live.List(s_Key);
        const size_t s_Count = s_List ? static_cast<size_t>(std::count(s_List->begin(), s_List->end(), p_Id)) : 0;

        if (s_Count)
            s_Where += fmt::format("{}{} x{}", s_Where.empty() ? "" : ", ", s_Key, s_Count);
    }

    return s_Where.empty() ? std::string("not owned") : "owned: " + s_Where;
}

// The lockpick-like pro.repo items (for the log, and for telling 47's own
// lockpicks), and whether the given item is in pro.repo. p_Entries: null
// when the scan gave none (p_Why).
void RandomLoadout::GrantCensus(const char* p_When, const std::vector<RepoLoadoutEntry>* p_Entries,
                                const std::string& p_Why) {
    m_LockpickIds.clear();
    m_LockpickNames.clear();
    m_GrantId = m_GrantChosen;
    m_LockpickIds.insert(m_GrantId);

    if (!p_Entries) {
        LogTo(fr::LogTopic::Loadout,
              "loadout lockpick grant: {}: {}; only Lockpick Mk III, the Collector's Lockpick and the given id count "
              "as lockpicks",
              p_When, p_Why);
        return;
    }

    const std::vector<RepoLoadoutEntry>& s_Entries = *p_Entries;
    const auto s_Owned = [&](const fr::Guid& p_Id) { return OwnedText(p_Id); };

    const RepoLoadoutEntry* s_GrantEntry = nullptr;

    for (const RepoLoadoutEntry& s_Entry : s_Entries) {
        if (s_Entry.guid == m_GrantId)
            s_GrantEntry = &s_Entry;
    }

    for (const RepoLoadoutEntry& s_Entry : s_Entries) {
        if (!fr::IsLockpickLikeName(s_Entry.title, s_Entry.commonName) && !fr::IsLockpickId(s_Entry.guid) &&
            s_Entry.guid != m_GrantId)
            continue;

        m_LockpickIds.insert(s_Entry.guid);
        m_LockpickNames[s_Entry.guid] = s_Entry.commonName[0] ? s_Entry.commonName : s_Entry.title;
        LogTo(fr::LogTopic::Loadout,
              "loadout lockpick grant: {}: lockpick-like item {} \"{}\" / \"{}\": type {}, traits [{}], rarity {}, "
              "capacity {}; {}{}",
              p_When, fr::FormatGuid(s_Entry.guid), s_Entry.title, s_Entry.commonName,
              s_Entry.itemType[0] ? s_Entry.itemType : "-", s_Entry.traits,
              s_Entry.hasRarity ? fmt::format("{}", s_Entry.rarity) : "-",
              s_Entry.hasCapacity ? fmt::format("{}", s_Entry.capacity) : "-", s_Owned(s_Entry.guid),
              s_Entry.guid == m_GrantId ? " (THE GIVEN ITEM)" : "");
    }

    LogTo(fr::LogTopic::Loadout,
          "loadout lockpick grant: {}: mode {}: {} lockpick-like items of {} pro.repo items count as 47's own "
          "lockpicks; the given item {} is {}",
          p_When, fr::LockpickGrantModeName(m_GrantMode), m_LockpickIds.size(), s_Entries.size(),
          fr::FormatGuid(m_GrantId), s_GrantEntry ? "in pro.repo" : "NOT among pro.repo's items (CreateItem may fail)");
}

bool RandomLoadout::GrantStartReady(double p_Timer, const char* p_What) const {
    const bool s_ListDone = m_IterationDone || p_Timer >= kListWaitSeconds;
    // Its CreateItems are in the item ledger from its Confirm phase on (Idle: none planned).
    const bool s_LoadoutPending = m_Phase == Phase::WaitList || m_Phase == Phase::Delay || m_Phase == Phase::Deliver;
    const fr::GrantStartVerdict s_Verdict = fr::DecideGrantStart({p_Timer, s_ListDone, s_LoadoutPending});

    if (s_Verdict == fr::GrantStartVerdict::GoTimedOut)
        LogTo(fr::LogTopic::Loadout, "{}: {} after {:.1f} s of game time: {}{}", p_What,
              fr::GrantStartVerdictName(s_Verdict), p_Timer,
              s_ListDone ? "" : "the packed items were not seen in (no OnDone from \"Inventory items\"); ",
              s_LoadoutPending ? "the random loadout has not created its items yet (it may still bring one)"
                               : "the random loadout is done");

    return s_Verdict != fr::GrantStartVerdict::Wait;
}

void RandomLoadout::GrantTick(double p_Seconds) {
    LockpickGrant& s_Grant = m_Grant;

    if (s_Grant.step == LockpickGrant::Step::Idle || s_Grant.step == LockpickGrant::Step::Done)
        return;

    s_Grant.timer += p_Seconds;

    switch (s_Grant.step) {
        case LockpickGrant::Step::Wait:
            // After the packed items and the random loadout's CreateItems (it may plan a lockpick).
            if (GrantStartReady(s_Grant.timer, "loadout lockpick grant")) {
                s_Grant.step = LockpickGrant::Step::Give;
                s_Grant.timer = 0;
            }
            break;

        case LockpickGrant::Step::Give:
            GrantGive();

            if (s_Grant.step == LockpickGrant::Step::Give && s_Grant.timer >= kHeroWaitSeconds) {
                if (s_Grant.outcome.empty())
                    s_Grant.outcome = "47 or his inventory was not found";
                LogTo(fr::LogTopic::Loadout,
                      "loadout lockpick grant: gave up after {:.0f} s of game time: {}; nothing given",
                      kHeroWaitSeconds, s_Grant.outcome);
                s_Grant.step = LockpickGrant::Step::Done;
            }
            break;

        case LockpickGrant::Step::Confirm:
            s_Grant.scanTimer += p_Seconds;

            if (s_Grant.scanTimer >= kScanEverySeconds || s_Grant.timer >= kConfirmSeconds) {
                s_Grant.scanTimer = 0;
                GrantConfirm(s_Grant.timer >= kConfirmSeconds);
            }
            break;

        default: break;
    }
}

// 47 carries no lockpick-like item: the mode's items are created in his
// inventory. Stays in Give while 47, his inventory or the scan are not ready.
void RandomLoadout::GrantGive() {
    std::string s_Found;
    const HeroFind s_Find = FindHeroInventory(s_Found);

    if (s_Find == HeroFind::NotYet)
        return;

    if (s_Find == HeroFind::Failed) {
        m_Grant.outcome = s_Found;
        LogTo(fr::LogTopic::Loadout, "loadout lockpick grant: {}; nothing given", s_Found);
        m_Grant.step = LockpickGrant::Step::Done;
        return;
    }

    // 47's lockpicks: the item ledger (packed, the random loadout's, picked up, minus removed), and the
    // world inventory scan when it sees anything (it reads no items in the current game).
    std::string s_Own;
    const auto s_AddOwn = [&](const fr::Guid& p_Id, size_t p_Count, const char* p_By) {
        const auto s_Name = m_LockpickNames.find(p_Id);
        s_Own += fmt::format("{}{} \"{}\" x{} ({})", s_Own.empty() ? "" : ", ", fr::FormatGuid(p_Id),
                             s_Name == m_LockpickNames.end() ? NameOf(p_Id) : s_Name->second, p_Count, p_By);
    };

    for (const fr::Guid& s_Id : m_LockpickIds) {
        if (const size_t s_Count = m_Ledger.Own(s_Id))
            s_AddOwn(s_Id, s_Count, "item events");
    }
    for (const std::string_view s_Id : {fr::kLockpickMk3Id, fr::kCollectorsLockpickId}) {
        const fr::Guid s_Guid = *fr::ParseGuid(s_Id);
        if (!m_LockpickIds.contains(s_Guid) && m_Ledger.Own(s_Guid))
            s_AddOwn(s_Guid, m_Ledger.Own(s_Guid), "item events");
    }

    ResolveWorldInventory();
    const WorldScan s_Scan = ScanWorld();
    m_Grant.before.clear();

    if (s_Scan.ok) {
        for (const WorldItem& s_Item : s_Scan.items) {
            if (!s_Item.hasId)
                continue;

            if (s_Item.id == m_GrantId)
                m_Grant.before.insert(s_Item.entity);

            if (s_Item.owner == s_Scan.hero && IsLockpickLike(s_Item.id))
                s_AddOwn(s_Item.id, 1, "world inventory scan");
        }
    }

    if (!s_Own.empty()) {
        m_Grant.outcome = "47 carries his own lockpick";
        LogTo(fr::LogTopic::Loadout,
              "loadout lockpick grant: 47 carries a lockpick of his own ({}); nothing given, picks are vanilla", s_Own);
        m_Grant.step = LockpickGrant::Step::Done;
        return;
    }

    const int32_t s_Count = fr::LockpickGrantCount(m_GrantMode, m_GrantNails);
    const fr::LoadoutItemTypeChoice s_Type = fr::ChooseLoadoutItemType(m_PackedCreateType);

    // Before the first CreateItem: the game may record the item in "Inventory items" at once.
    // Nails are not guarded: vanilla nails never come back to the safehouse.
    if (fr::LockpickGrantGuardsSave(m_GrantMode)) {
        m_GuardedIds.insert(m_GrantId);
        m_GrantListOn.store(true, std::memory_order_release);
    }
    std::string s_Results;

    for (int32_t i = 0; i < s_Count; ++i) {
        CreateArgs s_Args {m_HeroInventory, m_GrantId, s_Type.type, nullptr};
        m_Ledger.Expect(m_GrantId, true); // before: its ItemPickedUp may come at once
        m_InOwnCreate = true;
        const DWORD s_Code = SafeCreateItem(&s_Args);
        m_InOwnCreate = false;

        if (!s_Code)
            ++m_Grant.created;
        else
            m_Ledger.Retract(m_GrantId, true);

        s_Results += fmt::format("{}{}", s_Results.empty() ? "" : ", ",
                                 s_Code          ? FaultText(s_Code)
                                 : s_Args.result ? std::string("an entry")
                                                 : std::string("null"));
    }

    LogTo(fr::LogTopic::Loadout,
          "loadout lockpick grant: 47 carries no lockpick (item events{}; 47 {}; {} lockpick-like ids known): mode {}: "
          "CreateItem {} x{} (type {}, {}; no gear slot): [{}]; {} item entities with this id existed before; {}",
          s_Scan.ok ? fmt::format(", world inventory scan of {} entries", s_Scan.entries)
                    : ", world inventory scan failed: " + s_Scan.why,
          fmt::ptr(m_Hero), m_LockpickIds.size(), fr::LockpickGrantModeName(m_GrantMode), fr::FormatGuid(m_GrantId),
          s_Count, s_Type.type, fr::CreateItemTypeName(s_Type.type), s_Results, m_Grant.before.size(),
          fr::LockpickGrantGuardsSave(m_GrantMode)
              ? "the mission's \"Inventory items\" now takes no push or remove of it (so the save never holds it)"
              : "not kept out of the save (vanilla nails never persist; the save check logs it)");

    m_Grant.step = LockpickGrant::Step::Confirm;
    m_Grant.timer = 0;
    m_Grant.scanTimer = 0;
}

// The new item entities with the given id that 47 owns are the given ones.
void RandomLoadout::GrantConfirm(bool p_Final) {
    const WorldScan s_Scan = ScanWorld();

    if (s_Scan.ok) {
        for (const WorldItem& s_Item : s_Scan.items) {
            if (s_Item.hasId && s_Item.id == m_GrantId && s_Item.owner == s_Scan.hero &&
                !m_Grant.before.contains(s_Item.entity))
                m_Grant.given.insert(s_Item.entity);
        }
    }

    // Confirmed by their ItemPickedUp (the item ledger), or by the scan.
    const size_t s_Pending = m_Ledger.GivenPending(m_GrantId);
    const size_t s_Picked = static_cast<size_t>(m_Grant.created) > s_Pending ? m_Grant.created - s_Pending : 0;
    const size_t s_Confirmed = std::max(s_Picked, m_Grant.given.size());
    const bool s_All = static_cast<int32_t>(s_Confirmed) >= m_Grant.created;

    if (!s_All && !p_Final)
        return;

    std::string s_Entities;
    for (ZEntityType** s_Entity : m_Grant.given)
        s_Entities += fmt::format("{}{}", s_Entities.empty() ? "" : ", ", fmt::ptr(s_Entity));

    m_Grant.outcome = fmt::format("{} of {} given items in 47's inventory", s_Confirmed, m_Grant.created);
    LogTo(fr::LogTopic::Loadout,
          "loadout lockpick grant: {} after {:.1f} s ({} by ItemPickedUp; world inventory scan {}: [{}]){}",
          m_Grant.outcome, m_Grant.timer, s_Picked,
          s_Scan.ok ? fmt::format("of {} entries", s_Scan.entries) : "failed: " + s_Scan.why, s_Entities,
          s_All ? "; the item ledger counts them as given"
                : "; NOT all confirmed: the item ledger still counts them as given (a pick with no lockpick of his "
                  "own is held back)");
    m_Grant.step = LockpickGrant::Step::Done;
}

// The exit (EvergreenExitTriggered): the given ids that must not be saved
// (the slow lockpick's and the item grants') get the exit's RemoveRepositoryId
// rounds, unless 47 has one of his own with that id.
void RandomLoadout::GrantExitStart(uint32_t p_EventIndex, std::string_view p_EventName) {
    if (!m_Mission || m_Exit.on || m_Exit.done)
        return;

    std::vector<std::pair<fr::Guid, std::string>> s_Given;

    if (m_Grant.created > 0 && fr::LockpickGrantGuardsSave(m_GrantMode))
        s_Given.emplace_back(m_GrantId, "the given lockpick");
    for (const fr::GrantItem s_Item : fr::kGrantItems) {
        const ItemGrantJob& s_Job = m_Items.items[fr::GrantItemIndex(s_Item)];
        if (s_Job.created > 0)
            s_Given.emplace_back(s_Job.id, fr::GrantItemName(s_Item));
    }

    if (s_Given.empty())
        return;

    const bool s_ListFound = m_InventoryList.load(std::memory_order_relaxed) != nullptr;
    std::string s_Sent;
    m_Exit.ids.clear();

    for (const auto& [s_Id, s_What] : s_Given) {
        const size_t s_Own = m_Ledger.Own(s_Id);

        if (s_Own > 0 || !s_ListFound) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout grant exit (#{} {}): NO RemoveRepositoryId of {} ({}) on \"Inventory items\": {}",
                  p_EventIndex, p_EventName, fr::FormatGuid(s_Id), s_What,
                  s_Own > 0 ? fmt::format("47 has {} of his own with this id (item events)", s_Own)
                            : std::string("the list entity was not found"));
            continue;
        }

        m_Exit.ids.push_back(s_Id);
        s_Sent += fmt::format("{}{} ({}, {} given by the item events)", s_Sent.empty() ? "" : ", ",
                              fr::FormatGuid(s_Id), s_What, m_Ledger.Given(s_Id));
    }

    if (m_Exit.ids.empty()) {
        m_Exit.done = true;
        return;
    }

    m_Exit.on = true;
    m_Exit.timer = 0;
    m_Exit.sendTimer = kExitRemoveEverySeconds; // the first round at the next frame
    LogTo(fr::LogTopic::Loadout,
          "loadout grant exit (#{} {}): \"Inventory items\" gets a RemoveRepositoryId of each of [{}] every {:.1f} s "
          "(or {} frames) until the mission's CpdSet, at most {} rounds (the given items must not be saved)",
          p_EventIndex, p_EventName, s_Sent, kExitRemoveEverySeconds, kExitRemoveEveryTicks, kExitRemoveMax);
}

// From the exit until the mission's CpdSet: "Inventory items" gets a
// RemoveRepositoryId of each given id now and then (whether the exit fills
// the list with what 47 carries before the save is not known; a remove of an
// id it does not hold does nothing).
void RandomLoadout::GrantExitTick(double p_Seconds) {
    if (!m_Exit.on)
        return;

    m_Exit.timer += p_Seconds;
    m_Exit.sendTimer += p_Seconds;
    ++m_Exit.ticks;

    if (m_Exit.sendTimer < kExitRemoveEverySeconds && m_Exit.ticks < kExitRemoveEveryTicks)
        return;

    m_Exit.sendTimer = 0;
    m_Exit.ticks = 0;
    ZEntityType** const s_List = m_InventoryList.load(std::memory_order_relaxed);

    if (!s_List || m_Exit.sends >= kExitRemoveMax) {
        m_Exit.on = false;
        m_Exit.done = true;
        LogTo(fr::LogTopic::Loadout, "loadout grant exit: stopped after {} RemoveRepositoryId rounds and {:.1f} s ({})",
              m_Exit.sends, m_Exit.timer, s_List ? "the most sent" : "the list entity is gone");
        return;
    }

    ++m_Exit.sends;

    for (const fr::Guid& s_Id : m_Exit.ids) {
        const RepoIdSignal s_Remove {s_List, fr::kPinRemoveRepositoryId, s_Id};
        m_OwnListSignal.store(true, std::memory_order_release);
        const DWORD s_Code = SafeSignalRepoId(&s_Remove);
        m_OwnListSignal.store(false, std::memory_order_release);

        if (m_Exit.sends <= 3 || s_Code)
            LogTo(fr::LogTopic::Loadout,
                  "loadout grant exit: RemoveRepositoryId {} on \"Inventory items\" #{} at {:.1f} s: {}",
                  fr::FormatGuid(s_Id), m_Exit.sends, m_Exit.timer, s_Code ? FaultText(s_Code) : "sent");
    }
}

// A mission CpdSet (the end-of-mission save): the given id must not be in InventoryItems.
void RandomLoadout::GrantSaveCheck(uint32_t p_EventIndex, const std::string& p_Json) {
    fr::LoadoutState s_Own;
    fr::MergeCpdSet(s_Own, p_Json);
    const auto* s_Inventory = s_Own.List("InventoryItems");

    if (!s_Inventory)
        return;

    const size_t s_Saved = static_cast<size_t>(std::count(s_Inventory->begin(), s_Inventory->end(), m_GrantId));
    LogTo(fr::LogTopic::Loadout,
          "loadout lockpick grant save check (CpdSet #{}): {} \"{}\" in InventoryItems x{}: {} ({} pushes and {} "
          "removes of it blocked on \"Inventory items\", {} pins with an unreadable payload, {} exit "
          "RemoveRepositoryId sends; 47 has {} of his own with this id by the item events)",
          p_EventIndex, fr::FormatGuid(m_GrantId),
          m_LockpickNames.contains(m_GrantId) ? m_LockpickNames[m_GrantId] : "?", s_Saved,
          !fr::LockpickGrantGuardsSave(m_GrantMode)
              ? (s_Saved == 0 ? "not saved (nails are not guarded; the game does not keep them)"
                              : "SAVED (nails are not guarded, and the game kept them)")
          : s_Saved == 0 ? "OK: the given lockpick is not saved"
                         : "WRONG: SAVED, the player keeps it (or it is his own, picked up in the mission)",
          m_Grant.pushesBlocked, m_Grant.removesBlocked, m_Grant.unread, m_Exit.sends, m_Ledger.Own(m_GrantId));
}

// --- item grants (grant_makeshift_explosive, grant_lethal_poison, grant_coin) -------------

void RandomLoadout::ConfigureItemGrants(const std::array<bool, fr::kGrantItemCount>& p_On) {
    std::lock_guard s_Lock(m_Mutex);
    m_ItemGrantOn = p_On;
}

bool RandomLoadout::AnyItemGrant() const {
    return std::any_of(m_ItemGrantOn.begin(), m_ItemGrantOn.end(), [](bool p_On) { return p_On; });
}

void RandomLoadout::ResetItemGrants() {
    m_Items = ItemGrants {};

    for (const fr::GrantItem s_Item : fr::kGrantItems)
        m_Items.items[fr::GrantItemIndex(s_Item)].id = fr::GrantItemId(s_Item);
}

bool RandomLoadout::IsGrantKind(fr::GrantItem p_Item, const fr::Guid& p_Id) const {
    if (fr::IsGrantKindId(p_Item, p_Id))
        return true;

    const auto s_It = m_GrantKinds.find(p_Id);
    return s_It != m_GrantKinds.end() && s_It->second == p_Item;
}

// 47's items of the kind by the item ledger: his own, and the given ones.
fr::LockpickCarry RandomLoadout::KindCarry(fr::GrantItem p_Item) const {
    return m_Ledger.Carry([this, p_Item](const fr::Guid& p_Id) { return IsGrantKind(p_Item, p_Id); });
}

// The pro.repo items of each asked kind (for the log, and for telling what
// 47 already carries), and whether each given item is in pro.repo.
void RandomLoadout::ItemGrantCensus(const char* p_When, const std::vector<RepoLoadoutEntry>* p_Entries,
                                    const std::string& p_Why) {
    m_GrantKinds.clear();
    m_GrantKindNames.clear();

    if (!p_Entries) {
        LogTo(fr::LogTopic::Loadout, "loadout item grant: {}: {}; only the known ids count for each kind", p_When,
              p_Why);
        return;
    }

    std::array<size_t, fr::kGrantItemCount> s_Counts {};
    std::array<bool, fr::kGrantItemCount> s_GivenFound {};

    for (const RepoLoadoutEntry& s_Entry : *p_Entries) {
        for (const fr::GrantItem s_Item : fr::kGrantItems) {
            const size_t s_Index = fr::GrantItemIndex(s_Item);
            const bool s_Given = s_Entry.guid == fr::GrantItemId(s_Item);

            if (!m_ItemGrantOn[s_Index] ||
                (!s_Given && !fr::IsGrantKindId(s_Item, s_Entry.guid) &&
                 !fr::IsGrantKindItem(s_Item, s_Entry.title, s_Entry.commonName, s_Entry.traits)))
                continue;

            // An item of two kinds (none seen) counts as the first.
            m_GrantKinds.emplace(s_Entry.guid, s_Item);
            m_GrantKindNames[s_Entry.guid] = s_Entry.commonName[0] ? s_Entry.commonName : s_Entry.title;
            s_GivenFound[s_Index] = s_GivenFound[s_Index] || s_Given;

            if (++s_Counts[s_Index] <= kMaxKindCensusLines)
                LogTo(fr::LogTopic::Loadout,
                      "loadout item grant: {}: {} item {} \"{}\" / \"{}\": type {}, traits [{}], rarity {}, capacity "
                      "{}; {}{}",
                      p_When, fr::GrantKindName(s_Item), fr::FormatGuid(s_Entry.guid), s_Entry.title,
                      s_Entry.commonName, s_Entry.itemType[0] ? s_Entry.itemType : "-", s_Entry.traits,
                      s_Entry.hasRarity ? fmt::format("{}", s_Entry.rarity) : "-",
                      s_Entry.hasCapacity ? fmt::format("{}", s_Entry.capacity) : "-", OwnedText(s_Entry.guid),
                      s_Given ? " (THE GIVEN ITEM)" : "");
            break;
        }
    }

    for (const fr::GrantItem s_Item : fr::kGrantItems) {
        const size_t s_Index = fr::GrantItemIndex(s_Item);

        if (m_ItemGrantOn[s_Index])
            LogTo(fr::LogTopic::Loadout,
                  "loadout item grant: {}: {}: {} items of {} pro.repo items count as 47's own {}{}; the given item {} "
                  "\"{}\" is {}",
                  p_When, fr::GrantItemSetting(s_Item), s_Counts[s_Index], p_Entries->size(), fr::GrantKindName(s_Item),
                  s_Counts[s_Index] > kMaxKindCensusLines ? fmt::format(" (the first {} logged)", kMaxKindCensusLines)
                                                          : std::string(),
                  fr::FormatGuid(fr::GrantItemId(s_Item)), fr::GrantItemName(s_Item),
                  s_GivenFound[s_Index] ? "in pro.repo" : "NOT among pro.repo's items (CreateItem may fail)");
    }
}

void RandomLoadout::ItemGrantTick(double p_Seconds) {
    ItemGrants& s_Grants = m_Items;

    if (s_Grants.step == ItemGrants::Step::Idle || s_Grants.step == ItemGrants::Step::Done)
        return;

    s_Grants.timer += p_Seconds;

    switch (s_Grants.step) {
        case ItemGrants::Step::Wait:
            // After the packed items and the random loadout's CreateItems (it may bring one of the kinds).
            if (GrantStartReady(s_Grants.timer, "loadout item grant")) {
                s_Grants.step = ItemGrants::Step::Give;
                s_Grants.timer = 0;
            }
            break;

        case ItemGrants::Step::Give:
            ItemGrantGive();

            if (s_Grants.step == ItemGrants::Step::Give && s_Grants.timer >= kHeroWaitSeconds) {
                LogTo(fr::LogTopic::Loadout,
                      "loadout item grant: gave up after {:.0f} s of game time: 47 or his inventory was not found; "
                      "nothing given",
                      kHeroWaitSeconds);
                for (ItemGrantJob& s_Job : s_Grants.items)
                    s_Job.outcome = "47 or his inventory was not found";
                s_Grants.step = ItemGrants::Step::Done;
            }
            break;

        case ItemGrants::Step::Confirm:
            s_Grants.scanTimer += p_Seconds;

            if (s_Grants.scanTimer >= kScanEverySeconds || s_Grants.timer >= kConfirmSeconds) {
                s_Grants.scanTimer = 0;
                ItemGrantConfirm(s_Grants.timer >= kConfirmSeconds);
            }
            break;

        default: break;
    }
}

// Each asked item 47 carries none of (by kind) is created in his inventory,
// the lockpick grant's way. Stays in Give while 47 or his inventory are not there.
void RandomLoadout::ItemGrantGive() {
    std::string s_Found;
    const HeroFind s_Find = FindHeroInventory(s_Found);

    if (s_Find == HeroFind::NotYet)
        return;

    if (s_Find == HeroFind::Failed) {
        LogTo(fr::LogTopic::Loadout, "loadout item grant: {}; nothing given", s_Found);
        for (ItemGrantJob& s_Job : m_Items.items)
            s_Job.outcome = s_Found;
        m_Items.step = ItemGrants::Step::Done;
        return;
    }

    const fr::LoadoutItemTypeChoice s_Type = fr::ChooseLoadoutItemType(m_PackedCreateType);

    for (const fr::GrantItem s_Item : fr::kGrantItems) {
        const size_t s_Index = fr::GrantItemIndex(s_Item);
        ItemGrantJob& s_Job = m_Items.items[s_Index];

        if (!m_ItemGrantOn[s_Index])
            continue;

        // His own items of the kind: the item ledger (packed, the random loadout's, picked up, minus removed).
        const size_t s_OwnCount = KindCarry(s_Item).own;

        if (fr::DecideGrant(true, s_OwnCount) == fr::GrantVerdict::Carried) {
            std::string s_Own;
            for (const auto& [s_Id, s_Kind] : m_GrantKinds) {
                if (s_Kind == s_Item && m_Ledger.Own(s_Id))
                    s_Own += fmt::format("{}{} \"{}\" x{}", s_Own.empty() ? "" : ", ", fr::FormatGuid(s_Id),
                                         m_GrantKindNames[s_Id], m_Ledger.Own(s_Id));
            }
            s_Job.outcome = fmt::format("47 carries his own {}", fr::GrantKindName(s_Item));
            LogTo(fr::LogTopic::Loadout,
                  "loadout item grant: 47 carries his own {} ({} by the item events{}); no {} given",
                  fr::GrantKindName(s_Item), s_OwnCount, s_Own.empty() ? std::string() : ": " + s_Own,
                  fr::GrantItemName(s_Item));
            continue;
        }

        // Before the CreateItem: the game may record the item in "Inventory items" at once.
        m_GuardedIds.insert(s_Job.id);
        m_GrantListOn.store(true, std::memory_order_release);

        CreateArgs s_Args {m_HeroInventory, s_Job.id, s_Type.type, nullptr};
        m_Ledger.Expect(s_Job.id, true); // before: its ItemPickedUp may come at once
        m_InOwnCreate = true;
        const DWORD s_Code = SafeCreateItem(&s_Args);
        m_InOwnCreate = false;

        if (!s_Code)
            ++s_Job.created;
        else {
            m_Ledger.Retract(s_Job.id, true);
            s_Job.outcome = "CreateItem faulted: " + FaultText(s_Code);
        }

        LogTo(
            fr::LogTopic::Loadout,
            "loadout item grant: 47 carries no {} (item events; 47 {}; {} {} ids known): CreateItem {} \"{}\" x1 "
            "(type {}, {}; no gear slot): {}; the mission's \"Inventory items\" now takes no push or remove of it (so "
            "the save never holds it)",
            fr::GrantKindName(s_Item), fmt::ptr(m_Hero),
            std::count_if(m_GrantKinds.begin(), m_GrantKinds.end(),
                          [&](const auto& p_Entry) { return p_Entry.second == s_Item; }),
            fr::GrantKindName(s_Item), fr::FormatGuid(s_Job.id), fr::GrantItemName(s_Item), s_Type.type,
            fr::CreateItemTypeName(s_Type.type),
            s_Code          ? FaultText(s_Code)
            : s_Args.result ? std::string("an entry")
                            : std::string("null"));
    }

    m_Items.step = ItemGrants::Step::Confirm;
    m_Items.timer = 0;
    m_Items.scanTimer = 0;
}

// The given items are confirmed by their ItemPickedUp (the item ledger).
void RandomLoadout::ItemGrantConfirm(bool p_Final) {
    bool s_All = true;

    for (const fr::GrantItem s_Item : fr::kGrantItems) {
        ItemGrantJob& s_Job = m_Items.items[fr::GrantItemIndex(s_Item)];

        if (s_Job.created == 0 || s_Job.confirmDone)
            continue;

        const size_t s_Pending = m_Ledger.GivenPending(s_Job.id);
        const size_t s_Picked = static_cast<size_t>(s_Job.created) > s_Pending ? s_Job.created - s_Pending : 0;

        if (static_cast<int32_t>(s_Picked) < s_Job.created && !p_Final) {
            s_All = false;
            continue;
        }

        s_Job.confirmDone = true;
        s_Job.outcome = fmt::format("{} of {} given in 47's inventory", s_Picked, s_Job.created);
        LogTo(fr::LogTopic::Loadout, "loadout item grant: {} {} \"{}\": {} after {:.1f} s (by ItemPickedUp){}",
              fr::GrantKindName(s_Item), fr::FormatGuid(s_Job.id), fr::GrantItemName(s_Item), s_Job.outcome,
              m_Items.timer,
              static_cast<int32_t>(s_Picked) >= s_Job.created
                  ? "; the item ledger counts it as given"
                  : "; NOT confirmed: the item ledger still counts it as given");
    }

    if (s_All || p_Final)
        m_Items.step = ItemGrants::Step::Done;
}

// A mission CpdSet (the end-of-mission save): no given item may be in InventoryItems.
void RandomLoadout::ItemGrantSaveCheck(uint32_t p_EventIndex, const std::string& p_Json) {
    if (std::none_of(m_Items.items.begin(), m_Items.items.end(),
                     [](const ItemGrantJob& p_Job) { return p_Job.created > 0; }))
        return;

    fr::LoadoutState s_Own;
    fr::MergeCpdSet(s_Own, p_Json);
    const auto* s_Inventory = s_Own.List("InventoryItems");

    if (!s_Inventory)
        return;

    for (const fr::GrantItem s_Item : fr::kGrantItems) {
        const ItemGrantJob& s_Job = m_Items.items[fr::GrantItemIndex(s_Item)];

        if (s_Job.created == 0)
            continue;

        const size_t s_Saved = static_cast<size_t>(std::count(s_Inventory->begin(), s_Inventory->end(), s_Job.id));
        LogTo(
            fr::LogTopic::Loadout,
            "loadout item grant save check (CpdSet #{}): {} \"{}\" in InventoryItems x{}: {} ({} pushes and {} removes "
            "of it blocked on \"Inventory items\", {} exit RemoveRepositoryId rounds; 47 has {} of his own with this "
            "id by the item events)",
            p_EventIndex, fr::FormatGuid(s_Job.id), fr::GrantItemName(s_Item), s_Saved,
            s_Saved == 0 ? "OK: the given item is not saved"
                         : "WRONG: SAVED, the player keeps it (or it is his own, picked up in the mission)",
            s_Job.pushesBlocked, s_Job.removesBlocked, m_Exit.sends, m_Ledger.Own(s_Job.id));
    }
}

bool RandomLoadout::OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    ZEntityType** const s_List = m_InventoryList.load(std::memory_order_relaxed);

    if (!s_List || p_Entity.m_pObj != s_List || m_OwnListSignal.load(std::memory_order_relaxed))
        return false;

    if (p_PinId != fr::kPinPushRepositoryId && p_PinId != fr::kPinRemoveRepositoryId)
        return false;

    bool s_Block = false;

    Contained(kTag, "lockpick grant list pin", [&] {
        std::lock_guard s_Lock(m_Mutex);
        const bool s_Push = p_PinId == fr::kPinPushRepositoryId;
        std::string s_Why;
        const std::optional<fr::Guid> s_Read =
            RepoIdPayload(p_Data, m_RepoIdType.load(std::memory_order_acquire), &s_Why);

        if (!s_Read) {
            ++m_Grant.unread;
            LogTo(fr::LogTopic::Loadout,
                  "loadout lockpick grant: \"Inventory items\" {}: payload unreadable ({}); let through",
                  s_Push ? "PushRepositoryId" : "RemoveRepositoryId", s_Why);
            return;
        }

        const fr::Guid s_Id = *s_Read;

        // The guarded ids: the slow lockpick's and the item grants' (from just before their CreateItem).
        if (!fr::GrantListBlocks(m_GrantListOn.load(std::memory_order_relaxed), true, m_GuardedIds.contains(s_Id)))
            return;

        s_Block = true;
        const char* s_Who = "lockpick grant";

        if (s_Id == m_GrantId)
            ++(s_Push ? m_Grant.pushesBlocked : m_Grant.removesBlocked);
        for (ItemGrantJob& s_Job : m_Items.items) {
            if (s_Job.id == s_Id) {
                ++(s_Push ? s_Job.pushesBlocked : s_Job.removesBlocked);
                s_Who = "item grant";
            }
        }
        LogTo(fr::LogTopic::Loadout,
              "loadout {}: \"Inventory items\" {} {} blocked (the given items are not saved; one 47 picks up in the "
              "mission is not either)",
              s_Who, s_Push ? "PushRepositoryId" : "RemoveRepositoryId", fr::FormatGuid(s_Id));
    });

    return s_Block;
}

RandomLoadout::WorldScan RandomLoadout::ScanWorld() {
    WorldScan s_Scan;
    HeroOut s_Hero {};

    if (const DWORD s_Code = SafeHero(&s_Hero)) {
        s_Scan.why = fmt::format("reading the local player faulted ({})", FaultText(s_Code));
        return s_Scan;
    }

    if (!s_Hero.entity || !s_Hero.hitman) {
        s_Scan.why = "no local player (47) yet";
        return s_Scan;
    }

    s_Scan.hero = s_Hero.entity;
    const void* s_ItemType = nullptr;

    if (Globals::TypeRegistry && *Globals::TypeRegistry)
        s_ItemType = (*Globals::TypeRegistry)->GetTypeID(ZString("ZHM5Item"));

    if (!s_ItemType) {
        s_Scan.why = "the type ZHM5Item is not registered";
        return s_Scan;
    }

    if (!m_World) {
        s_Scan.why = "no usable world inventory (" + m_WorldWhy + ")";
        return s_Scan;
    }

    // Under m_Mutex: one buffer for every scan (8192 rows, about 300 KB).
    static std::vector<ScanRow> s_Rows(kMaxWorldItems);

    ScanArgs s_Args {s_ItemType, s_Rows.data(), s_Rows.size(), 0, 0, false, m_World};

    if (const DWORD s_Code = SafeScan(&s_Args)) {
        s_Scan.why =
            fmt::format("the world inventory scan faulted ({}) after {} items", FaultText(s_Code), s_Args.count);
        return s_Scan;
    }

    if (s_Args.noInventory) {
        s_Scan.why = "no world inventory";
        return s_Scan;
    }

    s_Scan.ok = true;
    s_Scan.entries = s_Args.entries;
    s_Scan.items.reserve(s_Args.count);

    for (size_t i = 0; i < s_Args.count; ++i) {
        WorldItem s_Item;
        s_Item.entity = s_Rows[i].entity;
        s_Item.owner = s_Rows[i].owner;
        s_Item.hasId = s_Rows[i].hasId != 0;
        std::memcpy(&s_Item.id, s_Rows[i].id, sizeof(s_Item.id));
        s_Scan.items.push_back(s_Item);
    }

    return s_Scan;
}

void RandomLoadout::DeliveryTick(double p_Seconds) {
    if (m_Phase == Phase::Idle || m_Phase == Phase::Done)
        return;

    m_Timer += p_Seconds;

    switch (m_Phase) {
        case Phase::WaitList:
            if (m_IterationDone || m_Timer >= kListWaitSeconds) {
                LogTo(fr::LogTopic::Loadout,
                      "loadout deliver: {}; {} packed ids given; creating the planned items in {:.1f} s of game time",
                      m_IterationDone ? "\"Inventory items\" is done"
                                      : fmt::format("no OnDone from \"Inventory items\" in {:.0f} s of game time",
                                                    kListWaitSeconds),
                      m_Emitted, m_DelaySeconds);
                m_Phase = Phase::Delay;
                m_Timer = 0;
            }
            break;

        case Phase::Delay:
            if (m_Timer >= m_DelaySeconds) {
                m_Phase = Phase::Deliver;
                m_Timer = 0;
            }
            break;

        case Phase::Deliver:
            if (Deliver()) {
                m_Phase = Phase::Confirm;
                m_Timer = 0;
                m_ScanTimer = 0;
            }
            else if (m_Phase == Phase::Deliver && m_Timer >= kHeroWaitSeconds) {
                LogTo(fr::LogTopic::Loadout,
                      "loadout deliver: gave up after {:.0f} s of game time without 47's inventory; nothing created, "
                      "nothing removed",
                      kHeroWaitSeconds);
                m_Phase = Phase::Done;
            }
            break;

        case Phase::Confirm:
            m_ScanTimer += p_Seconds;

            if (m_ScanTimer >= kScanEverySeconds || m_Timer >= kConfirmSeconds) {
                m_ScanTimer = 0;
                const bool s_Final = m_Timer >= kConfirmSeconds;
                ConfirmScan(s_Final);

                const bool s_AllDone = std::all_of(m_Plan.begin(), m_Plan.end(), [](const Planned& p_Planned) {
                    return !p_Planned.requested || p_Planned.confirmed || p_Planned.briefcase;
                });

                if (s_Final || s_AllDone)
                    m_Phase = Phase::Done;
            }
            break;

        default: break;
    }
}

RandomLoadout::HeroFind RandomLoadout::FindHeroInventory(std::string& p_Text) {
    HeroOut s_Hero {};

    if (SafeHero(&s_Hero) || !s_Hero.entity || !s_Hero.hitman)
        return HeroFind::NotYet;

    ControllersOut s_Controllers {};

    if (const DWORD s_Code = SafeControllers(s_Hero.hitman, &s_Controllers)) {
        p_Text = fmt::format("reading 47's subcontrollers faulted ({})", FaultText(s_Code));
        return HeroFind::Failed;
    }

    if (s_Controllers.count == 0)
        return HeroFind::NotYet;

    std::string s_Names;
    std::optional<size_t> s_ByName;

    for (size_t i = 0; i < std::min(s_Controllers.count, kMaxControllers); ++i) {
        std::string s_Class;
        uint64_t s_Id = 0;
        ReadEntityClassName(s_Controllers.entity[i], s_Class, &s_Id);
        s_Names += fmt::format("{}{} {}", s_Names.empty() ? "" : ", ", i, s_Class.empty() ? "?" : s_Class);

        if (!s_ByName && s_Class == "ZCharacterSubcontrollerInventory" && s_Controllers.iface[i])
            s_ByName = i;
    }

    const std::optional<size_t> s_Index = s_ByName                                            ? s_ByName
                                          : s_Controllers.count > 6 && s_Controllers.iface[6] ? std::optional<size_t>(6)
                                                                                              : std::nullopt;
    p_Text = fmt::format("47 {}; {} subcontrollers: {}; inventory: {}", fmt::ptr(s_Hero.entity), s_Controllers.count,
                         s_Names,
                         s_ByName  ? fmt::format("#{} (by class name)", *s_ByName)
                         : s_Index ? std::string("#6 (by position, as the SDK's Assets mod; no class name matched)")
                                   : std::string("NOT FOUND (47's inventory subcontroller)"));

    if (!s_Index)
        return HeroFind::Failed;

    m_Hero = s_Hero.entity;
    m_HeroInventory = s_Controllers.iface[*s_Index];
    return HeroFind::Found;
}

// Finds 47's inventory and creates each planned item 47 does not carry yet.
// False: 47 or his inventory is not there yet (tried again next frame).
bool RandomLoadout::Deliver() {
    std::string s_Found;
    const HeroFind s_Find = FindHeroInventory(s_Found);

    if (s_Find == HeroFind::NotYet)
        return false;

    LogTo(fr::LogTopic::Loadout, "loadout deliver: {}", s_Found);

    if (s_Find == HeroFind::Failed) {
        LogTo(fr::LogTopic::Loadout, "loadout deliver: 47's inventory was not found; nothing created, nothing removed");
        m_Phase = Phase::Done;
        return false;
    }

    ResolveWorldInventory();

    // What 47 carries now: proves the scan (the packed items should be listed).
    const WorldScan s_Scan = ScanWorld();
    std::set<fr::Guid> s_Carried;

    if (s_Scan.ok) {
        std::string s_Text;
        size_t s_Owned = 0;

        for (const WorldItem& s_Item : s_Scan.items) {
            if (s_Item.owner != m_Hero)
                continue;

            ++s_Owned;
            if (s_Item.hasId) {
                s_Carried.insert(s_Item.id);
                s_Text += fmt::format("{}{} \"{}\"", s_Text.empty() ? "" : "; ", fr::FormatGuid(s_Item.id),
                                      NameOf(s_Item.id));
            }
        }

        const size_t s_PackedCarried = static_cast<size_t>(std::count_if(
            m_Packed.begin(), m_Packed.end(), [&](const fr::Guid& p_Id) { return s_Carried.contains(p_Id); }));
        LogTo(
            fr::LogTopic::Loadout,
            "loadout deliver: world inventory {} entries, {} items; 47 owns {}: [{}]; packed items among them {}/{}{}",
            s_Scan.entries, s_Scan.items.size(), s_Owned, s_Text, s_PackedCarried, m_Packed.size(),
            m_Packed.size() && !s_PackedCarried
                ? " (NONE: the owner check may not work; only ItemPickedUp can confirm a delivery)"
                : "");
    }
    else {
        LogTo(fr::LogTopic::Loadout,
              "loadout deliver: world inventory not scanned: {}; 47's items unknown, only ItemPickedUp can confirm a "
              "delivery",
              s_Scan.why);
    }

    const fr::LoadoutItemTypeChoice s_Type = fr::ChooseLoadoutItemType(m_PackedCreateType);
    LogTo(fr::LogTopic::Loadout, "loadout deliver: create type {} ({}): {}", s_Type.type,
          fr::CreateItemTypeName(s_Type.type), s_Type.why);
    size_t s_Requested = 0;

    for (Planned& s_Planned : m_Plan) {
        const bool s_Has = s_Carried.contains(s_Planned.id);

        if (!fr::NeedsDelivery(s_Has, s_Planned.requested)) {
            s_Planned.skipNote = s_Has ? "47 already carries it" : "already requested";
            LogTo(fr::LogTopic::Loadout, "loadout spawn: {} \"{}\": {}; not created again (it stays on the {})",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, s_Planned.skipNote,
                  fr::LoadoutSourceName(s_Planned.source));
            continue;
        }

        s_Planned.before.clear();
        for (const WorldItem& s_Item : s_Scan.items)
            if (s_Item.hasId && s_Item.id == s_Planned.id)
                s_Planned.before.insert(s_Item.entity);

        // A large item: inside a briefcase, never CreateItem (that gives it loose).
        if (s_Planned.briefcase) {
            const size_t s_Index = static_cast<size_t>(&s_Planned - m_Plan.data());

            if (m_Job.step != BriefcaseJob::Step::Idle || m_Hold.plan) {
                s_Planned.skipNote = "a second briefcase item (vanilla carries one briefcase)";
                LogTo(fr::LogTopic::Loadout,
                      "loadout spawn: {} \"{}\" (BRIEFCASE item): {}; not created (it stays on the {})",
                      fr::FormatGuid(s_Planned.id), s_Planned.name, s_Planned.skipNote,
                      fr::LoadoutSourceName(s_Planned.source));
                continue;
            }

            // Not into 47's hands while he is in a blend spot or an act (lying
            // on a gurney, hiding, a start act): that ends it on the spot and
            // can leave him stuck in the furniture (fr::JudgeBlendHold). At the
            // delivery the limits are 0: not in one, or not readable, gives it
            // at once.
            const HeroStateRead s_State = ReadHeroState();
            const fr::BlendHoldVerdict s_Hold = fr::JudgeBlendHold(s_State.mask, 0, 0, 0, 0);
            const std::string s_StateText = HeroStateLogText(s_State.mask, s_State.movement);

            if (!fr::BlendHoldGives(s_Hold)) {
                m_Hold = BlendHold {};
                m_Hold.plan = s_Index;
                m_Hold.lastMask = s_State.mask;
                m_Hold.lastMove = s_State.movement;
                s_Planned.skipNote = "held while 47 is in a blend spot or an act";
                LogTo(fr::LogTopic::Loadout,
                      "loadout briefcase: {} \"{}\" (BRIEFCASE item): held back: {} ({}; holding on {}). The "
                      "briefcase would go into his hands, and that ends a blend or an act (lying on a gurney, hiding, "
                      "a start "
                      "act) on the spot and can leave him stuck in the furniture. It is given once none of those is on "
                      "for "
                      "{:.1f} s (or once his state has been unreadable for {:.0f} s); \"loadout briefcase:\" lines",
                      fr::FormatGuid(s_Planned.id), s_Planned.name, fr::BlendHoldVerdictName(s_Hold), s_StateText,
                      fr::HeroStateText(fr::kBlendHoldStates), fr::kBlendClearSeconds, fr::kBlendUnreadSeconds);
                continue;
            }

            LogTo(fr::LogTopic::Loadout, "loadout briefcase: {} \"{}\": not held: {} ({}{}); given now",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, fr::BlendHoldVerdictName(s_Hold), s_StateText,
                  s_State.why.empty() ? std::string() : "; " + s_State.why);
            s_Requested += StartBriefcase(s_Planned, s_Index) ? 1 : 0;
            continue;
        }

        // A large holsterable item: the game's pocket call (CreateItem gives it into 47's hands).
        if (s_Planned.pocket && StartPocket(s_Planned, static_cast<size_t>(&s_Planned - m_Plan.data()))) {
            ++s_Requested;
            continue;
        }

        s_Requested += CreatePlanned(s_Planned, s_Type) ? 1 : 0;
    }

    m_HandsWatch = kHandsWatchSeconds;
    LendRequested();

    LogTo(fr::LogTopic::Loadout,
          "loadout deliver: {} of {} planned items requested; watching for them in 47's inventory for {:.0f} s of game "
          "time (only a confirmed Freelancer tool is removed from the tools; gear wall items stay on the wall){}{}",
          s_Requested, m_Plan.size(), kConfirmSeconds,
          m_Job.step != BriefcaseJob::Step::Idle
              ? fmt::format("; the briefcase item is watched for {:.0f} s (\"loadout briefcase:\" lines)",
                            fr::kBriefcaseTimeoutSeconds)
          : m_Hold.plan ? std::string("; the briefcase item is held while 47 is in a blend spot or an act, then "
                                      "watched for its own time (\"loadout briefcase:\" lines)")
                        : std::string(),
          m_Pockets.empty()
              ? std::string()
              : fmt::format("; {} large holsterable item(s) go into the pockets through the game's pocket "
                            "call, one at a time (\"loadout holster:\" lines)",
                            m_Pockets.size()));
    return true;
}

// Gear wall items are lent, never taken off the wall: remembered until a CPD
// shows them back on it (fr::WallLoans). Called after each round of requests
// (the delivery, and a held briefcase item once given).
void RandomLoadout::LendRequested() {
    size_t s_Lent = 0;
    for (const Planned& s_Planned : m_Plan) {
        if (!s_Planned.requested || s_Planned.source != fr::LoadoutSource::Persistent)
            continue;
        if (m_Loans.Add(s_Planned.id, s_Planned.name)) {
            ++s_Lent;
            LogTo(fr::LogTopic::Loadout,
                  "loadout: \"{}\" ({}) is lent from your gear wall: it stays on the wall and is there again after the "
                  "mission, whatever happens to it (if 47 carries it out, it is returned to the wall at the exit "
                  "instead of "
                  "your loadout)",
                  s_Planned.name, fr::FormatGuid(s_Planned.id));
        }
    }
    if (s_Lent)
        SaveState("wall loans");
}

// --- the blend hold ----------------------------------------------------------------------

// Once per process: are 47's state bits and movement where this exe keeps
// them? Both code sites must be found exactly once with the offsets in them;
// otherwise the state is not read and the briefcase is never held.
void RandomLoadout::CheckHeroState() {
    if (m_HeroStateChecked)
        return;
    m_HeroStateChecked = true;
    m_HeroStateOk = false;
    CheckImage();

    if (!m_Image) {
        m_HeroStateWhy = "the game's exe headers could not be read";
        LogTo(fr::LogTopic::Loadout, "loadout exe: 47's state bits: NOT read ({}); a briefcase item is never held back",
              m_HeroStateWhy);
        return;
    }

    const fr::ImageInfo& s_Image = *m_Image;
    const fr::MemReader s_Read = GameMemory();
    const auto s_Matches = [&](const std::string& p_Pattern, std::string_view p_Mask) {
        std::vector<uint64_t> s_Found;
        for (const fr::ImageSection& s_Section : s_Image.sections) {
            if (!s_Section.executable)
                continue;
            for (const uint64_t s_At : fr::FindPattern(s_Read, s_Section.start, s_Section.size, p_Pattern, p_Mask, 4))
                s_Found.push_back(s_At);
        }
        return s_Found;
    };
    const auto s_Where = [&](const std::vector<uint64_t>& p_Found) {
        std::string s_Text;
        for (const uint64_t s_At : p_Found)
            s_Text += fmt::format("{}{}", s_Text.empty() ? "" : ", ", fr::DescribeAddress(s_Image, s_At));
        return s_Text.empty() ? std::string("none") : s_Text;
    };

    const std::vector<uint64_t> s_Reads = s_Matches(kHeroStateReadPattern, kHeroStateReadMask);
    const std::vector<uint64_t> s_Writes = s_Matches(kHeroStateWritePattern, kHeroStateWriteMask);

    if (s_Reads.size() != 1)
        m_HeroStateWhy =
            fmt::format("the game's hero state check (ZHeroStateConditionEntity, reading ZHitman5+0x{:X}) was "
                        "found {} times, not once",
                        kHeroStateMaskOffset, s_Reads.size());
    else if (s_Writes.size() != 1)
        m_HeroStateWhy =
            fmt::format("ZHitman5's movement-bits update (ZHitman5+0x{:X} and +0x{:X}) was found {} times, "
                        "not once",
                        kHeroMovementOffset, kHeroStateMaskOffset, s_Writes.size());
    else
        m_HeroStateOk = true;

    LogTo(fr::LogTopic::Loadout,
          "loadout exe: 47's state bits (ZHitman5+0x{:X}) and movement (ZHitman5+0x{:X}, type at +0x{:X}): "
          "the game's state check found at {}, its movement-bits update at {}: {}",
          kHeroStateMaskOffset, kHeroMovementOffset, kMovementTypeOffset, s_Where(s_Reads), s_Where(s_Writes),
          m_HeroStateOk ? std::string("read for the briefcase's blend hold")
                        : "NOT read (" + m_HeroStateWhy + "); a briefcase item is never held back");
}

// 47's state bits and movement now (game thread). The mask is empty when it
// cannot be trusted: the exe check failed, no 47, a fault, or a bit past
// eHGS_NUM (fr::IsHeroStateMaskSane).
RandomLoadout::HeroStateRead RandomLoadout::ReadHeroState() {
    HeroStateRead s_State;
    CheckHeroState();

    if (!m_HeroStateOk) {
        s_State.why = "the exe check failed: " + m_HeroStateWhy;
        return s_State;
    }

    HeroOut s_Hero {};
    if (const DWORD s_Code = SafeHero(&s_Hero)) {
        s_State.why = fmt::format("reading the local player faulted ({})", FaultText(s_Code));
        return s_State;
    }
    if (!s_Hero.hitman) {
        s_State.why = "no 47 (ZHitman5) yet";
        return s_State;
    }

    const uint64_t s_Hitman = reinterpret_cast<uint64_t>(s_Hero.hitman);
    const std::optional<uint64_t> s_Mask = ReadQword(s_Hitman + kHeroStateMaskOffset);

    if (!s_Mask)
        s_State.why =
            fmt::format("ZHitman5+0x{:X} at {} was not readable", kHeroStateMaskOffset, fmt::ptr(s_Hero.hitman));
    else if (!fr::IsHeroStateMaskSane(*s_Mask))
        s_State.why = fmt::format("ZHitman5+0x{:X} holds 0x{:X}, with bits past the {} hero states: not that field",
                                  kHeroStateMaskOffset, *s_Mask, fr::kHeroStateCount);
    else
        s_State.mask = s_Mask;

    // The movement, for the log only.
    const std::optional<uint64_t> s_Movement = ReadQword(s_Hitman + kHeroMovementOffset);
    if (s_Movement && *s_Movement) {
        int32_t s_Type = 0;
        if (GameMemory()(*s_Movement + kMovementTypeOffset, &s_Type, sizeof(s_Type)))
            s_State.movement = s_Type;
    }

    return s_State;
}

// Every frame while the briefcase item is held: given once none of the hold
// states is on for kBlendClearSeconds (or the state stays unreadable for
// kBlendUnreadSeconds); a line at each change of 47's state (at most
// kHoldChangeLines) and every kHoldLogSeconds.
void RandomLoadout::HoldTick(double p_Seconds) {
    if (!m_Hold.plan)
        return;

    if (*m_Hold.plan >= m_Plan.size()) {
        m_Hold.plan.reset();
        return;
    }

    m_Hold.seconds += p_Seconds;
    m_Hold.sinceLog += p_Seconds;

    const HeroStateRead s_State = ReadHeroState();
    const bool s_Read = s_State.mask.has_value();
    const bool s_InBlend = s_Read && (*s_State.mask & fr::kBlendHoldStates);
    m_Hold.unread = s_Read ? 0.0 : m_Hold.unread + p_Seconds;
    m_Hold.clear = s_Read && !s_InBlend ? m_Hold.clear + p_Seconds : 0.0;
    const fr::BlendHoldVerdict s_Verdict = fr::JudgeBlendHold(s_State.mask, m_Hold.clear, m_Hold.unread);

    const size_t s_Index = *m_Hold.plan;
    Planned& s_Planned = m_Plan[s_Index];
    const std::string s_StateText = HeroStateLogText(s_State.mask, s_State.movement);

    if (s_State.mask != m_Hold.lastMask || s_State.movement != m_Hold.lastMove) {
        m_Hold.lastMask = s_State.mask;
        m_Hold.lastMove = s_State.movement;
        if (m_Hold.changeLines < kHoldChangeLines) {
            ++m_Hold.changeLines;
            LogTo(fr::LogTopic::Loadout, "loadout briefcase: {} \"{}\": held {:.1f} s: 47's state changed: {}{}: {}",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, m_Hold.seconds, s_StateText,
                  s_State.why.empty() ? std::string() : "; " + s_State.why, fr::BlendHoldVerdictName(s_Verdict));
        }
    }

    if (!fr::BlendHoldGives(s_Verdict)) {
        if (m_Hold.sinceLog >= kHoldLogSeconds) {
            m_Hold.sinceLog = 0;
            LogTo(fr::LogTopic::Loadout, "loadout briefcase: {} \"{}\": still held after {:.0f} s: {} ({})",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, m_Hold.seconds, fr::BlendHoldVerdictName(s_Verdict),
                  s_StateText);
        }
        return;
    }

    m_Hold.plan.reset();
    s_Planned.skipNote.clear();
    LogTo(fr::LogTopic::Loadout, "loadout briefcase: {} \"{}\": {} after {:.1f} s held ({}{}): given now",
          fr::FormatGuid(s_Planned.id), s_Planned.name, fr::BlendHoldVerdictName(s_Verdict), m_Hold.seconds,
          s_StateText, s_State.why.empty() ? std::string() : "; " + s_State.why);

    if (StartBriefcase(s_Planned, s_Index))
        LendRequested();
}

// CreateItem of one planned item in 47's inventory.
bool RandomLoadout::CreatePlanned(Planned& p_Planned, const fr::LoadoutItemTypeChoice& p_Type) {
    CreateArgs s_Args {m_HeroInventory, p_Planned.id, p_Type.type, nullptr};
    m_Ledger.Expect(p_Planned.id); // before: its ItemPickedUp may come at once
    m_InOwnCreate = true;
    const DWORD s_Code = SafeCreateItem(&s_Args);
    m_InOwnCreate = false;

    if (s_Code)
        m_Ledger.Retract(p_Planned.id);

    p_Planned.requested = !s_Code;
    p_Planned.requestNote = s_Code ? fmt::format("CreateItem faulted: {}", FaultText(s_Code))
                                   : fmt::format("CreateItem {} returned {}", fr::CreateItemTypeName(p_Type.type),
                                                 s_Args.result ? "an entry" : "null");
    LogTo(fr::LogTopic::Loadout,
          "loadout spawn requested: {} \"{}\"{} from the {}: {} ({}); {} item entities with this id existed before",
          fr::FormatGuid(p_Planned.id), p_Planned.name, p_Planned.briefcase ? " (BRIEFCASE item)" : "",
          fr::LoadoutSourceName(p_Planned.source), p_Planned.requestNote, fmt::ptr(s_Args.result),
          p_Planned.before.size());
    return p_Planned.requested;
}

void RandomLoadout::ConfirmScan(bool p_Final) {
    const WorldScan s_Scan = ScanWorld();

    if (!s_Scan.ok && !m_ScanFaultLogged) {
        m_ScanFaultLogged = true;
        LogTo(fr::LogTopic::Loadout, "loadout scan: {}", s_Scan.why);
    }

    for (Planned& s_Planned : m_Plan) {
        if (!s_Planned.requested || s_Planned.briefcase) // the briefcase item: BriefcaseTick
            continue;

        if (s_Scan.ok) {
            size_t s_Ours = 0;
            size_t s_None = 0;
            size_t s_Other = 0;

            for (const WorldItem& s_Item : s_Scan.items) {
                if (!s_Item.hasId || s_Item.id != s_Planned.id || s_Planned.before.contains(s_Item.entity))
                    continue;

                if (s_Planned.spawned.insert(s_Item.entity).second)
                    LogTo(fr::LogTopic::Loadout, "loadout spawned: {} \"{}\": new item entity {}",
                          fr::FormatGuid(s_Planned.id), s_Planned.name, fmt::ptr(s_Item.entity));

                ++(s_Item.owner == s_Scan.hero ? s_Ours : s_Item.owner ? s_Other : s_None);
            }

            const std::string s_State =
                fmt::format("new item entities {}: owned by 47 {}, by no one {}, by another entity {}",
                            s_Ours + s_None + s_Other, s_Ours, s_None, s_Other);

            if (s_State != s_Planned.ownerState) {
                s_Planned.ownerState = s_State;
                LogTo(fr::LogTopic::Loadout, "loadout scan: {} \"{}\": {}", fr::FormatGuid(s_Planned.id),
                      s_Planned.name, s_State);
            }

            if (s_Ours && !s_Planned.confirmed) {
                s_Planned.confirmed = true;
                s_Planned.confirmedBy = "the inventory scan (47 owns a new item entity)";
                s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
                LogTo(fr::LogTopic::Loadout,
                      "loadout in inventory: {} \"{}\": CONFIRMED (47 owns the new item entity); {}",
                      fr::FormatGuid(s_Planned.id), s_Planned.name, AfterConfirm(s_Planned.source));
            }
        }

        if (p_Final && !s_Planned.confirmed)
            LogTo(fr::LogTopic::Loadout,
                  "loadout in inventory: {} \"{}\"{}: NOT confirmed after {:.0f} s ({}; no ItemPickedUp); NOT removed "
                  "from the {} (it stays owned)",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, s_Planned.briefcase ? " (BRIEFCASE item)" : "",
                  kConfirmSeconds,
                  s_Planned.ownerState.empty() ? std::string("no new item entity seen") : s_Planned.ownerState,
                  fr::LoadoutSourceName(s_Planned.source));
    }
}

// --- events --------------------------------------------------------------------------------

void RandomLoadout::OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
    Contained(kTag, "event", [&] { EventSent(p_EventIndex, p_Event); });
}

void RandomLoadout::EventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) {
    if (!m_Freelancer.load(std::memory_order_relaxed))
        return;

    EventNameOut s_Name {};

    if (SafeEventName(&p_Event, &s_Name))
        return;

    const std::string_view s_EventName = s_Name.name;
    const bool s_Cpd = s_EventName == "CpdSet";
    const bool s_Total = s_EventName == "GearSlotsTotal";
    const bool s_Used = s_EventName == "GearSlotsUsed";
    const bool s_Picked = s_EventName == "ItemPickedUp";
    const std::optional<fr::HeroItemEvent> s_ItemEvent = fr::ParseHeroItemEvent(s_EventName);
    const bool s_Exit = s_EventName == "EvergreenExitTriggered" || s_EventName == "EvergreenExitTriggeredOrWounded";
    const bool s_Held = s_EventName == "HoldingIllegalWeapon";

    if (!s_Cpd && !s_Total && !s_Used && !s_ItemEvent && !s_Exit && !s_Held)
        return;

    if (s_Exit) {
        std::lock_guard s_Lock(m_Mutex);
        // lockpick_grant and the item grants: the exit may now fill "Inventory items" with what 47 carries out.
        GrantExitStart(p_EventIndex, s_EventName);
        // The lent gear wall items: back to the wall, not into the loadout (not on "...OrWounded" alone: a
        // wounded exit is not a carry-out).
        if (s_EventName == "EvergreenExitTriggered")
            WallReturnStart(p_EventIndex);
        return;
    }

    const std::string s_Json = EventJson(p_Event);
    std::lock_guard s_Lock(m_Mutex);

    if (s_Held) {
        if (m_Mission && !m_Plan.empty())
            OnHoldingIllegalWeapon(p_EventIndex, s_Json);
        return;
    }

    if (s_ItemEvent) {
        if (m_Mission)
            LedgerEvent(*s_ItemEvent, p_EventIndex, s_Json);
        if (s_Picked && m_Mission && !m_Plan.empty())
            OnItemPickedUp(p_EventIndex, s_Json);
        return;
    }

    if (s_Total || s_Used) {
        if (!m_Safehouse)
            return;

        const auto s_Value = fr::GearSlotsValue(s_Json);
        std::optional<int32_t>& s_Slot = s_Total ? m_Live.slotsTotal : m_Live.slotsUsed;

        if (s_Value && s_Slot != s_Value) {
            LogTo(fr::LogTopic::Loadout, "loadout: {} {} (was {})", s_EventName, *s_Value,
                  s_Slot ? fmt::format("{}", *s_Slot) : "not seen");
            s_Slot = s_Value;
            m_Live.updatedUnix = UnixNow();
            m_SafehouseCpdSinceFreeze = true;
            SaveState(s_Total ? "GearSlotsTotal" : "GearSlotsUsed");
        }

        return;
    }

    // CpdSet.
    if (m_Mission && !m_Plan.empty())
        SaveCheck(p_EventIndex, s_Json);

    if (m_Mission && m_Grant.created > 0)
        GrantSaveCheck(p_EventIndex, s_Json);

    if (m_Mission)
        ItemGrantSaveCheck(p_EventIndex, s_Json);

    if (m_Mission && !m_Return.items.empty() && !m_Return.checked)
        WallReturnCheck(p_EventIndex, s_Json);

    if (m_Mission && m_Exit.on) {
        m_Exit.on = false;
        m_Exit.done = true;
        LogTo(fr::LogTopic::Loadout,
              "loadout grant exit: the mission's CpdSet #{} came after {:.1f} s and {} RemoveRepositoryId rounds; no "
              "more",
              p_EventIndex, m_Exit.timer, m_Exit.sends);
    }

    const fr::CpdMerge s_Merge = fr::MergeCpdSet(m_Live, s_Json);

    if (s_Merge.lists.empty() && s_Merge.ints.empty())
        return;

    m_Live.updatedUnix = UnixNow();

    if (m_Safehouse)
        m_SafehouseCpdSinceFreeze = true;

    std::string s_Lists;
    for (const std::string& s_List : s_Merge.lists)
        s_Lists += (s_Lists.empty() ? "" : ", ") + s_List;
    std::string s_Ints;
    for (const std::string& s_Int : s_Merge.ints)
        s_Ints += (s_Ints.empty() ? "" : ", ") + s_Int;

    LogTo(fr::LogTopic::Loadout, "loadout cpd #{} ({}): merged lists [{}], values [{}]{}", p_EventIndex,
          m_Safehouse ? "safehouse" : "mission", s_Lists, s_Ints,
          s_Merge.badIds ? fmt::format(", {} list entries not GUIDs", s_Merge.badIds) : std::string());

    // The departure's lists in full (ids; names are logged at the mission load).
    if (m_Safehouse) {
        for (const char* s_Key : {"InventoryItems", "TransientItems"}) {
            const auto* s_Ids = m_Live.List(s_Key);
            if (!s_Ids || std::none_of(s_Merge.lists.begin(), s_Merge.lists.end(), [&](const std::string& p_Entry) {
                    return p_Entry.starts_with(std::string(s_Key) + " ");
                }))
                continue;

            std::string s_Text;
            for (const fr::Guid& s_Id : *s_Ids)
                s_Text += (s_Text.empty() ? "" : ",") + fr::FormatGuid(s_Id);
            LogTo(fr::LogTopic::Loadout, "loadout cpd #{}: {} = [{}]", p_EventIndex, s_Key, s_Text);
        }
    }

    SaveState("CpdSet");
}

// An ItemPickedUp of a requested planned id: the game says 47 took it.
void RandomLoadout::OnItemPickedUp(uint32_t p_EventIndex, const std::string& p_Json) {
    const auto s_IdText = fr::JsonStringField(p_Json, "RepositoryId");

    if (!s_IdText)
        return;

    const auto s_Id = fr::ParseGuid(*s_IdText);

    if (!s_Id)
        return;

    const auto s_Name = fr::JsonStringField(p_Json, "ItemName");

    // The briefcase item's delivery: the ICA Briefcase reaching 47, or the item
    // itself reaching him loose.
    if (m_Job.step == BriefcaseJob::Step::Watch || m_Job.step == BriefcaseJob::Step::WaitItem) {
        if (*s_Id == *fr::ParseGuid(fr::kIcaBriefcaseId)) {
            m_Job.progress.briefcasePickedUp = true;
            LogTo(fr::LogTopic::Loadout,
                  "loadout briefcase: ItemPickedUp #{} \"{}\" (the ICA Briefcase) at {:.1f} s: 47 has a briefcase",
                  p_EventIndex, s_Name ? *s_Name : "", m_Job.progress.seconds);
        }
        else if (m_Job.plan < m_Plan.size() && *s_Id == m_Plan[m_Job.plan].id) {
            m_Job.progress.itemPickedUp = true;
            m_Job.progress.itemPickedUpAfterPlace = m_Job.progress.placeSent;
            LogTo(fr::LogTopic::Loadout,
                  "loadout briefcase: ItemPickedUp #{} \"{}\" of the item itself at {:.1f} s ({}): {}", p_EventIndex,
                  s_Name ? *s_Name : "", m_Job.progress.seconds,
                  m_Job.progress.placeSent ? "after the place into the briefcase" : "BEFORE any place into a briefcase",
                  m_Job.progress.placeSent ? "checked against the owner" : "it reached 47 LOOSE");
        }
    }

    // A pocket item reaching 47 (into the pockets or the hands: HoldingIllegalWeapon tells).
    for (PocketJob& s_Pocket : m_Pockets) {
        if (s_Pocket.plan >= m_Plan.size() || m_Plan[s_Pocket.plan].id != *s_Id || s_Pocket.progress.itemPickedUp ||
            (s_Pocket.step != PocketJob::Step::Ready && s_Pocket.step != PocketJob::Step::Watch))
            continue;
        s_Pocket.progress.itemPickedUp = true;
        LogTo(fr::LogTopic::Loadout,
              "loadout holster: ItemPickedUp #{} \"{}\" at {:.1f} s after its SetItem: 47 has it", p_EventIndex,
              s_Name ? *s_Name : "", s_Pocket.progress.seconds);
    }

    for (Planned& s_Planned : m_Plan) {
        if (s_Planned.id != *s_Id)
            continue;

        if (s_Planned.briefcase && s_Planned.requested) {
            if (s_Planned.confirmed)
                LogTo(fr::LogTopic::Loadout,
                      "loadout pickup #{}: {} \"{}\": ItemPickedUp (confirmed in the briefcase before; 47 took it out)",
                      p_EventIndex, *s_IdText, s_Name ? *s_Name : "");
            return; // judged by the briefcase delivery, never confirmed by a pickup
        }

        if (!s_Planned.requested) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout pickup #{}: {} \"{}\" (planned) picked up before or without a spawn request (one found in "
                  "the level?); not a confirmation",
                  p_EventIndex, *s_IdText, s_Name ? *s_Name : "");
            return;
        }

        if (s_Planned.confirmed) {
            LogTo(fr::LogTopic::Loadout, "loadout pickup #{}: {} \"{}\": ItemPickedUp (already confirmed by {})",
                  p_EventIndex, *s_IdText, s_Name ? *s_Name : "", s_Planned.confirmedBy);
            return;
        }

        s_Planned.confirmed = true;
        s_Planned.confirmedBy = fmt::format("ItemPickedUp #{}", p_EventIndex);
        s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
        LogTo(fr::LogTopic::Loadout, "loadout in inventory: {} \"{}\": CONFIRMED by ItemPickedUp #{} (\"{}\"); {}",
              fr::FormatGuid(s_Planned.id), s_Planned.name, p_EventIndex, s_Name ? *s_Name : "",
              AfterConfirm(s_Planned.source));
        return;
    }
}

// A mission's CpdSet (the end of the mission saves InventoryItems, rebuilt
// from what 47 carries, and the gear wall): where is each planned item?
void RandomLoadout::SaveCheck(uint32_t p_EventIndex, const std::string& p_Json) {
    fr::LoadoutState s_Own; // only this CpdSet's lists
    fr::MergeCpdSet(s_Own, p_Json);
    const auto* s_Inventory = s_Own.List("InventoryItems");
    std::optional<WorldScan> s_Scan;

    for (const Planned& s_Planned : m_Plan) {
        // A spawned safehouse pickup has no source list: judged by InventoryItems alone.
        const bool s_Spawned = s_Planned.source == fr::LoadoutSource::Spawned;
        const char* s_ListKey = s_Planned.source == fr::LoadoutSource::Transient ? "TransientItems" : "PersistentItems";
        const auto* s_Source = s_Spawned ? nullptr : s_Own.List(s_ListKey);

        if (!s_Inventory && !s_Source)
            continue;

        const auto s_In = [&](const std::vector<fr::Guid>* p_List) -> std::optional<bool> {
            if (!p_List)
                return std::nullopt;
            return std::find(p_List->begin(), p_List->end(), s_Planned.id) != p_List->end();
        };

        std::optional<bool> s_Holds;

        if (s_Planned.requested) {
            if (!s_Scan)
                s_Scan = ScanWorld();

            if (s_Scan->ok)
                s_Holds = std::any_of(s_Scan->items.begin(), s_Scan->items.end(), [&](const WorldItem& p_Item) {
                    return p_Item.owner == s_Scan->hero && p_Item.hasId && p_Item.id == s_Planned.id;
                });
        }
        else {
            s_Holds = false; // never created by the mod (a level copy would not count)
        }

        const auto s_InInventory = s_In(s_Inventory);
        const auto s_InSource = s_In(s_Source);
        const fr::SaveVerdict s_Verdict = fr::JudgeSave(s_InInventory, s_InSource, s_Planned.removed, s_Holds);
        const auto s_Text = [](const std::optional<bool>& p_Value, const char* p_Yes) {
            return p_Value ? (*p_Value ? p_Yes : "no") : "(not in this CpdSet)";
        };

        LogTo(fr::LogTopic::Loadout,
              "loadout save check (CpdSet #{}): {} \"{}\": requested {}, confirmed {}, removed from the {} {}; "
              "InventoryItems {}; {}; 47 holds it {}: {}",
              p_EventIndex, fr::FormatGuid(s_Planned.id), s_Planned.name, s_Planned.requested ? "yes" : "no",
              s_Planned.confirmed ? "yes" : "no", fr::LoadoutSourceName(s_Planned.source),
              s_Planned.removed ? "yes" : "no", s_Text(s_InInventory, "yes"),
              s_Spawned ? std::string("no source list (spawned)")
                        : fmt::format("{} {}", s_ListKey, s_Text(s_InSource, "YES")),
              s_Planned.requested ? (s_Holds ? (*s_Holds ? "yes" : "no") : "unknown") : "(not created)",
              fr::SaveVerdictText(s_Verdict));
    }
}

void RandomLoadout::Tick(double p_GameSeconds) noexcept {
    Contained(kTag, "frame update", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Mission)
            return;

        DeliveryTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        HoldTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        BriefcaseTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        PocketTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        GrantTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        ItemGrantTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        GrantExitTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        WallReturnTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        LoanRepairTick(p_GameSeconds > 0 ? p_GameSeconds : 0);
        Removals();
    });
}

// Removes each confirmed item from its source list (never one that was not
// confirmed in 47's inventory).
void RandomLoadout::Removals() {
    for (Planned& s_Planned : m_Plan) {
        if (!s_Planned.removeQueued || s_Planned.removed)
            continue;

        s_Planned.removeQueued = false;

        if (!fr::RemovesFromSource(s_Planned.source))
            continue; // a gear wall item is lent, never removed (fr::WallLoans)

        ZEntityType** const s_List =
            s_Planned.source == fr::LoadoutSource::Transient ? m_TransientList : m_PersistentList;

        if (!s_List) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout remove: {} \"{}\": the {} list entity was not found; NOT removed (the item may be "
                  "duplicated: it is in the inventory and still owned)",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, fr::LoadoutSourceName(s_Planned.source));
            continue;
        }

        const RepoIdSignal s_Remove {s_List, fr::kPinRemoveRepositoryId, s_Planned.id};
        const DWORD s_Code = SafeSignalRepoId(&s_Remove);
        s_Planned.removed = !s_Code;
        LogTo(fr::LogTopic::Loadout, "loadout remove: {} \"{}\": RemoveRepositoryId on the {} {}",
              fr::FormatGuid(s_Planned.id), s_Planned.name, fr::LoadoutSourceName(s_Planned.source),
              s_Code ? FaultText(s_Code) : "sent");
    }
}

// --- gear wall loans ---------------------------------------------------------------------------

void RandomLoadout::ReviewWallLoans(const char* p_When, const fr::LoadoutState& p_State,
                                    const fr::LoadoutState* p_Departure) {
    const size_t s_Before = m_Loans.loans.size();
    const std::vector<fr::LoanOutcome> s_Outcomes = fr::ReviewLoans(
        m_Loans, p_State, p_Departure ? std::optional<int64_t>(p_Departure->updatedUnix) : std::nullopt);

    for (const fr::LoanOutcome& s_Outcome : s_Outcomes) {
        LogTo(fr::LogTopic::Loadout, "{} ({}: {} by the {})", fr::LoanOutcomeText(s_Outcome), p_When,
              fr::LoanStatusName(s_Outcome.status), p_Departure ? "departure snapshot" : "last saved lists");
        if (s_Outcome.repair)
            m_LoanRepairs.push_back(s_Outcome.loan);
    }

    LogTo(fr::LogTopic::Loadout, "loadout wall loans ({}): {} reviewed, {} still lent{}", p_When, s_Before,
          m_Loans.loans.size(),
          m_LoanRepairs.empty()
              ? std::string()
              : fmt::format("; {} pushed back onto \"Persistent items\" {:.0f} s after the mission starts playing",
                            m_LoanRepairs.size(), kLoanRepairDelaySeconds));
    SaveState("wall loans reviewed");
}

// The mission's pushes back onto the gear wall: "Persistent items" takes its
// contents from the CPD as the scene starts (a push before that is lost), so
// they wait until the scene has played a few seconds. The mission's CpdSet
// saves the list (PushRepositoryId is the pin the game's "Add persistent
// item" drives).
void RandomLoadout::LoanRepairTick(double p_Seconds) {
    if (m_LoanRepairDone || m_LoanRepairs.empty() || !m_ScenePlaying)
        return;

    m_PlayingSeconds += p_Seconds;

    if (m_PlayingSeconds < kLoanRepairDelaySeconds)
        return;

    m_LoanRepairDone = true;

    if (!m_PersistentList) {
        LogTo(fr::LogTopic::Loadout,
              "loadout wall loans: the gear wall list (\"Persistent items\" {:016x}) was not found in this mission; {} "
              "item(s) NOT put back (tried again at the next mission)",
              kPersistentItemsId, m_LoanRepairs.size());
        return;
    }

    for (const fr::WallLoan& s_Loan : m_LoanRepairs) {
        const RepoIdSignal s_Push {m_PersistentList, fr::kPinPushRepositoryId, s_Loan.id};
        const DWORD s_Code = SafeSignalRepoId(&s_Push);
        LogTo(
            fr::LogTopic::Loadout,
            "loadout: \"{}\" ({}) put back on your gear wall: PushRepositoryId on \"Persistent items\" {} (saved with "
            "this mission; checked at the next load)",
            s_Loan.name, fr::FormatGuid(s_Loan.id), s_Code ? FaultText(s_Code) : "sent");
    }
}

// EvergreenExitTriggered: which lent gear wall items get the return rounds
// (fr::PlanWallReturn), each logged with its reason.
void RandomLoadout::WallReturnStart(uint32_t p_EventIndex) {
    if (!m_Mission || m_Return.on || m_Return.checked || !m_Return.items.empty() || !m_Departure)
        return;

    const auto* s_DepartureWall = m_Departure->List("PersistentItems");
    const auto* s_DeparturePacked = m_Departure->List("InventoryItems");
    const bool s_ListsFound = m_PersistentList && m_InventoryList.load(std::memory_order_relaxed);

    for (const Planned& s_Planned : m_Plan) {
        if (s_Planned.source != fr::LoadoutSource::Persistent)
            continue; // a Freelancer tool follows the vanilla rule

        fr::WallReturnInput s_Input;
        s_Input.delivered = s_Planned.requested && m_Loans.Find(s_Planned.id) != nullptr;
        s_Input.packed = fr::CountOf(s_DeparturePacked, s_Planned.id);
        s_Input.wall = fr::CountOf(s_DepartureWall, s_Planned.id);
        s_Input.heroCopies = m_Ledger.Own(s_Planned.id);
        fr::WallReturnPlan s_Plan = fr::PlanWallReturn(s_Input);

        if (s_Plan == fr::WallReturnPlan::Return && !s_ListsFound) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout wall return (#{} EvergreenExitTriggered): \"{}\" ({}): NOT returned: the mission's "
                  "\"Inventory "
                  "items\" or \"Persistent items\" list was not found; if 47 carries it out it comes back in your "
                  "loadout",
                  p_EventIndex, s_Planned.name, fr::FormatGuid(s_Planned.id));
            continue;
        }

        LogTo(fr::LogTopic::Loadout,
              "loadout wall return (#{} EvergreenExitTriggered): \"{}\" ({}): {} (departure: {} on the wall, {} "
              "packed; 47 has {} by the item events)",
              p_EventIndex, s_Planned.name, fr::FormatGuid(s_Planned.id), fr::WallReturnPlanText(s_Plan), s_Input.wall,
              s_Input.packed, s_Input.heroCopies);

        if (s_Plan == fr::WallReturnPlan::Return)
            m_Return.items.push_back({s_Planned.id, s_Planned.name, s_Input.wall, s_Input.packed});
    }

    if (m_Return.items.empty())
        return;

    m_Return.on = true;
    m_Return.timer = 0;
    m_Return.sendTimer = kExitRemoveEverySeconds; // the first round at the next frame
    LogTo(fr::LogTopic::Loadout,
          "loadout wall return: {} lent item(s): each gets RemoveRepositoryId on \"Inventory items\" and "
          "RemoveRepositoryId + PushRepositoryId on \"Persistent items\" every {:.1f} s (or {} frames) until the "
          "mission's "
          "CpdSet, at most {} rounds",
          m_Return.items.size(), kExitRemoveEverySeconds, kExitRemoveEveryTicks, kExitRemoveMax);
}

// One round per kExitRemoveEverySeconds (or kExitRemoveEveryTicks frames):
// the loadout loses the id (a remove of an id it does not hold does nothing,
// as the exit rounds assume), and the wall gets one remove then one push, so
// it holds exactly one whether or not the game took it off before this round.
void RandomLoadout::WallReturnTick(double p_Seconds) {
    if (!m_Return.on)
        return;

    m_Return.timer += p_Seconds;
    m_Return.sendTimer += p_Seconds;
    ++m_Return.ticks;

    if (m_Return.sendTimer < kExitRemoveEverySeconds && m_Return.ticks < kExitRemoveEveryTicks)
        return;

    m_Return.sendTimer = 0;
    m_Return.ticks = 0;
    ZEntityType** const s_Inventory = m_InventoryList.load(std::memory_order_relaxed);

    if (!s_Inventory || !m_PersistentList || m_Return.sends >= kExitRemoveMax) {
        m_Return.on = false;
        LogTo(fr::LogTopic::Loadout, "loadout wall return: stopped after {} rounds and {:.1f} s ({})", m_Return.sends,
              m_Return.timer, s_Inventory && m_PersistentList ? "the most sent" : "a list entity is gone");
        return;
    }

    ++m_Return.sends;

    for (const WallReturn::Item& s_Item : m_Return.items) {
        const RepoIdSignal s_Signals[] {
            {s_Inventory, fr::kPinRemoveRepositoryId, s_Item.id},
            {m_PersistentList, fr::kPinRemoveRepositoryId, s_Item.id},
            {m_PersistentList, fr::kPinPushRepositoryId, s_Item.id},
        };
        const char* const s_What[] {"\"Inventory items\" RemoveRepositoryId", "\"Persistent items\" RemoveRepositoryId",
                                    "\"Persistent items\" PushRepositoryId"};

        for (size_t i = 0; i < std::size(s_Signals); ++i) {
            m_OwnListSignal.store(i == 0, std::memory_order_release);
            const DWORD s_Code = SafeSignalRepoId(&s_Signals[i]);
            m_OwnListSignal.store(false, std::memory_order_release);

            if (m_Return.sends <= 2 || s_Code)
                LogTo(fr::LogTopic::Loadout, "loadout wall return: round {} at {:.1f} s: {} {}: {}", m_Return.sends,
                      m_Return.timer, s_What[i], fr::FormatGuid(s_Item.id), s_Code ? FaultText(s_Code) : "sent");

            if (s_Code && i == 1)
                break; // the wall's remove faulted: no push (it could double the item)
        }
    }
}

// The mission's CpdSet: where each returned item was saved.
void RandomLoadout::WallReturnCheck(uint32_t p_EventIndex, const std::string& p_Json) {
    m_Return.on = false;
    m_Return.checked = true;
    fr::LoadoutState s_Own; // only this CpdSet's lists
    fr::MergeCpdSet(s_Own, p_Json);
    const auto* s_Wall = s_Own.List("PersistentItems");
    const auto* s_Inventory = s_Own.List("InventoryItems");

    for (const WallReturn::Item& s_Item : m_Return.items) {
        const std::optional<size_t> s_SavedWall =
            s_Wall ? std::optional<size_t>(fr::CountOf(s_Wall, s_Item.id)) : std::nullopt;
        const std::optional<size_t> s_SavedInventory =
            s_Inventory ? std::optional<size_t>(fr::CountOf(s_Inventory, s_Item.id)) : std::nullopt;
        const fr::WallReturnVerdict s_Verdict =
            fr::JudgeWallReturn(s_Item.wall, s_Item.packed, s_SavedWall, s_SavedInventory);
        const auto s_Count = [](const std::optional<size_t>& p_Count) {
            return p_Count ? fmt::format("x{}", *p_Count) : std::string("(not in this CpdSet)");
        };

        LogTo(fr::LogTopic::Loadout,
              "loadout wall return check (CpdSet #{}, after {} rounds in {:.1f} s): \"{}\" ({}): PersistentItems {} "
              "(x{} at departure), InventoryItems {} (x{} packed): {}",
              p_EventIndex, m_Return.sends, m_Return.timer, s_Item.name, fr::FormatGuid(s_Item.id),
              s_Count(s_SavedWall), s_Item.wall, s_Count(s_SavedInventory), s_Item.packed,
              fr::WallReturnVerdictText(s_Verdict));
    }
}

// --- the world inventory and RequestNewItem, checked ------------------------------------------

void RandomLoadout::CheckImage() {
    if (m_ImageChecked)
        return;
    m_ImageChecked = true;
    m_RequestNewItemOk = false;

    const uint64_t s_Base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    const fr::MemReader s_Read = GameMemory();
    m_Image = fr::ParseImage(s_Read, s_Base);

    if (!m_Image) {
        m_RequestNewItemWhy = "the game's exe headers could not be read";
        LogTo(fr::LogTopic::Loadout,
              "loadout exe: the PE headers at {} could not be read; RequestNewItem will not be called",
              fmt::ptr(reinterpret_cast<void*>(s_Base)));
        return;
    }

    const fr::ImageInfo& s_Image = *m_Image;

    // ZWorldInventory::RequestNewItem: the SDK takes the first match from the
    // module base over SizeOfCode bytes, so it must be the only match in the
    // exe's code.
    const uint64_t s_Request = EngineFunctionAddress(Functions::ZWorldInventory_RequestNewItem);
    std::vector<uint64_t> s_RequestAll;

    for (const fr::ImageSection& s_Section : s_Image.sections) {
        if (!s_Section.executable)
            continue;
        for (const uint64_t s_At :
             fr::FindPattern(s_Read, s_Section.start, s_Section.size, kRequestNewItemPattern, kRequestNewItemMask, 16))
            s_RequestAll.push_back(s_At);
    }

    if (!s_Request)
        m_RequestNewItemWhy = "the SDK did not locate it";
    else if (s_RequestAll.size() != 1)
        m_RequestNewItemWhy = fmt::format("its SDK pattern (7 bytes) matches {}{} places in the exe's code, so the "
                                          "SDK's pick (the first) may be another function",
                                          s_RequestAll.size(), s_RequestAll.size() >= 16 ? "+" : "");
    else if (s_RequestAll[0] != s_Request)
        m_RequestNewItemWhy = "the SDK's address is not its pattern's match";
    else
        m_RequestNewItemOk = true;

    LogTo(fr::LogTopic::Loadout, "loadout exe: ZWorldInventory::RequestNewItem at {} ({}): {}",
          fmt::ptr(reinterpret_cast<void*>(s_Request)), fr::DescribeAddress(s_Image, s_Request),
          m_RequestNewItemOk ? "usable (its pattern's only match)" : "NOT called: " + m_RequestNewItemWhy);

    // ZWorldInventory's vtables by RTTI.
    const auto s_Start = std::chrono::steady_clock::now();
    m_WorldVtables = fr::RttiVtables(s_Read, s_Image, fr::kWorldInventoryRtti);
    LogTo(fr::LogTopic::Loadout, "loadout exe: ZWorldInventory vtables by RTTI: {} ({:.0f} ms)", m_WorldVtables.size(),
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count());
}

void RandomLoadout::ResolveWorldInventory() {
    if (m_WorldResolved)
        return;
    m_WorldResolved = true;
    m_World = nullptr;
    CheckImage();

    if (!m_Image) {
        m_WorldWhy = "the game's exe headers could not be read";
        LogTo(fr::LogTopic::Loadout, "loadout world inventory: NONE usable: {}", m_WorldWhy);
        return;
    }

    const fr::ImageInfo& s_Image = *m_Image;
    const fr::MemReader s_Read = GameMemory();
    const void* s_ItemType = nullptr;
    if (Globals::TypeRegistry && *Globals::TypeRegistry)
        s_ItemType = (*Globals::TypeRegistry)->GetTypeID(ZString("ZHM5Item"));

    std::vector<fr::InventoryCandidate> s_Candidates;
    std::vector<ScanRow> s_Rows(kMaxWorldItems);

    const auto s_Add = [&](std::string p_How, uint64_t p_Address) {
        if (p_Address < 0x10000)
            return;
        for (const fr::InventoryCandidate& s_Known : s_Candidates)
            if (s_Known.address == p_Address)
                return;

        fr::InventoryCandidate s_Candidate;
        s_Candidate.how = std::move(p_How);
        s_Candidate.address = p_Address;
        s_Candidate.rtti = fr::RttiTypeName(s_Read, s_Image, p_Address);
        const std::optional<uint64_t> s_Vtable = ReadQword(p_Address);
        s_Candidate.vtableMatches =
            s_Vtable && std::find(m_WorldVtables.begin(), m_WorldVtables.end(), *s_Vtable) != m_WorldVtables.end();

        // Its items: only read where a vtable in the exe (or the SDK) says
        // it is an object (pure reads, guarded).
        const bool s_Object = s_Vtable && s_Image.Contains(*s_Vtable);
        if (s_ItemType && (s_Object || s_Candidates.empty()) &&
            (!s_Candidate.rtti || *s_Candidate.rtti == fr::kWorldInventoryRtti)) {
            ScanArgs s_Args {
                s_ItemType, s_Rows.data(), s_Rows.size(), 0, 0, false, reinterpret_cast<ZWorldInventory*>(p_Address)};
            if (!SafeScan(&s_Args)) {
                s_Candidate.entries = static_cast<int64_t>(s_Args.entries);
                s_Candidate.items = static_cast<int64_t>(s_Args.count);
            }
        }

        s_Candidates.push_back(std::move(s_Candidate));
    };

    const uint64_t s_Global = reinterpret_cast<uint64_t>(Globals::WorldInventory);
    s_Add("Globals::WorldInventory", s_Global);
    if (const std::optional<uint64_t> s_Inner = s_Global ? ReadQword(s_Global) : std::nullopt)
        s_Add("*Globals::WorldInventory (the global as a pointer)", *s_Inner);

    // A static ZWorldInventory: its vtable stored in the exe's data.
    size_t s_Statics = 0;
    if (!m_WorldVtables.empty()) {
        ForEachDataQword(s_Image, [&](uint64_t p_At, uint64_t p_Value) {
            if (s_Statics < 8 &&
                std::find(m_WorldVtables.begin(), m_WorldVtables.end(), p_Value) != m_WorldVtables.end()) {
                ++s_Statics;
                s_Add("a static ZWorldInventory at " + fr::DescribeAddress(s_Image, p_At), p_At);
            }
        });
    }

    fr::InventoryChoice s_Choice = fr::ChooseWorldInventory(s_Candidates, !m_WorldVtables.empty());

    // A heap ZWorldInventory reached from a global pointer.
    if (!s_Choice.index && !m_WorldVtables.empty()) {
        const auto s_Start = std::chrono::steady_clock::now();
        size_t s_Considered = 0;
        const std::vector<Holder> s_Holders = HeapHolders(s_Image, m_WorldVtables, 8, s_Considered);
        for (const Holder& s_Holder : s_Holders)
            s_Add("the heap object pointed to from " + fr::DescribeAddress(s_Image, s_Holder.holder), s_Holder.object);
        LogTo(fr::LogTopic::Loadout,
              "loadout world inventory: heap search: {} pointers in the exe's data checked, {} reach a ZWorldInventory "
              "({:.0f} ms)",
              s_Considered, s_Holders.size(),
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count());
        s_Choice = fr::ChooseWorldInventory(s_Candidates, true);
    }

    for (const fr::InventoryCandidate& s_Candidate : s_Candidates)
        LogTo(fr::LogTopic::Loadout, "loadout world inventory candidate: {} ({})", fr::DescribeCandidate(s_Candidate),
              fr::DescribeAddress(s_Image, s_Candidate.address));

    m_WorldWhy = s_Choice.why;
    if (s_Choice.index) {
        m_World = reinterpret_cast<ZWorldInventory*>(s_Candidates[*s_Choice.index].address);
        LogTo(fr::LogTopic::Loadout, "loadout world inventory: using {} ({}){}", fmt::ptr(m_World), s_Choice.why,
              reinterpret_cast<uint64_t>(m_World) == s_Global ? " (the SDK's pointer)"
                                                              : " (NOT the SDK's Globals::WorldInventory)");
    }
    else {
        LogTo(fr::LogTopic::Loadout,
              "loadout world inventory: NONE usable: {}; no world inventory scan, RequestNewItem or DestroyItem",
              s_Choice.why);
    }
}

// The payload type SetItem takes: the packed items' (learned from their
// Pickup), else TEntityRef<IItem>. False: neither.
bool RandomLoadout::EnsureItemRefType() {
    if (m_ItemRefType)
        return true;

    if (Globals::TypeRegistry && *Globals::TypeRegistry)
        m_ItemRefType = (*Globals::TypeRegistry)->GetTypeID(ZString("TEntityRef<IItem>"));

    if (!m_ItemRefType)
        return false;

    m_ItemRefTypeName = "TEntityRef<IItem> (assumed: no packed item's Pickup was seen in this scene)";
    return true;
}

// --- the pocket items ----------------------------------------------------------------------

// Requests the item from the world inventory; once it comes, PocketTick hands
// it to "HeroItemAction".SetItem (the game's pocket call). False: not
// requested; the caller gives it with CreateItem.
bool RandomLoadout::StartPocket(Planned& p_Planned, size_t p_Index) {
    const auto s_NotStarted = [&](const std::string& p_Why) {
        LogTo(fr::LogTopic::Loadout,
              "loadout holster: {} \"{}\" (large, holsterable) from the {}: {}; given with CreateItem instead (it "
              "may arrive in 47's hands)",
              fr::FormatGuid(p_Planned.id), p_Planned.name, fr::LoadoutSourceName(p_Planned.source), p_Why);
        return false;
    };

    if (!m_FlowEntities[kFlowHeroActions])
        return s_NotStarted("the game's pocket call (evergreen_setup \"HeroItemAction\") was not found");

    if (!m_Hero)
        return s_NotStarted("47 is not known");

    if (!EnsureItemRefType())
        return s_NotStarted("no payload type for SetItem (no packed item's Pickup was seen and TEntityRef<IItem> is "
                            "not registered)");

    ResolveWorldInventory();

    if (!m_World)
        return s_NotStarted("no usable ZWorldInventory (" + m_WorldWhy + "); RequestNewItem not called");

    if (!m_RequestNewItemOk)
        return s_NotStarted("ZWorldInventory::RequestNewItem not called: " + m_RequestNewItemWhy);

    // Before the call: its callback may come inside it.
    m_Pockets.push_back(PocketJob {});
    m_Pockets.back().plan = p_Index;
    const size_t s_Job = m_Pockets.size() - 1;

    RequestArgs s_Args {this, m_Hero, p_Planned.id, m_World, 0, false, false, 0, 0};
    const DWORD s_Code = SafeRequestNewItem(&s_Args);

    if (s_Code || s_Args.noInventory || s_Args.invalid) {
        m_Pockets[s_Job].abandoned = true; // an item that comes anyway is destroyed
        m_Pockets[s_Job].step = PocketJob::Step::Done;
        m_Pockets[s_Job].verdict = fr::PocketVerdict::Failed;
        return s_NotStarted(
            s_Code ? fmt::format("RequestNewItem faulted {} ({})", RequestStageName(s_Args.stage), FaultText(s_Code))
            : s_Args.noInventory ? std::string("no world inventory or entity manager")
                                 : fmt::format("ZWorldInventory::RequestNewItem gave the invalid "
                                               "ticket {}",
                                               s_Args.ticket));
    }

    m_Pockets[s_Job].ticket = s_Args.ticket;
    p_Planned.requested = true;
    p_Planned.requestNote =
        fmt::format("into the pockets through the game's pocket call (RequestNewItem ticket {})", s_Args.ticket);
    LogTo(fr::LogTopic::Loadout,
          "loadout holster: {} \"{}\" (large, holsterable) from the {}: requested from the world inventory (ticket {}, "
          "item {}); then \"HeroItemAction\".SetItem with it as {}: the game's PickupIntoPocket should put it in 47's "
          "pockets, his hands empty; never CreateItem (that gave the Sawed Off Bartoli 12G into his hands)",
          fr::FormatGuid(p_Planned.id), p_Planned.name, fr::LoadoutSourceName(p_Planned.source), s_Args.ticket,
          m_Pockets[s_Job].step == PocketJob::Step::WaitItem ? "awaited" : "already given",
          m_ItemRefTypeName.empty() ? std::string("?") : m_ItemRefTypeName);
    return true;
}

// The world inventory's callback for a pocket item.
void RandomLoadout::PocketItemCreated(PocketJob& p_Job, uint32_t p_Ticket, ZEntityType** p_Entity, bool p_HasInterface,
                                      const std::string& p_What) {
    if (p_Job.step != PocketJob::Step::WaitItem || p_Job.abandoned) {
        // Too late: the item went another way (or was never wanted).
        ItemOut s_Info {};
        const DWORD s_Code = p_Entity ? SafeItem(p_Entity, &s_Info) : 0;
        if (p_Entity && !s_Code && s_Info.item && !s_Info.owner && m_World) {
            const DWORD s_Destroy = SafeDestroyItem(m_World, p_Entity, s_Info.item);
            LogTo(
                fr::LogTopic::Loadout,
                "loadout holster: the world inventory gave ticket {} late ({}); the delivery had ended: destroyed ({})",
                p_Ticket, p_What, s_Destroy ? FaultText(s_Destroy) : "ok");
        }
        else {
            LogTo(
                fr::LogTopic::Loadout,
                "loadout holster: the world inventory gave ticket {} late ({}); the delivery had ended; NOT destroyed "
                "(owned or unreadable)",
                p_Ticket, p_What);
        }
        return;
    }

    if (!p_Entity || !p_HasInterface) {
        p_Job.fallbackWhy = fmt::format("the world inventory gave ticket {} without an item ({})", p_Ticket, p_What);
        p_Job.verdict = fr::PocketVerdict::Failed;
        p_Job.step = PocketJob::Step::Fallback;
        return;
    }

    p_Job.item = p_Entity;
    p_Job.step = PocketJob::Step::Ready; // SetItem at a frame update, outside this callback
    LogTo(fr::LogTopic::Loadout, "loadout holster: the world inventory gave ticket {} after {:.1f} s: {}", p_Ticket,
          p_Job.waited, p_What);
}

void RandomLoadout::PocketTick(double p_Seconds) {
    if (m_HandsWatch > 0)
        m_HandsWatch -= p_Seconds;

    for (PocketJob& s_Pocket : m_Pockets) {
        if (s_Pocket.plan >= m_Plan.size())
            continue;

        if (s_Pocket.step == PocketJob::Step::WaitItem) {
            s_Pocket.waited += p_Seconds;
            if (s_Pocket.waited >= fr::kBriefcaseTimeoutSeconds) {
                s_Pocket.abandoned = true; // a late item is destroyed
                s_Pocket.verdict = fr::PocketVerdict::Failed;
                s_Pocket.fallbackWhy =
                    fmt::format("the world inventory gave no item in {:.0f} s", fr::kBriefcaseTimeoutSeconds);
                s_Pocket.step = PocketJob::Step::Fallback;
            }
        }
        else if (s_Pocket.step == PocketJob::Step::Ready) {
            s_Pocket.waited += p_Seconds;
        }

        if (s_Pocket.step == PocketJob::Step::Fallback)
            PocketFallback(s_Pocket);
    }

    // One item at a time on "HeroItemAction": the active one, else the next ready one.
    if (m_PocketActive && *m_PocketActive < m_Pockets.size() &&
        m_Pockets[*m_PocketActive].step == PocketJob::Step::Watch) {
        PocketWatch(m_Pockets[*m_PocketActive], *m_PocketActive, p_Seconds);
        return;
    }

    for (size_t i = 0; i < m_Pockets.size(); ++i) {
        if (m_Pockets[i].step == PocketJob::Step::Ready) {
            PocketWatch(m_Pockets[i], i, 0);
            return;
        }
    }
}

// Ready: hands the item to "HeroItemAction".SetItem. Watch: judges it.
void RandomLoadout::PocketWatch(PocketJob& p_Job, size_t p_Index, double p_Seconds) {
    Planned& s_Planned = m_Plan[p_Job.plan];
    fr::PocketProgress& s_Progress = p_Job.progress;

    if (p_Job.step == PocketJob::Step::Ready) {
        // The pins and events that come inside the signal are this job's.
        m_PocketActive = p_Index;
        p_Job.step = PocketJob::Step::Watch;
        s_Progress.setItemSent = true;
        m_Ledger.Expect(s_Planned.id); // before: its ItemPickedUp may come at once

        ItemSignal s_Signal {m_FlowEntities[kFlowHeroActions], fr::kPinSetItem, m_ItemRefType, p_Job.item, false, {}};
        const DWORD s_Code = SafeSignalItem(&s_Signal);
        LogTo(fr::LogTopic::Loadout,
              "loadout holster: {} \"{}\": \"HeroItemAction\".SetItem({} as {}) {} (-> OnItemChanged -> "
              "PickupIntoPocket)",
              fr::FormatGuid(s_Planned.id), s_Planned.name, fmt::ptr(p_Job.item), m_ItemRefTypeName,
              s_Code          ? fmt::format("faulted ({})", FaultText(s_Code))
              : s_Signal.sent ? std::string("sent")
                              : fmt::format("NOT sent: {}", s_Signal.why));

        if (s_Code || !s_Signal.sent) {
            s_Progress.setItemSent = false;
            m_Ledger.Retract(s_Planned.id);
            p_Job.verdict = fr::PocketVerdict::Failed;
            p_Job.fallbackWhy = "SetItem was not sent";
            p_Job.step = PocketJob::Step::Fallback;
            m_PocketActive.reset();
            PocketFallback(p_Job);
            return;
        }
    }

    s_Progress.seconds += p_Seconds;
    if (s_Progress.retrySent)
        s_Progress.secondsSinceRetry += p_Seconds;

    // The item's owner.
    ItemOut s_Info {};
    if (!SafeItem(p_Job.item, &s_Info) && s_Info.item) {
        s_Progress.ownerIsHero = s_Info.owner && s_Info.owner == m_Hero;

        if (!p_Job.ownerRead || s_Info.owner != p_Job.lastOwner) {
            std::string s_Class;
            uint64_t s_OwnerId = 0;
            if (s_Info.owner && s_Info.owner != m_Hero)
                ReadEntityClassName(s_Info.owner, s_Class, &s_OwnerId);
            LogTo(fr::LogTopic::Loadout, "loadout holster: at {:.1f} s the item's owner (ZHM5Item::m_pOwner) is {}",
                  s_Progress.seconds,
                  !s_Info.owner ? std::string("no one")
                  : s_Info.owner == m_Hero
                      ? std::string("47")
                      : fmt::format("another entity {} ({})", fmt::ptr(s_Info.owner), s_Class.empty() ? "?" : s_Class));
            p_Job.ownerRead = true;
            p_Job.lastOwner = s_Info.owner;
        }
    }

    const bool s_Held =
        (s_Progress.ownerIsHero && *s_Progress.ownerIsHero) || s_Progress.itemPickedUp || s_Progress.everInHands;
    if (s_Held)
        s_Progress.secondsSinceHeld += p_Seconds;

    p_Job.verdict = fr::JudgePocket(s_Progress);
    const std::string s_Text = fr::PocketProgressText(s_Progress);

    // Logged on each change of a step (not of the clock).
    const std::string s_Steps = s_Text.substr(0, s_Text.rfind(';'));
    if (s_Steps != p_Job.lastProgress) {
        p_Job.lastProgress = s_Steps;
        LogTo(fr::LogTopic::Loadout, "loadout holster: progress: {}", s_Text);
    }

    const auto s_Confirm = [&](const std::string& p_By) {
        if (s_Planned.confirmed)
            return;
        s_Planned.confirmed = true;
        s_Planned.confirmedBy = p_By;
        s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
    };

    switch (p_Job.verdict) {
        case fr::PocketVerdict::Pending: return;

        case fr::PocketVerdict::Retry: {
            // In his hands: the pocket call once more, on the item "HeroItemAction" holds.
            const VoidSignal s_Pocket {m_FlowEntities[kFlowHeroActions], fr::kPinPickupIntoPocket};
            const DWORD s_Code = SafeSignalVoid(&s_Pocket);
            s_Progress.retrySent = true;
            s_Progress.secondsSinceRetry = 0;
            LogTo(fr::LogTopic::Loadout,
                  "loadout holster: {} \"{}\" is in 47's hands {:.1f} s after its SetItem; \"HeroItemAction\""
                  ".PickupIntoPocket {} once more",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, s_Progress.seconds,
                  s_Code ? fmt::format("faulted ({})", FaultText(s_Code)) : std::string("sent"));
            return;
        }

        case fr::PocketVerdict::Pocketed:
            s_Confirm("the pocket delivery (in 47's pockets)");
            LogTo(
                fr::LogTopic::Loadout,
                "loadout holster: {} \"{}\": POCKETED: 47 has it and no HoldingIllegalWeapon names it in his hands for "
                "{:.1f} s ({}); {}",
                fr::FormatGuid(s_Planned.id), s_Planned.name, fr::kPocketSettleSeconds, s_Text,
                AfterConfirm(s_Planned.source));
            break;

        case fr::PocketVerdict::InHands:
            s_Confirm("the pocket delivery (WRONG: in 47's hands)");
            LogTo(fr::LogTopic::Loadout,
                  "loadout holster: {} \"{}\": WRONG: STILL IN 47'S HANDS after the pocket call and one retry ({}); he "
                  "has it; {}",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, s_Text, AfterConfirm(s_Planned.source));
            break;

        case fr::PocketVerdict::Refused:
        case fr::PocketVerdict::Failed:
            p_Job.fallbackWhy = fmt::format("{} ({})", fr::PocketVerdictName(p_Job.verdict), s_Text);
            p_Job.step = PocketJob::Step::Fallback;
            m_PocketActive.reset();
            PocketFallback(p_Job);
            return;
    }

    p_Job.step = PocketJob::Step::Done;
    m_PocketActive.reset();
}

// The pocket call did not give 47 the item: an unowned item is destroyed and
// the item is given with CreateItem (it may arrive in his hands);
// one 47 or another entity holds, or that cannot be read, is left as it is
// and not given again (never a second copy).
void RandomLoadout::PocketFallback(PocketJob& p_Job) {
    p_Job.step = PocketJob::Step::Done;
    Planned& s_Planned = m_Plan[p_Job.plan];
    const std::string s_Head =
        fmt::format("loadout holster: {} \"{}\": {}", fr::FormatGuid(s_Planned.id), s_Planned.name, p_Job.fallbackWhy);

    if (p_Job.item) {
        ItemOut s_Info {};
        if (const DWORD s_Code = SafeItem(p_Job.item, &s_Info); s_Code || !s_Info.item) {
            LogTo(fr::LogTopic::Loadout,
                  "{}; the item {} is unreadable ({}); left as it is, NOT given again (it stays on the {})", s_Head,
                  fmt::ptr(p_Job.item), s_Code ? FaultText(s_Code) : "not a ZHM5Item",
                  fr::LoadoutSourceName(s_Planned.source));
            return;
        }

        if (s_Info.owner == m_Hero && m_Hero) {
            if (!s_Planned.confirmed) {
                s_Planned.confirmed = true;
                s_Planned.confirmedBy = "the pocket delivery (owner 47 at the fallback)";
                s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
            }
            LogTo(fr::LogTopic::Loadout,
                  "{}; but its owner is 47: he has it (pockets or hands unknown); not given again; {}", s_Head,
                  AfterConfirm(s_Planned.source));
            return;
        }

        if (s_Info.owner) {
            LogTo(fr::LogTopic::Loadout,
                  "{}; the item is owned by another entity {}; left as it is, NOT given again (it stays on the {})",
                  s_Head, fmt::ptr(s_Info.owner), fr::LoadoutSourceName(s_Planned.source));
            return;
        }

        const DWORD s_Destroy = m_World ? SafeDestroyItem(m_World, p_Job.item, s_Info.item) : 0;
        if (!m_World || s_Destroy) {
            LogTo(fr::LogTopic::Loadout,
                  "{}; the unowned item {} could not be destroyed ({}); NOT given again (it stays on the {})", s_Head,
                  fmt::ptr(p_Job.item), !m_World ? "no usable world inventory" : FaultText(s_Destroy),
                  fr::LoadoutSourceName(s_Planned.source));
            return;
        }

        if (p_Job.progress.setItemSent)
            m_Ledger.Retract(s_Planned.id);
        LogTo(fr::LogTopic::Loadout, "{}; the unowned item {} was destroyed (ZWorldInventory::DestroyItem)", s_Head,
              fmt::ptr(p_Job.item));
    }
    else {
        LogTo(fr::LogTopic::Loadout, "{}; no item to undo", s_Head);
    }

    if (!m_HeroInventory) {
        s_Planned.requested = false;
        LogTo(fr::LogTopic::Loadout,
              "loadout holster: {} \"{}\": 47's inventory is not known; NOT created (it stays on the {})",
              fr::FormatGuid(s_Planned.id), s_Planned.name, fr::LoadoutSourceName(s_Planned.source));
        return;
    }

    LogTo(fr::LogTopic::Loadout,
          "loadout holster: {} \"{}\": given with CreateItem instead (it may arrive in 47's hands)",
          fr::FormatGuid(s_Planned.id), s_Planned.name);
    CreatePlanned(s_Planned, fr::ChooseLoadoutItemType(m_PackedCreateType));
}

// HoldingIllegalWeapon: the weapon 47 holds in his hands. A pocket item named
// there is in the hands (PocketWatch retries once); any delivered item named
// in the first kHandsWatchSeconds after the delivery is logged.
void RandomLoadout::OnHoldingIllegalWeapon(uint32_t p_EventIndex, const std::string& p_Json) {
    const auto s_Held = fr::ParseHoldingIllegalWeapon(p_Json);

    if (!s_Held)
        return;

    for (PocketJob& s_Pocket : m_Pockets) {
        if (s_Pocket.step != PocketJob::Step::Watch || s_Pocket.plan >= m_Plan.size())
            continue;

        fr::PocketProgress& s_Progress = s_Pocket.progress;
        const bool s_Named = s_Held->holding && s_Held->id && *s_Held->id == m_Plan[s_Pocket.plan].id;

        if (s_Named && !s_Progress.inHands) {
            s_Progress.inHands = s_Progress.everInHands = true;
            LogTo(fr::LogTopic::Loadout,
                  "loadout holster: HoldingIllegalWeapon #{} at {:.1f} s after its SetItem: 47 holds it in his hands",
                  p_EventIndex, s_Progress.seconds);
        }
        else if (!s_Named && s_Progress.inHands) {
            s_Progress.inHands = false;
            s_Progress.secondsSinceHeld = 0;
            LogTo(fr::LogTopic::Loadout,
                  "loadout holster: HoldingIllegalWeapon #{} at {:.1f} s after its SetItem: his hands are clear of it "
                  "({})",
                  p_EventIndex, s_Progress.seconds,
                  s_Held->holding ? fmt::format("he holds {} \"{}\"", s_Held->id ? fr::FormatGuid(*s_Held->id) : "?",
                                                s_Held->id ? NameOf(*s_Held->id) : "?")
                                  : std::string("he holds no illegal weapon"));
        }
    }

    const Planned* s_Delivered = nullptr;
    if (s_Held->holding && s_Held->id) {
        for (const Planned& s_Planned : m_Plan)
            if (s_Planned.id == *s_Held->id && s_Planned.requested && !s_Planned.briefcase)
                s_Delivered = &s_Planned;
    }

    if (s_Delivered && m_HandsWatch > 0 && m_HandsItem != s_Delivered->id) {
        m_HandsItem = s_Delivered->id;
        LogTo(fr::LogTopic::Loadout,
              "loadout holster: HoldingIllegalWeapon #{}: 47 holds {} \"{}\" in his hands {:.1f} s after the delivery: "
              "WRONG, a holsterable item must start in the pockets ({})",
              p_EventIndex, fr::FormatGuid(s_Delivered->id), s_Delivered->name, kHandsWatchSeconds - m_HandsWatch,
              s_Delivered->pocket ? "the pocket path sends PickupIntoPocket once more"
                                  : "it came by CreateItem; nothing more is done");
    }
    else if (m_HandsItem && !s_Delivered) {
        LogTo(fr::LogTopic::Loadout,
              "loadout holster: HoldingIllegalWeapon #{}: 47's hands are clear of {} \"{}\" ({})", p_EventIndex,
              fr::FormatGuid(*m_HandsItem), NameOf(*m_HandsItem),
              s_Held->holding ? fmt::format("he holds {}", s_Held->id ? NameOf(*s_Held->id) : std::string("?"))
                              : std::string("he holds no illegal weapon"));
        m_HandsItem.reset();
    }
}

// --- the briefcase item ----------------------------------------------------------------------

// Requests the item from the world inventory; its callback hands it to the
// game's purchase path. False: not requested (it stays owned).
bool RandomLoadout::StartBriefcase(Planned& p_Planned, size_t p_Index) {
    const auto s_NotCreated = [&](std::string p_Why) {
        p_Planned.skipNote = std::move(p_Why);
        LogTo(fr::LogTopic::Loadout,
              "loadout briefcase: {} \"{}\" from the {}: {}; NOT created, nothing loose (it stays on the {})",
              fr::FormatGuid(p_Planned.id), p_Planned.name, fr::LoadoutSourceName(p_Planned.source), p_Planned.skipNote,
              fr::LoadoutSourceName(p_Planned.source));
        return false;
    };

    if (!m_FlowEntities[kFlowPurchased] || !m_FlowEntities[kFlowBriefcase])
        return s_NotCreated("the game's briefcase path (evergreen_setup \"Purchased item actions\" / \"Briefcase\") "
                            "was not found");

    if (!m_Hero)
        return s_NotCreated("47 is not known");

    if (!EnsureItemRefType())
        return s_NotCreated("no payload type for SetItem (no packed item's Pickup was seen and "
                            "TEntityRef<IItem> is not registered)");

    ResolveWorldInventory();

    if (!m_World)
        return s_NotCreated("no usable ZWorldInventory (" + m_WorldWhy + "); RequestNewItem not called");

    if (!m_RequestNewItemOk)
        return s_NotCreated("ZWorldInventory::RequestNewItem not called: " + m_RequestNewItemWhy);

    m_Job = BriefcaseJob {};
    m_Job.plan = p_Index;
    m_Job.step = BriefcaseJob::Step::WaitItem;

    RequestArgs s_Args {this, m_Hero, p_Planned.id, m_World, 0, false, false, 0, 0};
    LogTo(fr::LogTopic::Loadout,
          "loadout briefcase: {} \"{}\": RequestNewItem(this {}, id, delegate, entity id from 47 {}, not loading, no "
          "parent, creator 47)",
          fr::FormatGuid(p_Planned.id), p_Planned.name, fmt::ptr(m_World), fmt::ptr(m_Hero));
    const DWORD s_Code = SafeRequestNewItem(&s_Args);

    if (s_Code || s_Args.noInventory || s_Args.invalid) {
        m_Job.abandoned = true; // an item that comes anyway is destroyed
        m_Job.step = BriefcaseJob::Step::Done;
        m_Job.verdict = fr::BriefcaseVerdict::Failed;
        return s_NotCreated(s_Code ? fmt::format("faulted {} ({}; dynamic entity id 0x{:x}; world inventory {})",
                                                 RequestStageName(s_Args.stage), FaultText(s_Code), s_Args.entityId,
                                                 fmt::ptr(m_World))
                            : s_Args.noInventory ? std::string("no world inventory or entity manager")
                                                 : fmt::format("ZWorldInventory::RequestNewItem gave the invalid "
                                                               "ticket {}",
                                                               s_Args.ticket));
    }

    m_Job.ticket = s_Args.ticket;
    p_Planned.requested = true;
    p_Planned.requestNote = fmt::format("inside an ICA Briefcase through the game's purchase path (RequestNewItem "
                                        "ticket {})",
                                        s_Args.ticket);
    LogTo(
        fr::LogTopic::Loadout,
        "loadout briefcase: {} \"{}\" (BRIEFCASE item) from the {}: requested from the world inventory (ticket {}, "
        "item {}); then \"Purchased item actions\".SetItem with it as {}: the game should fail to pocket it, spawn an "
        "ICA Briefcase into 47's hands and place it inside; never CreateItem (that gives it loose)",
        fr::FormatGuid(p_Planned.id), p_Planned.name, fr::LoadoutSourceName(p_Planned.source), s_Args.ticket,
        m_Job.progress.itemSpawned ? "already given" : "awaited",
        m_ItemRefTypeName.empty() ? std::string("?") : m_ItemRefTypeName);
    return true;
}

void RandomLoadout::OnBriefcaseItemCreated(uint32_t p_Ticket, TEntityRef<IItemBase> p_Item) {
    Contained(kTag, "briefcase item created", [&] {
        std::lock_guard s_Lock(m_Mutex);
        ZEntityType** const s_Entity = p_Item.m_entityRef.m_pObj;
        ItemOut s_Info {};
        const DWORD s_Code = SafeItem(s_Entity, &s_Info);
        fr::Guid s_Id {};
        if (!s_Code && s_Info.hasId)
            std::memcpy(&s_Id, s_Info.id, sizeof(s_Id));
        const std::string s_What =
            fmt::format("entity {} ({}), owner {}", fmt::ptr(s_Entity),
                        s_Code         ? FaultText(s_Code)
                        : s_Info.hasId ? fmt::format("{} \"{}\"", fr::FormatGuid(s_Id), NameOf(s_Id))
                                       : std::string("no item config"),
                        s_Info.owner ? fmt::format("{}", fmt::ptr(s_Info.owner)) : "none");

        // A pocket item's (by its ticket, or by its id when the callback came
        // inside RequestNewItem, before the ticket was known).
        for (PocketJob& s_Pocket : m_Pockets) {
            const bool s_ByTicket = s_Pocket.ticket != 0 && s_Pocket.ticket == p_Ticket;
            const bool s_ById = s_Pocket.step == PocketJob::Step::WaitItem && !s_Code && s_Info.hasId &&
                                s_Pocket.plan < m_Plan.size() && s_Id == m_Plan[s_Pocket.plan].id;
            if (s_ByTicket || s_ById) {
                PocketItemCreated(s_Pocket, p_Ticket, s_Entity, p_Item.m_pInterfaceRef != nullptr, s_What);
                return;
            }
        }

        if (m_Job.step != BriefcaseJob::Step::WaitItem || m_Job.abandoned) {
            // Ours (only this mod's request uses this delegate), but too late.
            if (s_Entity && !s_Code && s_Info.item && !s_Info.owner && m_World) {
                const DWORD s_Destroy = SafeDestroyItem(m_World, s_Entity, s_Info.item);
                LogTo(fr::LogTopic::Loadout,
                      "loadout briefcase: the world inventory gave ticket {} late ({}); the delivery had ended: "
                      "destroyed ({})",
                      p_Ticket, s_What, s_Destroy ? FaultText(s_Destroy) : "ok");
            }
            else {
                LogTo(fr::LogTopic::Loadout,
                      "loadout briefcase: the world inventory gave ticket {} late ({}); the delivery had ended; NOT "
                      "destroyed (owned or unreadable)",
                      p_Ticket, s_What);
            }
            return;
        }

        if (!s_Entity || !p_Item.m_pInterfaceRef) {
            LogTo(fr::LogTopic::Loadout,
                  "loadout briefcase: the world inventory gave ticket {} without an item ({}); the delivery failed",
                  p_Ticket, s_What);
            m_Job.verdict = fr::BriefcaseVerdict::Failed;
            m_Job.step = BriefcaseJob::Step::Undo;
            return;
        }

        m_Job.item = s_Entity;
        m_Job.progress.itemSpawned = true;
        m_Job.step = BriefcaseJob::Step::Watch; // SetItem at the next frame, outside this callback
        LogTo(fr::LogTopic::Loadout, "loadout briefcase: the world inventory gave ticket {} at {:.1f} s: {}{}",
              p_Ticket, m_Job.progress.seconds, s_What,
              m_Job.plan < m_Plan.size() && s_Info.hasId && s_Id != m_Plan[m_Job.plan].id ? " (NOT the planned id)"
                                                                                          : "");
    });
}

void RandomLoadout::BriefcaseTick(double p_Seconds) {
    if (m_Job.step == BriefcaseJob::Step::Idle || m_Job.step == BriefcaseJob::Step::Done || m_Job.plan >= m_Plan.size())
        return;

    fr::BriefcaseProgress& s_Progress = m_Job.progress;
    s_Progress.seconds += p_Seconds;
    if (s_Progress.placeSent)
        s_Progress.secondsSincePlace += p_Seconds;

    Planned& s_Planned = m_Plan[m_Job.plan];

    if (m_Job.step == BriefcaseJob::Step::WaitItem) {
        if (s_Progress.seconds >= fr::kBriefcaseTimeoutSeconds) {
            m_Job.abandoned = true;
            m_Job.verdict = fr::BriefcaseVerdict::Failed;
            LogTo(fr::LogTopic::Loadout,
                  "loadout briefcase: {} \"{}\": the world inventory gave no item in {:.0f} s; nothing to undo (a late "
                  "item is destroyed); NOT removed from the {}",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, fr::kBriefcaseTimeoutSeconds,
                  fr::LoadoutSourceName(s_Planned.source));
            BriefcaseFinish("no item from the world inventory");
        }
        return;
    }

    if (m_Job.step == BriefcaseJob::Step::Undo) {
        BriefcaseUndo(p_Seconds);
        return;
    }

    // Watch: hand the item to the game's purchase path once.
    if (!s_Progress.setItemSent) {
        ItemSignal s_Signal {m_FlowEntities[kFlowPurchased], fr::kPinSetItem, m_ItemRefType, m_Job.item, false, {}};
        const DWORD s_Code = SafeSignalItem(&s_Signal);
        s_Progress.setItemSent = !s_Code && s_Signal.sent;
        LogTo(fr::LogTopic::Loadout, "loadout briefcase: \"Purchased item actions\".SetItem({} as {}) {}",
              fmt::ptr(m_Job.item), m_ItemRefTypeName,
              s_Code          ? fmt::format("faulted ({})", FaultText(s_Code))
              : s_Signal.sent ? std::string("sent")
                              : fmt::format("NOT sent: {}", s_Signal.why));

        if (!s_Progress.setItemSent) {
            m_Job.verdict = fr::BriefcaseVerdict::Failed;
            m_Job.step = BriefcaseJob::Step::Undo;
            return;
        }
    }

    // The item's owner (47 before the place: loose).
    ItemOut s_Info {};
    if (!SafeItem(m_Job.item, &s_Info) && s_Info.item) {
        s_Progress.ownerIsHero = s_Info.owner && s_Info.owner == m_Hero;
        s_Progress.ownerIsBriefcase =
            m_Job.briefcase ? std::optional<bool>(s_Info.owner == m_Job.briefcase) : std::nullopt;

        if (!m_Job.ownerRead || s_Info.owner != m_Job.lastOwner) {
            std::string s_Class;
            uint64_t s_OwnerId = 0;
            if (s_Info.owner && s_Info.owner != m_Hero && s_Info.owner != m_Job.briefcase)
                ReadEntityClassName(s_Info.owner, s_Class, &s_OwnerId);
            m_Job.ownerIsStorage = s_Class == "ZItemStorageEntity";
            LogTo(fr::LogTopic::Loadout, "loadout briefcase: at {:.1f} s the item's owner (ZHM5Item::m_pOwner) is {}",
                  s_Progress.seconds,
                  !s_Info.owner            ? std::string("no one")
                  : s_Info.owner == m_Hero ? std::string("47")
                  : s_Info.owner == m_Job.briefcase
                      ? std::string("its ICA Briefcase")
                      : fmt::format("another entity {} ({})", fmt::ptr(s_Info.owner), s_Class.empty() ? "?" : s_Class));
            m_Job.ownerRead = true;
            m_Job.lastOwner = s_Info.owner;
        }
        s_Progress.ownerIsStorage = m_Job.ownerIsStorage;
    }

    m_Job.verdict = fr::JudgeBriefcase(s_Progress);
    const std::string s_Text = fr::BriefcaseProgressText(s_Progress);

    // Logged on each change of a step (not of the clock).
    const std::string s_Steps = s_Text.substr(0, s_Text.rfind(';'));
    if (s_Steps != m_Job.lastProgress) {
        m_Job.lastProgress = s_Steps;
        LogTo(fr::LogTopic::Loadout, "loadout briefcase: progress: {}", s_Text);
    }

    switch (m_Job.verdict) {
        case fr::BriefcaseVerdict::InBriefcase:
            s_Planned.confirmed = true;
            s_Planned.confirmedBy =
                fmt::format("the briefcase delivery (placed into ICA Briefcase {})", fmt::ptr(m_Job.briefcase));
            s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
            LogTo(fr::LogTopic::Loadout,
                  "loadout in inventory: {} \"{}\" (BRIEFCASE item): CONFIRMED IN THE BRIEFCASE ({}); {}",
                  fr::FormatGuid(s_Planned.id), s_Planned.name, s_Text, AfterConfirm(s_Planned.source));
            BriefcaseFinish("in the briefcase");
            break;

        case fr::BriefcaseVerdict::Loose:
        case fr::BriefcaseVerdict::Failed:
            LogTo(
                fr::LogTopic::Loadout,
                "loadout briefcase: {} \"{}\": {} ({}); undoing it: the item is dropped if 47 holds it and destroyed, "
                "an empty briefcase likewise; it stays on the {}",
                fr::FormatGuid(s_Planned.id), s_Planned.name, fr::BriefcaseVerdictName(m_Job.verdict), s_Text,
                fr::LoadoutSourceName(s_Planned.source));
            m_Job.step = BriefcaseJob::Step::Undo;
            m_Job.undoTimer = 0;
            break;

        case fr::BriefcaseVerdict::Pending: break;
    }
}

// Never leaves the item loose: dropped (if 47 holds it) and destroyed; an
// empty briefcase the game spawned for it likewise. An item 47 still holds
// after the drop is not destroyed (that could crash the game): it is logged
// and treated as delivered, so the save stays consistent.
void RandomLoadout::BriefcaseUndo(double p_Seconds) {
    constexpr double kDropWait = 2.0;
    m_Job.undoTimer += p_Seconds;
    Planned& s_Planned = m_Plan[m_Job.plan];

    const auto s_Handle = [&](ZEntityType** p_Entity, size_t p_DropFlow, const char* p_What, bool& p_Handled,
                              bool& p_DropSent, double& p_DropAt) {
        if (p_Handled)
            return;

        if (!p_Entity) {
            p_Handled = true;
            return;
        }

        ItemOut s_Info {};
        if (const DWORD s_Code = SafeItem(p_Entity, &s_Info); s_Code || !s_Info.item) {
            LogTo(fr::LogTopic::Loadout, "loadout briefcase undo: {} {}: unreadable ({}); left as it is", p_What,
                  fmt::ptr(p_Entity), s_Code ? FaultText(s_Code) : "not a ZHM5Item");
            p_Handled = true;
            return;
        }

        if (s_Info.owner == m_Hero) {
            if (!p_DropSent) {
                const VoidSignal s_Drop {m_FlowEntities[p_DropFlow], fr::kPinDrop};
                const DWORD s_Code = s_Drop.target ? SafeSignalVoid(&s_Drop) : 0;
                p_DropSent = true;
                p_DropAt = m_Job.undoTimer;
                LogTo(fr::LogTopic::Loadout, "loadout briefcase undo: {} {}: 47 holds it; \"{}\".Drop {}", p_What,
                      fmt::ptr(p_Entity), kFlowEntities[p_DropFlow].name,
                      !s_Drop.target ? "NOT sent (entity not found)"
                      : s_Code       ? FaultText(s_Code)
                                     : "sent");
                return;
            }

            if (m_Job.undoTimer - p_DropAt < kDropWait)
                return;

            LogTo(
                fr::LogTopic::Loadout,
                "loadout briefcase undo: {} {}: 47 STILL holds it {:.0f} s after the drop; NOT destroyed (a held item "
                "could crash the game)",
                p_What, fmt::ptr(p_Entity), kDropWait);
            p_Handled = true;

            if (p_Entity == m_Job.item) {
                // Loose with 47: a tool is removed from the tools so the save
                // does not duplicate it; a wall item stays on the wall (the
                // game moves a carried-out wall item into the loadout itself).
                s_Planned.confirmed = true;
                s_Planned.confirmedBy = "UNDO FAILED: loose in 47's inventory";
                s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
                LogTo(fr::LogTopic::Loadout, "loadout briefcase undo: {} \"{}\": WRONG: LEFT LOOSE with 47; {}",
                      fr::FormatGuid(s_Planned.id), s_Planned.name, AfterConfirm(s_Planned.source));
            }
            return;
        }

        if (s_Info.owner && s_Info.owner != m_Job.briefcase) {
            LogTo(fr::LogTopic::Loadout, "loadout briefcase undo: {} {}: owned by another entity {}; left as it is",
                  p_What, fmt::ptr(p_Entity), fmt::ptr(s_Info.owner));
            p_Handled = true;
            if (p_Entity == m_Job.item && m_Job.briefcase && !m_Job.briefcaseHandled) {
                // Held by something (likely the briefcase's storage): destroying
                // the briefcase around it can crash the game.
                LogTo(fr::LogTopic::Loadout,
                      "loadout briefcase undo: the item is held, so the briefcase {} is left with 47 too",
                      fmt::ptr(m_Job.briefcase));
                m_Job.briefcaseHandled = true;
            }
            return;
        }

        if (s_Info.owner == m_Job.briefcase && s_Info.owner) {
            // In the briefcase after all: that is the goal; keep it, counted as delivered.
            LogTo(fr::LogTopic::Loadout,
                  "loadout briefcase undo: {} {}: its owner is the briefcase: it IS in the briefcase; kept, {}", p_What,
                  fmt::ptr(p_Entity), AfterConfirm(s_Planned.source));
            s_Planned.confirmed = true;
            s_Planned.confirmedBy = "the briefcase delivery (owner read during the undo)";
            s_Planned.removeQueued = fr::RemovesFromSource(s_Planned.source);
            p_Handled = true;
            return;
        }

        const DWORD s_Code = m_World ? SafeDestroyItem(m_World, p_Entity, s_Info.item) : 0;
        LogTo(fr::LogTopic::Loadout, "loadout briefcase undo: {} {}: owned by no one; ZWorldInventory::DestroyItem {}",
              p_What, fmt::ptr(p_Entity),
              !m_World ? "NOT called (no usable world inventory)"
              : s_Code ? FaultText(s_Code)
                       : "done");
        p_Handled = true;
    };

    s_Handle(m_Job.item, kFlowPurchased, "the item", m_Job.itemHandled, m_Job.itemDropSent, m_Job.itemDropAt);

    // The briefcase only when the item never went into it (else it holds the item).
    if ((m_Job.progress.placeSent || s_Planned.confirmed) && m_Job.briefcase && !m_Job.briefcaseHandled) {
        LogTo(fr::LogTopic::Loadout,
              "loadout briefcase undo: the briefcase {} got the item (place sent); it is left with 47",
              fmt::ptr(m_Job.briefcase));
        m_Job.briefcaseHandled = true;
    }

    s_Handle(m_Job.briefcase, kFlowBriefcaseActions, "the empty briefcase", m_Job.briefcaseHandled,
             m_Job.briefcaseDropSent, m_Job.briefcaseDropAt);

    if (m_Job.itemHandled && m_Job.briefcaseHandled)
        BriefcaseFinish("undone");
    else if (m_Job.undoTimer >= 10.0) {
        LogTo(fr::LogTopic::Loadout,
              "loadout briefcase undo: gave up after 10 s (item handled {}, briefcase handled {})", m_Job.itemHandled,
              m_Job.briefcaseHandled);
        BriefcaseFinish("undo timed out");
    }
}

void RandomLoadout::BriefcaseFinish(const char* p_Why) {
    m_Job.step = BriefcaseJob::Step::Done;
    const Planned& s_Planned = m_Plan[m_Job.plan];
    LogTo(fr::LogTopic::Loadout, "loadout briefcase: {} \"{}\": {} ({}): {}; briefcase {}, item {}; {}",
          fr::FormatGuid(s_Planned.id), s_Planned.name, fr::BriefcaseVerdictName(m_Job.verdict), p_Why,
          fr::BriefcaseProgressText(m_Job.progress), fmt::ptr(m_Job.briefcase), fmt::ptr(m_Job.item),
          s_Planned.removeQueued || s_Planned.removed
              ? fmt::format("removed from the {}", fr::LoadoutSourceName(s_Planned.source))
              : fmt::format("stays on the {}", fr::LoadoutSourceName(s_Planned.source)));
}

// An output pin of an entity on the briefcase path (or the packed items' path).
void RandomLoadout::OnFlowPin(size_t p_Flow, uint32_t p_PinId, const ZObjectRef& p_Data) {
    EntityPayloadOut s_Payload {};
    const DWORD s_Code = SafeEntityPayload(&p_Data, &s_Payload);
    const bool s_Watching = m_Job.step == BriefcaseJob::Step::Watch;
    PocketJob* const s_Pocket = m_PocketActive && *m_PocketActive < m_Pockets.size() &&
                                        m_Pockets[*m_PocketActive].step == PocketJob::Step::Watch
                                    ? &m_Pockets[*m_PocketActive]
                                    : nullptr;
    // The payload item's id, when it is none of the deliveries' own entities.
    std::optional<fr::Guid> s_PayloadId;

    if (!s_Code && s_Payload.entity && s_Payload.entity != m_Job.item && s_Payload.entity != m_Job.briefcase &&
        !(s_Pocket && s_Payload.entity == s_Pocket->item)) {
        ItemOut s_Info {};
        if (!SafeItem(s_Payload.entity, &s_Info) && s_Info.hasId) {
            fr::Guid s_Id;
            std::memcpy(&s_Id, s_Info.id, sizeof(s_Id));
            s_PayloadId = s_Id;
        }
    }

    // The type SetItem takes: what the packed items' helper hands "Loaded item actions".
    if (p_Flow == kFlowHelper && p_PinId == fr::kPinPickup && !s_Code && s_Payload.type) {
        const bool s_First = !m_ItemRefType || m_ItemRefTypeName.find("assumed") != std::string::npos;
        if (s_First) {
            m_ItemRefType = s_Payload.type;
            m_ItemRefTypeName =
                fmt::format("{} ({} bytes, as the game's Pickup -> SetItem)", s_Payload.typeName, s_Payload.size);
            LogTo(fr::LogTopic::Loadout,
                  "loadout flow: SetItem payload type learned from \"EvergreenGearPersistenceHelper\".Pickup: {}",
                  m_ItemRefTypeName);
        }
    }

    if (s_Watching) {
        fr::BriefcaseProgress& s_Progress = m_Job.progress;

        if (p_Flow == kFlowPurchased && p_PinId == fr::kPinOnFailed && s_Progress.setItemSent)
            s_Progress.pocketFailed = true;

        if (p_Flow == kFlowBriefcase && p_PinId == fr::kPinItemSpawned && s_Progress.setItemSent && s_Payload.entity &&
            !m_Job.briefcase) {
            m_Job.briefcase = s_Payload.entity;
            s_Progress.briefcaseSpawned = true;
        }

        if (p_Flow == kFlowContainerActions && p_PinId == fr::kPinOnContainerItemSet && s_Progress.setItemSent)
            s_Progress.containerSet = true;

        // The pin's entity ref may differ from the spawned one's pointer for
        // the same item: matched by its id too.
        if (p_Flow == kFlowPurchased && p_PinId == fr::kPinItem && s_Payload.entity && m_Job.item &&
            (s_Payload.entity == m_Job.item || (m_Job.plan < m_Plan.size() && s_PayloadId == m_Plan[m_Job.plan].id)) &&
            !s_Progress.placeSent) {
            s_Progress.placeSent = true;
            s_Progress.secondsSincePlace = 0;
        }
    }

    // The pocket item's: the game would not pocket it.
    const bool s_PocketPin = s_Pocket && p_Flow == kFlowHeroActions;
    if (s_PocketPin && p_PinId == fr::kPinOnFailed && s_Pocket->progress.setItemSent &&
        !s_Pocket->progress.pocketFailed) {
        s_Pocket->progress.pocketFailed = true;
        LogTo(
            fr::LogTopic::Loadout,
            "loadout holster: \"HeroItemAction\".OnFailed at {:.1f} s after the SetItem: the game would not pocket it",
            s_Pocket->progress.seconds);
    }
}

// --- the CreateItem detour ---------------------------------------------------------------

// Every CreateItem in a mission, logged and passed on unchanged; the type the
// game uses for 47's packed items is learned (m_PackedCreateType) for the
// delivery's own calls.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, ZCharacterSubcontrollerInventory::SCreateItem*,
                     ZCharacterSubcontrollerInventory_CreateItem, ZCharacterSubcontrollerInventory* th,
                     ZRepositoryID& repId, const ZString& sOnlineInstanceId,
                     const TArray<ZRepositoryID>& /*instanceModifiersToApply*/,
                     ZCharacterSubcontrollerInventory::ECreateItemType createItemType) {
    (void)p_Hook; // declared by DEFINE_PLUGIN_DETOUR
    m_Loadout.OnCreateItem(th, repId, sOnlineInstanceId, static_cast<int32_t>(createItemType));
    return HookResult<ZCharacterSubcontrollerInventory::SCreateItem*>(HookAction::Continue());
}
