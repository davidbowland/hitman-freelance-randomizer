// Loot amounts (see MoneyItems.h). Every raw engine read and write follows
// the guard pattern in Guard.h.

#include "MoneyItems.h"

#include "ActorReads.h" // SafeOwning
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "Scene.h"
#include "SceneOf.h"
#include "SceneStage.h"

#include <Glacier/ZObject.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMaxParentSteps = 32;      // logical parents climbed for the owning setpiece
constexpr int kMaxOwnerSteps = 16;       // owning entities climbed from each of them
constexpr size_t kMaxAboveText = 24;     // entities above the item named in its first write line
constexpr size_t kMaxItemEntities = 400; // entities walked under one item (the small item has 138)
constexpr int kMaxWalks = 4;             // at activation, then the next frames
constexpr size_t kMaxItems = 512;        // items handled per load

// The entities above a money item, for its kind: its logical parents and,
// from each, its owning entities (a setpiece root is a plain ZEntity the
// logical-parent chain passes by, and a setpiece placed in a brick is a
// nested template instance with no blueprint factory of its own). An
// entity's template is its own factory or, without one, its entry in its
// owner's factory when that entry is this very entity.
class AboveWalk {
public:
    struct Entry {
        ZEntityType** entity;
        uint64_t id;
        std::string className;
        uint64_t rid; // the template it is the root of (0: none found)
        int parent;   // the logical parent step it was reached from (0: the item itself)
        int owner;    // owning entities climbed from there (0: the step itself)
    };

    std::vector<Entry> entries;
    std::string faults; // guarded reads that faulted

    void Climb(ZEntityType** p_Item) {
        std::vector<ZEntityType**> s_Chain {p_Item};
        ZEntityType** s_At = p_Item;

        for (int i = 0; i < kMaxParentSteps; ++i) {
            ZEntityType** s_Parent = nullptr;

            if (const uint32_t s_Code = ReadLogicalParent(s_At, &s_Parent)) {
                faults += fmt::format("{}parent {}", faults.empty() ? "" : ", ", FaultText(s_Code));
                break;
            }

            if (!s_Parent || std::find(s_Chain.begin(), s_Chain.end(), s_Parent) != s_Chain.end())
                break;

            s_Chain.push_back(s_Parent);
            s_At = s_Parent;
        }

        for (size_t p = 0; p < s_Chain.size(); ++p) {
            if (p > 0)
                Add(s_Chain[p], static_cast<int>(p), 0);

            ZEntityType** s_Owned = s_Chain[p];

            for (int o = 1; o <= kMaxOwnerSteps; ++o) {
                ZEntityType** s_Owner = OwnerOf(s_Owned);

                if (!s_Owner)
                    break;

                Add(s_Owner, static_cast<int>(p), o);
                s_Owned = s_Owner;
            }
        }
    }

private:
    std::vector<ZEntityType**> m_Seen;
    std::unordered_map<ZEntityType**, ZEntityBlueprintFactoryBase*> m_Factories;

    ZEntityType** OwnerOf(ZEntityType** p_Entity) {
        ZEntityType** s_Owner = nullptr;

        if (const DWORD s_Code = SafeOwning(p_Entity, &s_Owner)) {
            faults += fmt::format("{}owner {}", faults.empty() ? "" : ", ", FaultText(s_Code));
            return nullptr;
        }

        return s_Owner == p_Entity ? nullptr : s_Owner;
    }

    // The factory p_Entity's sub-entities come from, or null.
    ZEntityBlueprintFactoryBase* FactoryOf(ZEntityType** p_Entity, int p_Depth) {
        if (const auto s_Known = m_Factories.find(p_Entity); s_Known != m_Factories.end())
            return s_Known->second;

        ZEntityBlueprintFactoryBase* s_Factory = nullptr;

        if (ReadOwnBlueprintFactory(p_Entity, &s_Factory))
            s_Factory = nullptr;

        if (!s_Factory && p_Depth < kMaxOwnerSteps) {
            ZEntityType** s_Owner = OwnerOf(p_Entity);
            ZEntityBlueprintFactoryBase* s_OwnerFactory = s_Owner ? FactoryOf(s_Owner, p_Depth + 1) : nullptr;
            std::string s_Class;
            uint64_t s_Id = 0;

            if (s_OwnerFactory && !ReadEntityClassName(p_Entity, s_Class, &s_Id)) {
                const SubEntityLookup s_Lookup = LookUpSubEntity(s_Owner, s_OwnerFactory, s_Id);

                // Only when the owner's entry with that id is this very entity.
                if (s_Lookup.entity == p_Entity)
                    s_Factory = s_Lookup.factory;
            }
        }

        m_Factories.emplace(p_Entity, s_Factory);
        return s_Factory;
    }

    void Add(ZEntityType** p_Entity, int p_Parent, int p_Owner) {
        if (std::find(m_Seen.begin(), m_Seen.end(), p_Entity) != m_Seen.end())
            return;

        m_Seen.push_back(p_Entity);
        Entry s_Entry {p_Entity, 0, {}, 0, p_Parent, p_Owner};
        ReadEntityClassName(p_Entity, s_Entry.className, &s_Entry.id);

        if (ZEntityBlueprintFactoryBase* s_Factory = FactoryOf(p_Entity, 0)) {
            if (ReadFactoryRid(s_Factory, &s_Entry.rid))
                s_Entry.rid = 0;
        }

        entries.push_back(std::move(s_Entry));
    }
};

// "parent 2" or "owner 1 of parent 2".
std::string StepText(const AboveWalk::Entry& p_Entry) {
    return p_Entry.owner > 0 ? fmt::format("owner {} of parent {}", p_Entry.owner, p_Entry.parent)
                             : fmt::format("parent {}", p_Entry.parent);
}

// The storage of an int32 property without a getter, or 0 (*p_Why says why).
uintptr_t Int32Address(ZEntityType** p_Entity, std::string_view p_Name, std::string* p_Why) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    if (s_Location.status == PropertyLocation::Status::Fault) {
        *p_Why = fmt::format("{} {}", p_Name, FaultText(s_Location.faultCode));
        return 0;
    }

    if (s_Location.status != PropertyLocation::Status::Ok || s_Location.typeSize != sizeof(int32_t) ||
        std::strcmp(s_Location.typeName, "int32") != 0) {
        *p_Why = fmt::format("{} not a raw int32", p_Name);
        return 0;
    }

    return s_Location.address;
}

// ", draw 3" when out-of-range draws were redrawn, ", fallback: center +2 x 50" when all were.
std::string DrawText(const fr::TruncatedDraw& p_Draw) {
    if (p_Draw.fallback)
        return fmt::format(", fallback: center {:+} x {}", p_Draw.jitterSteps, fr::kLootAmountStep);

    return p_Draw.tries > 1 ? fmt::format(", draw {}", p_Draw.tries) : std::string();
}

constexpr std::string_view kTag = "loot amounts";

} // namespace

void MoneyItems::Configure(bool p_On, const std::array<fr::LootAmountBounds, fr::kMoneyKinds>& p_Bounds) {
    m_On = p_On;
    std::lock_guard s_Lock(m_Mutex);

    for (const fr::MoneyKind s_Kind :
         {fr::MoneyKind::Safe, fr::MoneyKind::Crate, fr::MoneyKind::Mule, fr::MoneyKind::Vault})
        m_Ranges[static_cast<size_t>(s_Kind)] = fr::LootAmountRangeOf(s_Kind, p_Bounds[static_cast<size_t>(s_Kind)]);

    if (!m_On)
        return;

    std::string s_Ranges;

    for (const fr::MoneyKind s_Kind :
         {fr::MoneyKind::Safe, fr::MoneyKind::Crate, fr::MoneyKind::Mule, fr::MoneyKind::Vault}) {
        const fr::LootAmountRange& s_Range = m_Ranges[static_cast<size_t>(s_Kind)];
        s_Ranges += fmt::format("{}{} [{}, {}] log-normal median {} sigma {}", s_Ranges.empty() ? "" : "; ",
                                fr::MoneyKindName(s_Kind), s_Range.lo, s_Range.hi, s_Range.median, s_Range.sigma);
    }

    Log("loot amounts: on: money items in Freelancer missions get their own amount (multiples of {}): {}",
        fr::kLootAmountStep, s_Ranges);
}

std::string MoneyItems::SeedText() const {
    return m_Seed ? fmt::format("seed {} ({})", m_Seed->seed, fr::SeedSourceName(m_Seed->source))
                  : std::string("no seed");
}

void MoneyItems::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                const std::optional<fr::SeedResolution>& p_Seed) noexcept {
    if (m_On)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context, p_Seed); });
}

void MoneyItems::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                              const std::optional<fr::SeedResolution>& p_Seed) {
    if (!p_Context)
        return;

    if (p_Stage == ESceneLoadingStage::eLoading_AssetsLoaded) {
        // A new load (a restart may come without a ClearScene).
        OnSceneClear();
        const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();
        const std::string s_Scene =
            SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene

        if (!fr::IsFreelancerMission(s_Type, s_Scene))
            return;

        {
            std::lock_guard s_Lock(m_Mutex);
            m_Scene = std::string(s_Scene);
        }

        m_Armed.store(true, std::memory_order_relaxed);
        LogTo(fr::LogTopic::Money, "loot amounts: {}: armed{}", ShortSceneName(s_Scene),
              p_Seed ? fmt::format(" (seed {})", p_Seed->seed) : std::string(", waiting for the scene seed"));

        if (p_Seed)
            TakeSeed(*p_Seed, "assets loaded");

        return;
    }

    if (!m_Armed.load(std::memory_order_relaxed))
        return;

    if (p_Seed) {
        TakeSeed(*p_Seed, StageName(p_Stage));
    }
    else if (p_Stage == ESceneLoadingStage::eLoading_SceneStarted) {
        LogTo(fr::LogTopic::Money, "loot amounts: no scene seed at scene start; items use seed 0");
        TakeSeed({fr::SeedSource::Fallback, 0}, "scene start (no seed)");
    }
}

// The first seed of the load is kept (the dials keep theirs for the load too).
void MoneyItems::TakeSeed(const fr::SeedResolution& p_Seed, const char* p_When) {
    std::vector<size_t> s_Waiting;
    {
        std::lock_guard s_Lock(m_Mutex);

        if (m_Seed) {
            if (m_Seed->seed != p_Seed.seed)
                LogTo(fr::LogTopic::Money, "loot amounts: seed {} at {} differs from {}; this load keeps {}",
                      p_Seed.seed, p_When, m_Seed->seed, m_Seed->seed);

            return;
        }

        m_Seed = p_Seed;
        s_Waiting.swap(m_Waiting);
    }

    if (!s_Waiting.empty())
        LogTo(fr::LogTopic::Money, "loot amounts: {} money items activated before the seed was known; writing them now",
              s_Waiting.size());

    for (const size_t s_Item : s_Waiting) {
        Contained(kTag, "waiting item", [&] {
            std::lock_guard s_Lock(m_Mutex);
            Start(s_Item);
        });
    }
}

void MoneyItems::OnSceneClear() noexcept {
    if (!m_On)
        return;

    Contained(kTag, "scene clear", [&] {
        m_Armed.store(false, std::memory_order_relaxed);
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Items.empty()) {
            size_t s_Written = 0;
            size_t s_Unknown = 0;
            std::string s_Amounts;

            for (const Item& s_Item : m_Items) {
                bool s_Any = false;

                for (const Rand& s_Rand : s_Item.rands)
                    s_Any = s_Any || s_Rand.written;

                s_Written += s_Any ? 1 : 0;
                s_Unknown += s_Item.known ? 0 : 1;
                s_Amounts += fmt::format("{}{} {}{}", s_Amounts.empty() ? "" : ", ", fr::MoneyKindName(s_Item.kind),
                                         s_Item.roll.amount, s_Any ? "" : " (not written)");
            }

            LogTo(
                fr::LogTopic::Money,
                "loot amounts summary {} ({}): {} money items, {} written, {} of unknown kind, {} re-activations, {} "
                "values re-applied before a roll, {} watch slots full; amounts: {}",
                ShortSceneName(m_Scene), SeedText(), m_Items.size(), s_Written, s_Unknown, m_Reactivations,
                [&] {
                    uint32_t s_Count = 0;

                    for (const Item& s_Item : m_Items)
                        s_Count += s_Item.reapplied;

                    return s_Count;
                }(),
                m_WatchFull, s_Amounts);
        }

        Forget();
    });
}

void MoneyItems::Forget() {
    m_Watch.Clear();
    m_Scene.clear();
    m_Seed.reset();
    m_Spawns = {};
    m_Items.clear();
    m_ByEntity.clear();
    m_Waiting.clear();
    m_Reactivations = 0;
    m_WatchFull = 0;
}

void MoneyItems::OnActivateEntity(const ZEntityRef* p_Entity) noexcept {
    if (!p_Entity || !p_Entity->m_pObj || !m_Armed.load(std::memory_order_relaxed))
        return;

    Contained(kTag, "activate entity", [&] {
        // The cheap filter first: every activation's own blueprint id, guarded.
        uint64_t s_Rid = 0;

        if (ReadBlueprintRid(p_Entity->m_pObj, &s_Rid) || fr::MoneyItemSizeOf(s_Rid) == fr::MoneyItemSize::None)
            return;

        Activate(p_Entity->m_pObj, s_Rid);
    });
}

// The kind from the loot setpieces above the entity, innermost first (its
// logical parents and their owning entities), or Vault for the bank vault's
// money. *p_Above names every entity above it (id, class, template), for the
// log: what tells two items apart when the kind is wrong.
fr::MoneyKindGuess MoneyItems::Classify(ZEntityType** p_Entity, fr::MoneyItemSize p_Size, std::string* p_Path,
                                        std::string* p_Above) {
    AboveWalk s_Walk;
    s_Walk.Climb(p_Entity);
    std::vector<fr::LootKind> s_Owners;
    std::vector<uint64_t> s_Ids;
    std::string s_Path;
    std::string s_Above;

    for (size_t i = 0; i < s_Walk.entries.size(); ++i) {
        const AboveWalk::Entry& s_Entry = s_Walk.entries[i];
        s_Ids.push_back(s_Entry.id);
        const fr::LootTemplate* s_Template = s_Entry.rid ? fr::FindLootTemplate(s_Entry.rid) : nullptr;

        if (s_Template) {
            s_Owners.push_back(s_Template->kind);
            s_Path += fmt::format("{}{} {:016x} ({})", s_Path.empty() ? "" : " < ", fr::LootKindName(s_Template->kind),
                                  s_Entry.id, StepText(s_Entry));
        }

        if (i < kMaxAboveText) {
            s_Above += fmt::format("{}{} {:016x} ({}){}", s_Above.empty() ? "" : " < ", StepText(s_Entry), s_Entry.id,
                                   s_Entry.className.empty() ? std::string("?") : s_Entry.className,
                                   s_Template    ? fmt::format(" [{}]", s_Template->name)
                                   : s_Entry.rid ? fmt::format(" [{:016X}]", s_Entry.rid)
                                                 : std::string());
        }
        else if (i == kMaxAboveText) {
            s_Above += fmt::format(" < ... ({} in all)", s_Walk.entries.size());
        }
    }

    if (!s_Walk.faults.empty())
        s_Above += fmt::format("{}faults: {}", s_Above.empty() ? "" : "; ", s_Walk.faults);

    uint64_t s_Id = 0;
    std::string s_Class;
    ReadEntityClassName(p_Entity, s_Class, &s_Id);
    const bool s_Vault = fr::IsBankVaultMoney(s_Id, s_Ids);
    const fr::MoneyKindGuess s_Guess = fr::ClassifyMoneyItem(p_Size, s_Owners, s_Vault);
    *p_Above = s_Above.empty() ? std::string("nothing") : s_Above;

    if (s_Vault) {
        *p_Path = fmt::format("bank vault (item {:016x}, spawner {:016x}){}{}", s_Id, fr::kBankVaultSpawnerId,
                              s_Path.empty() ? "" : "; setpieces: ", s_Path);
        return s_Guess;
    }

    *p_Path = s_Guess.known
                  ? s_Path
                  : fmt::format("kind unknown: {} item, no safe, mule or crate setpiece among {} entities above it",
                                fr::MoneyItemSizeName(p_Size), s_Walk.entries.size());
    return s_Guess;
}

// A money item: classified now (its parents are alive and set), then rolled
// and written now or when the seed is known. An entity address seen before
// is the same item only with the same blueprint, entity id and setpieces
// above it; otherwise the old item was freed and the address reused, and
// this is a new item (new spawn number, new amount).
void MoneyItems::Activate(ZEntityType** p_Entity, uint64_t p_Rid) {
    std::lock_guard s_Lock(m_Mutex);
    const fr::MoneyItemSize s_Size = fr::MoneyItemSizeOf(p_Rid);
    std::string s_Class;
    uint64_t s_Id = 0;
    ReadEntityClassName(p_Entity, s_Class, &s_Id);
    std::string s_Path;
    std::string s_Above;
    const fr::MoneyKindGuess s_Guess = Classify(p_Entity, s_Size, &s_Path, &s_Above);
    const auto s_Known = m_ByEntity.find(p_Entity);

    if (s_Known != m_ByEntity.end()) {
        const size_t s_Old = s_Known->second;
        const Item& s_Item = m_Items[s_Old];

        if (s_Item.blueprint == p_Rid && s_Item.id == s_Id && s_Item.kind == s_Guess.kind && s_Item.owner == s_Path) {
            Reactivate(s_Old);
            return;
        }

        Retire(s_Old, fmt::format("its address now holds another item (blueprint {:016X}, id {:016x}, {})", p_Rid, s_Id,
                                  s_Path.empty() ? std::string("-") : s_Path));
        m_ByEntity.erase(s_Known);
    }

    if (m_Items.size() >= kMaxItems) {
        if (m_Items.size() == kMaxItems)
            LogTo(fr::LogTopic::Money, "loot amounts: {} money items this load (cap); further ones are left vanilla",
                  kMaxItems);

        return;
    }

    Item s_Item {};
    s_Item.entity = p_Entity;
    s_Item.blueprint = p_Rid;
    s_Item.size = s_Size;
    s_Item.id = s_Id;
    s_Item.kind = s_Guess.kind;
    s_Item.known = s_Guess.known;
    s_Item.owner = std::move(s_Path);
    s_Item.above = std::move(s_Above);

    const size_t s_Index = m_Items.size();
    m_Items.push_back(std::move(s_Item));
    m_ByEntity.emplace(p_Entity, s_Index);

    if (!m_Seed) {
        m_Waiting.push_back(s_Index);
        LogTo(fr::LogTopic::Money,
              "loot amount: {} item {:016x} activated before the scene seed is known; written when it is",
              fr::MoneyKindName(m_Items[s_Index].kind), m_Items[s_Index].id);
        return;
    }

    Start(s_Index);
}

// Under m_Mutex: the same item activated again (picked up, dropped, ...):
// same amount, its tree walked and its addresses resolved again before the
// write (nothing saved from the earlier activation is written through).
void MoneyItems::Reactivate(size_t p_Item) {
    ++m_Reactivations;
    Item& s_Item = m_Items[p_Item];
    LogTo(fr::LogTopic::Money, "loot amount: {} item {:016x} activated again; amount {} kept",
          fr::MoneyKindName(s_Item.kind), s_Item.id, s_Item.roll.amount);

    if (!s_Item.done || s_Item.retired)
        return;

    for (const Rand& s_Rand : s_Item.rands)
        Unwatch(s_Rand.entity);

    if (!Walk(p_Item)) {
        LogTo(fr::LogTopic::Money,
              "loot amount: {} item {:016x} activated again: no ZRandEntity_int32 found; not written again",
              fr::MoneyKindName(s_Item.kind), s_Item.id);
        return;
    }

    Write(p_Item, "activated again");
}

// Under m_Mutex: the item is never read or written again.
void MoneyItems::Retire(size_t p_Item, const std::string& p_Why) {
    Item& s_Item = m_Items[p_Item];

    if (s_Item.retired)
        return;

    s_Item.retired = true;
    s_Item.done = true;

    for (Rand& s_Rand : s_Item.rands) {
        Unwatch(s_Rand.entity);
        s_Rand.written = false;
    }

    LogTo(fr::LogTopic::Money, "loot amount: {} item {:016x} (amount {}) dropped: {}", fr::MoneyKindName(s_Item.kind),
          s_Item.id, s_Item.roll.amount, p_Why);
}

// Under m_Mutex, with the seed known: the item's draw, then its walk and write.
void MoneyItems::Start(size_t p_Item) {
    Item& s_Item = m_Items[p_Item];

    if (s_Item.retired)
        return;

    s_Item.spawnIndex = m_Spawns[static_cast<size_t>(s_Item.kind)]++;
    s_Item.roll = fr::RollLootAmount(m_Seed ? m_Seed->seed : 0, m_Scene, s_Item.kind, s_Item.spawnIndex,
                                     m_Ranges[static_cast<size_t>(s_Item.kind)]);

    if (Walk(p_Item)) {
        Write(p_Item, "activation");
        return;
    }

    if (s_Item.tries >= kMaxWalks) {
        s_Item.done = true;
        LogTo(fr::LogTopic::Money,
              "loot amount: {} item {:016x} blueprint {:016X}: no ZRandEntity_int32 found in {} walks; left vanilla",
              fr::MoneyKindName(s_Item.kind), s_Item.id, s_Item.blueprint, s_Item.tries);
    }
}

// Under m_Mutex: the item's ZRandEntity_int32 members.
bool MoneyItems::Walk(size_t p_Item) {
    Item& s_Item = m_Items[p_Item];
    ++s_Item.tries;
    s_Item.rands.clear();

    VisitEntityTree(
        s_Item.entity, s_Item.blueprint,
        [&](const VisitedEntity& p_Visited) {
            std::string s_Class;
            uint64_t s_Id = 0;

            if (ReadEntityClassName(p_Visited.entity, s_Class, &s_Id))
                return false;

            if (s_Class == "ZRandEntity_int32") {
                Rand s_Rand {p_Visited.entity, s_Id, 0, 0, 0, 0, false};
                s_Item.rands.push_back(s_Rand);
                return true;
            }

            return false;
        },
        kMaxItemEntities);

    return !s_Item.rands.empty();
}

// Under m_Mutex: m_nMin = m_nMax = the amount in every ZRandEntity_int32 of
// the item; the first time, one line per item with the vanilla range.
void MoneyItems::Write(size_t p_Item, const char* p_When) {
    Item& s_Item = m_Items[p_Item];
    const int32_t s_Amount = s_Item.roll.amount;
    const bool s_First = !s_Item.done;
    std::string s_Problems;
    std::string s_Vanilla;

    for (Rand& s_Rand : s_Item.rands) {
        std::string s_Why;

        if (!s_Rand.minAddress) {
            s_Rand.minAddress = Int32Address(s_Rand.entity, "m_nMin", &s_Why);
            s_Rand.maxAddress = s_Rand.minAddress ? Int32Address(s_Rand.entity, "m_nMax", &s_Why) : 0;

            if (!s_Rand.minAddress || !s_Rand.maxAddress) {
                s_Rand.minAddress = 0;
                s_Problems += fmt::format("{}{:016x}: {}", s_Problems.empty() ? "" : "; ", s_Rand.id, s_Why);
                continue;
            }

            if (ReadEngineBytes(s_Rand.minAddress, &s_Rand.vanillaMin, sizeof(int32_t)) ||
                ReadEngineBytes(s_Rand.maxAddress, &s_Rand.vanillaMax, sizeof(int32_t))) {
                s_Rand.minAddress = 0;
                s_Problems += fmt::format("{}{:016x}: range unreadable", s_Problems.empty() ? "" : "; ", s_Rand.id);
                continue;
            }
        }

        s_Vanilla += fmt::format("{}[{},{}]", s_Vanilla.empty() ? "" : " ", s_Rand.vanillaMin, s_Rand.vanillaMax);

        const uint32_t s_MinCode = WriteEngineBytes(s_Rand.minAddress, &s_Amount, sizeof(s_Amount));
        const uint32_t s_MaxCode =
            s_MinCode ? s_MinCode : WriteEngineBytes(s_Rand.maxAddress, &s_Amount, sizeof(s_Amount));
        int32_t s_Back[2] {};

        if (s_MinCode || s_MaxCode) {
            s_Problems += fmt::format("{}{:016x}: write {}", s_Problems.empty() ? "" : "; ", s_Rand.id,
                                      FaultText(s_MinCode ? s_MinCode : s_MaxCode));
            continue;
        }

        if (ReadEngineBytes(s_Rand.minAddress, &s_Back[0], sizeof(int32_t)) ||
            ReadEngineBytes(s_Rand.maxAddress, &s_Back[1], sizeof(int32_t)) || s_Back[0] != s_Amount ||
            s_Back[1] != s_Amount) {
            s_Problems += fmt::format("{}{:016x}: read back [{},{}]", s_Problems.empty() ? "" : "; ", s_Rand.id,
                                      s_Back[0], s_Back[1]);
            continue;
        }

        s_Rand.written = true;
        Watch(s_Rand.entity, p_Item, kRoleRand);
    }

    if (!s_First) {
        if (!s_Problems.empty())
            LogTo(fr::LogTopic::Money, "loot amount: {} item {:016x} written again at {}: {}",
                  fr::MoneyKindName(s_Item.kind), s_Item.id, p_When, s_Problems);

        return;
    }

    s_Item.done = true;

    const fr::LootAmountRange& s_Range = m_Ranges[static_cast<size_t>(s_Item.kind)];
    LogTo(
        fr::LogTopic::Money,
        "loot amount: {}{} {:016x} {:016X} ({} item) vanilla range {} -> {} ({}, spawn #{} of the kind; range [{}, {}] "
        "median {}{}); at {}{}; owner: {}; {} ZRandEntity_int32; above: {}",
        fr::MoneyKindName(s_Item.kind), s_Item.known ? "" : " (kind unknown)", s_Item.id, s_Item.blueprint,
        fr::MoneyItemSizeName(s_Item.size), s_Vanilla.empty() ? std::string("?") : s_Vanilla, s_Amount, SeedText(),
        s_Item.spawnIndex, s_Range.lo, s_Range.hi, s_Range.median, DrawText(s_Item.roll.draw), p_When,
        s_Problems.empty() ? std::string() : fmt::format("; NOT WRITTEN: {}", s_Problems),
        s_Item.owner.empty() ? std::string("-") : s_Item.owner, s_Item.rands.size(), s_Item.above);
}

void MoneyItems::Tick() noexcept {
    if (!m_On || !m_Armed.load(std::memory_order_relaxed))
        return;

    Contained(kTag, "frame", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Seed)
            return;

        for (size_t i = 0; i < m_Items.size(); ++i) {
            Item& s_Item = m_Items[i];

            if (s_Item.done || s_Item.tries == 0)
                continue;

            uint64_t s_Rid = 0;

            if (ReadBlueprintRid(s_Item.entity, &s_Rid) || s_Rid != s_Item.blueprint) {
                Retire(i, fmt::format("its blueprint reads {:016X} at frame retry {} (freed?)", s_Rid, s_Item.tries));
                continue;
            }

            if (Walk(i)) {
                Write(i, fmt::format("frame retry {}", s_Item.tries).c_str());
            }
            else if (s_Item.tries >= kMaxWalks) {
                s_Item.done = true;
                LogTo(fr::LogTopic::Money,
                      "loot amount: {} item {:016x} blueprint {:016X}: no ZRandEntity_int32 found in {} walks; left "
                      "vanilla",
                      fr::MoneyKindName(s_Item.kind), s_Item.id, s_Item.blueprint, s_Item.tries);
            }
        }
    });
}

// Under m_Mutex.
void MoneyItems::Watch(const void* p_Entity, size_t p_Item, uint32_t p_Role) {
    if (!m_Watch.Watch(p_Entity, static_cast<uint32_t>(p_Item << 2) | p_Role))
        ++m_WatchFull;
}

// Under m_Mutex: the entry stays (so other keys' chains hold) but no longer
// leads anywhere.
void MoneyItems::Unwatch(const void* p_Entity) {
    m_Watch.Replace(p_Entity, kRoleDropped);
}

// Hit path only: the class is still ZRandEntity_int32 with the same entity
// id, and m_nMin / m_nMax resolve to the saved addresses (a freed entity
// whose memory another entity now holds fails one of these).
bool MoneyItems::StillRand(const Rand& p_Rand, std::string* p_Why) const {
    std::string s_Class;
    uint64_t s_Id = 0;

    if (const uint32_t s_Code = ReadEntityClassName(p_Rand.entity, s_Class, &s_Id)) {
        *p_Why = fmt::format("class {}", FaultText(s_Code));
        return false;
    }

    if (s_Class != "ZRandEntity_int32" || s_Id != p_Rand.id) {
        *p_Why = fmt::format("now {:016x} ({})", s_Id, s_Class.empty() ? std::string("<no class>") : s_Class);
        return false;
    }

    const uintptr_t s_Min = Int32Address(p_Rand.entity, "m_nMin", p_Why);
    const uintptr_t s_Max = s_Min ? Int32Address(p_Rand.entity, "m_nMax", p_Why) : 0;

    if (!s_Min || !s_Max)
        return false;

    if (s_Min != p_Rand.minAddress || s_Max != p_Rand.maxAddress) {
        *p_Why = "m_nMin / m_nMax moved";
        return false;
    }

    return true;
}

void MoneyItems::OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef&) noexcept {
    if (!p_Entity.m_pObj || m_Watch.Empty())
        return;

    Contained(kTag, "input pin", [&] { Pin(p_Entity, p_PinId); });
}

// An input pin of a watched ZRandEntity_int32 (a roll may follow): its range
// is checked and written again if it changed.
void MoneyItems::Pin(const ZEntityRef& p_Entity, uint32_t p_PinId) {
    const std::optional<uint32_t> s_Value = m_Watch.Lookup(p_Entity.m_pObj);

    if (!s_Value)
        return;

    const uint32_t s_Role = *s_Value & 3;
    const size_t s_Index = *s_Value >> 2;

    if (s_Role != kRoleRand)
        return;

    std::lock_guard s_Lock(m_Mutex);

    if (s_Index >= m_Items.size() || m_Items[s_Index].retired)
        return;

    Item& s_Item = m_Items[s_Index];

    for (Rand& s_Rand : s_Item.rands) {
        if (s_Rand.entity != p_Entity.m_pObj || !s_Rand.written)
            continue;

        // Before any write: still this item's random entity, at the same addresses.
        std::string s_Why;

        if (!StillRand(s_Rand, &s_Why)) {
            s_Rand.written = false;
            Unwatch(s_Rand.entity);
            LogTo(fr::LogTopic::Money,
                  "loot amount: {} item {:016x} ZRandEntity_int32 {:016x} no longer checks out ({}); watch dropped, "
                  "nothing written",
                  fr::MoneyKindName(s_Item.kind), s_Item.id, s_Rand.id, s_Why);
            return;
        }

        int32_t s_Now[2] {};

        if (ReadEngineBytes(s_Rand.minAddress, &s_Now[0], sizeof(int32_t)) ||
            ReadEngineBytes(s_Rand.maxAddress, &s_Now[1], sizeof(int32_t)))
            return;

        if (s_Now[0] == s_Item.roll.amount && s_Now[1] == s_Item.roll.amount)
            return;

        const int32_t s_Amount = s_Item.roll.amount;
        const bool s_Ok = !WriteEngineBytes(s_Rand.minAddress, &s_Amount, sizeof(s_Amount)) &&
                          !WriteEngineBytes(s_Rand.maxAddress, &s_Amount, sizeof(s_Amount));
        ++s_Item.reapplied;
        LogTo(fr::LogTopic::Money,
              "loot amount: {} item {:016x} ZRandEntity_int32 {:016x} input pin {:08x}: range was [{},{}], {} {}",
              fr::MoneyKindName(s_Item.kind), s_Item.id, s_Rand.id, p_PinId, s_Now[0], s_Now[1],
              s_Ok ? "written again to" : "FAILED to write again", s_Amount);
        return;
    }
}
