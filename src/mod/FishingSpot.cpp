// Fishing script (see FishingSpot.h). Every raw engine read and
// write follows the guard pattern in Guard.h.

#include "FishingSpot.h"

#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "EntityReads.h"
#include "Repository.h"
#include "Scene.h"
#include "SceneOf.h"

#include <Glacier/ZObject.h>

#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <span>

namespace {

const char* kRoleNames[] = {"fishing spot", "TimerSimple 2", "RandomSelector", "CaughtSomething", "HeroItemAction"};

std::string RoleText(size_t p_Role) {
    if (p_Role < std::size(kRoleNames))
        return kRoleNames[p_Role];

    if (p_Role < fr::kFishingChoices + std::size(kRoleNames))
        return fmt::format("choice {}", p_Role - std::size(kRoleNames) + 1);

    return fmt::format("ItemSpawner {}", p_Role - std::size(kRoleNames) - fr::kFishingChoices + 1);
}

uint64_t RoleId(size_t p_Role) {
    switch (p_Role) {
        case 0: return fr::kFishingSpotId;
        case 1: return fr::kFishingTimerId;
        case 2: return fr::kFishingSelectorId;
        case 3: return fr::kFishingCaughtId;
        case 4: return fr::kFishingHandsId;
        default: break;
    }

    const size_t s_Index = p_Role - std::size(kRoleNames);
    return s_Index < fr::kFishingChoices ? fr::kFishingChoiceIds[s_Index]
                                         : fr::kFishingSpawnerIds[s_Index - fr::kFishingChoices];
}

std::optional<float> ReadFloatAt(uintptr_t p_Address) {
    float s_Value = 0.0f;

    if (!p_Address || ReadEngineBytes(p_Address, &s_Value, sizeof(s_Value)))
        return std::nullopt;

    return s_Value;
}

bool WriteFloatAt(uintptr_t p_Address, float p_Value) {
    float s_Back = 0.0f;
    return p_Address && !WriteEngineBytes(p_Address, &p_Value, sizeof(p_Value)) &&
           !ReadEngineBytes(p_Address, &s_Back, sizeof(s_Back)) && s_Back == p_Value;
}

constexpr std::string_view kTag = "fishing";

} // namespace

static_assert(std::size(kRoleNames) == 5, "RoleText and RoleId assume five single roles before the choices");

void FishingSpot::Configure(bool p_On) {
    m_On = p_On;
}

void FishingSpot::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    if (m_On)
        Contained(kTag, "loading stage", [&] { LoadingStage(p_Stage, p_Context); });
}

void FishingSpot::Reset() {
    m_PinsOn.store(false, std::memory_order_relaxed);

    for (auto& s_Entity : m_Entities)
        s_Entity.store(nullptr, std::memory_order_relaxed);

    m_Done = false;
    m_Scene.clear();

    std::lock_guard s_Lock(m_Mutex);
    m_WeightAddress = {};
    m_ItemText = {};
    m_Items.assign(fr::kFishingChoices, fr::FishCatch::Other);
    m_ItemsRead = false;
    m_Rolls.reset();
    m_Playing = false;
    m_Now = 0.0;
    m_Originals.clear();
    m_Written = false;
    m_RetryNow = false;
    m_Summary.clear();
}

void FishingSpot::OnSceneClear() noexcept {
    Contained(kTag, "scene clear", [&] {
        std::lock_guard s_Lock(m_Mutex);

        if (!m_Rolls)
            return;

        if (m_Written)
            Restore("the scene is cleared during a roll");

        const fr::FishingRolls& r = *m_Rolls;
        LogTo(fr::LogTopic::Safehouse,
              "fishing: summary of the visit: {} roll(s), {} steered, {} as planned, {} missed, {} vanilla re-pick(s); "
              "catches counted {} (fishing_script {}){}{}",
              r.Rolls(), r.Steered(), r.AsPlanned(), r.Missed(), r.Retries(), r.Catches(), m_On ? "on" : "off",
              m_Summary.empty() ? "" : "; ", m_Summary);
    });

    Reset();
}

void FishingSpot::LoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) {
    if (!p_Context)
        return;

    const char* s_Stage = nullptr;

    switch (p_Stage) {
        case ESceneLoadingStage::eLoading_AssetsLoaded: Reset(); return;
        case ESceneLoadingStage::eLoading_SceneAllocated: s_Stage = "eLoading_SceneAllocated"; break;
        case ESceneLoadingStage::eLoading_SceneStarted: s_Stage = "eLoading_SceneStarted"; break;
        case ESceneLoadingStage::eLoading_ScenePlaying:
            if (m_PinsOn.load(std::memory_order_relaxed)) {
                if (!m_ItemsRead)
                    ReadItems();

                std::lock_guard s_Lock(m_Mutex);
                m_Playing = true;
                m_Now = 0.0;
                LogTo(fr::LogTopic::Safehouse, "fishing: scene playing: weights {}; next catch #{} ({})",
                      WeightsText(ReadWeights()), m_Catches + 1,
                      !m_On ? "fishing_script off: vanilla"
                      : fr::ScriptedCatch(m_Catches + 1)
                          ? fmt::format("scripted: {}", fr::FishCatchName(*fr::ScriptedCatch(m_Catches + 1)))
                          : std::string("past the script: vanilla"));
            }
            return;
        default: return;
    }

    if (m_Done)
        return;

    const std::string s_Scene =
        SceneOf(p_Context); // a full variant (Landslide, The Author) counts as its contract's scene
    const std::string_view s_Type = p_Context->m_SceneInitParameters.m_Type.ToStringView();

    if (!fr::IsFreelancerScene(s_Type) || fr::IsFreelancerMission(s_Type, s_Scene)) {
        m_Done = true;
        return;
    }

    m_Scene = s_Scene;
    m_Done = Find(p_Context, s_Stage);

    if (!m_Entities[kSelector].load(std::memory_order_relaxed))
        return;

    ReadItems();

    // The script restarts from the fish on every safehouse visit.
    m_Catches = 0;

    {
        std::lock_guard s_Lock(m_Mutex);
        m_Rolls.emplace(m_On, m_Catches, m_Items);
    }

    LogTo(fr::LogTopic::Safehouse, "fishing: {} (restarts each safehouse visit)",
          m_On ? "fishing_script on: catch 1 a fish, 2 the fishing line, 3 the pirate saber, then vanilla"
               : "fishing_script off: watching only");
    m_PinsOn.store(true, std::memory_order_release);
}

bool FishingSpot::Find(ZEntitySceneContext* p_Context, const char* p_Stage) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const bool s_LastChance = std::string_view(p_Stage) == "eLoading_SceneStarted";
    ZEntityType** s_Root = nullptr;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_Bricks[i].m_RuntimeResourceID.GetID() == fr::kFishingBrickRid)
            s_Root = s_Bricks[i].m_EntityRef.m_pObj;
    }

    if (!s_Root) {
        LogTo(fr::LogTopic::Safehouse, "fishing: {} at {}: safehouse brick {:016X} not loaded{}", m_Scene, p_Stage,
              fr::kFishingBrickRid,
              s_LastChance ? "; nothing watched or changed" : "; trying again at eLoading_SceneStarted");
        return s_LastChance;
    }

    std::string s_Found;
    size_t s_Missing = 0;

    for (size_t i = 0; i < kRoles; ++i) {
        const SubEntityLookup s_Lookup = LookUpSubEntity(s_Root, nullptr, RoleId(i));
        m_Entities[i].store(s_Lookup.entity, std::memory_order_relaxed);
        s_Missing += s_Lookup.entity ? 0 : 1;
        s_Found +=
            fmt::format("{}{} {} {}", i ? ", " : "", RoleText(i), IdText(RoleId(i)),
                        s_Lookup.faultStep ? fmt::format("{} {}", s_Lookup.faultStep, FaultText(s_Lookup.faultCode))
                        : s_Lookup.entity  ? EntityText(s_Lookup.entity)
                                           : std::string("MISSING"));
    }

    LogTo(fr::LogTopic::Safehouse, "fishing: {} at {}: {}", m_Scene, p_Stage, s_Found);

    if (s_Missing == kRoles && !s_LastChance) {
        LogTo(fr::LogTopic::Safehouse, "fishing: nothing found yet; trying again at eLoading_SceneStarted");
        return false;
    }

    if (!m_Entities[kSelector].load(std::memory_order_relaxed)) {
        LogTo(fr::LogTopic::Safehouse,
              "fishing: the RandomSelector {} is missing: nothing watched or changed (fishing stays vanilla)",
              IdText(fr::kFishingSelectorId));

        for (auto& s_Entity : m_Entities)
            s_Entity.store(nullptr, std::memory_order_relaxed);

        return true;
    }

    if (s_Missing > 0)
        LogTo(fr::LogTopic::Safehouse,
              "fishing: {} of {} entities missing; a choice without its entity is never weighted by the script",
              s_Missing, static_cast<size_t>(kRoles));

    return true;
}

// Each spawner's item (m_rMainItemKey -> m_RepositoryId), its title from
// pro.repo, and each choice's m_Weight address.
void FishingSpot::ReadItems() {
    std::map<fr::Guid, std::string> s_Titles;
    std::string s_RepoWhy;

    if (m_Repo.Ready(s_RepoWhy)) {
        std::vector<RepoItemEntry> s_Entries;
        size_t s_Overflow = 0;
        size_t s_Scanned = 0;

        if (const uint32_t s_Code = m_Repo.ScanItems(s_Entries, &s_Overflow, &s_Scanned))
            s_RepoWhy = "scan " + FaultText(s_Code);

        for (const RepoItemEntry& s_Entry : s_Entries)
            s_Titles.emplace(s_Entry.guid, s_Entry.title);
    }

    size_t s_Readable = 0;
    std::string s_Text;
    std::vector<fr::FishCatch> s_Items(fr::kFishingChoices, fr::FishCatch::Other);
    // Built here and stored under m_Mutex below: rolls on other threads read them.
    std::array<uintptr_t, fr::kFishingChoices> s_WeightAddress {};
    std::array<std::string, fr::kFishingChoices> s_ItemText {};

    for (size_t i = 0; i < fr::kFishingChoices; ++i) {
        ZEntityType** const s_Spawner = m_Entities[kSpawner0 + i].load(std::memory_order_relaxed);
        ZEntityType** const s_Choice = m_Entities[kChoice0 + i].load(std::memory_order_relaxed);
        std::string s_What;
        std::string s_WeightWhy;
        s_WeightAddress[i] = s_Choice ? Raw4Address(s_Choice, "m_Weight", "float32", &s_WeightWhy) : 0;

        if (!s_Choice)
            s_WeightWhy = "choice missing";

        if (!s_Spawner) {
            s_What = "spawner missing";
        }
        else {
            const RefRead s_Key = ReadEntityRefProperty(s_Spawner, "m_rMainItemKey");
            uint64_t s_KeyId = 0;
            std::string s_KeyClass;
            std::optional<fr::Guid> s_Repo;
            uint32_t s_Fault = 0;

            if (s_Key.entity) {
                ReadEntityClassName(s_Key.entity, s_KeyClass, &s_KeyId);
                s_Repo = ReadRepositoryId(ZEntityRef(s_Key.entity), &s_Fault);
            }

            std::string s_Title;

            if (s_Repo) {
                const auto s_It = s_Titles.find(*s_Repo);
                s_Title = s_It != s_Titles.end() ? s_It->second : std::string();
            }

            s_Items[i] = fr::ClassifyFishItem(s_Repo, s_KeyId, s_Title);
            s_Readable += s_Repo || s_KeyId ? 1 : 0;

            std::string s_KeyName;

            for (const fr::FishingKey& s_Known : fr::kFishingKeys) {
                if (s_Known.id == s_KeyId)
                    s_KeyName = fmt::format(" \"{}\"", s_Known.name);
            }

            s_What = s_Key.entity
                         ? fmt::format("key {} ({}){}: {} \"{}\" -> {}", IdText(s_KeyId), s_KeyClass, s_KeyName,
                                       s_Repo ? fr::FormatGuid(*s_Repo)
                                              : (s_Fault ? FaultText(s_Fault) : std::string("<no id>")),
                                       s_Title.empty() ? "?" : s_Title, fr::FishCatchName(s_Items[i]))
                         : fmt::format("m_rMainItemKey unreadable: {}", s_Key.why.empty() ? "null" : s_Key.why);
        }

        const std::optional<float> s_Weight = ReadFloatAt(s_WeightAddress[i]);
        s_ItemText[i] = s_What;
        s_Text += fmt::format("{}choice {} / spawner {}: {}; weight {}", i ? "; " : "", i + 1, i + 1, s_What,
                              s_Weight ? fmt::format("{}", *s_Weight) : "unreadable (" + s_WeightWhy + ")");
    }

    std::string s_Counts;

    for (const fr::FishCatch s_Catch : {fr::FishCatch::Fish, fr::FishCatch::FishingLine, fr::FishCatch::PirateSaber}) {
        size_t s_Count = 0;

        for (const fr::FishCatch s_Item : s_Items)
            s_Count += s_Item == s_Catch ? 1 : 0;

        s_Counts +=
            fmt::format("{}{} on {} choice(s)", s_Counts.empty() ? "" : ", ", fr::FishCatchName(s_Catch), s_Count);
    }

    LogTo(fr::LogTopic::Safehouse, "fishing: items{}: {}; {}",
          s_Titles.empty() ? fmt::format(" (no pro.repo titles: {})", s_RepoWhy) : "", s_Text, s_Counts);

    std::lock_guard s_Lock(m_Mutex);
    m_WeightAddress = s_WeightAddress;
    m_ItemText = std::move(s_ItemText);
    m_Items = s_Items;
    m_ItemsRead = s_Readable > 0;

    // A visit whose items were read late (at scene playing) re-creates its rolls with them.
    if (m_Rolls && m_Rolls->Rolls() == 0)
        m_Rolls.emplace(m_On, m_Catches, m_Items);
}

std::optional<size_t> FishingSpot::RoleOf(ZEntityType** p_Entity) const {
    if (!p_Entity)
        return std::nullopt;

    for (size_t i = 0; i < kRoles; ++i) {
        if (m_Entities[i].load(std::memory_order_relaxed) == p_Entity)
            return i;
    }

    return std::nullopt;
}

std::vector<std::optional<float>> FishingSpot::ReadWeights() const {
    std::vector<std::optional<float>> s_Out(fr::kFishingChoices);

    for (size_t i = 0; i < fr::kFishingChoices; ++i)
        s_Out[i] = ReadFloatAt(m_WeightAddress[i]);

    return s_Out;
}

std::string FishingSpot::WeightsText(const std::vector<std::optional<float>>& p_Weights) const {
    std::string s_Text = "[";

    for (size_t i = 0; i < p_Weights.size(); ++i)
        s_Text +=
            fmt::format("{}{}", i ? ", " : "", p_Weights[i] ? fmt::format("{}", *p_Weights[i]) : std::string("?"));

    return s_Text + "]";
}

void FishingSpot::BeginRoll(const char* p_Source) {
    if (!m_Rolls)
        return;

    const std::vector<std::optional<float>> s_Weights = ReadWeights();
    const fr::FishingRollStart s_Start = m_Rolls->Start(s_Weights, m_Now);

    if (!s_Start.opened)
        return;

    std::string s_Written;

    if (s_Start.steer) {
        m_Originals = s_Weights;
        m_Written = true;
        size_t s_Held = 0;

        for (size_t i = 0; i < s_Start.plan.weights.size(); ++i)
            s_Held += WriteFloatAt(m_WeightAddress[i], s_Start.plan.weights[i]) ? 1 : 0;

        s_Written =
            fmt::format("; weights written {} (read back {} of {}: {})", WeightsText(ReadWeights()), s_Held,
                        s_Start.plan.weights.size(), s_Held == s_Start.plan.weights.size() ? "held" : "NOT HELD");
    }

    LogTo(fr::LogTopic::Safehouse, "fishing: catch #{} rolls at {:.1f} s ({}): {}{}; game weights {}{}", s_Start.number,
          m_Now, p_Source,
          s_Start.wanted ? fmt::format("planned {}: ", fr::FishCatchName(*s_Start.wanted)) : std::string(), s_Start.why,
          WeightsText(s_Weights), s_Written);
}

void FishingSpot::Restore(const char* p_Why) {
    if (!m_Written)
        return;

    size_t s_Restored = 0;

    for (size_t i = 0; i < m_Originals.size() && i < fr::kFishingChoices; ++i) {
        if (m_Originals[i] && WriteFloatAt(m_WeightAddress[i], *m_Originals[i]))
            ++s_Restored;
    }

    LogTo(fr::LogTopic::Safehouse, "fishing: weights restored ({}): {} of {} written back: {}", p_Why, s_Restored,
          m_Originals.size(), WeightsText(ReadWeights()));
    m_Written = false;
    m_Originals.clear();
}

void FishingSpot::EndRoll(bool p_Done) {
    if (!m_Rolls || !m_Rolls->Open())
        return;

    const std::vector<std::optional<float>> s_AtAnswer = ReadWeights();
    const fr::FishingRollEnd s_End = p_Done ? m_Rolls->Done(m_Now) : m_Rolls->Failed(m_Now);

    if (!s_End.closed)
        return;

    const std::string s_Picked =
        s_End.picked ? fmt::format("choice {} ({})", *s_End.picked + 1,
                                   *s_End.picked < m_ItemText.size() ? m_ItemText[*s_End.picked] : std::string("?"))
                     : std::string("no OnPicked seen");

    LogTo(fr::LogTopic::Safehouse, "fishing: catch #{} {} at {:.1f} s: {}; picked {}; weights at the answer {}{}",
          s_End.number, p_Done ? "OnPickDone" : "OnPickFailed", m_Now, s_End.verdict, s_Picked, WeightsText(s_AtAnswer),
          s_End.counted ? fmt::format("; catches counted: {}", s_End.catches) : std::string());

    if (s_End.restore)
        Restore(p_Done ? "OnPickDone" : "OnPickFailed");

    if (s_End.counted) {
        m_Catches = s_End.catches;
        m_Summary += fmt::format("{}#{} {}", m_Summary.empty() ? "" : ", ", s_End.number, s_End.verdict);
    }

    m_RetryNow = s_End.retry;
}

void FishingSpot::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept {
    const std::optional<size_t> s_Role = RoleOf(p_Entity.m_pObj);

    if (!s_Role)
        return;

    Contained(kTag, "output pin", [&] {
        std::lock_guard s_Lock(m_Mutex);
        const size_t r = *s_Role;

        // Before the engine passes the timer's Out on to the selector's Pick.
        if (r == kTimer && p_PinId == fr::kPinOut)
            BeginRoll("TimerSimple 2 Out, before the selector's Pick");

        if (r >= kChoice0 && r < kSpawner0 && p_PinId == fr::kPinOnPicked && m_Rolls)
            m_Rolls->Picked(r - kChoice0);

        if (r == kSelector && (p_PinId == fr::kPinOnPickDone || p_PinId == fr::kPinOnPickFailed))
            EndRoll(p_PinId == fr::kPinOnPickDone);
    });
}

void FishingSpot::OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept {
    const std::optional<size_t> s_Role = RoleOf(p_Entity.m_pObj);

    if (!s_Role)
        return;

    Contained(kTag, "input pin", [&] {
        std::lock_guard s_Lock(m_Mutex);
        const size_t r = *s_Role;

        if (r == kSelector && p_PinId == fr::kPinPick)
            BeginRoll("the selector's Pick input");
    });
}

void FishingSpot::Tick(double p_Dt) noexcept {
    if (!m_PinsOn.load(std::memory_order_relaxed))
        return;

    Contained(kTag, "tick", [&] {
        bool s_Retry = false;

        {
            std::lock_guard s_Lock(m_Mutex);

            if (!m_Playing || !m_Rolls)
                return;

            m_Now += p_Dt;
            const fr::FishingRollEnd s_Timeout = m_Rolls->Tick(m_Now);

            if (s_Timeout.closed) {
                LogTo(fr::LogTopic::Safehouse, "fishing: catch #{}: {}", s_Timeout.number, s_Timeout.verdict);

                if (s_Timeout.restore)
                    Restore("timeout");
            }

            if (m_RetryNow && m_Rolls->TakeRetry()) {
                m_RetryNow = false;
                s_Retry = true;
                BeginRoll("the mod's re-pick");
            }
        }

        // Outside the lock: the selector answers within the call (OnPicked, OnPickDone come back here).
        if (s_Retry) {
            const uint32_t s_Code = SendInputPin(m_Entities[kSelector].load(std::memory_order_relaxed), fr::kPinPick);
            LogTo(fr::LogTopic::Safehouse, "fishing: the mod's re-pick: selector Pick {}",
                  s_Code ? FaultText(s_Code) : std::string("signaled"));
        }
    });
}
