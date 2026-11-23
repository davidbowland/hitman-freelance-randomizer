// Client requirements shuffle (see ClientReqShuffle.h). Every raw engine read
// and call follows the guard pattern in Guard.h.

#include "ClientReqShuffle.h"

#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "Merces.h" // fr::JsonEventName, fr::JsonScalarsWithKeyContaining, fr::kValueIntValueId
#include "LogTopics.h"
#include "ModLog.h"
#include "PinIds.h"
#include "EntityReads.h"
#include "Scene.h"
#include "SceneOf.h"
#include "WrappedText.h"

#include <Functions.h>
#include <Globals.h>

#include <Glacier/ZObject.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <exception>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr uint64_t kSafehouseBrick = 0x00A9B93A825F5BDC; // [assembly:/_pro/scenes/missions/snug/mission_vanilla.brick]
constexpr auto kFirstDelay = std::chrono::milliseconds(1'000);   // after a generation or a visit's first frame
constexpr auto kRetryDelay = std::chrono::milliseconds(2'000);   // before another walk
constexpr auto kPollInterval = std::chrono::milliseconds(1'000); // re-reads of the cached values
constexpr int kMaxWalksPerVisit = 3;                             // walks (and broken watches) per safehouse visit
constexpr auto kCheckWaitMs = std::chrono::milliseconds(30'000); // a check's wait for the campaign's values
constexpr int kSlots = 3;           // Mild<Loc>_ClientReq01..03 (the CpdSets never carry 04..08)
constexpr int kMaxCpdSetChecks = 6; // CpdSets without any written key before the check gives up

constexpr const char* kDefClass = "ZEvergreenBonusRequirementDefinitionEntity";
constexpr const char* kTerritoryClass = "ZEvergreenTerritoryStatusEntity";
constexpr const char* kChangedClass = "ZValueInt_Changed";

// The safehouse brick's CPD string helper the chosen campaign's CrimeSector
// output sets (CampaignDefinition .CrimeSector -> .Set), and the ValueInt_Set
// its CrimeSectorIndex goes to.
constexpr uint64_t kSectorNameHelperId = 0x258fc1d700fc7374; // helper_evergreen_cpd_string_CPD_CrimeSectorName
constexpr uint64_t kSectorIndexSetId = 0x576db455e8a95191;   // ValueInt_Set_CPDCrimeSectorIndex

constexpr std::array<std::string_view, 1> kCpdParts {"ClientReq"};
constexpr std::array<std::string_view, 1> kSectorParts {"CrimeSector"};

// The campaign entities of the safehouse brick (measured in game:
// ZEvergreenCampaignManager fcb531a06b3db083 with eight m_CrimeSectors; eight
// accessors "CampaignDefinition1..8", m_Index 0..7, and one more with -1; eight
// icons; four receivers that signal the chosen campaign's CrimeSectorIndex).
constexpr const char* kAccessorClass = "ZEvergreenCampaignDefinitionAccessor";
constexpr const char* kIconClass = "ZEvergreenCampaignIcon";
constexpr const char* kTextClass = "ZEvergreenCampaignText";
constexpr const char* kSectorClass = "ZEvergreenCrimeSectorDefinition";
constexpr const char* kManagerClass = "ZEvergreenCampaignManager";
constexpr const char* kReceiverClass = "ZEvergreenCampaignDefinitionReceiverEntity";
constexpr size_t kMaxCampaignEntities = 48;

// The ZEvergreenCampaignText entities named "CrimeSector_CrimeType_01..07"
// (07 twice) in the safehouse brick: eight texts, presumably one crime sector name per
// book. The class has 312 entities (the leaders' names too), so only these.
constexpr std::array<uint64_t, 8> kBookTextIds {
    0xba74fdd8837d2211ull, // CrimeSector_CrimeType_01
    0x3bc6314b0a25c2dcull, // CrimeSector_CrimeType_02
    0x3e544f58d6007cc7ull, // CrimeSector_CrimeType_03
    0xb9703186399668f3ull, // CrimeSector_CrimeType_04
    0x823db56e1fe71604ull, // CrimeSector_CrimeType_05
    0xb55ff782c0cb4d45ull, // CrimeSector_CrimeType_06
    0x976b700f79f81a67ull, // CrimeSector_CrimeType_07
    0x3f37cdaeb783bb07ull, // CrimeSector_CrimeType_07 (second)
};

constexpr size_t kMaxListedProperties = 48;

std::string DestinationText(int32_t p_Destination) {
    const std::string_view s_Name = fr::DestinationName(p_Destination);
    return s_Name.empty() ? fmt::format("destination {}", p_Destination) : std::string(s_Name);
}

std::string PicksText(const std::vector<int32_t>& p_Picks) {
    std::string s_Text;

    for (const int32_t s_Id : p_Picks)
        s_Text += fmt::format("{}{}", s_Text.empty() ? "" : ", ", s_Id ? fr::ClientReqLabel(s_Id) : "none");

    return s_Text;
}

// "Spotted, Sniper Kill" (names only, for the overlay's notes).
std::string NamesText(const std::vector<int32_t>& p_Ids) {
    std::string s_Text;

    for (const int32_t s_Id : p_Ids) {
        if (s_Id)
            s_Text += fmt::format("{}{}", s_Text.empty() ? "" : ", ", fr::ClientReqDisplayName(s_Id));
    }

    return s_Text.empty() ? std::string("none") : s_Text;
}

// "[-, 1, 2, 3, -]": each m_idsBonusRequirements entry's slot (- : not 1..3, ? : not readable).
std::string SlotOrderText(const std::vector<int>& p_Slots) {
    std::string s_Text;

    for (const int s_Slot : p_Slots)
        s_Text += fmt::format("{}{}", s_Text.empty() ? "" : ", ",
                              s_Slot > 0    ? std::to_string(s_Slot)
                              : s_Slot == 0 ? std::string("-")
                                            : std::string("?"));

    return "[" + s_Text + "]";
}

// "HawkesBay" -> "Hawkes Bay".
std::string PrettyDestination(int32_t p_Destination) {
    const std::string_view s_Name = fr::DestinationName(p_Destination);

    if (s_Name.empty())
        return fmt::format("destination {}", p_Destination);

    std::string s_Text;

    for (size_t i = 0; i < s_Name.size(); ++i) {
        if (i && s_Name[i] >= 'A' && s_Name[i] <= 'Z')
            s_Text += ' ';

        s_Text += s_Name[i];
    }

    return s_Text;
}

std::string QuotedList(const std::vector<std::string>& p_Values) {
    std::string s_Text;

    for (const std::string& s_Value : p_Values)
        s_Text += fmt::format("{}\"{}\"", s_Text.empty() ? "" : ", ", s_Value);

    return s_Text.empty() ? std::string("none") : s_Text;
}

// Wrapped text in the disabled color (ImGui::TextDisabled does not wrap).
void WrappedDisabled(const std::string& p_Text) {
    WrappedTextAvail(p_Text, &ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
}

std::string ValueText(const std::optional<int32_t>& p_Value) {
    return p_Value ? fmt::format("{}", *p_Value) : std::string("?");
}

struct IntWriteArgs {
    ZEntityType** entity;
    int32_t value;
    bool ok;
};

// The int's SetValue input pin (ZValueInt_Basic's), as a ValueInt_Set sends it.
__declspec(noinline) void SignalSetValueOf(IntWriteArgs* p_Args) {
    ZEntityRef(p_Args->entity).SignalInputPin(fr::kPinSetValue, ZObjectRef::From<int32_t>(p_Args->value));
    p_Args->ok = true;
}

__declspec(noinline) DWORD SafeSignalSetValue(IntWriteArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SignalSetValueOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// m_nValue through the engine's property setter, with change handlers.
__declspec(noinline) void SetValuePropertyOf(IntWriteArgs* p_Args) {
    ZEntityRef s_Entity(p_Args->entity);
    p_Args->ok = s_Entity.SetProperty(fr::kValueIntValueId, ZObjectRef::From<int32_t>(p_Args->value), true);
}

__declspec(noinline) DWORD SafeSetValueProperty(IntWriteArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SetValuePropertyOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// An int entity's m_nValue, read raw (a getter or another size: empty).
std::optional<int32_t> ReadIntValue(ZEntityType** p_Entity, std::string* p_Why = nullptr) {
    if (!p_Entity) {
        if (p_Why)
            *p_Why = "no entity";
        return std::nullopt;
    }

    const RawRead32 s_Read = ReadPropertyRaw32(ZEntityRef(p_Entity), "m_nValue");

    switch (s_Read.status) {
        case RawRead32::Status::Ok: return std::bit_cast<int32_t>(s_Read.bits);
        case RawRead32::Status::Missing:
            if (p_Why)
                *p_Why = "no m_nValue";
            break;
        case RawRead32::Status::NotRaw:
            if (p_Why)
                *p_Why = fmt::format("m_nValue has a getter or size {}", s_Read.typeSize);
            break;
        case RawRead32::Status::Fault:
            if (p_Why)
                *p_Why = FaultText(s_Read.faultCode);
            break;
    }

    return std::nullopt;
}

constexpr std::string_view kTag = "client reqs";

int64_t MsBetween(std::chrono::steady_clock::time_point p_From, std::chrono::steady_clock::time_point p_To) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(p_To - p_From).count();
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool IsRefType(std::string_view p_Type) {
    return p_Type == "ZEntityRef" || p_Type.starts_with("TEntityRef<");
}

// A raw ZString's text (at most 200 characters, unprintable ones as '?'), or empty.
std::optional<std::string> RawStringText(const TableProperty& p_Entry) {
    if (p_Entry.byteCount < 16 || p_Entry.typeName != "ZString")
        return std::nullopt;

    uint32_t s_Length = 0;
    uintptr_t s_Chars = 0;
    std::memcpy(&s_Length, p_Entry.bytes, 4);
    std::memcpy(&s_Chars, p_Entry.bytes + 8, 8);
    s_Length &= 0x3FFFFFFF;

    if (s_Length == 0)
        return std::string();

    if (s_Length > 200 || !s_Chars)
        return std::nullopt;

    std::string s_Text(s_Length, '\0');

    if (ReadEngineBytes(s_Chars, s_Text.data(), s_Length))
        return std::nullopt;

    for (char& c : s_Text)
        c = c >= 0x20 && c < 0x7F ? c : '?';

    return s_Text;
}

// A 4-byte number property's value (int32, uint32 or an enum), or empty.
std::optional<int32_t> NumberValue(const TableProperty& p_Entry) {
    const std::string_view s_Type(p_Entry.typeName);

    if (p_Entry.byteCount < 4 || p_Entry.typeSize != 4 || s_Type == "float32" || s_Type == "bool" || IsRefType(s_Type))
        return std::nullopt;

    int32_t s_Value = 0;
    std::memcpy(&s_Value, p_Entry.bytes, 4);
    return s_Value;
}

} // namespace

void ClientReqShuffle::Configure(bool p_On, std::set<int32_t> p_Exclude, fr::ClientReqMode p_Mode, int32_t p_PoolSize) {
    m_On = p_On;
    m_Exclude = std::move(p_Exclude);
    m_Mode = p_Mode;
    m_PoolSize = p_PoolSize;

    std::string s_Excluded;

    for (const int32_t s_Id : m_Exclude)
        s_Excluded += fmt::format("{}{}", s_Excluded.empty() ? "" : ", ", fr::ClientReqLabel(s_Id));

    Log("config: client_reqs {}, client_reqs_exclude [{}], client_reqs_mode {}, "
        "client_reqs_pool_size {}{}{}",
        m_On, s_Excluded, fr::ClientReqModeName(m_Mode), m_PoolSize,
        m_PoolSize <= 0 ? fmt::format(" (default {})", fr::kDefaultClientReqPoolSize) : std::string(),
        m_On ? "" : " (the game's picks and the planned ones are still logged when a campaign is chosen)");
}

void ClientReqShuffle::SetPersist(std::function<void(int32_t)> p_Persist) {
    m_Persist = std::move(p_Persist);
}

void ClientReqShuffle::SetCampaign(std::optional<int32_t> p_Seed, std::string p_CheckedText,
                                   std::function<void(int32_t)> p_PersistChecked) {
    m_CampaignSeed = p_Seed && *p_Seed ? p_Seed : std::nullopt;
    m_CheckedText = std::move(p_CheckedText);
    m_PersistChecked = std::move(p_PersistChecked);

    if (CheckDue())
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: campaign seed {} is not checked yet (client_reqs_checked \"{}\"): the next safehouse visit "
              "checks its locations for ids the rules never allow and rewrites those locations",
              *m_CampaignSeed, m_CheckedText);
}

bool ClientReqShuffle::CheckDue() const {
    return fr::ClientReqCheckDue(m_On, m_CampaignSeed, m_Pending.has_value(), m_CheckedText);
}

void ClientReqShuffle::MarkChecked(int32_t p_Seed) {
    m_CheckedText = p_Seed ? fmt::format("{}", p_Seed) : std::string();

    if (m_PersistChecked)
        m_PersistChecked(p_Seed);
}

void ClientReqShuffle::SetPending(int32_t p_Seed, bool p_ThisVisit) {
    const auto s_Now = std::chrono::steady_clock::now();
    m_Pending = Pending {p_Seed, p_ThisVisit};
    m_LoggedPicksAppeared = false;
    m_LoggedNotSafehouse = false;

    // A watch of this visit keeps its entities; its pick state starts over.
    if (m_Watch) {
        m_Watch->picks = fr::ClientReqPickWatch(p_ThisVisit);
        m_Watch->guard.reset();
        m_Watch->nextRead = s_Now + kFirstDelay;
        m_Watch->watchedSince = s_Now;
    }
    else {
        m_NextWalk = s_Now + kFirstDelay;
    }

    if (m_Persist)
        m_Persist(p_Seed);

    if (m_Watch)
        Preview(p_Seed);
    else
        SetOverlayStage(Overlay::Stage::Reading, p_Seed, {"Reading the safehouse (about a second after it loads)..."});
}

void ClientReqShuffle::Resume(int32_t p_Seed) noexcept {
    Contained(kTag, "resume", [&] {
        if (m_Pending || m_DoneSeeds.contains(p_Seed))
            return;

        m_Pending = Pending {p_Seed, false};
        SetOverlayStage(Overlay::Stage::Reading, p_Seed,
                        {"A campaign offered in an earlier session hasn't been chosen yet.",
                         "Its objectives appear here once the safehouse has been read."});
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: campaign seed {} was generated in an earlier session and no campaign pick was seen; "
              "waiting for the campaign pick at the next safehouse visit (picks already there then are left alone)",
              p_Seed);
    });
}

void ClientReqShuffle::OnCampaignGenerated(std::optional<int32_t> p_Seed) noexcept {
    Contained(kTag, "campaign generated", [&] {
        if (!p_Seed || *p_Seed == 0) {
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: campaign generated with an unknown seed; client requirements left as the game picks "
                  "them");
            return;
        }

        m_CampaignSeed = p_Seed;

        if (m_DoneSeeds.contains(*p_Seed)) {
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: campaign seed {} was already handled this session; not again", *p_Seed);
            return;
        }

        if (m_Pending && m_Pending->seed != *p_Seed)
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: campaign seed {} was never picked; replaced by seed {}",
                  m_Pending->seed, *p_Seed);

        SetPending(*p_Seed, true);
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: campaign seed {} generated: the game writes the picks when a campaign is chosen; the "
              "safehouse brick is walked once, then the picks are re-read until they appear{}",
              *p_Seed, m_On ? ", then replaced" : " (client_reqs off: logged only)");
    });
}

void ClientReqShuffle::OnLoadingStage(ESceneLoadingStage p_Stage) noexcept {
    if (p_Stage != ESceneLoadingStage::eLoading_ScenePlaying)
        return;

    m_ScenePlaying = true;
    m_NextWalk = std::chrono::steady_clock::now() + kFirstDelay;
}

void ClientReqShuffle::OnSceneClear() noexcept {
    Contained(kTag, "scene clear", [&] {
        const bool s_Watched = m_Watch.has_value();
        DropWatch();
        m_ScenePlaying = false;
        m_WalksThisVisit = 0;
        m_CheckedThisVisit = false;
        m_LoggedNotSafehouse = false;

        if (!m_Pending)
            return;

        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: scene cleared {} the pick of campaign seed {}; watched again at the next safehouse visit",
              s_Watched ? "while waiting for" : "before watching for", m_Pending->seed);
        m_Pending->thisVisit = false;
    });
}

void ClientReqShuffle::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept {
    ZEntityType** const s_Entity = p_Entity.m_pObj;

    if (!s_Entity)
        return;

    if (m_PinEntities.Lookup(s_Entity)) {
        m_DirtyPin.store(p_PinId, std::memory_order_relaxed);
        m_Dirty.store(true, std::memory_order_release);
    }
}

void ClientReqShuffle::PublishPins() {
    ClearPins();

    if (!m_Watch)
        return;

    const auto s_Add = [&](ZEntityType** p_Entity) {
        if (p_Entity)
            m_PinEntities.Watch(p_Entity, 1);
    };

    for (const Location& s_Location : m_Watch->locations) {
        for (const Slot& s_Slot : s_Location.slots) {
            s_Add(s_Slot.cpd);
            s_Add(s_Slot.changed);
            s_Add(s_Slot.mirror);
        }
    }

    m_Watch->entities = m_PinEntities.Count();
    m_Dirty.store(false, std::memory_order_relaxed);
    m_PinsOn.store(!m_PinEntities.Empty(), std::memory_order_release);
}

void ClientReqShuffle::ClearPins() noexcept {
    m_PinsOn.store(false, std::memory_order_release);

    m_PinEntities.Clear();

    m_Dirty.store(false, std::memory_order_relaxed);
}

void ClientReqShuffle::DropWatch() noexcept {
    ClearPins();
    m_Watch.reset();
}

void ClientReqShuffle::Finish(const char* p_Why, bool p_KeepWatch) {
    if (!m_Pending)
        return;

    m_DoneSeeds.insert(m_Pending->seed);
    LogTo(fr::LogTopic::CommonObjectives, "client reqs: campaign seed {}: done ({}); no longer waiting for a pick",
          m_Pending->seed, p_Why);
    m_Pending.reset();

    if (!p_KeepWatch)
        DropWatch();

    if (m_Persist)
        m_Persist(0);
}

void ClientReqShuffle::Tick() noexcept {
    if (!m_ScenePlaying)
        return;

    const bool s_Guarding = m_Watch && m_Watch->guard.has_value();

    if (!m_Pending && !s_Guarding && (m_CheckedThisVisit || !CheckDue()))
        return;

    const auto s_Now = std::chrono::steady_clock::now();

    if (!m_Watch && s_Now < m_NextWalk)
        return;

    if (m_Watch && s_Now < m_Watch->nextRead && !m_Dirty.load(std::memory_order_acquire))
        return;

    const bool s_Ok = Contained(kTag, "tick", [&] {
        const int32_t s_Seed = m_Pending ? m_Pending->seed : m_CampaignSeed.value_or(0);

        if (!m_Watch) {
            ZEntitySceneContext* s_Context = CurrentSceneContext();

            if (!s_Context) {
                m_NextWalk = s_Now + kRetryDelay;
                return;
            }

            const std::string_view s_Type = s_Context->m_SceneInitParameters.m_Type.ToStringView();
            const std::string s_Scene = SceneOf(s_Context);

            if (!fr::IsFreelancerScene(s_Type) || fr::IsFreelancerMission(s_Type, s_Scene)) {
                if (!m_LoggedNotSafehouse && m_Pending)
                    LogTo(
                        fr::LogTopic::CommonObjectives,
                        "client reqs: campaign seed {}: not in the Freelancer safehouse ({}); waiting for the campaign "
                        "pick at the next safehouse visit",
                        s_Seed, s_Scene);

                m_LoggedNotSafehouse = true;
                m_NextWalk = std::chrono::steady_clock::time_point::max(); // until the next scene plays
                return;
            }

            if (m_WalksThisVisit >= kMaxWalksPerVisit) {
                m_NextWalk = std::chrono::steady_clock::time_point::max();
                m_CheckedThisVisit = true;
                return;
            }

            ++m_WalksThisVisit;

            if (!Walk(s_Context)) {
                if (m_WalksThisVisit >= kMaxWalksPerVisit) {
                    LogTo(fr::LogTopic::CommonObjectives,
                          "client reqs: campaign seed {}: nothing to watch after {} walks; not walked again this visit "
                          "(watched again at the next safehouse visit)",
                          s_Seed, m_WalksThisVisit);

                    if (m_Pending)
                        SetOverlayStage(Overlay::Stage::Reading, s_Seed,
                                        {"The campaigns on offer could not be read this visit (details in the "
                                         "log file).",
                                         "No preview this time."});
                }

                m_NextWalk = s_Now + kRetryDelay;
                return;
            }

            // A check accepts values at its first read (they are the campaign's).
            m_Watch->picks = fr::ClientReqPickWatch(m_Pending ? m_Pending->thisVisit : true);
            m_Watch->watchedSince = std::chrono::steady_clock::now();
            m_Watch->nextRead = m_Watch->watchedSince;
            PublishPins();

            if (m_Pending)
                Preview(s_Seed);
        }

        const bool s_ByPin = m_Dirty.exchange(false, std::memory_order_acq_rel);
        const uint32_t s_Pin = m_DirtyPin.load(std::memory_order_relaxed);
        std::vector<int32_t> s_Flat;
        std::string s_Why;

        if (!ReadSlots(s_Flat, s_Why)) {
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: campaign seed {}: a cached value no longer reads ({}); {}", s_Seed, s_Why,
                  m_WalksThisVisit < kMaxWalksPerVisit ? "walking the safehouse brick again"
                                                       : "not walked again this visit");
            DropWatch();
            m_NextWalk = s_Now + kRetryDelay;
            return;
        }

        if (m_Watch->guard)
            TickGuard(s_Flat);
        else if (m_Pending)
            TickPick(s_Flat, s_ByPin, s_Pin);
        else
            TickCheck(s_Flat);
    });

    // A throw above must not run again every frame.
    if (!s_Ok) {
        DropWatch();
        ++m_WalksThisVisit;
        m_CheckedThisVisit = true;
        m_NextWalk = s_Now + kRetryDelay;
    }
}

void ClientReqShuffle::TickPick(const std::vector<int32_t>& p_Flat, bool p_ByPin, uint32_t p_Pin) {
    const int32_t s_Seed = m_Pending->seed;
    Watch& s_Watch = *m_Watch;
    const int s_ReadsBefore = s_Watch.picks.reads();
    const fr::ClientReqWatchStep s_Step = s_Watch.picks.OnRead(p_Flat, NowMs());
    const auto s_Waited = MsBetween(s_Watch.watchedSince, std::chrono::steady_clock::now());
    size_t s_Values = 0;

    for (const Location& s_Location : s_Watch.locations)
        for (const Slot& s_Slot : s_Location.slots)
            s_Values += s_Slot.cpd ? 1 : 0;

    switch (s_Step) {
        case fr::ClientReqWatchStep::Waiting:
            if (s_ReadsBefore == 0)
                LogTo(fr::LogTopic::CommonObjectives,
                      "client reqs: campaign seed {}: waiting for the campaign pick ({} locations, {} CPD values "
                      "re-read every {} ms and at the next frame after one of {} cached entities signals a pin; "
                      "the game writes the picks when a campaign is chosen)",
                      s_Seed, s_Watch.locations.size(), s_Values, kPollInterval.count(), s_Watch.entities);
            else if (m_LoggedPicksAppeared)
                LogTo(fr::LogTopic::CommonObjectives,
                      "client reqs: campaign seed {}: the picks went back to none; waiting again", s_Seed);

            m_LoggedPicksAppeared = false;
            s_Watch.nextRead = std::chrono::steady_clock::now() + kPollInterval;
            return;

        case fr::ClientReqWatchStep::AlreadyThere: {
            std::string s_Where;

            for (size_t l = 0; l < s_Watch.locations.size(); ++l) {
                std::vector<int32_t> s_Picks(p_Flat.begin() + l * kSlots, p_Flat.begin() + (l + 1) * kSlots);

                if (std::any_of(s_Picks.begin(), s_Picks.end(), [](int32_t p_Id) { return p_Id != 0; }))
                    s_Where += fmt::format("{}Mild{} {}", s_Where.empty() ? "" : "; ", s_Watch.locations[l].known->code,
                                           PicksText(s_Picks));
            }

            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: campaign seed {}: the locations already had picks when this safehouse visit's "
                  "watch began ({}): the campaign was chosen while the mod was not watching; left as they are (except "
                  "ids the rules never allow: see the check below)",
                  s_Seed, s_Where);
            SetOverlayStage(Overlay::Stage::Left, s_Seed,
                            {"The campaign was chosen before the mod could read it.",
                             "Its objectives stay as the game picked them (any that are never allowed are replaced)."});
            m_CampaignSeed = s_Seed;
            Finish("picks were there before the watch", true);

            if (m_OnPicked)
                m_OnPicked(s_Seed);

            // The check (if due) runs at the next read with this walk.
            if (CheckDue() && !m_CheckedThisVisit)
                m_Watch->nextRead = std::chrono::steady_clock::now();
            else
                DropWatch();

            return;
        }

        case fr::ClientReqWatchStep::Settling: {
            if (!m_LoggedPicksAppeared) {
                size_t s_With = 0;

                for (size_t l = 0; l < s_Watch.locations.size(); ++l)
                    s_With += std::any_of(p_Flat.begin() + l * kSlots, p_Flat.begin() + (l + 1) * kSlots,
                                          [](int32_t p_Id) { return p_Id != 0; })
                                  ? 1
                                  : 0;

                LogTo(fr::LogTopic::CommonObjectives,
                      "client reqs: campaign seed {}: picks appeared in {} locations after {} ms of watching ({}); "
                      "replaced once every slot holds for {} ms",
                      s_Seed, s_With, s_Waited,
                      p_ByPin ? fmt::format("seen at a pin signal, pin {:08x}", p_Pin) : "seen at a periodic read",
                      fr::kClientReqSettleMs);
                m_LoggedPicksAppeared = true;
            }

            const auto s_Due = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(std::max<int64_t>(1, s_Watch.picks.candidateSinceMs() +
                                                                                  fr::kClientReqSettleMs - NowMs()));
            s_Watch.nextRead = std::min(s_Due, std::chrono::steady_clock::now() + kPollInterval);
            return;
        }

        case fr::ClientReqWatchStep::Apply: {
            ClearPins(); // the writes below signal the same entities
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: campaign seed {}: picks held for {} ms; planning",
                  s_Seed, fr::kClientReqSettleMs);
            m_CampaignSeed = s_Seed;
            const ApplyOutcome s_Outcome = Apply(s_Seed, ApplyKind::Pick);
            Finish(m_On ? "picks replaced or kept, see above" : "picks logged", m_On);

            if (m_OnPicked)
                m_OnPicked(s_Seed);

            if (!m_On)
                return;

            if (s_Outcome.complete)
                MarkChecked(s_Seed);

            // The rest of the visit: the slots must keep what was written.
            m_Watch->guardSeed = s_Seed;
            m_Watch->guard.emplace(s_Outcome.flat);
            m_Watch->loggedGuardChange = false;
            m_Watch->nextRead = std::chrono::steady_clock::now() + kPollInterval;
            PublishPins();
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: campaign seed {}: watching the {} slots for the rest of this visit; if the game writes "
                  "them again, the plan is applied again once they hold for {} ms (at most {} times)",
                  s_Seed, s_Outcome.flat.size(), fr::kClientReqSettleMs, fr::kMaxClientReqRewrites);
            return;
        }
    }
}

void ClientReqShuffle::TickGuard(const std::vector<int32_t>& p_Flat) {
    Watch& s_Watch = *m_Watch;
    fr::ClientReqWriteGuard& s_Guard = *s_Watch.guard;
    const fr::ClientReqGuardStep s_Step = s_Guard.OnRead(p_Flat, NowMs());
    const auto s_Changed = [&] {
        std::string s_Text;

        for (size_t l = 0; l < s_Watch.locations.size(); ++l) {
            const std::vector<int32_t> s_Now(p_Flat.begin() + l * kSlots, p_Flat.begin() + (l + 1) * kSlots);
            const std::vector<int32_t> s_Was(s_Guard.expected().begin() + l * kSlots,
                                             s_Guard.expected().begin() + (l + 1) * kSlots);

            if (s_Now != s_Was)
                s_Text += fmt::format("{}Mild{} {} (written {})", s_Text.empty() ? "" : "; ",
                                      s_Watch.locations[l].known->code, PicksText(s_Now), PicksText(s_Was));
        }

        return s_Text;
    };

    switch (s_Step) {
        case fr::ClientReqGuardStep::Holding:
            s_Watch.loggedGuardChange = false;
            s_Watch.nextRead = std::chrono::steady_clock::now() + kPollInterval;
            return;

        case fr::ClientReqGuardStep::Settling:
            if (!s_Watch.loggedGuardChange)
                LogTo(fr::LogTopic::CommonObjectives,
                      "client reqs: campaign seed {}: the game changed written slots: {}; applying the plan again once "
                      "they hold for {} ms",
                      s_Watch.guardSeed, s_Changed(), fr::kClientReqSettleMs);

            s_Watch.loggedGuardChange = true;
            s_Watch.nextRead = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            return;

        case fr::ClientReqGuardStep::Rewrite: {
            ClearPins();
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: campaign seed {}: rewrite {} of {}: {}",
                  s_Watch.guardSeed, s_Guard.rewrites() + 1, fr::kMaxClientReqRewrites, s_Changed());
            const ApplyOutcome s_Outcome = Apply(s_Watch.guardSeed, ApplyKind::Rewrite);
            s_Guard.Rewritten(s_Outcome.flat);
            s_Watch.loggedGuardChange = false;
            s_Watch.nextRead = std::chrono::steady_clock::now() + kPollInterval;
            PublishPins();

            // Ids the rules never allow left behind: the next visit checks again.
            MarkChecked(s_Outcome.complete ? s_Watch.guardSeed : 0);
            return;
        }

        case fr::ClientReqGuardStep::GaveUp:
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: campaign seed {}: the game changed the slots again after {} rewrites: {}; left as they "
                  "are (the next visit's check repairs ids the rules never allow)",
                  s_Watch.guardSeed, s_Guard.rewrites(), s_Changed());
            MarkChecked(0); // the next visit checks again
            DropWatch();
            return;
    }
}

void ClientReqShuffle::TickCheck(const std::vector<int32_t>& p_Flat) {
    if (!CheckDue() || !m_CampaignSeed) {
        m_CheckedThisVisit = true;
        DropWatch();
        return;
    }

    const int32_t s_Seed = *m_CampaignSeed;
    Watch& s_Watch = *m_Watch;
    const int s_ReadsBefore = s_Watch.picks.reads();
    const auto s_Waited = MsBetween(s_Watch.watchedSince, std::chrono::steady_clock::now());

    // The campaign's values must be there (and hold) before they are judged.
    switch (s_Watch.picks.OnRead(p_Flat, NowMs())) {
        case fr::ClientReqWatchStep::Waiting:
            if (s_ReadsBefore == 0)
                LogTo(fr::LogTopic::CommonObjectives,
                      "client reqs: campaign seed {}: check: every slot reads 0 so far; waiting up to {} ms for the "
                      "campaign's values",
                      s_Seed, kCheckWaitMs.count());

            if (s_Waited >= kCheckWaitMs.count()) {
                LogTo(
                    fr::LogTopic::CommonObjectives,
                    "client reqs: campaign seed {}: check: every slot still reads 0 after {} ms (the safehouse did not "
                    "load the campaign's values); nothing checked, tried again at the next safehouse visit",
                    s_Seed, s_Waited);
                m_CheckedThisVisit = true;
                DropWatch();
                return;
            }

            s_Watch.nextRead = std::chrono::steady_clock::now() + kPollInterval;
            return;

        case fr::ClientReqWatchStep::AlreadyThere:
        case fr::ClientReqWatchStep::Settling:
            s_Watch.nextRead = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
            return;

        case fr::ClientReqWatchStep::Apply: break;
    }

    m_CheckedThisVisit = true;
    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: campaign seed {}: checking the campaign's locations for ids the rules never allow (not client "
          "requirements, or in client_reqs_exclude)",
          s_Seed);
    ClearPins();
    const ApplyOutcome s_Outcome = Apply(s_Seed, ApplyKind::Repair);

    if (s_Outcome.complete) {
        MarkChecked(s_Seed);
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: campaign seed {}: check done; client_reqs_checked = {} (not checked again)", s_Seed,
              s_Seed);
    }
    else {
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: campaign seed {}: check incomplete (see above); checked again at the next safehouse visit",
              s_Seed);
    }

    DropWatch();
}

bool ClientReqShuffle::Walk(ZEntitySceneContext* p_Context) {
    const auto s_Start = std::chrono::steady_clock::now();
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    std::vector<size_t> s_Order;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_Bricks[i].m_RuntimeResourceID.GetID() == kSafehouseBrick)
            s_Order.push_back(i);
    }

    if (s_Order.empty()) {
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: safehouse brick {:016X} not loaded; nothing read",
              kSafehouseBrick);
        return false;
    }

    // One walk of the safehouse brick: the definitions and locations by
    // class; every entity is kept for the mirrors' children.
    std::vector<ZEntityType**> s_Defs;
    std::vector<std::pair<ZEntityType**, uint64_t>> s_Territories;
    ZEntityType** s_NameHelper = nullptr;
    ZEntityType** s_IndexSet = nullptr;
    std::vector<std::pair<ZEntityType**, bool>> s_All;             // entity, is a ZValueInt_Changed
    std::vector<std::pair<ZEntityType**, std::string>> s_Campaign; // the books' entities (ReadBooks)
    std::string s_Class;

    const WalkStats s_Stats = VisitEntitiesInOrder(
        s_Bricks, s_Order,
        [&](const VisitedEntity& p_Visited) {
            uint64_t s_Id = 0;

            if (ReadEntityClassName(p_Visited.entity, s_Class, &s_Id))
                return false;

            s_All.emplace_back(p_Visited.entity, s_Class == kChangedClass);

            if (s_Id == kSectorNameHelperId)
                s_NameHelper = p_Visited.entity;
            else if (s_Id == kSectorIndexSetId)
                s_IndexSet = p_Visited.entity;

            const bool s_BookText = s_Class == kTextClass &&
                                    std::find(kBookTextIds.begin(), kBookTextIds.end(), s_Id) != kBookTextIds.end();

            if ((s_BookText || s_Class == kAccessorClass || s_Class == kIconClass || s_Class == kSectorClass ||
                 s_Class == kManagerClass || s_Class == kReceiverClass) &&
                s_Campaign.size() < kMaxCampaignEntities)
                s_Campaign.emplace_back(p_Visited.entity, s_Class);

            if (s_Class == kDefClass)
                s_Defs.push_back(p_Visited.entity);
            else if (s_Class == kTerritoryClass)
                s_Territories.emplace_back(p_Visited.entity, s_Id);
            else
                return false;

            return true;
        },
        kWideWalkEntityCap, nullptr);

    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: walked the safehouse brick: {} entities, {} faults{}: {} requirement definitions, {} "
          "territories",
          s_Stats.visited, s_Stats.faults, s_Stats.capped ? ", stopped at the entity cap" : "", s_Defs.size(),
          s_Territories.size());

    Watch s_Watch;
    s_Watch.context = p_Context;
    s_Watch.sectorNameHelper = s_NameHelper;

    if (s_IndexSet) {
        const RefRead s_Target = ReadEntityRefProperty(s_IndexSet, "m_rValueEntity");
        s_Watch.sectorIndexInt = s_Target.why.empty() ? s_Target.entity : nullptr;
    }

    // The definitions and their blacklists. Only client requirements count
    // (the prestige objectives 101..136 share the class). A destination
    // blacklist that cannot be read stops the writes (a pick might be
    // impossible there).
    std::unordered_map<ZEntityType**, std::optional<int32_t>> s_DestinationOf;
    std::set<int32_t> s_Ignored;

    for (ZEntityType** s_Entity : s_Defs) {
        fr::ClientReqDef s_Def;
        const RawRead32 s_Id = ReadPropertyRaw32(ZEntityRef(s_Entity), "m_id");

        if (s_Id.status != RawRead32::Status::Ok) {
            s_Watch.unreadable.push_back(fmt::format("{}: m_id not readable", EntityText(s_Entity)));
            continue;
        }

        s_Def.id = std::bit_cast<int32_t>(s_Id.bits);

        if (!fr::IsClientReqId(s_Def.id)) {
            s_Ignored.insert(s_Def.id);
            continue;
        }

        const RefArrayRead s_Destinations = ReadEntityRefArray(s_Entity, "m_destinationBlacklist", 64);

        if (!s_Destinations.why.empty())
            s_Watch.unreadable.push_back(
                fmt::format("{}: m_destinationBlacklist: {}", fr::ClientReqLabel(s_Def.id), s_Destinations.why));

        for (ZEntityType** s_Destination : s_Destinations.entities) {
            auto s_Cached = s_DestinationOf.find(s_Destination);

            if (s_Cached == s_DestinationOf.end()) {
                const RawRead32 s_Value = ReadPropertyRaw32(ZEntityRef(s_Destination), "m_eDestination");
                s_Cached = s_DestinationOf
                               .emplace(s_Destination, s_Value.status == RawRead32::Status::Ok
                                                           ? std::optional(std::bit_cast<int32_t>(s_Value.bits))
                                                           : std::nullopt)
                               .first;
            }

            if (s_Cached->second)
                s_Def.destinationBlacklist.push_back(*s_Cached->second);
            else
                s_Watch.unreadable.push_back(
                    fmt::format("{}: blacklisted destination {} has no readable m_eDestination",
                                fr::ClientReqLabel(s_Def.id), EntityText(s_Destination)));
        }

        if (!s_Def.destinationBlacklist.empty()) {
            std::string s_Dest;

            for (const int32_t s_Value : s_Def.destinationBlacklist)
                s_Dest += fmt::format("{}{}", s_Dest.empty() ? "" : ", ", DestinationText(s_Value));

            LogTo(fr::LogTopic::CommonObjectives, "client reqs: definition {} ({}): destinations blacklisted [{}]",
                  fr::ClientReqLabel(s_Def.id), EntityText(s_Entity), s_Dest);
        }

        s_Watch.defs.push_back(std::move(s_Def));
    }

    {
        std::set<int32_t> s_Ids;

        for (const auto& s_Def : s_Watch.defs)
            s_Ids.insert(s_Def.id);

        std::set<int32_t> s_Missing;

        for (const int32_t s_Id : fr::ClientReqIds()) {
            if (!s_Ids.contains(s_Id))
                s_Missing.insert(s_Id);
        }

        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: {} client requirement definitions, {} distinct ids [{}]{}; {} other definition ids ignored "
              "(not client requirements: prestige objectives and the like) [{}]",
              s_Watch.defs.size(), s_Ids.size(), fr::FormatClientReqIds(s_Ids),
              s_Missing.empty() ? "" : fmt::format(" (none for {})", fr::FormatClientReqIds(s_Missing)),
              s_Ignored.size(), fr::FormatClientReqIds(s_Ignored));
    }

    // The locations and their slots' mirrors, each told by its entity id (the
    // array is not in slot order).
    std::unordered_map<ZEntityType**, std::pair<size_t, int>> s_MirrorSlot;
    std::string s_Orders;

    for (const auto& [s_Entity, s_Id] : s_Territories) {
        const fr::ClientReqLocation* s_Known = fr::FindClientReqLocation(s_Id);

        if (!s_Known) {
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: territory {} is not in the location table; left alone",
                  EntityText(s_Entity));
            continue;
        }

        const RawRead32 s_Destination = ReadPropertyRaw32(ZEntityRef(s_Entity), "m_destination");
        const RefArrayRead s_Mirrors = ReadEntityRefArray(s_Entity, "m_idsBonusRequirements", 16);

        if (s_Destination.status != RawRead32::Status::Ok || !s_Mirrors.why.empty()) {
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: Mild{}: territory {}: m_destination {}, m_idsBonusRequirements {} ({} entries); left "
                  "alone",
                  s_Known->code, EntityText(s_Entity),
                  s_Destination.status == RawRead32::Status::Ok ? "read" : "not readable",
                  s_Mirrors.why.empty() ? "read" : s_Mirrors.why, s_Mirrors.entities.size());
            continue;
        }

        std::array<ZEntityType**, kSlots> s_BySlot {};
        std::vector<int> s_SlotOrder;
        bool s_Duplicate = false;

        for (ZEntityType** s_Mirror : s_Mirrors.entities) {
            uint64_t s_MirrorId = 0;

            if (!s_Mirror || ReadEntityClassName(s_Mirror, s_Class, &s_MirrorId)) {
                s_SlotOrder.push_back(-1);
                continue;
            }

            const int s_Slot = fr::ClientReqMirrorSlot(s_MirrorId);
            s_SlotOrder.push_back(s_Slot);

            if (s_Slot < 1 || s_Slot > kSlots)
                continue;

            if (s_BySlot[s_Slot - 1] && s_BySlot[s_Slot - 1] != s_Mirror)
                s_Duplicate = true;

            s_BySlot[s_Slot - 1] = s_Mirror;
        }

        s_Orders += fmt::format("{}{} {}", s_Orders.empty() ? "" : "; ", s_Known->code, SlotOrderText(s_SlotOrder));

        if (s_Duplicate ||
            std::any_of(s_BySlot.begin(), s_BySlot.end(), [](ZEntityType** p_Mirror) { return !p_Mirror; })) {
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: Mild{}: territory {}: the board values of slots 1-3 are not each found once among its "
                  "{} m_idsBonusRequirements entries (slots {}); left alone",
                  s_Known->code, EntityText(s_Entity), s_Mirrors.entities.size(), SlotOrderText(s_SlotOrder));
            continue;
        }

        Location s_Location {s_Known, s_Entity, std::bit_cast<int32_t>(s_Destination.bits), {}, {}};

        for (int i = 0; i < kSlots; ++i) {
            s_Location.slots[i].mirror = s_BySlot[i];
            s_MirrorSlot[s_BySlot[i]] = {s_Watch.locations.size(), i};
        }

        s_Watch.locations.push_back(s_Location);
    }

    LogTo(
        fr::LogTopic::CommonObjectives,
        "client reqs: m_idsBonusRequirements slot order per location (by the board values' entity ids; - is Req 04..08 "
        "or unknown): {}",
        s_Orders.empty() ? "none" : s_Orders);

    // Each mirror's CPD int: m_rValueEntity of its child that follows it (a
    // ZValueInt_Changed; another child only when no ZValueInt_Changed has one).
    struct CpdOf {
        ZEntityType** cpd;
        ZEntityType** child;
        bool fromChanged;
    };
    std::unordered_map<ZEntityType**, CpdOf> s_CpdOf; // by mirror
    std::unordered_set<ZEntityType**> s_Ambiguous;
    size_t s_ParentFaults = 0;

    for (const auto& [s_Entity, s_IsChanged] : s_All) {
        ZEntityType** s_Parent = nullptr;

        if (ReadLogicalParent(s_Entity, &s_Parent)) {
            ++s_ParentFaults;
            continue;
        }

        if (s_Parent && s_Parent == s_Watch.sectorNameHelper)
            s_Watch.sectorNameChildren.push_back(s_Entity);

        if (!s_Parent || !s_MirrorSlot.contains(s_Parent))
            continue;

        const RefRead s_Target = ReadEntityRefProperty(s_Entity, "m_rValueEntity");

        if (!s_Target.why.empty() || !s_Target.entity || s_Target.entity == s_Parent)
            continue;

        const auto s_Known = s_CpdOf.find(s_Parent);

        if (s_Known == s_CpdOf.end() || (!s_Known->second.fromChanged && s_IsChanged))
            s_CpdOf[s_Parent] = {s_Target.entity, s_Entity, s_IsChanged};
        else if (s_Known->second.cpd != s_Target.entity && s_Known->second.fromChanged == s_IsChanged)
            s_Ambiguous.insert(s_Parent);
    }

    if (s_ParentFaults)
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: {} logical parent reads faulted", s_ParentFaults);

    size_t s_Values = 0;
    std::vector<std::array<uint64_t, kSlots>> s_CpdIds;

    for (Location& s_Location : s_Watch.locations) {
        std::array<uint64_t, kSlots> s_Ids {};

        for (int i = 0; i < kSlots; ++i) {
            Slot& s_Slot = s_Location.slots[i];
            const auto s_Cpd = s_CpdOf.find(s_Slot.mirror);

            if (s_Ambiguous.contains(s_Slot.mirror))
                s_Slot.cpdWhy = "more than one CPD int";
            else if (s_Cpd == s_CpdOf.end())
                s_Slot.cpdWhy = "no CPD int found";
            else {
                s_Slot.cpd = s_Cpd->second.cpd;
                s_Slot.changed = s_Cpd->second.child;

                if (ReadEntityClassName(s_Slot.cpd, s_Class, &s_Slot.cpdId))
                    s_Slot.cpdId = 0;

                ++s_Values;
            }

            s_Ids[static_cast<size_t>(i)] = s_Slot.cpdId;

            if (!s_Slot.cpd)
                s_Location.notWritable = fmt::format("slot {}: {}", i + 1, s_Slot.cpdWhy);
        }

        s_CpdIds.push_back(s_Ids);
    }

    // Every location's helper is one template: slot N's CPD int has the same
    // entity id everywhere. An odd one out is a mapping error: never written.
    const std::set<size_t> s_Outliers = fr::ClientReqSlotOutliers(s_CpdIds);
    std::string s_IdsText;

    for (int i = 0; i < kSlots; ++i) {
        std::map<uint64_t, size_t> s_Counts;

        for (const auto& s_Ids : s_CpdIds)
            ++s_Counts[s_Ids[static_cast<size_t>(i)]];

        std::string s_Slot;

        for (const auto& [s_Id, s_Count] : s_Counts)
            s_Slot += fmt::format("{}{} x{}", s_Slot.empty() ? "" : ", ", IdText(s_Id), s_Count);

        const uint64_t s_Expected = fr::KnownClientReqCpdIntId(i + 1);
        s_IdsText += fmt::format("{}ClientReq0{}: {}{}", i ? "; " : "", i + 1, s_Slot,
                                 s_Expected ? fmt::format(" (seen before: {}{})", IdText(s_Expected),
                                                          s_Counts.contains(s_Expected) ? "" : ", NOT here")
                                            : std::string());
    }

    LogTo(fr::LogTopic::CommonObjectives, "client reqs: CPD int entity ids per slot: {}", s_IdsText);

    for (const size_t s_Index : s_Outliers) {
        Location& s_Location = s_Watch.locations[s_Index];

        if (s_Location.notWritable.empty())
            s_Location.notWritable = "its CPD ints are not the ones the other locations have";

        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: Mild{}: CPD ints {}, {}, {} differ from the other locations'; never written",
              s_Location.known->code, IdText(s_CpdIds[s_Index][0]), IdText(s_CpdIds[s_Index][1]),
              IdText(s_CpdIds[s_Index][2]));
    }

    const auto s_Ms = MsBetween(s_Start, std::chrono::steady_clock::now());

    if (s_Watch.locations.empty() || s_Values == 0) {
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: {} locations, {} CPD values found ({} ms); nothing to watch", s_Watch.locations.size(),
              s_Values, s_Ms);
        return false;
    }

    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: cached {} locations, {} of {} slots with a CPD value ({} ms); later reads use the cache",
          s_Watch.locations.size(), s_Values, s_Watch.locations.size() * kSlots, s_Ms);
    m_Watch = std::move(s_Watch);
    m_Watch->sectorNamesAtWalk = ReadSectorNameStrings();
    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: CPD_CrimeSectorName helper {} ({} sub-entities): strings at the walk: {}; "
          "CPDCrimeSectorIndex int {} = {}",
          EntityText(m_Watch->sectorNameHelper), m_Watch->sectorNameChildren.size(),
          QuotedList(m_Watch->sectorNamesAtWalk), EntityText(m_Watch->sectorIndexInt),
          ValueText(ReadIntValue(m_Watch->sectorIndexInt)));
    ReadBooks(s_Campaign);
    return true;
}

void ClientReqShuffle::ReadBooks(const std::vector<std::pair<ZEntityType**, std::string>>& p_Entities) {
    // The accessors' campaign slots (m_Index; -1 is the one that is no book).
    std::unordered_map<ZEntityType**, int32_t> s_SlotOf;
    std::map<int32_t, std::string> s_Accessors;

    for (const auto& [s_Entity, s_Class] : p_Entities) {
        if (s_Class != kAccessorClass)
            continue;

        const RawRead32 s_Index = ReadPropertyRaw32(ZEntityRef(s_Entity), "m_Index");

        if (s_Index.status != RawRead32::Status::Ok)
            continue;

        const int32_t s_Slot = std::bit_cast<int32_t>(s_Index.bits);
        s_Accessors[s_Slot] = EntityText(s_Entity);

        if (s_Slot >= 0 && s_Slot < fr::kMaxClientReqBooks)
            s_SlotOf[s_Entity] = s_Slot;
    }

    std::map<std::string, size_t> s_Counts;
    std::string s_Read;

    for (const auto& [s_Entity, s_Class] : p_Entities) {
        ++s_Counts[s_Class];
        const EntityPropertyTable s_List = ListEntityProperties(s_Entity, kMaxListedProperties);
        const StringProperties s_Strings = ReadStringProperties(s_Entity);
        ZEntityType** s_Parent = nullptr;
        const DWORD s_ParentCode = ReadLogicalParent(s_Entity, &s_Parent);

        // Only the book texts and icons name a book.
        if (s_Class != kTextClass && s_Class != kIconClass)
            continue;

        std::vector<fr::ClientReqBookProperty> s_Properties;

        for (const TableProperty& s_Entry : s_List.properties) {
            fr::ClientReqBookProperty s_Property {s_Entry.hasName ? s_Entry.name : fmt::format("{:08x}", s_Entry.id),
                                                  NumberValue(s_Entry), RawStringText(s_Entry), std::nullopt};

            if (IsRefType(s_Entry.typeName) && s_Entry.byteCount > 0) {
                const RefRead s_Ref = ReadEntityRefProperty(s_Entity, s_Entry.name);
                const auto s_Slot = s_Ref.entity ? s_SlotOf.find(s_Ref.entity) : s_SlotOf.end();

                if (s_Slot != s_SlotOf.end())
                    s_Property.refSlot = s_Slot->second;
            }

            s_Properties.push_back(std::move(s_Property));
        }

        for (const NamedString& s_String : s_Strings.strings) {
            if (s_String.read && s_String.throughGetter)
                s_Properties.push_back({s_String.name, std::nullopt, s_String.value, std::nullopt});
        }

        if (const auto s_Slot = s_Parent && !s_ParentCode ? s_SlotOf.find(s_Parent) : s_SlotOf.end();
            s_Slot != s_SlotOf.end())
            s_Properties.push_back({"<logical parent>", std::nullopt, std::nullopt, s_Slot->second});

        fr::ClientReqBook s_Book = fr::ReadClientReqBook(s_Properties);
        s_Read += fmt::format("{}{}: {}", s_Read.empty() ? "" : "; ", EntityText(s_Entity), s_Book.how);

        if (s_Book.slot || s_Book.type)
            m_Watch->books.push_back(std::move(s_Book));
    }

    std::string s_CountText;

    for (const auto& [s_Class, s_Count] : s_Counts)
        s_CountText += fmt::format("{}{} x{}", s_CountText.empty() ? "" : ", ", s_Class, s_Count);

    std::string s_AccessorText;

    for (const auto& [s_Slot, s_Text] : s_Accessors)
        s_AccessorText += fmt::format("{}{} {}", s_AccessorText.empty() ? "" : ", ", s_Slot, s_Text);

    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: books: campaign entities [{}]{}; accessors by m_Index [{}]; {} of the book texts and icons "
          "tell a slot or a crime sector ({})",
          s_CountText.empty() ? std::string("none") : s_CountText,
          p_Entities.size() >= kMaxCampaignEntities ? " (capped)" : "",
          s_AccessorText.empty() ? std::string("none") : s_AccessorText, m_Watch->books.size(),
          s_Read.empty() ? std::string("none read") : s_Read);
}

bool ClientReqShuffle::ReadSlots(std::vector<int32_t>& p_Flat, std::string& p_Why) {
    p_Flat.clear();

    for (Location& s_Location : m_Watch->locations) {
        for (int i = 0; i < kSlots; ++i) {
            Slot& s_Slot = s_Location.slots[i];
            const bool s_Before = s_Slot.cpdValue.has_value();
            std::string s_Why;
            s_Slot.cpdValue = s_Slot.cpd ? ReadIntValue(s_Slot.cpd, &s_Why) : std::nullopt;

            // A value that read at an earlier read and no longer does: the cache is stale.
            if (s_Before && !s_Slot.cpdValue) {
                p_Why = fmt::format("{}: {}", fr::ClientReqCpdKey(s_Location.known->code, i + 1), s_Why);
                return false;
            }

            if (s_Slot.cpd && !s_Slot.cpdValue)
                s_Slot.cpdWhy = s_Why;

            p_Flat.push_back(s_Slot.cpdValue.value_or(0));
        }
    }

    return true;
}

ClientReqShuffle::ApplyOutcome ClientReqShuffle::Apply(int32_t p_Seed, ApplyKind p_Kind) {
    const auto s_Start = std::chrono::steady_clock::now();
    Watch& s_Watch = *m_Watch;
    const char* const s_What = p_Kind == ApplyKind::Pick ? "pick" : p_Kind == ApplyKind::Rewrite ? "rewrite" : "repair";

    // The campaign's locations: those with picks in the CPD ints just read
    // (the mirrors only for the log).
    std::vector<fr::ClientReqSite> s_Sites;
    std::vector<size_t> s_SiteLocation;
    bool s_SlotsComplete = true;

    for (size_t l = 0; l < s_Watch.locations.size(); ++l) {
        Location& s_Location = s_Watch.locations[l];
        fr::ClientReqSite s_Site {fmt::format("Mild{}", s_Location.known->code), s_Location.destination, {}};
        std::string s_Detail;
        bool s_Complete = true;

        for (int i = 0; i < kSlots; ++i) {
            Slot& s_Slot = s_Location.slots[i];
            s_Slot.mirrorValue = ReadIntValue(s_Slot.mirror);

            if (!s_Slot.cpdValue)
                s_Complete = false;

            s_Site.gamePicks.push_back(s_Slot.cpdValue.value_or(0));
            s_Detail += fmt::format("{}slot {}: CPD int {} = {}{}, board mirror {} = {}", i ? "; " : "", i + 1,
                                    EntityText(s_Slot.cpd), ValueText(s_Slot.cpdValue),
                                    s_Slot.cpdValue ? "" : fmt::format(" ({})", s_Slot.cpdWhy),
                                    EntityText(s_Slot.mirror), ValueText(s_Slot.mirrorValue));
        }

        s_SlotsComplete &= s_Complete;
        const bool s_Any =
            std::any_of(s_Site.gamePicks.begin(), s_Site.gamePicks.end(), [](int32_t p_Id) { return p_Id != 0; });

        if (s_Any || !s_Complete) {
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: {} ({}): {} {}{}", s_Site.key,
                  DestinationText(s_Site.destination), p_Kind == ApplyKind::Pick ? "game picks" : "holds",
                  PicksText(s_Site.gamePicks), s_Complete ? "" : " (some slots not readable)");

            if (p_Kind != ApplyKind::Rewrite)
                LogTo(fr::LogTopic::CommonObjectives, "client reqs: {} entities: {}", s_Site.key, s_Detail);
        }

        if (s_Any) {
            s_Sites.push_back(std::move(s_Site));
            s_SiteLocation.push_back(l);
        }
    }

    // The campaign type: from the CPD_CrimeSectorName string the game set (the
    // name on the book), decided at the pick and kept for rewrites.
    if (p_Kind != ApplyKind::Rewrite) {
        const std::vector<std::string> s_Names = ReadSectorNameStrings();
        const std::optional<int32_t> s_Index = ReadIntValue(s_Watch.sectorIndexInt);
        const std::vector<std::string> s_Before =
            p_Kind == ApplyKind::Pick ? s_Watch.sectorNamesAtWalk : std::vector<std::string> {};
        s_Watch.type = fr::ResolveClientReqCampaignType(s_Before, s_Names, s_Index);
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: CPD_CrimeSectorName strings now: {}{}; CPDCrimeSectorIndex = {}", QuotedList(s_Names),
              p_Kind == ApplyKind::Pick ? fmt::format(" (at the walk: {})", QuotedList(s_Watch.sectorNamesAtWalk)) : "",
              ValueText(s_Index));
    }

    // Previewed: listed by this visit's preview (a check without a preview: a table type).
    const bool s_Previewed =
        s_Watch.type.name && (s_Watch.previewed.empty() ? fr::IsKnownCampaignTypeName(*s_Watch.type.name)
                                                        : s_Watch.previewed.contains(*s_Watch.type.name));

    if (s_Watch.type.name)
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: campaign type \"{}\" ({}); its pool is DialHash key \"{}\"{}", *s_Watch.type.name,
              s_Watch.type.how, fr::ClientReqCampaignKey(s_Watch.type.name),
              s_Previewed ? " (the one previewed for it)"
              : fr::IsKnownCampaignTypeName(*s_Watch.type.name)
                  ? " (NOT previewed: the books read at the walk did not include it)"
                  : " (a name not in the table: NOT previewed; add it to kCampaignTypes)");
    else
        LogTo(
            fr::LogTopic::CommonObjectives,
            "client reqs: campaign type unknown ({}); falling back to the seed-only pool (DialHash key \"{}\"), which "
            "was NOT previewed",
            s_Watch.type.how, fr::ClientReqCampaignKey(std::nullopt));

    // What to plan: every campaign location, or for a repair only those
    // holding an id the rules never allow.
    std::vector<fr::ClientReqSite> s_PlanSites;
    std::vector<size_t> s_PlanLocation; // index into s_Sites

    if (p_Kind == ApplyKind::Repair) {
        for (const size_t s_Index : fr::ClientReqSitesToRepair(s_Sites, m_Exclude)) {
            s_PlanSites.push_back(s_Sites[s_Index]);
            s_PlanLocation.push_back(s_Index);
        }

        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: repair: {} of {} campaign locations hold an id the rules never allow{}", s_PlanSites.size(),
              s_Sites.size(), s_PlanSites.empty() ? "; nothing to rewrite" : ", rewritten from the plan below");
    }
    else {
        s_PlanSites = s_Sites;

        for (size_t i = 0; i < s_Sites.size(); ++i)
            s_PlanLocation.push_back(i);
    }

    const fr::ClientReqCampaignPlan s_Campaign =
        fr::PlanClientReqTypeCampaign(p_Seed, s_Watch.defs, s_PlanSites, BaseOptions(), s_Watch.type.name);
    const std::vector<fr::ClientReqPlan>& s_Plans = s_Campaign.plans;

    for (const int32_t s_Id : s_Campaign.blocked)
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: {} is never picked in this campaign: it conflicts with one ranked before it",
              fr::ClientReqLabel(s_Id));

    if (s_Campaign.mode == fr::ClientReqMode::Campaign)
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: campaign pool ({} of {} campaign ids; size {}): {}",
              s_Campaign.pool.size(), s_Campaign.campaignAllowed, s_Campaign.poolSizeWhy, PicksText(s_Campaign.pool));
    else
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: client_reqs_mode mission: each location draws from the {} campaign ids",
              s_Campaign.campaignAllowed);

    for (const fr::ClientReqPlan& s_Plan : s_Plans) {
        if (s_Plan.fallback)
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: keeps its picks: {} ({} allowed)", s_Plan.key,
                  s_Plan.why, s_Plan.allowed);
        else if (s_Campaign.mode == fr::ClientReqMode::Campaign)
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: {}: planned {} (from {} pool ids allowed here{}; held {})", s_Plan.key,
                  PicksText(s_Plan.picks), s_Plan.fromPool,
                  s_Plan.toppedUp ? fmt::format(", {} topped up from outside the pool", s_Plan.toppedUp) : "",
                  PicksText(s_Plan.gamePicks));
        else
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: planned {} (from {} allowed ids; held {})",
                  s_Plan.key, PicksText(s_Plan.picks), s_Plan.allowed, PicksText(s_Plan.gamePicks));
    }

    // Notes for the overlay, by site key.
    std::map<std::string, std::vector<std::string>> s_Notes;

    for (const fr::ClientReqPlan& s_Plan : s_Plans) {
        if (s_Plan.fallback)
            s_Notes[s_Plan.key].push_back(fmt::format("Kept as the game picked them: only {} objectives are allowed "
                                                      "here for {} slots.",
                                                      s_Plan.allowed, s_Plan.needed));
        else if (s_Plan.toppedUp)
            s_Notes[s_Plan.key].push_back(
                fmt::format("{} from outside the list (this destination rules some out).", s_Plan.toppedUp));
    }

    // Writes (with client_reqs on and everything readable).
    std::string s_NotWritten;

    if (!m_On)
        s_NotWritten = "\"Shuffle common objectives\" is off.";
    else if (s_Watch.defs.empty() || !s_Watch.unreadable.empty()) {
        for (const std::string& s_Line : s_Watch.unreadable)
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: unreadable: {}", s_Line);

        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: definitions or blacklists not readable ({} definitions, {} problems); nothing written",
              s_Watch.defs.size(), s_Watch.unreadable.size());
        s_NotWritten = "The objectives could not be read (details in the log file).";
    }

    size_t s_Written = 0;
    size_t s_Kept = 0;
    std::map<std::string, std::pair<int32_t, int32_t>> s_Verify;

    for (size_t p = 0; p < s_Plans.size(); ++p) {
        const fr::ClientReqPlan& s_Plan = s_Plans[p];
        Location& s_Location = s_Watch.locations[s_SiteLocation[s_PlanLocation[p]]];

        if (s_Plan.fallback || s_Plan.picks == s_Plan.gamePicks) {
            ++s_Kept;
            continue;
        }

        if (!s_NotWritten.empty()) {
            s_Notes[s_Plan.key].push_back("Planned (not applied): " + NamesText(s_Plan.picks) + ".");
            ++s_Kept;
            continue;
        }

        // Every changed slot needs its CPD int; a location is written whole or not at all.
        bool s_Writable = s_Location.notWritable.empty();

        for (int i = 0; i < kSlots; ++i) {
            if (s_Plan.picks[i] != s_Plan.gamePicks[i] && (!s_Location.slots[i].cpd || !s_Location.slots[i].cpdValue))
                s_Writable = false;
        }

        if (!s_Writable) {
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: {}; keeps its picks", s_Plan.key,
                  s_Location.notWritable.empty() ? std::string("a slot's CPD int is unreadable")
                                                 : s_Location.notWritable);
            s_Notes[s_Plan.key].push_back("Not applied (details in the log file). Planned: " + NamesText(s_Plan.picks) +
                                          ".");
            ++s_Kept;
            continue;
        }

        std::vector<int> s_Done;
        bool s_Failed = false;

        for (int i = 0; i < kSlots && !s_Failed; ++i) {
            if (s_Plan.picks[i] == s_Plan.gamePicks[i])
                continue;

            if (WriteSlot(fr::ClientReqCpdKey(s_Location.known->code, i + 1), s_Location.slots[i], s_Plan.picks[i]))
                s_Done.push_back(i);
            else
                s_Failed = true;
        }

        if (s_Failed) {
            // Back to the held picks, so the location is never half changed.
            for (const int i : s_Done)
                WriteSlot(fr::ClientReqCpdKey(s_Location.known->code, i + 1), s_Location.slots[i], s_Plan.gamePicks[i]);

            LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: a write did not take; {} written slots set back",
                  s_Plan.key, s_Done.size());
            s_Notes[s_Plan.key].push_back(
                "Not applied (the game did not accept the change). Planned: " + NamesText(s_Plan.picks) + ".");
            ++s_Kept;
            continue;
        }

        ++s_Written;
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: now {} (was {}){}", s_Plan.key, PicksText(s_Plan.picks),
              PicksText(s_Plan.gamePicks), p_Kind == ApplyKind::Repair ? " (repair)" : "");

        if (p_Kind == ApplyKind::Repair)
            s_Notes[s_Plan.key].push_back("Replaced: it had " + NamesText(s_Plan.gamePicks) + ".");

        for (int i = 0; i < kSlots; ++i)
            s_Verify[fr::ClientReqCpdKey(s_Location.known->code, i + 1)] = {s_Plan.gamePicks[i], s_Plan.picks[i]};
    }

    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: campaign seed {} ({}): {} locations rewritten, {} kept{}; {} ms", p_Seed, s_What, s_Written,
          s_Kept, s_SlotsComplete ? "" : " (some slots were not readable)",
          MsBetween(s_Start, std::chrono::steady_clock::now()));

    // The outcome: every slot as it is now, and whether every campaign
    // location holds only allowed ids.
    ApplyOutcome s_Outcome;
    std::vector<fr::ClientReqSite> s_After;

    for (const Location& s_Location : s_Watch.locations) {
        fr::ClientReqSite s_Site {fmt::format("Mild{}", s_Location.known->code), s_Location.destination, {}};

        for (const Slot& s_Slot : s_Location.slots) {
            s_Outcome.flat.push_back(s_Slot.cpdValue.value_or(0));
            s_Site.gamePicks.push_back(s_Slot.cpdValue.value_or(0));
        }

        s_After.push_back(std::move(s_Site));
    }

    const std::vector<size_t> s_Invalid = fr::ClientReqSitesToRepair(s_After, m_Exclude);
    // Nothing read (every slot 0) proves nothing.
    s_Outcome.complete = m_On && s_SlotsComplete && s_Invalid.empty() && !s_Sites.empty();

    for (const size_t s_Index : s_Invalid)
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: {} still holds an id the rules never allow: {}",
              s_After[s_Index].key, PicksText(s_After[s_Index].gamePicks));

    // The overlay: every campaign location with what it holds now.
    Overlay s_Overlay;
    s_Overlay.stage = Overlay::Stage::Applied;
    s_Overlay.seed = p_Seed;
    s_Overlay.type = s_Watch.type.name ? *s_Watch.type.name : std::string("unknown");
    s_Overlay.pool = s_Campaign.mode == fr::ClientReqMode::Campaign ? s_Campaign.pool : std::vector<int32_t> {};

    if (!s_Watch.type.name)
        s_Overlay.notes.push_back("The campaign's type could not be read, so its list wasn't shown in "
                                  "advance.");
    else if (!s_Previewed)
        s_Overlay.notes.push_back("This campaign type's list wasn't shown in advance (the mod doesn't know its "
                                  "name).");

    if (p_Kind == ApplyKind::Repair)
        s_Overlay.notes.push_back(s_Written ? fmt::format("Replaced objectives that are never allowed in {} "
                                                          "location(s).",
                                                          s_Written)
                                            : std::string("Checked: every location's objectives are allowed."));
    else if (s_NotWritten.empty())
        s_Overlay.notes.push_back(
            fmt::format("{} location(s) changed, {} kept. The board now shows these.", s_Written, s_Kept));
    else
        s_Overlay.notes.push_back(s_NotWritten + " The board keeps the game's objectives (shown below).");

    for (size_t s_Site = 0; s_Site < s_Sites.size(); ++s_Site) {
        const Location& s_Location = s_Watch.locations[s_SiteLocation[s_Site]];
        OverlayLocation s_Shown;
        s_Shown.destination = PrettyDestination(s_Location.destination);

        for (const Slot& s_Slot : s_Location.slots)
            s_Shown.picks.push_back(s_Slot.cpdValue.value_or(0));

        s_Shown.notes = s_Notes[s_Sites[s_Site].key];
        s_Overlay.locations.push_back(std::move(s_Shown));
    }

    std::sort(s_Overlay.locations.begin(), s_Overlay.locations.end(),
              [](const OverlayLocation& a, const OverlayLocation& b) { return a.destination < b.destination; });
    PublishOverlay(std::move(s_Overlay));

    if (!s_Verify.empty()) {
        std::lock_guard s_Lock(m_VerifyMutex);

        // A rewrite adds to the keys still awaiting the save check.
        for (auto& [s_Key, s_Values] : s_Verify)
            m_Verify[s_Key] = s_Values;

        m_VerifyCpdSets = 0;
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: checking the next CpdSet for the {} written keys",
              m_Verify.size());
    }

    return s_Outcome;
}

// Writes one slot's CPD int and checks the board's mirror followed. True when
// the CPD int reads back the value.
bool ClientReqShuffle::WriteSlot(const std::string& p_Key, Slot& p_Slot, int32_t p_Value) {
    const std::optional<int32_t> s_Before = ReadIntValue(p_Slot.cpd);
    IntWriteArgs s_Args {p_Slot.cpd, p_Value, false};
    std::string s_How = "SetValue";

    if (const DWORD s_Code = SafeSignalSetValue(&s_Args))
        Log("client reqs: {}: SetValue on the CPD int faulted ({})", p_Key, FaultText(s_Code));

    std::optional<int32_t> s_After = ReadIntValue(p_Slot.cpd);

    if (s_After != p_Value) {
        s_Args.ok = false;
        s_How = "m_nValue property";

        if (const DWORD s_Code = SafeSetValueProperty(&s_Args))
            Log("client reqs: {}: m_nValue write on the CPD int faulted ({})", p_Key, FaultText(s_Code));

        s_After = ReadIntValue(p_Slot.cpd);
    }

    if (s_After != p_Value) {
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: CPD int {} {} -> {}: not taken (reads {})", p_Key,
              EntityText(p_Slot.cpd), ValueText(s_Before), p_Value, ValueText(s_After));
        return false;
    }

    p_Slot.cpdValue = s_After;
    std::optional<int32_t> s_Mirror = ReadIntValue(p_Slot.mirror);
    std::string s_MirrorHow = "followed";

    if (s_Mirror != p_Value) {
        IntWriteArgs s_MirrorArgs {p_Slot.mirror, p_Value, false};

        if (const DWORD s_Code = SafeSignalSetValue(&s_MirrorArgs))
            Log("client reqs: {}: SetValue on the board mirror faulted ({})", p_Key, FaultText(s_Code));

        s_Mirror = ReadIntValue(p_Slot.mirror);
        s_MirrorHow = s_Mirror == p_Value ? "set with SetValue" : "NOT updated";
    }

    p_Slot.mirrorValue = s_Mirror;
    LogTo(fr::LogTopic::CommonObjectives, "client reqs: {}: {} -> {} ({}) written with {}; board mirror {} {}", p_Key,
          ValueText(s_Before), fr::ClientReqLabel(p_Value), EntityText(p_Slot.cpd), s_How, ValueText(s_Mirror),
          s_MirrorHow);
    return true;
}

void ClientReqShuffle::OnEventSent(uint32_t p_EventIndex, const ZDynamicObject& p_Event) noexcept {
    Contained(kTag, "sent event", [&] {
        std::lock_guard s_Lock(m_VerifyMutex);

        if (m_Verify.empty())
            return;

        ZString s_Json;
        // ZDynamicObject_ToString takes a non-const pointer and only reads it.
        Functions::ZDynamicObject_ToString->Call(const_cast<ZDynamicObject*>(&p_Event), s_Json);
        const std::string_view s_Text = s_Json.ToStringView();

        if (fr::JsonEventName(s_Text) != "CpdSet")
            return;

        size_t s_Saved = 0;
        size_t s_Present = 0;
        std::string s_Others;

        for (const fr::JsonScalar& s_Scalar : fr::JsonScalarsWithKeyContaining(s_Text, kCpdParts)) {
            const auto s_Written = m_Verify.find(std::string(s_Scalar.key));

            if (s_Written == m_Verify.end()) {
                // The showdown's picks (Hot_ClientReq0N) and the other locations', for the record.
                if (s_Scalar.key.starts_with("Hot_ClientReq0") && s_Scalar.value != "0.000000")
                    s_Others += fmt::format("{}{} {}", s_Others.empty() ? "" : ", ", s_Scalar.key, s_Scalar.value);

                continue;
            }

            ++s_Present;
            const std::optional<int32_t> s_Value = fr::ParseCpdInt(s_Scalar.value);
            const auto [s_Game, s_Ours] = s_Written->second;
            const bool s_Ok = s_Value == s_Ours;
            s_Saved += s_Ok ? 1 : 0;

            LogTo(fr::LogTopic::CommonObjectives, "client reqs: save check (CpdSet #{}): {} = {}: {}", p_EventIndex,
                  s_Scalar.key, s_Scalar.value,
                  s_Ok                ? fmt::format("written {}, saved", fr::ClientReqLabel(s_Ours))
                  : s_Value == s_Game ? fmt::format("the GAME'S pick {}, not the written {}: NOT saved",
                                                    fr::ClientReqLabel(s_Game), fr::ClientReqLabel(s_Ours))
                                      : fmt::format("neither the written {} nor the game's {}", s_Ours, s_Game));
        }

        if (s_Present == 0) {
            if (++m_VerifyCpdSets >= kMaxCpdSetChecks) {
                LogTo(fr::LogTopic::CommonObjectives,
                      "client reqs: save check: {} CpdSets without the written keys; check stopped", m_VerifyCpdSets);
                m_Verify.clear();
            }

            return;
        }

        for (const fr::JsonScalar& s_Scalar : fr::JsonScalarsWithKeyContaining(s_Text, kSectorParts))
            LogTo(fr::LogTopic::CommonObjectives, "client reqs: save check (CpdSet #{}): {} {}", p_EventIndex,
                  s_Scalar.key, s_Scalar.value);

        if (!s_Others.empty())
            LogTo(fr::LogTopic::CommonObjectives,
                  "client reqs: save check (CpdSet #{}): showdown picks (not changed): {}", p_EventIndex, s_Others);

        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: save check (CpdSet #{}): {} of {} written keys present, {} saved as written{}",
              p_EventIndex, s_Present, m_Verify.size(), s_Saved,
              s_Saved == m_Verify.size() ? ""
                                         : "; the missions read the saved values, so check the board against them");
        m_Verify.clear();
    });
}

fr::ClientReqOptions ClientReqShuffle::BaseOptions() const {
    fr::ClientReqOptions s_Options;
    s_Options.excluded = m_Exclude;
    s_Options.mode = m_Mode;
    s_Options.poolSize = m_PoolSize;
    return s_Options;
}

std::vector<std::string> ClientReqShuffle::ReadSectorNameStrings() const {
    std::vector<std::string> s_Names;

    if (!m_Watch || !m_Watch->sectorNameHelper)
        return s_Names;

    std::vector<ZEntityType**> s_Entities {m_Watch->sectorNameHelper};
    s_Entities.insert(s_Entities.end(), m_Watch->sectorNameChildren.begin(), m_Watch->sectorNameChildren.end());

    for (ZEntityType** s_Entity : s_Entities) {
        for (const NamedString& s_String : ReadStringProperties(s_Entity).strings) {
            if (s_String.read && !s_String.value.empty())
                s_Names.push_back(s_String.value);
        }
    }

    return s_Names;
}

void ClientReqShuffle::Preview(int32_t p_Seed) {
    if (!m_Watch)
        return;

    const Watch& s_Watch = *m_Watch;
    Overlay s_Overlay;
    s_Overlay.stage = Overlay::Stage::Preview;
    s_Overlay.seed = p_Seed;

    if (m_Mode != fr::ClientReqMode::Campaign) {
        s_Overlay.notes = {"\"Pick from\" is set to \"Every allowed objective\": each location picks from all the "
                           "objectives you allow, so there is no list per campaign type."};
        LogTo(fr::LogTopic::CommonObjectives,
              "client reqs: preview: campaign seed {}: client_reqs_mode mission, no pools", p_Seed);
        PublishOverlay(std::move(s_Overlay));
        return;
    }

    const fr::ClientReqPreviewOrder s_Order = fr::OrderClientReqPreview(s_Watch.books);
    m_Watch->previewed.clear();

    for (const fr::ClientReqTypePreview& s_Preview :
         fr::PreviewClientReqCampaignTypes(p_Seed, s_Watch.defs, BaseOptions(), s_Watch.books)) {
        LogTo(fr::LogTopic::CommonObjectives, "client reqs: preview: {}{}: {} (pool {} of {}; size {})",
              s_Preview.label, s_Preview.slot ? fmt::format(" (book slot {})", *s_Preview.slot) : std::string(),
              PicksText(s_Preview.plan.pool), s_Preview.plan.pool.size(), s_Preview.plan.campaignAllowed,
              s_Preview.plan.poolSizeWhy);
        s_Overlay.types.push_back({s_Preview.label, s_Preview.plan.pool});
        m_Watch->previewed.insert(s_Preview.name);
    }

    s_Overlay.notes = {"The campaign you choose takes its objectives from the list for its type (the "
                       "name on the book, e.g. Big Pharma).",
                       s_Order.byBooks ? "The books on offer, in the game's campaign order."
                                       : "All eight campaign types (the mod couldn't tell which books are on "
                                         "offer): find the name on your book.",
                       "A destination can rule some out; that location then takes others.",
                       "If you leave without choosing, these lists change."};

    if (!m_On)
        s_Overlay.notes.push_back("\"Shuffle common objectives\" is off: the game's objectives stay.");
    else if (s_Watch.defs.empty() || !s_Watch.unreadable.empty())
        s_Overlay.notes.push_back("Some objectives could not be read, so nothing will change (details in the log "
                                  "file).");

    LogTo(fr::LogTopic::CommonObjectives,
          "client reqs: preview: campaign seed {}: {} campaign types previewed ({}); the chosen campaign's locations "
          "draw from its type's pool",
          p_Seed, s_Overlay.types.size(), s_Order.why);
    PublishOverlay(std::move(s_Overlay));
}

void ClientReqShuffle::PublishOverlay(Overlay p_Overlay) {
    std::lock_guard s_Lock(m_OverlayMutex);
    m_Overlay = std::move(p_Overlay);
}

void ClientReqShuffle::SetOverlayStage(Overlay::Stage p_Stage, int32_t p_Seed, std::vector<std::string> p_Notes) {
    Overlay s_Overlay;
    s_Overlay.stage = p_Stage;
    s_Overlay.seed = p_Seed;
    s_Overlay.notes = std::move(p_Notes);
    PublishOverlay(std::move(s_Overlay));
}

bool ClientReqShuffle::Fold(const std::string& p_Label, const std::string& p_Id, bool p_DefaultOpen) {
    bool& s_Open = m_FoldOpen.try_emplace(p_Id, p_DefaultOpen).first->second;
    ImGui::SetNextItemOpen(s_Open);
    const bool s_Now = ImGui::TreeNodeEx(fmt::format("{}###clientreqs-{}", p_Label, p_Id).c_str(),
                                         ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth);

    if (s_Now != s_Open && m_HeaderDebounce.Accept(ImGui::GetTime()))
        s_Open = s_Now;

    return s_Open;
}

// The overlay's client requirements section: a collapsing header (open by
// default, toggled with the tap debounce); before the pick every campaign
// type's list, always unfolded, one objective per line; after it the campaign type and
// one block per location. Notes are short lines of their own.
bool ClientReqShuffle::HasOverlay() {
    std::lock_guard s_Lock(m_OverlayMutex);
    return m_Overlay.stage != Overlay::Stage::None;
}

void ClientReqShuffle::DrawOverlay() {
    Overlay s_Overlay;
    {
        std::lock_guard s_Lock(m_OverlayMutex);

        if (m_Overlay.stage == Overlay::Stage::None)
            return;

        s_Overlay = m_Overlay;
    }

    const char* s_Title = s_Overlay.stage == Overlay::Stage::Applied ? "Common objectives of this campaign"
                                                                     : "Common objectives of the next campaign";
    // A fold-out group of the Status tab's Payouts & objectives section.
    ImGui::SetNextItemOpen(m_HeaderOpen);
    const bool s_HeaderOpen = ImGui::CollapsingHeader(fmt::format("{}###clientreqs", s_Title).c_str());

    if (s_HeaderOpen != m_HeaderOpen && m_HeaderDebounce.Accept(ImGui::GetTime()))
        m_HeaderOpen = s_HeaderOpen;

    if (!m_HeaderOpen)
        return;

    const auto s_Bullets = [](const std::vector<int32_t>& p_Ids) {
        bool s_Any = false;

        for (const int32_t s_Id : p_Ids) {
            if (!s_Id)
                continue;

            ImGui::BulletText("%s", fr::ClientReqDisplayName(s_Id).c_str());
            s_Any = true;
        }

        if (!s_Any)
            WrappedDisabled("No objectives.");
    };

    switch (s_Overlay.stage) {
        case Overlay::Stage::None: return;

        case Overlay::Stage::Reading:
        case Overlay::Stage::Left:
            for (const std::string& s_Note : s_Overlay.notes)
                WrappedTextAvail(s_Note);

            return;

        case Overlay::Stage::Preview:
            for (const std::string& s_Note : s_Overlay.notes)
                WrappedDisabled(s_Note);

            // Every type's list is always shown, never folded: picking a
            // campaign means comparing them all.
            for (const OverlayType& s_Type : s_Overlay.types) {
                ImGui::SeparatorText(fmt::format("{} ({})", s_Type.name, s_Type.pool.size()).c_str());
                s_Bullets(s_Type.pool);
            }

            return;

        case Overlay::Stage::Applied:
            WrappedTextAvail(fmt::format("Campaign type: {}", s_Overlay.type));

            for (const std::string& s_Note : s_Overlay.notes)
                WrappedDisabled(s_Note);

            if (!s_Overlay.pool.empty() &&
                Fold(fmt::format("The campaign's list ({})", s_Overlay.pool.size()), "applied-list", false))
                s_Bullets(s_Overlay.pool);

            for (const OverlayLocation& s_Location : s_Overlay.locations) {
                ImGui::SeparatorText(s_Location.destination.c_str());
                s_Bullets(s_Location.picks);

                for (const std::string& s_Note : s_Location.notes)
                    WrappedDisabled(s_Note);
            }

            return;
    }
}
