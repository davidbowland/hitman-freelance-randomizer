// Official map variants (see Variants.h).

#include "Variants.h"

#include "../core/DianaBriefing.h"
#include "../core/MissionStories.h"
#include "../core/SceneKinds.h" // fr::MissionBrickPath, fr::RoleFromActorName
#include "../core/VariantComments.h"
#include "ActorReads.h"
#include "BlueprintRead.h"
#include "BriefingGraphs.h"
#include "EngineText.h"
#include "EntityWalk.h"
#include "Guard.h"
#include "LoadFaultWatch.h"
#include "LogTopics.h"
#include "EntityReads.h"
#include "ModLog.h"
#include "PinIds.h"
#include "Scene.h"
#include "SceneOf.h"
#include "SettingsTable.h"
#include "VariantDamage.h"
#include "VariantMarkers.h"
#include "WrappedText.h"

#include <Globals.h>
#include <IModSDK.h>
#include <imgui.h>

#include <Glacier/Hash.h>
#include <Glacier/TArray.h>
#include <Glacier/ZActor.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZTypeRegistry.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZString.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr size_t kMaxBricks = 64;     // additional bricks read (a mission has 1 to 3)
constexpr size_t kMaxPathChars = 511; // characters read per brick path
constexpr size_t kMaxChunks = 16;     // chunk indices read per resource
constexpr size_t kMaxCheckLines = 40; // problem lines per check

// The overlay's name for an option: its chance setting's label without the
// place ("The Source"), or "normal map" for none.
std::string ShownVariant(const fr::VariantOption* p_Option) {
    if (!p_Option)
        return "normal map";

    if (const fr::SettingInfo* s_Info = fr::FindSetting(fr::VariantChanceKey(*p_Option))) {
        const std::string_view s_Label = s_Info->label;
        const size_t s_Colon = s_Label.find(": ");
        return std::string(s_Colon == std::string_view::npos ? s_Label : s_Label.substr(s_Colon + 2));
    }

    return std::string(fr::VariantName(p_Option));
}

// Raw-pointer steps follow the guard pattern in Guard.h.

__declspec(noinline) void BrickCountOf(const TArray<ZString>* p_Bricks, size_t* p_Out) {
    *p_Out = p_Bricks->size();
}

__declspec(noinline) DWORD SafeBrickCount(const TArray<ZString>* p_Bricks, size_t* p_Out) {
    DWORD s_Code = 0;

    __try {
        BrickCountOf(p_Bricks, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct BrickTextOut {
    char text[kMaxPathChars + 1];
    size_t size; // the full length (more than kMaxPathChars when truncated)
};

__declspec(noinline) void BrickTextOf(const TArray<ZString>* p_Bricks, size_t p_Index, BrickTextOut* p_Out) {
    const std::string_view s_Text = p_Bricks->begin()[p_Index].ToStringView();
    const size_t s_Count = std::min(s_Text.size(), kMaxPathChars);
    std::memcpy(p_Out->text, s_Text.data(), s_Count);
    p_Out->text[s_Count] = '\0';
    p_Out->size = s_Text.size();
}

__declspec(noinline) DWORD SafeBrickText(const TArray<ZString>* p_Bricks, size_t p_Index, BrickTextOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        BrickTextOf(p_Bricks, p_Index, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

struct ChunksOut {
    size_t count; // as the SDK lists them (more than kMaxChunks: only the first are read)
    uint32_t indices[kMaxChunks];
    bool mounted[kMaxChunks];
};

// The SDK builds its resource-to-chunk map from the game's packages on first
// use; an empty list means the resource is in no chunk (or the map failed).
__declspec(noinline) void ChunksOf(uint64_t p_Rid, ChunksOut* p_Out) {
    const TArray<uint32_t>& s_Chunks = SDK()->GetChunkIndicesForRuntimeResourceId(ZRuntimeResourceID(p_Rid));
    p_Out->count = s_Chunks.size();

    for (size_t i = 0; i < std::min(p_Out->count, kMaxChunks); ++i) {
        p_Out->indices[i] = s_Chunks.begin()[i];
        p_Out->mounted[i] = SDK()->IsChunkMounted(p_Out->indices[i]);
    }
}

__declspec(noinline) DWORD SafeChunks(uint64_t p_Rid, ChunksOut* p_Out) {
    DWORD s_Code = 0;

    __try {
        ChunksOf(p_Rid, p_Out);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

__declspec(noinline) void MountChunkOf(uint32_t p_Chunk, bool* p_Mounted) {
    SDK()->MountChunk(p_Chunk);
    *p_Mounted = SDK()->IsChunkMounted(p_Chunk);
}

__declspec(noinline) DWORD SafeMountChunk(uint32_t p_Chunk, bool* p_Mounted) {
    DWORD s_Code = 0;

    __try {
        MountChunkOf(p_Chunk, p_Mounted);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// An owning copy (allocated by the game's allocator, refcounted), never the
// std::string_view constructor, which keeps pointing at the caller's buffer.
__declspec(noinline) void AppendBrickOf(TArray<ZString>* p_Bricks, const char* p_Path, uint32_t p_Size) {
    p_Bricks->push_back(ZString::AllocateFromCStr(p_Path, p_Size));
}

__declspec(noinline) DWORD SafeAppendBrick(TArray<ZString>* p_Bricks, const char* p_Path, uint32_t p_Size) {
    DWORD s_Code = 0;

    __try {
        AppendBrickOf(p_Bricks, p_Path, p_Size);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

// --- A variant's own scene instead of appended bricks (the full variants) ---

struct SwapSceneArgs {
    SSceneInitParameters* parameters;
    std::string_view scene;
    const std::string_view* bricks;
    size_t count;
};

// Owning copies, as in AppendBrickOf. The old scene string is left to the
// engine (assigning releases it).
__declspec(noinline) void SwapSceneOf(const SwapSceneArgs* p_Args) {
    p_Args->parameters->m_SceneResource =
        ZString::AllocateFromCStr(p_Args->scene.data(), static_cast<uint32_t>(p_Args->scene.size()));
    p_Args->parameters->m_aAdditionalBrickResources.clear();

    for (size_t i = 0; i < p_Args->count; ++i)
        p_Args->parameters->m_aAdditionalBrickResources.push_back(
            ZString::AllocateFromCStr(p_Args->bricks[i].data(), static_cast<uint32_t>(p_Args->bricks[i].size())));
}

__declspec(noinline) DWORD SafeSwapScene(const SwapSceneArgs* p_Args) {
    DWORD s_Code = 0;

    __try {
        SwapSceneOf(p_Args);
    }
    __except (CaptureCode(GetExceptionCode(), &s_Code)) {
    }

    return s_Code;
}

} // namespace

// At most kMaxBricks.
BrickList ReadBricks(const TArray<ZString>* p_Bricks) {
    BrickList s_List;

    if (const DWORD s_Code = SafeBrickCount(p_Bricks, &s_List.count)) {
        s_List.fault = s_Code;
        return s_List;
    }

    for (size_t i = 0; i < std::min(s_List.count, kMaxBricks); ++i) {
        BrickTextOut s_Text {};

        if (const DWORD s_Code = SafeBrickText(p_Bricks, i, &s_Text)) {
            s_List.fault = s_Code;
            return s_List;
        }

        s_List.paths.emplace_back(s_Text.text);

        if (s_Text.size > kMaxPathChars)
            s_List.paths.back() += "...";
    }

    return s_List;
}

namespace {

// "[a, b]", with the count and any fault when not all were read.
std::string DescribeBricks(const BrickList& p_List) {
    std::string s_Text = "[";

    for (const auto& s_Path : p_List.paths)
        s_Text += fmt::format("{}{}", s_Text.size() > 1 ? ", " : "", s_Path);

    s_Text += "]";

    if (p_List.paths.size() < p_List.count)
        s_Text += fmt::format(" ({} of {} read)", p_List.paths.size(), p_List.count);

    if (p_List.fault)
        s_Text += fmt::format(" (read stopped: {})", FaultText(p_List.fault));

    return s_Text;
}

// "chunk22, chunk24 (not mounted)".
std::string DescribeChunks(const ChunksOut& p_Chunks) {
    std::string s_Text;

    for (size_t i = 0; i < std::min(p_Chunks.count, kMaxChunks); ++i)
        s_Text += fmt::format("{}chunk{}{}", s_Text.empty() ? "" : ", ", p_Chunks.indices[i],
                              p_Chunks.mounted[i] ? "" : " (not mounted)");

    return s_Text;
}

bool ListsChunk(const ChunksOut& p_Chunks, uint32_t p_Chunk) {
    for (size_t i = 0; i < std::min(p_Chunks.count, kMaxChunks); ++i) {
        if (p_Chunks.indices[i] == p_Chunk)
            return true;
    }

    return false;
}

// A full variant's swap bricks by resource id (ArmStories; game thread): the
// names of its vanilla bricks, which the catalog does not list.
std::vector<std::pair<uint64_t, std::string>>& FullBrickNames() {
    static std::vector<std::pair<uint64_t, std::string>> s_Names;
    return s_Names;
}

} // namespace

// Whether the brick's resource is in a chunk that is (or is made) available:
// one of its chunks already mounted, or one the scene itself is in (mounted
// with the scene); otherwise its first chunk is mounted. The reason (logged)
// when it is not. A brick found in no chunk is remembered for the session, so
// it is not looked up again (the SDK may rescan the game's packages).
bool Variants::CheckResource(const fr::VariantBrick& p_Brick, std::string_view p_Scene, std::string& p_Why) {
    const ZRuntimeResourceID s_Rid = ZRuntimeResourceID::FromString(fr::BrickResourcePath(p_Brick.path));

    if (s_Rid.GetID() != p_Brick.rid) {
        p_Why = fmt::format("resource id {:016X} is not the expected {:016X}", s_Rid.GetID(), p_Brick.rid);
        return false;
    }

    if (m_NotFound.contains(p_Brick.rid)) {
        p_Why = "resource not found in any chunk (looked up earlier this session)";
        return false;
    }

    ChunksOut s_Chunks {};
    const auto s_Start = std::chrono::steady_clock::now();
    const DWORD s_Code = SafeChunks(p_Brick.rid, &s_Chunks);
    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();

    if (s_Code) {
        p_Why = fmt::format("chunk lookup of {:016X}: {}", p_Brick.rid, FaultText(s_Code));
        return false;
    }

    LogTo(fr::LogTopic::MapVariants, "variant: {} resource {:016X}: in {} chunks [{}] ({:.1f} ms)", p_Brick.name,
          p_Brick.rid, s_Chunks.count, DescribeChunks(s_Chunks), s_Ms);

    if (s_Chunks.count == 0) {
        m_NotFound.insert(p_Brick.rid);
        p_Why = "resource not found in any chunk (not looked up again this session)";
        return false;
    }

    for (size_t i = 0; i < std::min(s_Chunks.count, kMaxChunks); ++i) {
        if (s_Chunks.mounted[i])
            return true;
    }

    // None mounted yet: a chunk the scene itself is in is mounted with it.
    const uint64_t s_SceneRid = ZRuntimeResourceID::FromString(fr::SceneResourcePath(p_Scene)).GetID();
    ChunksOut s_SceneChunks {};

    if (const DWORD s_SceneCode = SafeChunks(s_SceneRid, &s_SceneChunks)) {
        LogTo(fr::LogTopic::MapVariants, "variant: chunk lookup of the scene {:016X}: {}", s_SceneRid,
              FaultText(s_SceneCode));
    }
    else {
        LogTo(fr::LogTopic::MapVariants, "variant: scene resource {:016X}: in {} chunks [{}]", s_SceneRid,
              s_SceneChunks.count, DescribeChunks(s_SceneChunks));

        for (size_t i = 0; i < std::min(s_Chunks.count, kMaxChunks); ++i) {
            if (ListsChunk(s_SceneChunks, s_Chunks.indices[i]))
                return true;
        }
    }

    const uint32_t s_First = s_Chunks.indices[0];
    bool s_Mounted = false;

    if (const DWORD s_MountCode = SafeMountChunk(s_First, &s_Mounted)) {
        p_Why = fmt::format("mounting chunk{}: {}", s_First, FaultText(s_MountCode));
        return false;
    }

    LogTo(fr::LogTopic::MapVariants, "variant: mounted chunk{} for {}: {}", s_First, p_Brick.name,
          s_Mounted ? "now mounted" : "still not mounted");

    if (!s_Mounted) {
        p_Why = fmt::format("chunk{} could not be mounted", s_First);
        return false;
    }

    return true;
}

void Variants::OnLoadScene(SSceneInitParameters& p_Parameters, std::string_view p_Scene, bool p_IsMission,
                           const std::optional<fr::SeedResolution>& p_Seed, const fr::Config& p_Config) {
    LoadScene(p_Parameters, p_Scene, p_IsMission, p_Seed, p_Config);

    // A load with no map variant to mute: the blueprints copied for an earlier one are not needed again.
    if (m_BriefingScene.empty()) {
        try {
            briefing_graphs::Clear();
        }
        catch (...) {
        }
    }
}

void Variants::LoadScene(SSceneInitParameters& p_Parameters, std::string_view p_Scene, bool p_IsMission,
                         const std::optional<fr::SeedResolution>& p_Seed, const fr::Config& p_Config) {
    Contained("variant", "LoadScene", [&] {
        // Safehouse and non-Freelancer scenes: never read or changed.
        if (!p_IsMission)
            return;

        const fr::VariantMap* s_Map = fr::FindVariantMap(p_Scene);

        if (!s_Map) {
            LogTo(fr::LogTopic::MapVariants, "variant: no official variants for this map");
            return;
        }

        if (!p_Config.variants) {
            LogTo(fr::LogTopic::MapVariants, "variant: {}: off (variants = false); unchanged", s_Map->name);
            Publish("normal map (\"Map variants\" is off)");
            return;
        }

        if (!p_Seed) {
            LogTo(fr::LogTopic::MapVariants, "variant: {}: seed not resolved; unchanged", s_Map->name);
            Publish("normal map (the campaign seed was not found)");
            return;
        }

        TArray<ZString>* s_Array = &p_Parameters.m_aAdditionalBrickResources;
        const BrickList s_Before = ReadBricks(s_Array);
        // A full variant that can't be picked this session (Full map variants off, not built from the game's
        // files, or its package not installed) never is: its chance counts as 0, and forcing it falls back to the chances.
        const fr::FullVariantAvailable s_Available = [this](const fr::FullVariant& p_Full) {
            return m_LandslidePackage && m_LandslidePackage->Available(p_Full);
        };
        const fr::VariantChances s_Chances = fr::AvailableVariantChances(*s_Map, p_Config.variantChances, s_Available);
        std::string_view s_Force = p_Config.variantForce;

        if (const fr::VariantOption* s_Forced = fr::FindVariantOption(s_Force);
            s_Forced && fr::VariantUnavailable(*s_Forced, *s_Map, s_Available)) {
            LogTo(fr::LogTopic::MapVariants,
                  "variant: variant_force = {} is unavailable this session (see the package lines at game start); "
                  "using the chances",
                  s_Force);
            s_Force = {};
        }

        const fr::VariantPlan s_Plan =
            fr::PlanVariant(*s_Map, p_Seed->seed, p_Scene, s_Chances, s_Before.paths, s_Force);
        const std::string_view s_Name = fr::VariantName(s_Plan.option);
        std::string s_Shown =
            fmt::format("{}{}", ShownVariant(s_Plan.option), s_Plan.forced ? " (forced in Settings)" : "");

        if (s_Plan.walk.scaled)
            LogTo(fr::LogTopic::MapVariants, "variant: {}: chances sum to {}% (over 100); scaled down proportionally",
                  s_Map->name, s_Plan.walk.sum);

        LogTo(fr::LogTopic::MapVariants, "variant: {}: chances [{}]; seed {} ({}) rolls {} of {}: {}{}", s_Map->name,
              fr::DescribeWalk(s_Plan.walk), p_Seed->seed, fr::SeedSourceName(p_Seed->source), s_Plan.roll,
              fr::kVariantRollRange, fr::VariantName(s_Plan.seeded),
              s_Plan.forced ? fmt::format("; variant_force picks {}", s_Name) : std::string());

        if (s_Plan.forceNotOffered)
            LogTo(fr::LogTopic::MapVariants, "variant: variant_force = {} is not offered on {}; using the seeded pick",
                  p_Config.variantForce, s_Map->name);

        if (s_Plan.option) {
            std::string s_Bricks;

            for (const fr::VariantBrick* s_Brick : s_Plan.option->bricks)
                s_Bricks += fmt::format("{}{}", s_Bricks.empty() ? "" : ", ", s_Brick->name);

            LogTo(fr::LogTopic::MapVariants, "variant: {} appends {} brick{}: {}", s_Name, s_Plan.option->bricks.size(),
                  s_Plan.option->bricks.size() == 1 ? "" : "s", s_Bricks);
        }

        LogTo(fr::LogTopic::MapVariants, "variant: bricks before: {}", DescribeBricks(s_Before));

        // A list not read in full could hide a brick.
        if (s_Plan.option && (s_Before.fault || s_Before.paths.size() < s_Before.count)) {
            LogTo(fr::LogTopic::MapVariants, "variant: brick list not fully read; {} not added", s_Name);
            Publish(fmt::format("normal map ({} could not be loaded)", s_Shown));
            return;
        }

        // A full variant (the full Landslide, the full Author, ...) has no
        // simple version: when its checks fail, the normal map plays. One that
        // appends (the full Gluttony) goes on below once its checks pass: the
        // option's bricks are appended as usual, then its package's own.
        const fr::FullVariant* s_Full = s_Plan.option ? fr::FullVariantOf(s_Plan.option->name, s_Map->name) : nullptr;
        std::vector<std::string_view> s_PackageBricks;

        if (s_Full && !s_Full->appends) {
            std::string s_Overlay;

            if (!TryFullVariant(*s_Full, p_Parameters, p_Scene, s_Before, s_Plan.forced, &s_Overlay))
                Publish(fmt::format("{}{}", s_Overlay, s_Plan.forced ? " (forced in Settings)" : ""));

            return;
        }

        if (s_Full) {
            std::string s_Overlay;
            std::vector<std::string> s_Dropped; // none: it keeps the contract's bricks

            if (!CheckFullVariant(*s_Full, s_Before, s_Plan.forced, &s_Dropped, &s_Overlay)) {
                Publish(fmt::format("{}{}", s_Overlay, s_Plan.forced ? " (forced in Settings)" : ""));
                return;
            }

            for (const std::string_view s_Path : LandslidePackage::SwapBricks(*s_Full)) {
                const auto s_Same = [&](const fr::VariantBrick* p_Brick) {
                    return fr::ToLower(p_Brick->path) == fr::ToLower(s_Path);
                };

                if (std::none_of(s_Plan.option->bricks.begin(), s_Plan.option->bricks.end(), s_Same))
                    s_PackageBricks.push_back(s_Path);
            }

            if (s_PackageBricks.empty()) {
                LogTo(fr::LogTopic::MapVariants,
                      "{}: full version not used: the package's table adds no brick of its own; playing the normal map "
                      "instead",
                      s_Full->key);
                Publish(fmt::format("{}{}", fr::FullOverlay(*s_Full, fr::LandslideBlock::NotBuiltIn),
                                    s_Plan.forced ? " (forced in Settings)" : ""));
                return;
            }
        }

        for (const fr::VariantBrickPlan& s_Step : s_Plan.bricks) {
            switch (s_Step.action) {
                case fr::VariantAction::AlreadyPresent:
                    LogTo(fr::LogTopic::MapVariants, "variant: {} already present; not added again",
                          s_Step.brick->name);
                    break;
                default: LogTo(fr::LogTopic::MapVariants, "variant: {}: to add", s_Step.brick->name); break;
            }
        }

        switch (s_Plan.action) {
            case fr::VariantAction::Unchanged:
                LogTo(fr::LogTopic::MapVariants, "variant: unchanged (none picked)");
                Publish(s_Shown);
                return;
            case fr::VariantAction::AlreadyPresent:
                LogTo(fr::LogTopic::MapVariants, "variant: {}: every brick already present; nothing added{}", s_Name,
                      s_Full
                          ? fmt::format(" (nor {}'s package bricks: the contract already loads its set)", s_Full->key)
                          : std::string());
                Publish(s_Shown);

                if (s_Plan.option->check) {
                    m_CheckScene = std::string(p_Scene);
                    m_CheckOption = std::string(s_Name);
                }

                ArmBriefings(p_Scene, *s_Plan.option);
                m_Cleanup.Arm(p_Scene, *s_Plan.option);
                ArmHeroStart(p_Scene, *s_Plan.option);
                return;
            case fr::VariantAction::Add: break;
        }

        const fr::VariantAppend s_Append =
            fr::ResolveVariantAppend(s_Plan, [&](const fr::VariantBrick& p_Brick, std::string& p_Why) {
                return CheckResource(p_Brick, p_Scene, p_Why);
            });

        if (s_Append.failed) {
            LogTo(fr::LogTopic::MapVariants, "variant: {}: {}: {}; skipped (none of {}'s bricks added)", s_Name,
                  s_Append.failed->name, s_Append.why, s_Name);
            Publish(fmt::format("normal map ({} could not be loaded)", s_Shown));
            return;
        }

        // An appending full variant: the option's bricks, then the package's
        // own (its fix brick) after them. The sentinel first: a crash inside
        // the load leaves it.
        std::vector<std::string_view> s_Paths;

        for (const fr::VariantBrick* s_Brick : s_Append.add)
            s_Paths.push_back(s_Brick->path);

        s_Paths.insert(s_Paths.end(), s_PackageBricks.begin(), s_PackageBricks.end());

        if (s_Full) {
            m_LandslidePackage->WriteSentinel(*s_Full, p_Scene);
            m_FullPending = s_Full;
            m_FullLoaded = s_Full;
        }

        size_t s_Added = 0;

        for (const std::string_view s_Path : s_Paths) {
            if (const DWORD s_Code = SafeAppendBrick(s_Array, s_Path.data(), static_cast<uint32_t>(s_Path.size()))) {
                Log("variant: appending {}: {}; the brick list may be damaged; {} of {} bricks added", s_Path,
                    FaultText(s_Code), s_Added, s_Paths.size());
                Publish(fmt::format("{} (may be only partly loaded)", s_Shown));
                return;
            }

            ++s_Added;
            LogTo(fr::LogTopic::MapVariants, "variant: added {}{}", s_Path,
                  s_Added > s_Append.add.size() ? fmt::format(" ({}'s package)", s_Full->key) : std::string());
        }

        const BrickList s_After = ReadBricks(s_Array);
        std::string s_Missing;

        for (const fr::VariantBrick* s_Brick : s_Append.add) {
            if (!fr::HasBrick(s_After.paths, s_Brick->path))
                s_Missing += fmt::format("{}{}", s_Missing.empty() ? "" : ", ", s_Brick->name);
        }

        for (const std::string_view s_Path : s_PackageBricks) {
            if (!fr::HasBrick(s_After.paths, s_Path))
                s_Missing += fmt::format("{}{}", s_Missing.empty() ? "" : ", ", s_Path);
        }

        if (s_Full) {
            std::string s_Own;

            for (const std::string_view s_Path : s_PackageBricks)
                s_Own += fmt::format("{}{}", s_Own.empty() ? "" : ", ", s_Path);

            LogTo(fr::LogTopic::MapVariants,
                  "{}: full version: the contract's scene and bricks kept; {} appended after {}'s", s_Full->key, s_Own,
                  s_Name);
        }

        LogTo(fr::LogTopic::MapVariants, "variant: bricks after: {}{}", DescribeBricks(s_After),
              s_Missing.empty() ? "" : fmt::format(" (added but not listed: {})", s_Missing));
        Publish(s_Shown);

        if (s_Plan.option->check) {
            m_CheckScene = std::string(p_Scene);
            m_CheckOption = std::string(s_Name);
            LogTo(fr::LogTopic::MapVariants,
                  "variant check: {} loads story NPCs or set dressing; the target pool is checked at scene start and "
                  "after {:.0f} s of play",
                  s_Name, fr::kVariantCheckPlaySeconds);
        }

        ArmBriefings(p_Scene, *s_Plan.option);
        m_Cleanup.Arm(p_Scene, *s_Plan.option);
        ArmHeroStart(p_Scene, *s_Plan.option);
    });
}

bool Variants::CheckFullVariant(const fr::FullVariant& p_Variant, const BrickList& p_Before, bool p_Forced,
                                std::vector<std::string>* p_Dropped, std::string* p_Overlay) {
    if (!m_LandslidePackage) {
        LogTo(fr::LogTopic::MapVariants,
              "{}: full version not used: the package manager is not set up; playing the normal map instead",
              p_Variant.key);
        *p_Overlay = fr::FullOverlay(p_Variant, fr::LandslideBlock::NotInstalled);
        return false;
    }

    // The gate and the contract (no engine reads; an appending variant keeps
    // the contract's bricks, whatever they are), then the map's chunk (the
    // walk below needs it mounted to see the package), then the resource
    // container.
    fr::LandslideVerdict s_Verdict = fr::CheckFullGate(p_Variant, m_LandslidePackage->Gate(p_Variant, p_Forced));

    if (s_Verdict.block == fr::LandslideBlock::None && !p_Variant.appends) {
        fr::ContractVerdict s_Contract = fr::CheckFullContract(p_Variant, p_Before.paths);
        s_Verdict = std::move(s_Contract.verdict);
        *p_Dropped = std::move(s_Contract.dropped);
    }

    if (s_Verdict.block == fr::LandslideBlock::None) {
        bool s_Mounted = false;
        const uint32_t s_Chunk = static_cast<uint32_t>(p_Variant.partition);

        if (const DWORD s_Code = SafeMountChunk(s_Chunk, &s_Mounted))
            s_Verdict = {fr::LandslideBlock::ReadFault,
                         fmt::format("mounting chunk{}: {}", s_Chunk, FaultText(s_Code))};
        else if (!s_Mounted)
            s_Verdict = {fr::LandslideBlock::NotMounted,
                         fmt::format("chunk{} ({}) could not be mounted", s_Chunk, p_Variant.location)};
    }

    if (s_Verdict.block == fr::LandslideBlock::None)
        s_Verdict = m_LandslidePackage->CheckResources(p_Variant);

    if (s_Verdict.block != fr::LandslideBlock::None) {
        LogTo(fr::LogTopic::MapVariants, "{}: full version not used: {}; playing the normal map instead", p_Variant.key,
              s_Verdict.why);
        *p_Overlay = fr::FullOverlay(p_Variant, s_Verdict.block);
        return false;
    }

    return true;
}

bool Variants::TryFullVariant(const fr::FullVariant& p_Variant, SSceneInitParameters& p_Parameters,
                              std::string_view p_Scene, const BrickList& p_Before, bool p_Forced,
                              std::string* p_Overlay) {
    std::vector<std::string> s_Dropped;

    if (!CheckFullVariant(p_Variant, p_Before, p_Forced, &s_Dropped, p_Overlay))
        return false;

    // A difficulty's brick of the contract (the hard contract's extra guards)
    // belongs to the normal level: the swap's brick list leaves it out.
    for (const std::string& s_Brick : s_Dropped)
        LogTo(fr::LogTopic::MapVariants, "{}: full version: {}", p_Variant.key,
              fr::DroppedBrickNote(s_Brick, p_Variant.droppedWhy));

    // The sentinel first: a crash inside the swap or the load leaves it.
    m_LandslidePackage->WriteSentinel(p_Variant, p_Variant.scene);
    // Its faults (with module and offset) go to a file the next game start reports (LoadFaultWatch.h).
    load_fault::Arm(fmt::format("{} (full version)", p_Variant.key));
    m_FullPending = &p_Variant;
    m_FullLoaded = &p_Variant;

    const std::vector<std::string_view> s_Bricks = LandslidePackage::SwapBricks(p_Variant);
    const SwapSceneArgs s_Args {&p_Parameters, p_Variant.scene, s_Bricks.data(), s_Bricks.size()};

    if (const DWORD s_Code = SafeSwapScene(&s_Args)) {
        Log("{}: full version: swapping the scene: {}; the load parameters may be damaged", p_Variant.key,
            FaultText(s_Code));
        Publish(fmt::format("{} (the full version could not be loaded)", p_Variant.display));
        return true;
    }

    m_SwappedScene = std::string(p_Parameters.m_SceneResource.ToStringView());
    LogTo(fr::LogTopic::MapVariants, "{}: full version: scene now {}; bricks {} (the contract's were {})",
          p_Variant.key, m_SwappedScene, DescribeBricks(ReadBricks(&p_Parameters.m_aAdditionalBrickResources)),
          DescribeBricks(p_Before));

    // The rest of the mod counts the swapped scene as the contract's (SceneOf.h),
    // with the package's mission brick in place of the contract's
    // (mission_mild_bingo, mission_mild_mancala).
    fr::SceneAlias s_Alias;
    s_Alias.actual = m_SwappedScene;
    s_Alias.logical = std::string(p_Scene);
    s_Alias.name = std::string(p_Variant.fullName);
    s_Alias.fullKey = std::string(p_Variant.key);
    s_Alias.sharesCrowd = p_Variant.sharesCrowd;

    if (LandslidePackage::FreelancerLayer() && !s_Bricks.empty())
        s_Alias.missionBrick = fr::ToLower(s_Bricks.front());

    LogTo(fr::LogTopic::MapVariants, "{}: full version: the mod's features treat {} as {} (mission brick {})",
          p_Variant.key, s_Alias.actual, s_Alias.logical,
          s_Alias.missionBrick.empty() ? std::string("none: the test package") : s_Alias.missionBrick);
    SetSceneAlias(std::move(s_Alias));

    // The whole scene is the variant's: no brick of its own to prefer.
    m_HeroStart.Arm(m_SwappedScene, fmt::format("{} (full version)", p_Variant.key), {});
    // Its exits are its package's business: listed only.
    m_Exits.Arm(m_SwappedScene, fmt::format("{} (full version)", p_Variant.key), {}, false);
    // Its mission stories are switched off as an appended variant's are (core/MissionStories.h).
    ArmStories(m_SwappedScene, p_Variant);
    m_Applied = {std::string(p_Scene), std::string(p_Variant.key), true, {}};

    // The variant check walks the package's mission brick (its target pool)
    // at scene start and after 10 s of play.
    if (LandslidePackage::FreelancerLayer() && !s_Bricks.empty()) {
        m_CheckScene = m_SwappedScene;
        m_CheckOption = fmt::format("{} (full version)", p_Variant.key);
        m_CheckBrick = std::string(s_Bricks.front());
    }
    // Without the Freelancer layer (the test package) there are no targets.
    Publish(fmt::format("{}{}{}", fr::FullOverlay(p_Variant, fr::LandslideBlock::None),
                        LandslidePackage::FreelancerLayer() ? "" : " (full version test: no targets)",
                        p_Forced ? " (forced in Settings)" : ""));
    return true;
}

void Variants::OnRestart(const SSceneInitParameters& p_Parameters, std::string_view p_Scene, bool p_IsMission) {
    Contained("variant", "restart", [&] {
        if (!p_IsMission || !fr::FindVariantMap(p_Scene))
            return;

        LogTo(fr::LogTopic::MapVariants, "variant: restart: bricks {}",
              DescribeBricks(ReadBricks(&p_Parameters.m_aAdditionalBrickResources)));
    });
}

void Variants::DrawOverlay() {
    std::string s_Line;
    {
        std::lock_guard s_Lock(m_OverlayMutex);
        s_Line = m_Overlay;
    }

    if (!s_Line.empty())
        WrappedTextAvail(fmt::format("Map variant: {}", s_Line));
}

void Variants::ClearOverlay() {
    Publish({});
    m_SwappedScene.clear();
    m_FullPending = nullptr; // a sentinel still on disk stays: that load did not reach play
    load_fault::Disarm("another load began before this one reached play");
    m_FullLoaded = nullptr;
    m_CheckScene.clear();
    m_CheckOption.clear();
    m_CheckBrick.clear();
    m_LoadChecked = false;
    m_Watching = false;
    m_Pool.clear();
    m_BriefingScene.clear();
    m_BriefingOption = nullptr;
    m_BriefingName.clear();
    m_StoriesOnly = false;
    m_StoryKeepRids.clear();
    m_DianaRids.clear();
    ForgetBriefingEntities();
    briefing_graphs::SetCapture(false);
    m_Cleanup.Forget();
    m_HeroStart.Forget();
    m_Exits.Forget();
    m_Applied = {};
}

void Variants::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context) noexcept {
    // A full variant's load reached play: its crash sentinel goes.
    if (p_Stage == ESceneLoadingStage::eLoading_ScenePlaying && m_FullPending && m_LandslidePackage) {
        const fr::FullVariant* s_Variant = m_FullPending;
        m_FullPending = nullptr;
        m_LandslidePackage->ClearSentinel(*s_Variant);
        load_fault::Disarm("the load reached play");
    }

    // As early as the entities exist (a briefing can start during the loading screen); again at scene start
    // when the option's bricks were not loaded yet.
    Contained("variant", "finding Diana's briefing", [&] {
        if (p_Stage == ESceneLoadingStage::eLoading_SceneAllocated ||
            p_Stage == ESceneLoadingStage::eLoading_SceneStarted)
            FindBriefings(p_Context, p_Stage == ESceneLoadingStage::eLoading_SceneStarted);
    });

    m_Cleanup.OnLoadingStage(p_Stage, p_Context);
    m_HeroStart.OnLoadingStage(p_Stage, p_Context);
    m_Exits.OnLoadingStage(p_Stage, p_Context);

    Contained("variant check", "scene start", [&] {
        if (p_Stage != ESceneLoadingStage::eLoading_SceneStarted || !p_Context || m_CheckScene.empty() || m_LoadChecked)
            return;

        if (fr::ToLower(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView()) != fr::ToLower(m_CheckScene))
            return;

        m_LoadChecked = true;
        LoadCheck(p_Context);
    });
}

void Variants::OnSceneClear() noexcept {
    // A restart allocates the scene again: everything is found again (the pick stays).
    m_LoadChecked = false;
    m_Watching = false;
    m_Played = 0.0;
    m_Pool.clear();
    ForgetBriefingEntities(); // the pick stays: found again at the restart's scene start
    m_Cleanup.OnSceneClear();
    m_HeroStart.OnSceneClear();
    m_Exits.OnSceneClear();
}

void Variants::ArmBriefings(std::string_view p_Scene, const fr::VariantOption& p_Option) {
    m_BriefingScene = std::string(p_Scene);
    m_BriefingOption = &p_Option;
    m_BriefingName = std::string(p_Option.name);
    m_StoriesOnly = false;
    m_StoryKeepRids.clear();
    m_DianaRids.clear();
    // The blueprints this load builds are copied (the game drops them before the scene starts).
    briefing_graphs::SetCapture(true);
}

void Variants::ArmStories(std::string_view p_Scene, const fr::FullVariant& p_Variant) {
    m_BriefingScene = std::string(p_Scene);
    m_BriefingOption = nullptr;
    m_BriefingName = fmt::format("{} (full version)", p_Variant.key);
    m_StoriesOnly = true;
    m_StoryKeepRids.clear();
    m_DianaRids.clear();

    // The whole scene is the variant's but for the package's Freelancer layer (its mission brick, first in the
    // swap's list): Freelancer's own logic, never searched.
    const std::vector<std::string_view> s_Bricks = LandslidePackage::SwapBricks(p_Variant);

    if (LandslidePackage::FreelancerLayer() && !s_Bricks.empty())
        m_StoryKeepRids.push_back(ZRuntimeResourceID::FromString(fr::BrickResourcePath(s_Bricks.front())).GetID());

    // Diana's briefing and lines: in the vanilla bricks the swap appends after the package's (a layer's contract
    // brick: the full Bad Boy's mission_caipirinha), never in the package's own or the scene's other bricks.
    std::vector<uint64_t> s_Swap;
    std::vector<uint64_t> s_Own;
    std::vector<std::pair<uint64_t, std::string>> s_Names;

    for (const std::string_view s_Path : s_Bricks) {
        const uint64_t s_Rid = ZRuntimeResourceID::FromString(fr::BrickResourcePath(s_Path)).GetID();
        const size_t s_Slash = s_Path.find_last_of('/');
        s_Swap.push_back(s_Rid);
        s_Names.emplace_back(s_Rid,
                             std::string(s_Slash == std::string_view::npos ? s_Path : s_Path.substr(s_Slash + 1)));
    }

    for (const LandslidePackage::OwnBrick& s_Brick : LandslidePackage::OwnBricks(p_Variant))
        s_Own.push_back(s_Brick.rid);

    m_DianaRids = fr::FullDianaBricks(s_Swap, s_Own);
    FullBrickNames() = std::move(s_Names);

    briefing_graphs::SetCapture(true);
}

void Variants::ArmHeroStart(std::string_view p_Scene, const fr::VariantOption& p_Option) {
    std::vector<uint64_t> s_Rids;

    for (const fr::VariantBrick* s_Brick : p_Option.bricks)
        s_Rids.push_back(s_Brick->rid);

    m_Applied = {std::string(p_Scene), std::string(p_Option.name), false, s_Rids};
    m_Exits.Arm(p_Scene, p_Option.name, s_Rids, true);
    m_HeroStart.Arm(p_Scene, p_Option.name, std::move(s_Rids));
}

namespace {

// One blueprint (a brick's own, or a template's under one of its instances)
// with a game event listener, a DianaSpeak or a mission story (an opportunity
// template instance) among its sub-entities.
struct BriefingGroup {
    ZEntityBlueprintFactoryBase* factory = nullptr;
    ZEntityType** parent = nullptr;
    uint64_t brickRid = 0;
    std::vector<std::pair<int, int32_t>> listeners; // sub-entity index, m_eEvent
    std::vector<int> speakers;
    std::vector<int32_t> stories;  // fr::IsOpportunityResource
    std::vector<int32_t> vitality; // fr::IsVitalityResource (Sloth's)
    std::vector<int32_t> sinLines; // fr::FindSinLine (Envy's, Lust's)
    std::map<int, ZEntityType**> entities;
    std::vector<std::pair<int32_t, uint64_t>> ids; // sub-entity index, the entity's own id (m_nEntityID)
};

int32_t ReadListenerEvent(ZEntityType** p_Entity) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, "m_eEvent");

    if (s_Location.status != PropertyLocation::Status::Ok ||
        (s_Location.typeSize != 0 && s_Location.typeSize != sizeof(int32_t)))
        return fr::kGameEventUnread;

    int32_t s_Event = 0;

    if (ReadEngineBytes(s_Location.address, &s_Event, sizeof(s_Event)))
        return fr::kGameEventUnread;

    return s_Event;
}

std::string BriefingBrickName(uint64_t p_Rid) {
    if (const fr::VariantBrick* s_Brick = fr::FindVariantBrick(p_Rid))
        return std::string(s_Brick->name);

    for (const auto& [s_Rid, s_Name] : FullBrickNames()) {
        if (s_Rid == p_Rid)
            return s_Name;
    }

    return fmt::format("brick {:016X}", p_Rid);
}

using briefing_graphs::SubName;

uint64_t SubId(const fr::BriefingGraph& p_Graph, int p_Index) {
    return p_Index >= 0 && static_cast<size_t>(p_Index) < p_Graph.ids.size() ? p_Graph.ids[static_cast<size_t>(p_Index)]
                                                                             : 0;
}

// The blueprint of the group's factory (briefing_graphs::GraphOf), checked against the entities the walk read.
std::optional<fr::BriefingGraph> BriefingGraphOf(const BriefingGroup& p_Group, std::string* p_Why) {
    return briefing_graphs::GraphOf(p_Group.factory, p_Group.ids, p_Why);
}

// The class of sub-entity p_Index of the group's blueprint, for the log.
std::string BriefingClassOf(const BriefingGroup& p_Group, int32_t p_Index) {
    ZEntityType** s_Entity = nullptr;

    if (const auto s_Known = p_Group.entities.find(p_Index); s_Known != p_Group.entities.end()) {
        s_Entity = s_Known->second;
    }
    else {
        ZEntityBlueprintFactoryBase* s_Sub = nullptr;

        if (ReadSubEntityAt(p_Group.factory, p_Group.parent, p_Index, &s_Entity, &s_Sub))
            return "class unread";
    }

    std::string s_Class;
    uint64_t s_Id = 0;

    if (!s_Entity || ReadEntityClassName(s_Entity, s_Class, &s_Id) || s_Class.empty())
        return "class unread";

    return s_Class;
}

// Sub-entity p_Index of the group's blueprint: as the walk read it, else looked up. Null with *p_Why set.
ZEntityType** GroupEntityAt(const BriefingGroup& p_Group, int32_t p_Index, std::string* p_Why) {
    if (const auto s_Known = p_Group.entities.find(p_Index); s_Known != p_Group.entities.end())
        return s_Known->second;

    ZEntityType** s_Entity = nullptr;
    ZEntityBlueprintFactoryBase* s_Sub = nullptr;

    if (const uint32_t s_Code = ReadSubEntityAt(p_Group.factory, p_Group.parent, p_Index, &s_Entity, &s_Sub)) {
        *p_Why = fmt::format("looking it up: {}", FaultText(s_Code));
        return nullptr;
    }

    if (!s_Entity)
        *p_Why = "entity not found";

    return s_Entity;
}

// The links a blocked pin's signal is sent on along (p_Links: indices into the graph's links), resolved to their
// entities, with their names in *p_Names; empty with *p_Why set when one could not be looked up.
std::optional<std::vector<briefing_graphs::Resend>> ResolveResend(const BriefingGroup& p_Group,
                                                                  const fr::BriefingGraph& p_Graph,
                                                                  std::span<const size_t> p_Links, std::string* p_Names,
                                                                  std::string* p_Why) {
    std::vector<briefing_graphs::Resend> s_Resend;

    for (const size_t s_Link : p_Links) {
        const fr::BriefingLink& s_To = p_Graph.links[s_Link];
        const bool s_Output = s_To.kind == fr::BriefingLinkKind::OutputForwarding;
        const std::string s_Name =
            fmt::format("\"{}\" {}{}", SubName(p_Graph, s_To.to), s_To.toPin, s_Output ? " (forwarded output)" : "");
        std::string s_TargetWhy;
        ZEntityType** const s_Target = GroupEntityAt(p_Group, s_To.to, &s_TargetWhy);

        if (!s_Target) {
            *p_Why = fmt::format("{}: {}", s_Name, s_TargetWhy);
            return std::nullopt;
        }

        s_Resend.push_back({s_Target, Hash::Crc32(s_To.toPin.c_str(), s_To.toPin.size()), s_Output});
        *p_Names += fmt::format("{}{}", p_Names->empty() ? "" : ", ", s_Name);
    }

    return s_Resend;
}

// A map tracker in the variant's bricks (core/VariantMarkers.h).
struct MarkerSeen {
    ZEntityType** entity = nullptr;
    uint64_t id = 0;
    variant_markers::Seen seen;
};

// A ZHitmanDamageOverTime in the variant's bricks (core/VariantDamage.h).
struct DamageSeen {
    ZEntityType** entity = nullptr;
    uint64_t id = 0;
};

// A ZHintEntity in the variant's bricks (core/VariantComments.h), muted after the walk.
struct HintSeen {
    ZEntityType** entity = nullptr;
    ZEntityBlueprintFactoryBase* parentFactory = nullptr;
    int index = -1;
    uint64_t id = 0;
    uint64_t brickRid = 0;
};

// The variant's timed damage switched off (core/VariantDamage.h): never hurts 47, never ticks.
fr::DamageTally SwitchOffDamage(const std::vector<DamageSeen>& p_Damage, std::string* p_Failures) {
    fr::DamageTally s_Tally;

    for (const DamageSeen& s_Damage : p_Damage) {
        const fr::DamageBefore s_Before = variant_damage::Read(s_Damage.entity);
        const std::string s_Why = variant_damage::SwitchOff(s_Damage.entity);

        fr::AddDamage(s_Tally, s_Before, s_Why.empty());

        if (!s_Why.empty())
            *p_Failures += fmt::format("{}{:016X}: {}", p_Failures->empty() ? "" : "; ", s_Damage.id, s_Why);
    }

    return s_Tally;
}

// The variant's objective and opportunity markers switched off (fr::DecideMarker); the others counted.
fr::MarkerTally SwitchOffMarkers(const std::vector<MarkerSeen>& p_Markers, std::string* p_Failures) {
    fr::MarkerTally s_Tally;

    for (const MarkerSeen& s_Marker : p_Markers) {
        const std::string s_Class(fr::MarkerClassOf(s_Marker.seen.interfaces));

        if (fr::DecideMarker(s_Marker.seen.interfaces, s_Marker.seen.layer) != fr::MarkerAction::SwitchOff) {
            ++s_Tally.leftByKind[fmt::format("{} ({} layer)", s_Class,
                                             s_Marker.seen.layer ? fr::MapLayerName(*s_Marker.seen.layer)
                                                                 : std::string_view("unread"))];
            continue;
        }

        const std::string s_Why = variant_markers::SwitchOff(s_Marker.entity);

        if (s_Why.empty()) {
            ++s_Tally.switchedOff;
            ++s_Tally.offByClass[s_Class];
            continue;
        }

        ++s_Tally.failed;
        *p_Failures += fmt::format("{}{} {:016X}: {}", p_Failures->empty() ? "" : "; ", s_Class, s_Marker.id, s_Why);
    }

    return s_Tally;
}

// One entity of the variant's bricks named for the log (StoryName).
struct StoryEntity {
    std::string className;
    ZEntityType** entity = nullptr;
    ZEntityBlueprintFactoryBase* parentFactory = nullptr;
    int index = -1;
    uint64_t id = 0;
    uint64_t brickRid = 0;
};

// The entity's authored name, from the blueprint copied while the scene
// loaded when it is that blueprint's sub-entity (the same id), else empty.
std::string StoryName(const StoryEntity& p_Story) {
    if (!p_Story.parentFactory || p_Story.index < 0)
        return {};

    const std::optional<fr::BriefingGraph> s_Graph = briefing_graphs::Captured(p_Story.parentFactory);
    const size_t s_Index = static_cast<size_t>(p_Story.index);

    if (!s_Graph || s_Index >= s_Graph->ids.size() || s_Graph->ids[s_Index] != p_Story.id)
        return {};

    return s_Graph->names[s_Index];
}

// What else of the variant's own story is in its bricks, by engine class
// (fr::IsStoryClass): left as it is.
void LogStory(std::string_view p_Name, const std::map<std::string, size_t>& p_Counts) {
    if (p_Counts.empty()) {
        LogTo(
            fr::LogTopic::MapVariants,
            "variant: {}: story entities: none of the story classes (VIP, escape, evacuation, opportunity, objective, "
            "contract, HUD, hint, intel) in this variant's bricks",
            p_Name);
        return;
    }

    std::string s_List;

    for (const auto& [s_Class, s_Count] : p_Counts)
        s_List += fmt::format("{}{} x{}", s_List.empty() ? "" : ", ", s_Class, s_Count);

    LogTo(fr::LogTopic::MapVariants, "variant: {}: story entities in this variant's bricks (left as they are): {}",
          p_Name, s_List);
}

} // namespace

// The option's loaded bricks walked once per scene allocation (or start, when
// they were not loaded at allocation): each blueprint that has a game event
// listener and a DianaSpeak gets its pin connections checked
// (fr::FindBriefingMutes; the blueprint as copied while the scene loaded); the
// pins found are blocked in OnOutputPin. Each blueprint with a mission story
// gets the pins that feed it blocked the same way (fr::FindStoryMutes). A
// full variant's swapped scene (m_StoriesOnly) is searched for its mission
// stories only, in every loaded brick but the package's Freelancer layer.
void Variants::FindBriefings(ZEntitySceneContext* p_Context, bool p_LastChance) {
    if ((!m_BriefingOption && !m_StoriesOnly) || m_BriefingScene.empty() || m_BriefingsFound)
        return;

    if (!p_Context)
        p_Context = CurrentSceneContext();

    if (!p_Context ||
        fr::ToLower(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView()) != fr::ToLower(m_BriefingScene))
        return;

    const std::string& s_Name = m_BriefingName;
    const auto s_Start = std::chrono::steady_clock::now();
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    std::vector<size_t> s_Order;
    std::string s_NotLoaded;

    // A full variant's swapped scene: every loaded brick but the package's Freelancer layer.
    for (size_t b = 0; m_StoriesOnly && b < s_Bricks.size(); ++b) {
        if (s_Bricks[b].m_EntityRef.m_pObj &&
            std::find(m_StoryKeepRids.begin(), m_StoryKeepRids.end(), s_Bricks[b].m_RuntimeResourceID.GetID()) ==
                m_StoryKeepRids.end())
            s_Order.push_back(b);
    }

    // ... of which the bricks its swap appends after the package's are searched for Diana too (m_DianaRids).
    const auto s_DianaBrick = [&](uint64_t p_Rid) {
        return !m_StoriesOnly || std::find(m_DianaRids.begin(), m_DianaRids.end(), p_Rid) != m_DianaRids.end();
    };

    for (const uint64_t s_Rid : m_DianaRids) {
        const bool s_Loaded = std::any_of(s_Bricks.begin(), s_Bricks.end(), [&](const auto& p_Brick) {
            return p_Brick.m_RuntimeResourceID.GetID() == s_Rid && p_Brick.m_EntityRef.m_pObj;
        });

        if (!s_Loaded)
            s_NotLoaded += fmt::format("{}{}", s_NotLoaded.empty() ? "" : ", ", BriefingBrickName(s_Rid));
    }

    static const std::vector<const fr::VariantBrick*> kNoBricks;

    for (const fr::VariantBrick* s_Brick : m_StoriesOnly ? kNoBricks : m_BriefingOption->bricks) {
        bool s_Found = false;

        for (size_t b = 0; b < s_Bricks.size(); ++b) {
            if (s_Bricks[b].m_RuntimeResourceID.GetID() == s_Brick->rid && s_Bricks[b].m_EntityRef.m_pObj) {
                s_Order.push_back(b);
                s_Found = true;
                break;
            }
        }

        if (!s_Found)
            s_NotLoaded += fmt::format("{}{}", s_NotLoaded.empty() ? "" : ", ", s_Brick->name);
    }

    // None of the option's bricks loaded yet: looked for again at scene start.
    if (s_Order.empty() && !p_LastChance)
        return;

    m_BriefingsFound = true;

    // The variant layers the option's bricks load themselves (The Deceivers' wrapper loads The Congressman's and
    // The Guru's; the full Reflection, Harbinger and Herbalist load their dressing): any other loaded brick of a
    // variant option.
    for (size_t b = 0; !m_StoriesOnly && b < s_Bricks.size(); ++b) {
        if (!s_Bricks[b].m_EntityRef.m_pObj || std::find(s_Order.begin(), s_Order.end(), b) != s_Order.end())
            continue;

        if (fr::FindVariantBrick(s_Bricks[b].m_RuntimeResourceID.GetID()))
            s_Order.push_back(b);
    }

    std::map<std::pair<ZEntityBlueprintFactoryBase*, ZEntityType**>, BriefingGroup> s_Groups;
    size_t s_Listeners = 0;
    size_t s_Speakers = 0;
    size_t s_ReadFaults = 0;
    std::string s_Class;
    std::map<std::string, size_t> s_StoryCounts; // fr::IsStoryClass classes in the variant's bricks
    std::vector<MarkerSeen> s_Markers;           // the map trackers (core/VariantMarkers.h)
    std::vector<DamageSeen> s_Damage;            // the timed damage entities (core/VariantDamage.h)
    std::vector<HintSeen> s_Hints;               // the commentary's hints (core/VariantComments.h)

    const WalkStats s_Walk =
        s_Order.empty()
            ? WalkStats {}
            : VisitEntitiesInOrder(s_Bricks, s_Order,
                                   [&](const VisitedEntity& p_Visited) {
                                       if (!p_Visited.parentFactory || p_Visited.index < 0)
                                           return false;

                                       uint64_t s_Rid = 0;

                                       if (p_Visited.factory && ReadFactoryRid(p_Visited.factory, &s_Rid)) {
                                           ++s_ReadFaults;
                                           s_Rid = 0;
                                       }

                                       // A full variant's swapped scene: its mission stories, and Diana only in the bricks its swap appends.
                                       const bool s_Diana = s_DianaBrick(p_Visited.brickRid);
                                       const bool s_Speaker = s_Diana && s_Rid != 0 && fr::IsDianaSpeakResource(s_Rid);
                                       const bool s_Story = s_Rid != 0 && fr::IsOpportunityResource(s_Rid);
                                       const bool s_Vitality = s_Diana && s_Rid != 0 && fr::IsVitalityResource(s_Rid);
                                       int32_t s_Event = fr::kGameEventUnread;
                                       uint64_t s_Id = 0;

                                       if (!s_Diana && !s_Story)
                                           return false;

                                       if (ReadEntityClassName(p_Visited.entity, s_Class, &s_Id)) {
                                           ++s_ReadFaults;
                                           return false;
                                       }

                                       // A line of the sin's own voice (Envy's, Lust's, core/VariantComments.h): muted after the walk.
                                       const bool s_SinLine = s_Diana && fr::FindSinLine(s_Id);

                                       // The variant's own map markers (core/VariantMarkers.h): switched off after the walk.
                                       if (s_Diana) {
                                           variant_markers::Seen s_Marker = variant_markers::Read(p_Visited.entity);

                                           if (s_Marker.fault)
                                               ++s_ReadFaults;
                                           else if (s_Marker.tracker)
                                               s_Markers.push_back({p_Visited.entity, s_Id, std::move(s_Marker)});
                                       }

                                       // The variant's own timed damage (Sloth's vitality, core/VariantDamage.h): switched off after the walk.
                                       if (s_Diana && fr::IsDamageOverTimeClass(s_Class))
                                           s_Damage.push_back({p_Visited.entity, s_Id});

                                       // The variant's own commentary (Sloth's vitality comments, core/VariantComments.h): muted after the walk.
                                       if (s_Diana && fr::IsHintClass(s_Class))
                                           s_Hints.push_back({p_Visited.entity, p_Visited.parentFactory,
                                                              p_Visited.index, s_Id, p_Visited.brickRid});

                                       if (s_Diana && fr::IsStoryClass(s_Class))
                                           ++s_StoryCounts[s_Class];

                                       if (!s_Speaker && !s_Story && !s_Vitality && !s_SinLine) {
                                           if (!s_Diana || s_Class != fr::kGameEventListenerClass)
                                               return false;

                                           s_Event = ReadListenerEvent(p_Visited.entity);
                                       }

                                       auto [s_It, s_Added] =
                                           s_Groups.try_emplace({p_Visited.parentFactory, p_Visited.parent});
                                       BriefingGroup& s_Group = s_It->second;

                                       if (s_Added) {
                                           s_Group.factory = p_Visited.parentFactory;
                                           s_Group.parent = p_Visited.parent;
                                           s_Group.brickRid = p_Visited.brickRid;
                                       }

                                       s_Group.entities[p_Visited.index] = p_Visited.entity;
                                       s_Group.ids.emplace_back(p_Visited.index, s_Id);

                                       if (s_Speaker) {
                                           s_Group.speakers.push_back(p_Visited.index);
                                           ++s_Speakers;
                                       }
                                       else if (s_Story) {
                                           s_Group.stories.push_back(p_Visited.index);
                                       }
                                       else if (s_Vitality) {
                                           s_Group.vitality.push_back(p_Visited.index);
                                       }
                                       else if (s_SinLine) {
                                           s_Group.sinLines.push_back(p_Visited.index);
                                       }
                                       else {
                                           s_Group.listeners.emplace_back(p_Visited.index, s_Event);
                                           ++s_Listeners;
                                       }

                                       return true;
                                   },
                                   kWalkEntityCap, {});

    std::vector<std::string> s_Muted;
    std::vector<std::string> s_Notes;
    std::vector<std::string> s_LeftOn;
    std::vector<std::string> s_Lines; // Diana's other lines, muted (AddLine)
    std::vector<std::string> s_Kept;  // Diana's other lines whose feeding pin also starts other logic
    size_t s_Pins = 0;
    size_t s_Read = 0;

    for (auto& [s_Key, s_Group] : s_Groups) {
        const bool s_AnyEvent =
            std::any_of(s_Group.listeners.begin(), s_Group.listeners.end(),
                        [](const auto& p_Listener) { return fr::IsBriefingEvent(p_Listener.second); });

        if (s_Group.speakers.empty())
            continue;

        const std::string s_BrickName = BriefingBrickName(s_Group.brickRid);
        std::string s_Why;
        const std::optional<fr::BriefingGraph> s_Graph = BriefingGraphOf(s_Group, &s_Why);

        if (!s_Graph) {
            // Only a blueprint that may hold a briefing is copied: without a briefing listener, a missing copy is
            // expected (its lines are listed below as "its blueprint was not read").
            if (s_AnyEvent)
                s_Notes.push_back(fmt::format("a blueprint in {} with {} listeners and {} DianaSpeaks could not be "
                                              "read ({})",
                                              s_BrickName, s_Group.listeners.size(), s_Group.speakers.size(), s_Why));
            continue;
        }

        ++s_Read;
        const size_t s_Count = s_Graph->ids.size();
        std::vector<fr::BriefingNode> s_Nodes(s_Count);

        for (const auto& [s_Index, s_Event] : s_Group.listeners) {
            if (static_cast<size_t>(s_Index) < s_Count)
                s_Nodes[static_cast<size_t>(s_Index)] = {fr::BriefingNodeKind::Listener, s_Event};
        }

        for (const int s_Index : s_Group.speakers) {
            if (static_cast<size_t>(s_Index) < s_Count)
                s_Nodes[static_cast<size_t>(s_Index)].kind = fr::BriefingNodeKind::Speaker;
        }

        const fr::BriefingFind s_Find = fr::FindBriefingMutes(s_Nodes, s_Graph->links);
        std::vector<int32_t> s_Silenced;

        for (const fr::BriefingMute& s_Mute : s_Find.mutes) {
            const std::string s_EntityName = SubName(*s_Graph, s_Mute.entity);
            ZEntityType** s_Entity = nullptr;

            if (const auto s_Known = s_Group.entities.find(s_Mute.entity); s_Known != s_Group.entities.end()) {
                s_Entity = s_Known->second;
            }
            else {
                ZEntityBlueprintFactoryBase* s_Sub = nullptr;

                if (const uint32_t s_Code =
                        ReadSubEntityAt(s_Group.factory, s_Group.parent, s_Mute.entity, &s_Entity, &s_Sub)) {
                    s_Notes.push_back(fmt::format("{} \"{}\" {} not muted: looking it up: {}", s_BrickName,
                                                  s_EntityName, s_Mute.pin, FaultText(s_Code)));
                    continue;
                }
            }

            if (!s_Entity) {
                s_Notes.push_back(
                    fmt::format("{} \"{}\" {} not muted: entity not found", s_BrickName, s_EntityName, s_Mute.pin));
                continue;
            }

            if (s_Pins >= kMaxBriefings) {
                s_Notes.push_back(fmt::format("{} \"{}\" {} not muted: more than {} pins to block", s_BrickName,
                                              s_EntityName, s_Mute.pin, kMaxBriefings));
                continue;
            }

            m_BriefingPins[s_Pins].pin.store(Hash::Crc32(s_Mute.pin.c_str(), s_Mute.pin.size()),
                                             std::memory_order_relaxed);
            m_BriefingPins[s_Pins].entity.store(s_Entity, std::memory_order_relaxed);
            ++s_Pins;
            s_Silenced.insert(s_Silenced.end(), s_Mute.speakers.begin(), s_Mute.speakers.end());

            int32_t s_Event = fr::kGameEventUnread;

            for (const auto& [s_Index, s_ListenerEvent] : s_Group.listeners) {
                if (s_Index == s_Mute.listener)
                    s_Event = s_ListenerEvent;
            }

            std::string s_SpeakerNames;

            for (const int32_t s_Speaker : s_Mute.speakers)
                s_SpeakerNames +=
                    fmt::format("{}\"{}\"", s_SpeakerNames.empty() ? "" : ", ", SubName(*s_Graph, s_Speaker));

            const std::string s_From = s_Mute.entity == s_Mute.listener
                                           ? std::string()
                                           : fmt::format(", started by \"{}\"", SubName(*s_Graph, s_Mute.listener));
            s_Muted.push_back(fmt::format("{} \"{}\" {} ({:016X}; {}{}; it starts Diana's {} after {} step{})",
                                          s_BrickName, s_EntityName, s_Mute.pin, SubId(*s_Graph, s_Mute.entity),
                                          fr::GameEventName(s_Event), s_From, s_SpeakerNames, s_Mute.steps,
                                          s_Mute.steps == 1 ? "" : "s"));
        }

        for (const fr::BriefingSkip& s_Skip : s_Find.skipped)
            s_Notes.push_back(fmt::format("{} \"{}\" {} also starts other logic, so it was left on (Diana may still "
                                          "talk; listener \"{}\")",
                                          s_BrickName, SubName(*s_Graph, s_Skip.feeder), s_Skip.pin,
                                          SubName(*s_Graph, s_Skip.listener)));

        // The Diana lines left on, with what starts each: if one still talks over Freelancer's briefing, this
        // names the entity that starts it.
        std::map<int32_t, std::string> s_Feeds;

        for (const fr::BriefingFeed& s_Feed : fr::SpeakerFeeds(s_Nodes, s_Graph->links)) {
            if (std::find(s_Silenced.begin(), s_Silenced.end(), s_Feed.speaker) != s_Silenced.end())
                continue;

            std::string& s_Text = s_Feeds[s_Feed.speaker];

            if (std::count(s_Text.begin(), s_Text.end(), ';') >= 3)
                continue; // enough to go on

            s_Text += fmt::format("{}\"{}\" {} ({}) -> {}", s_Text.empty() ? "" : "; ", SubName(*s_Graph, s_Feed.from),
                                  s_Feed.fromPin, BriefingClassOf(s_Group, s_Feed.from), s_Feed.toPin);
        }

        // Diana's other lines, muted at the output pin that feeds each one's Trigger (connection-driven inputs never
        // reach OnInputPin), unless that pin also starts other logic.
        const fr::LineFind s_LineFind = fr::FindLineMutes(s_Nodes, s_Graph->links, s_Silenced);
        std::map<int32_t, std::string> s_LineBlocks; // speaker -> where it is muted (or why not)
        std::map<int32_t, std::string> s_LineSkips;  // speaker -> the pins left on

        for (const fr::LineMute& s_Mute : s_LineFind.mutes) {
            const std::string s_Feeder = fmt::format("\"{}\" {} ({})", SubName(*s_Graph, s_Mute.entity), s_Mute.pin,
                                                     BriefingClassOf(s_Group, s_Mute.entity));
            std::string s_SpeakerNames;

            for (const int32_t s_Speaker : s_Mute.speakers)
                s_SpeakerNames +=
                    fmt::format("{}\"{}\"", s_SpeakerNames.empty() ? "" : ", ", SubName(*s_Graph, s_Speaker));

            std::string s_EntityWhy;
            ZEntityType** const s_Entity = GroupEntityAt(s_Group, s_Mute.entity, &s_EntityWhy);
            std::string s_Result;

            // The pin's other logic: the signal is sent on along these links when the pin is blocked.
            std::string s_ResendText;
            std::string s_ResendWhy;
            const auto s_Resend = ResolveResend(s_Group, *s_Graph, s_Mute.resend, &s_ResendText, &s_ResendWhy);
            const std::string s_Others =
                s_ResendText.empty() ? std::string() : fmt::format("; its other logic sent on to {}", s_ResendText);

            if (!s_Entity)
                s_Result = fmt::format("not blocked at {}: {}", s_Feeder, s_EntityWhy);
            else if (!s_Resend)
                s_Result = fmt::format("not blocked at {}: it also starts {}, which could not be looked up, so it "
                                       "was left on",
                                       s_Feeder, s_ResendWhy);
            else if (!AddLineOut(s_Entity, s_Mute.pin, *s_Resend,
                                 fmt::format("{} {} ({:016X}; it starts Diana's {}{})", s_BrickName, s_Feeder,
                                             SubId(*s_Graph, s_Mute.entity), s_SpeakerNames, s_Others)))
                s_Result = fmt::format("not blocked at {}: more than {} pins to block", s_Feeder, kMaxLines);
            else
                s_Result = fmt::format("blocked at {}{}", s_Feeder, s_Others);

            for (const int32_t s_Speaker : s_Mute.speakers) {
                std::string& s_Text = s_LineBlocks[s_Speaker];
                s_Text += fmt::format("{}{}", s_Text.empty() ? "" : ", ", s_Result);
            }
        }

        for (const fr::LineSkip& s_Skip : s_LineFind.skipped) {
            std::string& s_Text = s_LineSkips[s_Skip.speaker];
            s_Text += fmt::format("{}\"{}\" {} ({})", s_Text.empty() ? "" : ", ", SubName(*s_Graph, s_Skip.feeder),
                                  s_Skip.pin, BriefingClassOf(s_Group, s_Skip.feeder));
        }

        for (const int s_Speaker : s_Group.speakers) {
            const auto s_Entity = s_Group.entities.find(s_Speaker);
            const std::string s_Label = fmt::format("{} \"{}\" ({:016X})", s_BrickName, SubName(*s_Graph, s_Speaker),
                                                    SubId(*s_Graph, s_Speaker));
            const bool s_Added = s_Entity != s_Group.entities.end() && AddLine(s_Entity->second, s_Label);

            if (std::find(s_Silenced.begin(), s_Silenced.end(), s_Speaker) != s_Silenced.end())
                continue;

            const auto s_It = s_Feeds.find(s_Speaker);
            const std::string s_StartedBy =
                s_It == s_Feeds.end() ? std::string("nothing in its blueprint") : s_It->second;
            const auto s_Block = s_LineBlocks.find(s_Speaker);
            const auto s_Skip = s_LineSkips.find(s_Speaker);
            const std::string s_Line = fmt::format(
                "{} \"{}\" ({:016X}; started by {}; {})", s_BrickName, SubName(*s_Graph, s_Speaker),
                SubId(*s_Graph, s_Speaker), s_StartedBy,
                s_Block != s_LineBlocks.end() ? s_Block->second
                                              : std::string("only its Trigger input is blocked, which a "
                                                            "connection's signal bypasses: it may still play"));

            if (s_Skip != s_LineSkips.end())
                s_Kept.push_back(fmt::format("{} \"{}\" ({:016X}): {}", s_BrickName, SubName(*s_Graph, s_Speaker),
                                             SubId(*s_Graph, s_Speaker), s_Skip->second));

            if (s_Block != s_LineBlocks.end() || s_Skip == s_LineSkips.end())
                (s_Added || s_Block != s_LineBlocks.end() ? s_Lines : s_LeftOn).push_back(s_Line);
        }
    }

    // The DianaSpeaks of the blueprints not read above (no briefing listener, or not copied): muted too, named by
    // their id.
    for (const auto& [s_Key, s_Group] : s_Groups) {
        for (const int s_Speaker : s_Group.speakers) {
            const auto s_Entity = s_Group.entities.find(s_Speaker);
            uint64_t s_Id = 0;

            for (const auto& [s_Index, s_SeenId] : s_Group.ids) {
                if (s_Index == s_Speaker)
                    s_Id = s_SeenId;
            }

            const std::string s_Label = fmt::format("{} DianaSpeak {:016X}", BriefingBrickName(s_Group.brickRid), s_Id);

            if (s_Entity == s_Group.entities.end() || m_LineEntities.contains(s_Entity->second))
                continue;

            (AddLine(s_Entity->second, s_Label) ? s_Lines : s_LeftOn)
                .push_back(fmt::format("{} (its blueprint was not read, so only its Trigger input is blocked, which a "
                                       "connection's signal bypasses: it may still play)",
                                       s_Label));
        }
    }

    // The mission stories (opportunity template instances, core/MissionStories.h): each pin connection into one
    // is blocked at its feeding output pin, the signal sent on along that pin's other links, so the story is never
    // unlocked or tracked.
    struct StoryBrick {
        uint64_t rid = 0;
        size_t found = 0;
        size_t off = 0;                 // every feed blocked
        std::vector<std::string> items; // one per story
    };
    std::vector<StoryBrick> s_StoryBricks; // in walk order

    const auto s_StoryBrick = [&](uint64_t p_Rid) -> StoryBrick& {
        for (StoryBrick& s_Brick : s_StoryBricks) {
            if (s_Brick.rid == p_Rid)
                return s_Brick;
        }

        s_StoryBricks.emplace_back().rid = p_Rid;
        return s_StoryBricks.back();
    };

    for (const auto& [s_Key, s_Group] : s_Groups) {
        if (s_Group.stories.empty())
            continue;

        StoryBrick& s_Brick = s_StoryBrick(s_Group.brickRid);
        s_Brick.found += s_Group.stories.size();
        std::string s_Why;
        const std::optional<fr::BriefingGraph> s_Graph = BriefingGraphOf(s_Group, &s_Why);

        if (!s_Graph) {
            for (const int32_t s_Story : s_Group.stories) {
                uint64_t s_Id = 0;

                for (const auto& [s_Index, s_SeenId] : s_Group.ids) {
                    if (s_Index == s_Story)
                        s_Id = s_SeenId;
                }

                // A blueprint with a connection into Unlock or SetTracked is always copied (unless the copy budget
                // dropped it): one not copied feeds the story by no such pin.
                s_Brick.items.push_back(fmt::format("{:016X}: not checked, its blueprint could not be read ({}); if it "
                                                    "shows in game, this is why",
                                                    s_Id, s_Why));
            }

            continue;
        }

        const fr::StoryFind s_Find = fr::FindStoryMutes(s_Group.stories, s_Graph->ids.size(), s_Graph->links);
        std::map<int32_t, std::string> s_Blocked; // story -> where it is blocked
        std::map<int32_t, std::string> s_Left;    // story -> the feeds left on, and why

        for (const fr::StoryMute& s_Mute : s_Find.mutes) {
            const std::string s_Feeder = fmt::format("\"{}\" {} ({})", SubName(*s_Graph, s_Mute.entity), s_Mute.pin,
                                                     BriefingClassOf(s_Group, s_Mute.entity));
            std::string s_EntityWhy;
            ZEntityType** const s_Entity = GroupEntityAt(s_Group, s_Mute.entity, &s_EntityWhy);

            // The pin's other logic: the signal is sent on along these links when the pin is blocked.
            std::string s_ResendText;
            std::string s_ResendWhy;
            const auto s_Resend = ResolveResend(s_Group, *s_Graph, s_Mute.resend, &s_ResendText, &s_ResendWhy);
            const std::string s_Others =
                s_ResendText.empty() ? std::string() : fmt::format("; its other logic sent on to {}", s_ResendText);
            std::string s_Failed;
            std::string s_Pins; // the story pins it feeds

            for (size_t k = 0; k < s_Mute.stories.size(); ++k)
                s_Pins += fmt::format("{}\"{}\" {}", s_Pins.empty() ? "" : ", ", SubName(*s_Graph, s_Mute.stories[k]),
                                      s_Mute.storyPins[k]);

            if (!s_Entity)
                s_Failed = s_EntityWhy;
            else if (!s_Resend)
                s_Failed = fmt::format("it also starts {}, which could not be looked up", s_ResendWhy);
            else if (!AddLineOut(s_Entity, s_Mute.pin, *s_Resend,
                                 fmt::format("{} {} ({:016X}; it feeds the mission story's {}{})",
                                             BriefingBrickName(s_Group.brickRid), s_Feeder,
                                             SubId(*s_Graph, s_Mute.entity), s_Pins, s_Others),
                                 LineOutKind::Story))
                s_Failed = fmt::format("more than {} pins to block", kMaxLines);

            for (size_t k = 0; k < s_Mute.stories.size(); ++k) {
                std::string& s_Text = s_Failed.empty() ? s_Blocked[s_Mute.stories[k]] : s_Left[s_Mute.stories[k]];
                s_Text += fmt::format("{}{} -> {}{}", s_Text.empty() ? "" : ", ", s_Feeder, s_Mute.storyPins[k],
                                      s_Failed.empty() ? s_Others : fmt::format(" (not blocked: {})", s_Failed));
            }
        }

        for (const fr::StorySkip& s_Skip : s_Find.skipped) {
            std::string& s_Text = s_Left[s_Skip.story];
            s_Text +=
                fmt::format("{}\"{}\" {} -> {} ({})", s_Text.empty() ? "" : ", ", SubName(*s_Graph, s_Skip.feeder),
                            s_Skip.pin, s_Skip.storyPin,
                            s_Skip.why == fr::StorySkipWhy::Forwarded
                                ? std::string("forwarded from the blueprint above, which is not searched")
                                : fmt::format("that pin also starts more than {} other links", fr::kLineMaxResend));
        }

        for (const int32_t s_Story : s_Group.stories) {
            const std::string s_Label =
                fmt::format("\"{}\" ({:016X})", SubName(*s_Graph, s_Story), SubId(*s_Graph, s_Story));
            const auto s_Off = s_Blocked.find(s_Story);
            const auto s_On = s_Left.find(s_Story);

            if (s_On != s_Left.end()) {
                s_Brick.items.push_back(fmt::format(
                    "{}: not switched off, it may still show: left on at {}{}", s_Label, s_On->second,
                    s_Off != s_Blocked.end() ? fmt::format("; blocked at {}", s_Off->second) : std::string()));
            }
            else if (s_Off != s_Blocked.end()) {
                ++s_Brick.off;
                s_Brick.items.push_back(fmt::format("{}: switched off, blocked at {}", s_Label, s_Off->second));
            }
            else {
                s_Brick.items.push_back(fmt::format("{}: left as it is, nothing in its blueprint unlocks or tracks it "
                                                    "(hidden unless its own reveal shows it when the player finds it)",
                                                    s_Label));
            }
        }
    }

    // The variant's own commentary (core/VariantComments.h): each ZHintEntity's OnHintStarted blocked, so no line
    // starts; Sloth's vitality kept full by blocking every pin into its helper, as a mission story's feeds are.
    fr::CommentTally s_Comments;
    std::string s_CommentFailures;

    for (const HintSeen& s_Hint : s_Hints) {
        ++s_Comments.hints;
        const std::string s_HintName = StoryName({std::string(fr::kHintEntityClass), s_Hint.entity,
                                                  s_Hint.parentFactory, s_Hint.index, s_Hint.id, s_Hint.brickRid});
        const std::string s_Label =
            fmt::format("{} ZHintEntity {:016X}{} {}", BriefingBrickName(s_Hint.brickRid), s_Hint.id,
                        s_HintName.empty() ? std::string() : fmt::format(" \"{}\"", s_HintName), fr::kHintStartedPin);

        if (AddLineOut(s_Hint.entity, std::string(fr::kHintStartedPin), {}, s_Label, LineOutKind::Comment))
            ++s_Comments.hintsMuted;
        else
            s_CommentFailures += fmt::format("{}{}: more than {} pins to block", s_CommentFailures.empty() ? "" : "; ",
                                             s_Label, kMaxLines);
    }

    // Blocks one feeding pin (fr::FindStoryMutes, fr::FindSinLineMutes), its signal sent on along the mute's resend
    // links: empty, or why it was left on.
    const auto s_ArmFeed = [&](const BriefingGroup& p_Group, const fr::BriefingGraph& p_Graph,
                               const fr::StoryMute& p_Mute, const std::string& p_Label) -> std::string {
        std::string s_EntityWhy;
        ZEntityType** const s_Entity = GroupEntityAt(p_Group, p_Mute.entity, &s_EntityWhy);

        if (!s_Entity)
            return s_EntityWhy;

        // The pin's other logic: the signal is sent on along these links when the pin is blocked.
        std::string s_Names;
        std::string s_Why;
        const auto s_Resend = ResolveResend(p_Group, p_Graph, p_Mute.resend, &s_Names, &s_Why);

        if (!s_Resend)
            return fmt::format("it also starts {}, which could not be looked up", s_Why);

        if (!AddLineOut(s_Entity, p_Mute.pin, *s_Resend, p_Label, LineOutKind::Comment))
            return fmt::format("more than {} pins to block", kMaxLines);

        return {};
    };

    for (const auto& [s_Key, s_Group] : s_Groups) {
        if (s_Group.vitality.empty())
            continue;

        s_Comments.vitality += s_Group.vitality.size();
        std::string s_Why;
        const std::optional<fr::BriefingGraph> s_Graph = BriefingGraphOf(s_Group, &s_Why);

        if (!s_Graph) {
            s_Comments.vitalityWhy = fmt::format("its blueprint was not read: {}", s_Why);
            continue;
        }

        const fr::StoryFind s_Find = fr::FindStoryMutes(s_Group.vitality, s_Graph->ids.size(), s_Graph->links);
        s_Comments.feedsLeft += s_Find.skipped.size();

        for (const fr::StoryMute& s_Mute : s_Find.mutes) {
            const std::string s_Feeder = fmt::format("\"{}\" {} ({})", SubName(*s_Graph, s_Mute.entity), s_Mute.pin,
                                                     BriefingClassOf(s_Group, s_Mute.entity));
            const std::string s_Failed =
                s_ArmFeed(s_Group, *s_Graph, s_Mute,
                          fmt::format("{} {} ({:016X}; it changes Sloth's vitality)",
                                      BriefingBrickName(s_Group.brickRid), s_Feeder, SubId(*s_Graph, s_Mute.entity)));

            if (s_Failed.empty()) {
                ++s_Comments.feedsBlocked;
                continue;
            }

            ++s_Comments.feedsLeft;
            s_CommentFailures +=
                fmt::format("{}{} left on: {}", s_CommentFailures.empty() ? "" : "; ", s_Feeder, s_Failed);
        }
    }

    // Envy's and Lust's lines (core/VariantComments.h): every pin that starts one blocked, its signal sent on along
    // its other links and the line's own end links, so the line is skipped as if it had played.
    for (const auto& [s_Key, s_Group] : s_Groups) {
        if (s_Group.sinLines.empty())
            continue;

        s_Comments.lines += s_Group.sinLines.size();
        std::string s_Why;
        const std::optional<fr::BriefingGraph> s_Graph = BriefingGraphOf(s_Group, &s_Why);

        if (!s_Graph) {
            s_Comments.linesWhy = fmt::format("its blueprint was not read: {}", s_Why);
            continue;
        }

        const fr::StoryFind s_Find = fr::FindSinLineMutes(s_Group.sinLines, s_Graph->ids.size(), s_Graph->links);
        std::set<int32_t> s_Left; // lines that may still play

        for (const fr::StoryMute& s_Mute : s_Find.mutes) {
            const std::string s_Feeder = fmt::format("\"{}\" {} ({})", SubName(*s_Graph, s_Mute.entity), s_Mute.pin,
                                                     BriefingClassOf(s_Group, s_Mute.entity));
            std::string s_Spoken;

            for (const int32_t s_Line : s_Mute.stories) {
                const fr::SinLine* const s_Known = fr::FindSinLine(SubId(*s_Graph, s_Line));
                s_Spoken +=
                    fmt::format("{}{}'s \"{}\"", s_Spoken.empty() ? "" : ", ",
                                s_Known ? s_Known->sin : std::string_view("the sin"), SubName(*s_Graph, s_Line));
            }

            const std::string s_Failed =
                s_ArmFeed(s_Group, *s_Graph, s_Mute,
                          fmt::format("{} {} ({:016X}; it starts {})", BriefingBrickName(s_Group.brickRid), s_Feeder,
                                      SubId(*s_Graph, s_Mute.entity), s_Spoken));

            if (s_Failed.empty()) {
                ++s_Comments.lineFeeds;
                continue;
            }

            s_Left.insert(s_Mute.stories.begin(), s_Mute.stories.end());
            s_CommentFailures += fmt::format("{}{} -> {} left on: {}", s_CommentFailures.empty() ? "" : "; ", s_Feeder,
                                             s_Spoken, s_Failed);
        }

        for (const fr::StorySkip& s_Skip : s_Find.skipped) {
            s_Left.insert(s_Skip.story);
            s_CommentFailures +=
                fmt::format("{}\"{}\" {} -> \"{}\" left on: {}", s_CommentFailures.empty() ? "" : "; ",
                            SubName(*s_Graph, s_Skip.feeder), s_Skip.pin, SubName(*s_Graph, s_Skip.story),
                            s_Skip.why == fr::StorySkipWhy::Forwarded
                                ? std::string("forwarded from the blueprint above, which is not searched")
                                : fmt::format("that pin also starts more than {} other links", fr::kLineMaxResend));
        }

        for (const int32_t s_Line : s_Find.unfed) {
            s_Left.insert(s_Line);
            s_CommentFailures += fmt::format("{}\"{}\": nothing in its blueprint starts it, so it was not found",
                                             s_CommentFailures.empty() ? "" : "; ", SubName(*s_Graph, s_Line));
        }

        for (const int32_t s_Line : s_Group.sinLines) {
            if (!s_Left.contains(s_Line))
                ++s_Comments.linesMuted;
        }
    }

    if (s_Pins > 0)
        m_BriefingPinsOn.store(true, std::memory_order_release);

    if (m_LineCount > 0)
        m_LinePinsOn.store(true, std::memory_order_release);

    if (m_LineOutCount > 0)
        m_LineOutsOn.store(true, std::memory_order_release);

    if (s_Pins > 0 || m_LineOutCount > 0)
        m_OutputPinsOn.store(true, std::memory_order_release);

    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    std::string s_Searched = fmt::format(
        "searched {} brick{}, {} entities, {} game event listeners, {} Diana speech entities{}{}, {:.1f} ms",
        s_Order.size(), s_Order.size() == 1 ? "" : "s", s_Walk.visited, s_Listeners, s_Speakers,
        s_Walk.capped ? " (stopped at the entity cap)" : "",
        s_Walk.faults + s_ReadFaults ? fmt::format(", {} read faults", s_Walk.faults + s_ReadFaults) : "", s_Ms);
    const briefing_graphs::Stats s_Copies = briefing_graphs::GetStats();
    s_Searched += fmt::format("; {} of their blueprints read; {} blueprints copied while loading ({} held)", s_Read,
                              s_Copies.copied, s_Copies.kept);

    if (!s_NotLoaded.empty())
        s_Notes.push_back(fmt::format("not loaded, so not searched: {}", s_NotLoaded));

    std::string s_NoteText;

    for (const std::string& s_Note : s_Notes)
        s_NoteText += "; " + s_Note;

    const char* s_When = p_LastChance ? "scene start" : "scene allocated";

    // One line for the mission stories, per brick that has any.
    const auto s_LogStories = [&] {
        const std::string s_StorySearched = fmt::format(
            "at {}; searched {} brick{}, {} entities{}{}, {:.1f} ms; {} blueprints copied while loading ({} held)",
            s_When, s_Order.size(), s_Order.size() == 1 ? "" : "s", s_Walk.visited,
            s_Walk.capped ? " (stopped at the entity cap)" : "",
            s_Walk.faults + s_ReadFaults ? fmt::format(", {} read faults", s_Walk.faults + s_ReadFaults) : "", s_Ms,
            s_Copies.copied, s_Copies.kept);
        const std::string s_StoryNotes =
            s_NotLoaded.empty() ? std::string() : fmt::format("; not loaded, so not searched: {}", s_NotLoaded);

        if (s_StoryBricks.empty()) {
            LogTo(fr::LogTopic::MapVariants,
                  "variant: {}: mission stories: none in this variant's bricks, nothing to switch off ({}){}", s_Name,
                  s_StorySearched, s_StoryNotes);
            return;
        }

        std::string s_List;
        size_t s_Found = 0;
        size_t s_Off = 0;

        for (const StoryBrick& s_Brick : s_StoryBricks) {
            std::string s_Items;

            for (const std::string& s_Item : s_Brick.items)
                s_Items += fmt::format("{}{}", s_Items.empty() ? "" : "; ", s_Item);

            s_List += fmt::format("{}{}: {} found, {} switched off: {}", s_List.empty() ? "" : "; ",
                                  BriefingBrickName(s_Brick.rid), s_Brick.found, s_Brick.off, s_Items);
            s_Found += s_Brick.found;
            s_Off += s_Brick.off;
        }

        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: mission stories: {} found, {} switched off (Freelancer has none; this variant's own are "
              "kept from unlocking and tracking, each block logged once when it fires; if one still shows in game, "
              "this line names where it is fed): {} ({}){}",
              s_Name, s_Found, s_Off, s_List, s_StorySearched, s_StoryNotes);
    };

    // The variant's own map markers (its contract's objectives and mission stories; Freelancer's live in other
    // bricks, never searched): never shown on the map.
    if (!m_StoriesOnly || !m_DianaRids.empty()) {
        std::string s_MarkerFailures;
        const fr::MarkerTally s_MarkerTally = SwitchOffMarkers(s_Markers, &s_MarkerFailures);

        LogTo(
            fr::LogTopic::MapVariants,
            "variant: {}: map markers of this variant's own contract (objectives, mission stories) in its "
            "bricks: {} (at {}; Freelancer's own markers are in other bricks and stay){}; if a marker of this variant "
            "still shows on the map, this line says what was found",
            s_Name, fr::MarkerTallyText(s_MarkerTally), s_When,
            s_MarkerFailures.empty() ? std::string() : fmt::format("; not switched off: {}", s_MarkerFailures));
    }

    // The variant's own timed damage (Sloth's: 10 health a second once its vitality runs out): never hurts 47.
    if (!m_StoriesOnly || !m_DianaRids.empty()) {
        std::string s_DamageFailures;
        const fr::DamageTally s_DamageTally = SwitchOffDamage(s_Damage, &s_DamageFailures);

        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: {} (at {}){}; if 47 still loses health on his own on this variant, this line "
              "says what was found",
              s_Name, fr::DamageTallyText(s_DamageTally), s_When,
              s_DamageFailures.empty() ? std::string() : fmt::format("; not switched off: {}", s_DamageFailures));

        // The variant's own commentary (Sloth's vitality comments and the vitality behind them, Envy's and Lust's
        // lines).
        LogTo(
            fr::LogTopic::MapVariants,
            "variant: {}: {} (at {}){}; if the variant's voice still comments on 47 "
            "(Sloth's \"that's ninety\", Envy's score against its rival, Lust's clues), this line says what was found",
            s_Name, fr::CommentTallyText(s_Comments), s_When,
            s_CommentFailures.empty() ? std::string() : fmt::format("; not muted: {}", s_CommentFailures));
    }

    // A full variant's swapped scene without a brick appended after the package's: its mission stories only.
    if (m_StoriesOnly && m_DianaRids.empty()) {
        s_LogStories();
        return;
    }

    if (s_Muted.empty()) {
        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: Diana's briefing: none found in this variant's bricks, nothing muted (at {}; {}){}; if "
              "Diana talks over Freelancer's briefing on this variant, this line and the next ones help find why",
              s_Name, s_When, s_Searched, s_NoteText);
    }
    else {
        std::string s_List;

        for (const std::string& s_Item : s_Muted)
            s_List += fmt::format("{}{}", s_List.empty() ? "" : ", ", s_Item);

        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: Diana's briefing muted so it does not talk over Freelancer's: {} (at {}; {}){}", s_Name,
              s_List, s_When, s_Searched, s_NoteText);
    }

    if (!s_Lines.empty()) {
        std::string s_List;

        for (const std::string& s_Item : s_Lines)
            s_List += fmt::format("{}{}", s_List.empty() ? "" : ", ", s_Item);

        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: Diana's other lines muted (they belong to this variant's own story, not to Freelancer's "
              "mission; each logged once when blocked): {}",
              s_Name, s_List);
    }

    if (!s_LeftOn.empty()) {
        std::string s_List;

        for (const std::string& s_Item : s_LeftOn)
            s_List += fmt::format("{}{}", s_List.empty() ? "" : ", ", s_Item);

        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: Diana lines left on (more than {} to mute; they play when their own logic starts them): {}",
              s_Name, kMaxLines, s_List);
    }

    if (!s_Kept.empty()) {
        std::string s_List;

        for (const std::string& s_Item : s_Kept)
            s_List += fmt::format("{}{}", s_List.empty() ? "" : ", ", s_Item);

        LogTo(fr::LogTopic::MapVariants,
              "variant: {}: Diana lines left on (the pin that starts each also starts more than {} other links, so it "
              "was left on; Diana may still talk): {}",
              s_Name, fr::kLineMaxResend, s_List);
    }

    s_LogStories();
    LogStory(s_Name, s_StoryCounts);
}

bool Variants::AddLine(ZEntityType** p_Entity, const std::string& p_Label) {
    if (!p_Entity || m_LineCount >= kMaxLines || !m_LineEntities.insert(p_Entity).second)
        return false;

    LinePin& s_Pin = m_LinePins[m_LineCount];
    m_LineLabels[m_LineCount] = p_Label;
    s_Pin.logged.store(false, std::memory_order_relaxed);
    s_Pin.entity.store(p_Entity, std::memory_order_relaxed);
    ++m_LineCount;
    return true;
}

bool Variants::AddLineOut(ZEntityType** p_Entity, const std::string& p_Pin, const std::vector<Resend>& p_Resend,
                          const std::string& p_Label, LineOutKind p_Kind) {
    if (!p_Entity || p_Resend.size() > fr::kLineMaxResend)
        return false;

    const uint32_t s_PinId = Hash::Crc32(p_Pin.c_str(), p_Pin.size());
    const auto s_Flag = [p_Kind](LineOut& p_Out) -> bool& {
        return p_Kind == LineOutKind::Story ? p_Out.story : p_Kind == LineOutKind::Comment ? p_Out.comment : p_Out.line;
    };

    for (size_t i = 0; i < m_LineOutCount; ++i) {
        LineOut& s_Out = m_LineOuts[i];

        if (s_Out.entity.load(std::memory_order_relaxed) != p_Entity ||
            s_Out.pin.load(std::memory_order_relaxed) != s_PinId)
            continue;

        if (s_Flag(s_Out))
            return true; // armed already (one pin into lines of two blueprints)

        // One pin into a Diana line and a mission story: each block's resend list leaves out what it blocks, so
        // only the links both send on stay. Not armed yet (m_LineOutsOn is set once the search is done).
        size_t s_Kept = 0;

        for (size_t r = 0; r < s_Out.resendCount; ++r) {
            const Resend& s_Resend = s_Out.resend[r];
            const bool s_Both = std::any_of(p_Resend.begin(), p_Resend.end(), [&](const Resend& p_Other) {
                return p_Other.entity == s_Resend.entity && p_Other.pin == s_Resend.pin &&
                       p_Other.output == s_Resend.output;
            });

            if (s_Both)
                s_Out.resend[s_Kept++] = s_Resend;
        }

        s_Out.resendCount = s_Kept;
        s_Flag(s_Out) = true;
        m_LineOutLabels[i] += "; also " + p_Label;
        return true;
    }

    if (m_LineOutCount >= kMaxLines)
        return false;

    LineOut& s_Out = m_LineOuts[m_LineOutCount];
    m_LineOutLabels[m_LineOutCount] = p_Label;
    std::copy(p_Resend.begin(), p_Resend.end(), s_Out.resend.begin());
    s_Out.resendCount = p_Resend.size();
    s_Out.line = p_Kind == LineOutKind::Line;
    s_Out.story = p_Kind == LineOutKind::Story;
    s_Out.comment = p_Kind == LineOutKind::Comment;
    s_Out.logged.store(false, std::memory_order_relaxed);
    s_Out.pin.store(s_PinId, std::memory_order_relaxed);
    s_Out.entity.store(p_Entity, std::memory_order_relaxed);
    ++m_LineOutCount;
    return true;
}

void Variants::ForgetBriefingEntities() {
    m_OutputPinsOn.store(false, std::memory_order_relaxed);
    m_LineOutsOn.store(false, std::memory_order_relaxed);

    for (LineOut& s_Out : m_LineOuts) {
        s_Out.entity.store(nullptr, std::memory_order_relaxed);
        s_Out.pin.store(0, std::memory_order_relaxed);
        s_Out.line = false;
        s_Out.story = false;
        s_Out.comment = false;
    }

    m_LineOutCount = 0;

    m_BriefingPinsOn.store(false, std::memory_order_relaxed);

    for (BriefingPin& s_Pin : m_BriefingPins) {
        s_Pin.entity.store(nullptr, std::memory_order_relaxed);
        s_Pin.pin.store(0, std::memory_order_relaxed);
    }

    m_BriefingBlockLogged.store(false, std::memory_order_relaxed);
    m_BriefingsFound = false;

    m_LinePinsOn.store(false, std::memory_order_relaxed);

    for (LinePin& s_Pin : m_LinePins)
        s_Pin.entity.store(nullptr, std::memory_order_relaxed);

    m_LineCount = 0;
    m_LineEntities.clear();
}

bool Variants::OnInputPin(const ZEntityRef& p_Entity, uint32_t p_PinId) noexcept {
    static_assert(Hash::Crc32(fr::kDianaLinePin.data(), fr::kDianaLinePin.size()) == fr::kPinTrigger);
    ZEntityType** const s_Entity = p_Entity.m_pObj;

    if (!s_Entity || p_PinId != fr::kPinTrigger || !m_LinePinsOn.load(std::memory_order_acquire))
        return false;

    for (size_t i = 0; i < kMaxLines; ++i) {
        ZEntityType** const s_Muted = m_LinePins[i].entity.load(std::memory_order_relaxed);

        if (!s_Muted)
            break; // filled in order

        if (s_Muted != s_Entity)
            continue;

        if (!m_LinePins[i].logged.exchange(true, std::memory_order_relaxed)) {
            try {
                LogTo(fr::LogTopic::MapVariants,
                      "variant: Diana line blocked: {} (this variant's own story, not Freelancer's)", m_LineLabels[i]);
            }
            catch (...) {
            }
        }

        return true;
    }

    return false;
}

bool Variants::OnOutputPin(const ZEntityRef& p_Entity, uint32_t p_PinId, const ZObjectRef& p_Data) noexcept {
    ZEntityType** const s_Entity = p_Entity.m_pObj;

    if (!s_Entity)
        return false;

    if (m_LineOutsOn.load(std::memory_order_acquire)) {
        for (size_t i = 0; i < kMaxLines; ++i) {
            ZEntityType** const s_Muted = m_LineOuts[i].entity.load(std::memory_order_relaxed);

            if (!s_Muted)
                break; // filled in order

            if (s_Muted != s_Entity || m_LineOuts[i].pin.load(std::memory_order_relaxed) != p_PinId)
                continue;

            const bool s_First = !m_LineOuts[i].logged.exchange(true, std::memory_order_relaxed);
            const char* const s_What = m_LineOuts[i].comment && !m_LineOuts[i].line && !m_LineOuts[i].story
                                           ? "variant commentary blocked"
                                       : !m_LineOuts[i].story ? "Diana line blocked"
                                       : m_LineOuts[i].line   ? "Diana line and mission story blocked"
                                                              : "mission story blocked";

            if (s_First) {
                try {
                    LogTo(fr::LogTopic::MapVariants, "variant: {}: {} (this variant's own story, not Freelancer's)",
                          s_What, m_LineOutLabels[i]);
                }
                catch (...) {
                }
            }

            // The pin's other logic still runs: the signal goes on along its other links (the speech's is dropped).
            const size_t s_Count = std::min(m_LineOuts[i].resendCount, fr::kLineMaxResend);
            const briefing_graphs::ResendResult s_Sent =
                briefing_graphs::SendOn(std::span(m_LineOuts[i].resend.data(), s_Count), s_Entity, p_PinId, p_Data);

            if (s_First && s_Count > 0) {
                try {
                    LogTo(fr::LogTopic::MapVariants, "variant: {}: its other logic sent on along {} of {} links{}",
                          s_What, s_Sent.sent, s_Count,
                          s_Sent.faults ? fmt::format(" ({} faulted)", s_Sent.faults) : std::string());
                }
                catch (...) {
                }
            }

            return true;
        }
    }

    if (!m_BriefingPinsOn.load(std::memory_order_acquire))
        return false;

    for (const BriefingPin& s_Pin : m_BriefingPins) {
        ZEntityType** const s_Muted = s_Pin.entity.load(std::memory_order_relaxed);

        if (!s_Muted)
            break; // filled in order

        if (s_Muted != s_Entity || s_Pin.pin.load(std::memory_order_relaxed) != p_PinId)
            continue;

        if (!m_BriefingBlockLogged.exchange(true, std::memory_order_relaxed)) {
            try {
                LogTo(fr::LogTopic::MapVariants,
                      "variant: Diana's briefing blocked (it would have talked over Freelancer's)");
            }
            catch (...) {
            }
        }

        return true;
    }

    return false;
}

void Variants::Tick(double p_GameSeconds) noexcept {
    m_HeroStart.Tick(p_GameSeconds);
    m_Exits.Tick(p_GameSeconds);

    if (!m_Watching)
        return;

    Contained("variant check", "play check", [&] {
        m_Played += p_GameSeconds;

        if (m_Played < fr::kVariantCheckPlaySeconds)
            return;

        m_Watching = false;
        PlayCheck();
    });
}

// The pickers of the mission brick (its walk also tells which pool actors
// are its own, dedicated targets), their pools, and each element's bits.
void Variants::LoadCheck(ZEntitySceneContext* p_Context) {
    m_Pool.clear();
    m_Played = 0.0;
    m_Watching = true; // the supplier is checked in play even when the pool is not

    const std::string s_Scene(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView());
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    const auto s_BrickPath = m_CheckBrick.empty() ? fr::MissionBrickPath(s_Scene) : std::optional(m_CheckBrick);

    // A full variant: whether each of its bricks in the package loaded, and
    // each vanilla brick its swap appends (the full Bad Boy's
    // mission_caipirinha, the full Gluttony's scenario_azalea).
    if (m_FullLoaded && (!m_CheckBrick.empty() || m_FullLoaded->appends)) {
        std::string s_Loaded;
        const auto s_Note = [&](uint64_t p_Rid, std::string_view p_Path) {
            const bool s_In = std::any_of(s_Bricks.begin(), s_Bricks.end(), [&](const auto& p_Brick) {
                return p_Brick.m_RuntimeResourceID.GetID() == p_Rid;
            });
            const size_t s_Slash = p_Path.find_last_of('/');
            s_Loaded += fmt::format("{}{} {}", s_Loaded.empty() ? "" : ", ",
                                    s_Slash == std::string_view::npos ? p_Path : p_Path.substr(s_Slash + 1),
                                    s_In ? "loaded" : "NOT LOADED");
        };
        const std::vector<LandslidePackage::OwnBrick> s_Own = LandslidePackage::OwnBricks(*m_FullLoaded);

        for (const LandslidePackage::OwnBrick& s_Brick : s_Own)
            s_Note(s_Brick.rid, s_Brick.path);

        for (const std::string_view s_Path : LandslidePackage::SwapBricks(*m_FullLoaded)) {
            const uint64_t s_Rid = ZRuntimeResourceID::FromString(fr::BrickResourcePath(s_Path)).GetID();

            if (std::none_of(s_Own.begin(), s_Own.end(), [&](const auto& p_Own) { return p_Own.rid == s_Rid; }))
                s_Note(s_Rid, s_Path);
        }

        LogTo(fr::LogTopic::MapVariants, "{}: full version at scene start: {} bricks loaded: {}", m_FullLoaded->key,
              s_Bricks.size(), s_Loaded);
    }

    if (!s_BrickPath) {
        LogTo(fr::LogTopic::MapVariants, "variant check: {}: no mission brick path; target pool not checked",
              m_CheckOption);
        return;
    }

    const uint64_t s_BrickRid = ZRuntimeResourceID::FromString(fr::BrickResourcePath(*s_BrickPath)).GetID();
    std::vector<size_t> s_Order;

    for (size_t i = 0; i < s_Bricks.size(); ++i) {
        if (s_Bricks[i].m_RuntimeResourceID.GetID() == s_BrickRid)
            s_Order.push_back(i);
    }

    if (s_Order.empty()) {
        LogTo(fr::LogTopic::MapVariants, "variant check: {}: mission brick {} not loaded; target pool not checked",
              m_CheckOption, *s_BrickPath);
        return;
    }

    const STypeID* s_ActorType =
        *Globals::TypeRegistry ? (*Globals::TypeRegistry)->GetTypeID(ZString(std::string_view("ZActor"))) : nullptr;

    if (!s_ActorType) {
        LogTo(fr::LogTopic::MapVariants, "variant check: {}: ZActor type unknown; target pool not checked",
              m_CheckOption);
        return;
    }

    const auto s_Start = std::chrono::steady_clock::now();
    std::unordered_set<ZEntityType**> s_Own;
    const TargetPickers s_Found = FindTargetPickers(s_Bricks, s_Order, &s_Own);
    const std::vector<ZEntityType**>& s_Pickers = s_Found.pickers;
    const WalkStats& s_Walk = s_Found.walk;
    std::string s_Class;

    // Each distinct pool once (a picker and its copies may share one list).
    std::vector<std::vector<ActorRef>> s_Pools;

    for (ZEntityType** s_Picker : s_Pickers) {
        ActorRefList s_List = ReadActorRefs(s_Picker);

        if (!s_List.why.empty()) {
            LogTo(fr::LogTopic::MapVariants, "variant check: {} unreadable: {}", kTargetPickerClass, s_List.why);
            continue;
        }

        if (std::find(s_Pools.begin(), s_Pools.end(), s_List.items) == s_Pools.end())
            s_Pools.push_back(std::move(s_List.items));
    }

    for (const auto& s_Items : s_Pools) {
        for (const ActorRef& s_Ref : s_Items) {
            CheckEntry s_Entry;
            s_Entry.entity = s_Ref.first;
            s_Entry.dedicated = s_Ref.first && s_Own.contains(s_Ref.first);

            if (s_Ref.first) {
                void* s_Actor = nullptr;

                if (!SafeInterface(s_Ref.first, s_ActorType, &s_Actor))
                    s_Entry.actor = static_cast<const ZActor*>(s_Actor);

                ReadEntityClassName(s_Ref.first, s_Class, &s_Entry.id);
            }

            m_Pool.push_back(s_Entry);
        }
    }

    const double s_Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s_Start).count();
    LogTo(fr::LogTopic::MapVariants,
          "variant check: {}: mission brick walk {} entities, {} faults{}, {:.1f} ms: {} {}, {} pool{}", m_CheckOption,
          s_Walk.visited, s_Walk.faults, s_Walk.capped ? " (capped: dedicated targets may count as scene NPCs)" : "",
          s_Ms, s_Pickers.size(), kTargetPickerClass, s_Pools.size(), s_Pools.size() == 1 ? "" : "s");

    if (s_Pools.empty()) {
        LogTo(fr::LogTopic::MapVariants, "variant check: {}: no readable target pool in the mission brick",
              m_CheckOption);
        return;
    }

    LogCheck("scene start", false);
}

void Variants::PlayCheck() {
    if (!m_Pool.empty())
        LogCheck(fmt::format("{:.0f} s of play", fr::kVariantCheckPlaySeconds).c_str(), true);

    // The supplier, by name among the activated actors (no walk).
    std::vector<ActorRef> s_Actors;
    size_t s_Total = 0;

    if (!Globals::ActorManager) {
        LogTo(fr::LogTopic::MapVariants, "variant check: {}: no actor manager; supplier not checked", m_CheckOption);
        return;
    }

    if (const uint32_t s_Code = ReadActivatedActors(Globals::ActorManager, s_Actors, &s_Total)) {
        LogTo(fr::LogTopic::MapVariants, "variant check: {}: activated actors unreadable ({}); supplier not checked",
              m_CheckOption, FaultText(s_Code));
        return;
    }

    size_t s_Suppliers = 0;

    for (const ActorRef& s_Ref : s_Actors) {
        const ActorFacts s_Facts = ReadActor(s_Ref.second);
        const auto s_Role = fr::RoleFromActorName(s_Facts.plainName);

        if (!s_Role || *s_Role != "supplier")
            continue;

        ++s_Suppliers;
        fr::VariantCheckActor s_Check {
            true, s_Facts.hasCore, false, s_Facts.core.startEnabled, s_Facts.core.enabled, s_Facts.core.alive};
        const auto s_Problem = fr::VariantCheckProblem(s_Check, true);
        LogTo(fr::LogTopic::MapVariants, "variant check: supplier {}: {}", s_Facts.name,
              s_Problem ? *s_Problem : std::string("ok"));
    }

    if (s_Suppliers == 0)
        LogTo(fr::LogTopic::MapVariants,
              "variant check: {}: no supplier among {} activated actors: it may be missing (softlock risk if the "
              "mission needs it)",
              m_CheckOption, s_Total);
}

void Variants::LogCheck(const char* p_Moment, bool p_Playing) {
    std::vector<fr::VariantCheckActor> s_Checks;
    size_t s_Lines = 0;

    for (size_t i = 0; i < m_Pool.size(); ++i) {
        const CheckEntry& s_Entry = m_Pool[i];
        fr::VariantCheckActor s_Check;
        s_Check.present = s_Entry.entity && s_Entry.actor;
        s_Check.dedicated = s_Entry.dedicated;
        ActorFacts s_Facts;

        if (s_Check.present) {
            s_Facts = ReadActor(s_Entry.actor);
            s_Check.readable = s_Facts.hasCore;
            s_Check.startEnabled = s_Facts.core.startEnabled;
            s_Check.enabled = s_Facts.core.enabled;
            s_Check.alive = s_Facts.core.alive;
        }

        s_Checks.push_back(s_Check);

        if (const auto s_Problem = fr::VariantCheckProblem(s_Check, p_Playing); s_Problem && s_Lines < kMaxCheckLines) {
            ++s_Lines;
            LogTo(fr::LogTopic::MapVariants, "variant check: [{}] {} {:016x} ({}): {}", i,
                  s_Facts.name.empty() ? "<no actor>" : s_Facts.name, s_Entry.id,
                  s_Entry.dedicated ? "mission brick" : "scene NPC", *s_Problem);
        }
    }

    LogTo(fr::LogTopic::MapVariants, "variant check: {} at {}: {}", m_CheckOption, p_Moment,
          fr::VariantCheckSummary(s_Checks, p_Playing));
}

void Variants::Publish(std::string p_Line) {
    std::lock_guard s_Lock(m_OverlayMutex);
    m_Overlay = std::move(p_Line);
}
