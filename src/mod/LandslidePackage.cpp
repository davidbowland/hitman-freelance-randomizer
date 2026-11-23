// The full map variants' packages (one per partition) and their safety net (see LandslidePackage.h).

#include "LandslidePackage.h"

#include "FreelancerRandomizer.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "PatchPackage.h"
#include "PatchPackageGlue.h"
#include "../core/Recipe.h"
#include "../core/Scene.h" // fr::ToLower

#include <Hooks.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>

namespace {

namespace fs = std::filesystem;

// The packages' recipes and tables (tools/landslide/gen_package_inc.py --recipes; committed: no game data).
namespace data {
#include "LandslideRecipes.inc"
} // namespace data

// p_Variant's tables in its package (its name, scene and partition), or null.
const data::Variant* TableOf(const fr::FullVariant& p_Variant) {
    for (const data::Variant& s_Table : data::kVariants) {
        if (p_Variant.key == s_Table.name && fr::ToLower(p_Variant.scene) == fr::ToLower(s_Table.scene) &&
            p_Variant.partition == s_Table.partition)
            return &s_Table;
    }

    return nullptr;
}

// p_Variant's position in fr::FullVariants() (the sentinel flags' index).
size_t IndexOf(const fr::FullVariant& p_Variant) {
    const auto s_All = fr::FullVariants();

    for (size_t i = 0; i < s_All.size(); ++i)
        if (s_All[i] == &p_Variant)
            return i;

    return s_All.size();
}

// The resources partition p_Partition's package holds when every variant builds: a file of the mod's holds only
// these (fewer when some can't be built).
std::vector<uint64_t> OwnRids(int p_Partition) {
    std::vector<uint64_t> s_Out;

    for (const fr::RecipeResource& s_Res : data::kResources)
        if (s_Res.partition == p_Partition)
            s_Out.push_back(s_Res.rid);

    return s_Out;
}

// The resources earlier builds' packages for partition p_Partition held (a stale file is still the mod's).
std::vector<uint64_t> FormerRids(int p_Partition) {
    if (p_Partition != data::kFormerPartition)
        return {};

    return std::vector<uint64_t>(data::kFormerRids.begin(), data::kFormerRids.end());
}

// Partition p_Partition's variants as the recipes build them (a layer needs its base), and each one's
// fr::FullVariants() index.
std::vector<std::pair<fr::RecipeVariant, size_t>> PartitionVariants(int p_Partition) {
    std::vector<std::pair<fr::RecipeVariant, size_t>> s_Out;
    const auto s_All = fr::FullVariants();

    for (size_t i = 0; i < s_All.size(); ++i)
        if (const data::Variant* s_Table = TableOf(*s_All[i]); s_Table && s_Table->partition == p_Partition)
            s_Out.push_back({{s_All[i]->key, s_Table->own, s_All[i]->layerOf}, i});

    return s_Out;
}

// "00F48FAD6DC84140 TEMP [...fr_mission_mild_landslide.brick], a copy of 00D15B6A8159D00D from chunk26patch3.rpkg; ..."
// for p_Rids.
std::string DescribeResources(std::span<const uint64_t> p_Rids) {
    std::string s_Text;

    for (const fr::RecipeResource& s_Res : data::kResources)
        if (std::find(p_Rids.begin(), p_Rids.end(), s_Res.rid) != p_Rids.end())
            s_Text += fmt::format("{}{:016X} {} [{}], a copy of {:016X} from {}", s_Text.empty() ? "" : "; ", s_Res.rid,
                                  s_Res.type, s_Res.path, s_Res.sourceRid, s_Res.source);

    return s_Text;
}

// "ok", or "SourceHash on 00CAFC0C93568981 scenario_rat.brick".
std::string DescribeOutcome(const fr::RecipeOutcome& p_Outcome) {
    if (p_Outcome.failure == fr::RecipeFailure::None)
        return "ok";

    std::string_view s_Name;

    for (const fr::RecipeSource& s_Source : data::kSources)
        if (s_Source.rid == p_Outcome.sourceRid)
            s_Name = s_Source.name;

    return fmt::format("{} on {:016X}{}{}", fr::RecipeFailureName(p_Outcome.failure), p_Outcome.sourceRid,
                       s_Name.empty() ? "" : " ", s_Name);
}

// The other patch numbers of chunk p_Partition in p_Runtime; *p_Error set when the listing failed.
std::vector<int> ListOtherPatches(const fs::path& p_Runtime, int p_Partition, std::string* p_Error) {
    std::vector<int> s_Out;
    std::error_code s_Error;

    for (const fs::directory_entry& s_Entry : fs::directory_iterator(p_Runtime, s_Error)) {
        const std::optional<int> s_Number = pkg::ChunkPatchNumber(p_Partition, s_Entry.path().filename().string());

        if (s_Number && *s_Number != LandslidePackage::kPatch)
            s_Out.push_back(*s_Number);
    }

    if (s_Error)
        *p_Error = s_Error.message();

    return s_Out;
}

// Why a file in the mod's place is left alone, for the log.
std::string_view LeftAloneWhy(bool p_Readable) {
    return p_Readable ? "is not FreelancerRandomizer's package (another mod's?)" : "could not be read";
}

// The sentinel's lines joined with "; ", for one log line.
std::string OneLine(std::string_view p_Text) {
    std::string s_Out;

    for (const char c : p_Text) {
        if (c == '\r')
            continue;

        s_Out += c == '\n' ? std::string("; ") : std::string(1, c);
    }

    while (s_Out.size() >= 2 && s_Out.ends_with("; "))
        s_Out.resize(s_Out.size() - 2);

    return s_Out;
}

std::string LocalTime() {
    const std::time_t s_Now = std::time(nullptr);
    std::tm s_Local {};
    localtime_s(&s_Local, &s_Now);
    char s_Text[32];
    std::strftime(s_Text, sizeof(s_Text), "%Y-%m-%d %H:%M:%S", &s_Local);
    return s_Text;
}

// The newest copy of p_Rid in the resource container (the last hop of its chain).
fr::ResourceSeen Seen(uint64_t p_Rid, bool* p_Container) {
    fr::ResourceSeen s_Seen;
    pkg::ResourceChainOut s_Out {};

    if (const DWORD s_Code = pkg::SafeResourceChain(p_Rid, &s_Out)) {
        s_Seen.fault = s_Code;
        return s_Seen;
    }

    *p_Container = *p_Container && s_Out.container;

    if (!s_Out.found || s_Out.hops == 0)
        return s_Seen;

    const pkg::ResourceHop& s_Newest = s_Out.hop[s_Out.hops - 1];
    s_Seen.found = true;
    s_Seen.size = s_Newest.dataSize;
    s_Seen.package = s_Newest.package;
    return s_Seen;
}

} // namespace

LandslidePackage::LandslidePackage() {
    // The partitions with a package: every full variant's, in order of first appearance (Sapienza's first).
    for (const fr::FullVariant* s_Variant : fr::FullVariants()) {
        if (PartOf(s_Variant->partition))
            continue;

        Part& s_Part = m_Parts.emplace_back();
        s_Part.index = s_Variant->partition;
        s_Part.id = s_Variant->partitionId;
        s_Part.log = fr::PackageLogName(s_Variant->partition, s_Variant->partitionId);
    }
}

LandslidePackage::Part* LandslidePackage::PartOf(int p_Index) noexcept {
    for (Part& s_Part : m_Parts)
        if (s_Part.index == p_Index)
            return &s_Part;

    return nullptr;
}

const LandslidePackage::Part* LandslidePackage::PartOf(int p_Index) const noexcept {
    for (const Part& s_Part : m_Parts)
        if (s_Part.index == p_Index)
            return &s_Part;

    return nullptr;
}

std::string LandslidePackage::FileName(int p_Partition) {
    return fr::FullPackageFileName(p_Partition, kPatch);
}

bool LandslidePackage::Installed() const noexcept {
    for (const Part& s_Part : m_Parts)
        if (s_Part.target.load() > 0)
            return true;

    return false;
}

bool LandslidePackage::Handles(int32_t p_Index) const noexcept {
    const Part* s_Part = PartOf(p_Index);
    return s_Part && s_Part->target.load() > 0;
}

bool LandslidePackage::BuiltIn() {
    return std::size(data::kResources) > 0;
}

bool LandslidePackage::BuiltIn(const fr::FullVariant& p_Variant) {
    return BuiltIn() && TableOf(p_Variant) != nullptr;
}

bool LandslidePackage::Buildable(const fr::FullVariant& p_Variant) const noexcept {
    const size_t s_Index = IndexOf(p_Variant);
    return s_Index < m_Unbuildable.size() && !m_Unbuildable[s_Index].load();
}

fr::FullAvailability LandslidePackage::Availability(const fr::FullVariant& p_Variant) const noexcept {
    const size_t s_Index = IndexOf(p_Variant);
    const Part* s_Part = PartOf(p_Variant.partition);
    return fr::JudgeFullAvailability({BuiltIn(p_Variant), SwitchedOff(), Buildable(p_Variant),
                                      s_Index < m_ReadFailed.size() && m_ReadFailed[s_Index].load(),
                                      !m_InstallRan.load() || (s_Part && s_Part->target.load() > 0)});
}

bool LandslidePackage::FreelancerLayer() {
    return data::kFreelancerLayer;
}

std::vector<std::string_view> LandslidePackage::SwapBricks(const fr::FullVariant& p_Variant) {
    std::vector<std::string_view> s_Out;

    if (const data::Variant* s_Table = TableOf(p_Variant))
        for (const char* s_Brick : s_Table->swapBricks)
            s_Out.push_back(s_Brick);

    return s_Out;
}

std::vector<LandslidePackage::OwnBrick> LandslidePackage::OwnBricks(const fr::FullVariant& p_Variant) {
    std::vector<OwnBrick> s_Out;
    const data::Variant* s_Table = TableOf(p_Variant);

    if (!s_Table)
        return s_Out;

    for (const fr::RecipeResource& s_Res : data::kResources)
        if (s_Res.type == "TEMP" &&
            std::find(s_Table->own.begin(), s_Table->own.end(), s_Res.rid) != s_Table->own.end())
            s_Out.push_back({s_Res.rid, s_Res.path});

    return s_Out;
}

fs::path LandslidePackage::SentinelPath(const fr::FullVariant& p_Variant) const {
    const fs::path s_Directory = ModDirectory();
    return s_Directory.empty() ? s_Directory : s_Directory / fr::FullSentinelFileName(p_Variant);
}

fs::path LandslidePackage::CachePath() const {
    const fs::path s_Directory = ModDirectory();
    return s_Directory.empty() ? s_Directory : s_Directory / fr::kPackageCacheFileName;
}

void LandslidePackage::Install(std::string_view p_Version, bool p_On) noexcept {
    m_SwitchedOff.store(!p_On);
    m_InstallRan.store(true);

    try {
        const fs::path s_Runtime = pkg::RuntimeFolder();
        bool s_IniRead = false;
        const std::vector<uint8_t> s_ModsIni =
            pkg::ReadFileBytes(s_Runtime.parent_path() / "Retail" / "mods.ini", &s_IniRead, size_t(1) << 20);
        m_MaxPatchLevel =
            s_IniRead &&
            pkg::IniHasSection(std::string_view(reinterpret_cast<const char*>(s_ModsIni.data()), s_ModsIni.size()),
                               "maxpatchlevel");
    }
    catch (const std::exception& e) {
        LogError("landslide package: reading mods.ini failed: {}", e.what());
    }
    catch (...) {
        LogError("landslide package: reading mods.ini failed: unknown exception");
    }

    try {
        if (p_On)
            InstallAll(p_Version);
        else
            Uninstall();
    }
    catch (const std::exception& e) {
        LogError("landslide package: installing the packages failed: {}", e.what());
    }
    catch (...) {
        LogError("landslide package: installing the packages failed: unknown exception");
    }

    // Each variant's build id names its own entries in the installed package (the same whether it was built
    // now or found in the cache): new entries give that full version a fresh try.
    try {
        const auto s_All = fr::FullVariants();

        for (size_t i = 0; i < s_All.size() && i < m_Builds.size(); ++i) {
            const Part* s_Part = PartOf(s_All[i]->partition);
            const std::optional<uint64_t> s_Hash =
                s_Part && s_Part->target.load() > 0 ? m_VariantHashes[i] : std::nullopt;
            m_Builds[i] = fr::VariantBuildId(p_Version, s_Hash);
            LogTo(fr::LogTopic::MapVariants, "{}: build {} (entries {})", s_All[i]->key, m_Builds[i],
                  fr::PartitionIdText(s_Hash));
        }
    }
    catch (const std::exception& e) {
        LogError("landslide package: computing the build id failed: {}", e.what());
    }
    catch (...) {
        LogError("landslide package: computing the build id failed: unknown exception");
    }

    // Each variant's sentinel on its own: one failing read never skips another's.
    for (const fr::FullVariant* s_Variant : fr::FullVariants()) {
        try {
            ReadSentinel(*s_Variant);
        }
        catch (const std::exception& e) {
            LogError("{}: reading the crash sentinel failed: {}", s_Variant->key, e.what());
        }
        catch (...) {
            LogError("{}: reading the crash sentinel failed: unknown exception", s_Variant->key);
        }
    }
}

void LandslidePackage::InstallAll(std::string_view p_Version) {
    const fs::path s_CachePath = CachePath();
    std::vector<fr::PackageCacheLine> s_Cached;

    if (!s_CachePath.empty()) {
        bool s_Read = false;
        const std::vector<uint8_t> s_Text = pkg::ReadFileBytes(s_CachePath, &s_Read, size_t(1) << 20);

        if (s_Read)
            s_Cached =
                fr::ParsePackageCache(std::string_view(reinterpret_cast<const char*>(s_Text.data()), s_Text.size()));
    }

    const uint64_t s_Tables = fr::RecipeTableHash(data::kSources, data::kLiterals, data::kResources);
    pkg::RuntimeIndexes s_Indexes(pkg::RuntimeFolder());
    std::vector<fr::PackageCacheLine> s_Lines;

    // Each partition's package on its own: one failing never stops another.
    for (Part& s_Part : m_Parts) {
        try {
            if (std::optional<fr::PackageCacheLine> s_Line =
                    InstallUnguarded(s_Part, p_Version, s_Tables, s_Indexes, s_Cached))
                s_Lines.push_back(std::move(*s_Line));

            continue;
        }
        catch (const std::exception& e) {
            LogError("{}: install failed: {}; its full versions are off", s_Part.log, e.what());
        }
        catch (...) {
            LogError("{}: install failed: unknown exception; its full versions are off", s_Part.log);
        }

        s_Part.target.store(0);
    }

    // Read at every start, cache hit or not.
    LogTo(fr::LogTopic::MapVariants, "landslide package: read {} package indexes in {} ms", s_Indexes.PackagesRead(),
          std::chrono::duration_cast<std::chrono::milliseconds>(s_Indexes.ReadTime()).count());

    // A partition left out is built again at the next start.
    const std::string s_Text = fr::FormatPackageCache(s_Lines);
    std::string s_Error = "the mod's folder was not found";

    if (s_CachePath.empty() ||
        !pkg::WriteFileAtomically(s_CachePath, std::vector<uint8_t>(s_Text.begin(), s_Text.end()), &s_Error))
        LogError("landslide package: could not write the cache {}: {}; the next start builds the packages again",
                 s_CachePath.string(), s_Error);
}

void LandslidePackage::Uninstall() {
    const fs::path s_Runtime = pkg::RuntimeFolder();

    for (const Part& s_Part : m_Parts) {
        const fs::path s_Own = s_Runtime / FileName(s_Part.index);
        std::error_code s_Error;

        if (!fs::exists(s_Own, s_Error))
            continue;

        bool s_Readable = true;
        const std::vector<uint8_t> s_Current = pkg::ReadFileBytes(s_Own, &s_Readable, size_t(64) << 20);

        switch (pkg::DecidePackageFile(false, &s_Current, s_Readable, OwnRids(s_Part.index), {},
                                       FormerRids(s_Part.index))) {
            case pkg::FileAction::Remove: {
                std::error_code s_RemoveError;
                fs::remove(s_Own, s_RemoveError);

                if (s_RemoveError)
                    LogError("{}: removing {} failed: {}", s_Part.log, s_Own.string(), s_RemoveError.message());
                else
                    LogTo(fr::LogTopic::MapVariants, "{}: removed {} (Full map variants is off)", s_Part.log,
                          s_Own.string());

                break;
            }
            case pkg::FileAction::NotOurs:
                LogTo(fr::LogTopic::MapVariants, "{}: {} exists and {}; left alone", s_Part.log, s_Own.string(),
                      LeftAloneWhy(s_Readable));
                break;
            default: break;
        }
    }

    const fs::path s_CachePath = CachePath();
    std::error_code s_Error;

    if (!s_CachePath.empty() && fs::exists(s_CachePath, s_Error) && !fs::remove(s_CachePath, s_Error))
        LogError("landslide package: removing {} failed: {}", s_CachePath.string(), s_Error.message());

    LogTo(fr::LogTopic::MapVariants,
          "landslide package: Full map variants is off (full_map_variants = false): no package installed; missions "
          "never pick the full variants");
}

void LandslidePackage::ReadSentinel(const fr::FullVariant& p_Variant) {
    std::lock_guard s_Lock(m_SentinelMutex);
    const fs::path s_Path = SentinelPath(p_Variant);
    const size_t s_Index = IndexOf(p_Variant);
    std::error_code s_Error;

    if (s_Path.empty() || s_Index >= m_TurnedOff.size() || !fs::exists(s_Path, s_Error))
        return;

    bool s_Readable = false;
    const std::vector<uint8_t> s_Bytes = pkg::ReadFileBytes(s_Path, &s_Readable, size_t(64) << 10);
    const std::string s_Text(s_Bytes.begin(), s_Bytes.end());

    switch (fr::JudgeSentinel(&s_Text, s_Readable, m_Builds[s_Index])) {
        case fr::SentinelAction::None: return;
        case fr::SentinelAction::TurnOff:
            m_TurnedOff[s_Index].store(true);
            Log("{}: full version turned off: the last {} load did not finish (crash or quit while loading; {} {}). "
                "Turn it back on in Settings to try again.",
                p_Variant.key, p_Variant.display, s_Path.string(),
                s_Readable ? fmt::format("says: {}", OneLine(s_Text)) : std::string("could not be read"));
            return;
        case fr::SentinelAction::Discard:
            fs::remove(s_Path, s_Error);
            LogTo(fr::LogTopic::MapVariants,
                  "{}: removed {} from another build (\"{}\", this one is \"{}\"): the full version gets a fresh try",
                  p_Variant.key, s_Path.string(), fr::LandslideSentinelBuild(s_Text), m_Builds[s_Index]);
            return;
    }
}

std::optional<fr::PackageCacheLine> LandslidePackage::InstallUnguarded(Part& p_Part, std::string_view p_Version,
                                                                       uint64_t p_Tables,
                                                                       pkg::RuntimeIndexes& p_Indexes,
                                                                       std::span<const fr::PackageCacheLine> p_Cached) {
    const fs::path s_Runtime = pkg::RuntimeFolder();
    const fs::path s_Own = s_Runtime / FileName(p_Part.index);
    const std::string s_Chunk = "chunk" + std::to_string(p_Part.index) + ".rpkg";
    const std::string_view s_Log = p_Part.log;
    std::error_code s_Error;

    if (!fs::exists(s_Runtime / s_Chunk, s_Error)) {
        LogTo(fr::LogTopic::MapVariants, "{}: not installed: no {} in {}", s_Log, s_Chunk, s_Runtime.string());
        return std::nullopt;
    }

    const pkg::RecipeTables s_Tables {data::kSources, data::kLiterals, data::kResources};
    const std::vector<std::pair<fr::RecipeVariant, size_t>> s_Indexed = PartitionVariants(p_Part.index);
    std::vector<fr::RecipeVariant> s_Variants;
    std::vector<std::string_view> s_Keys;

    for (const auto& [s_Variant, s_Index] : s_Indexed) {
        s_Variants.push_back(s_Variant);
        s_Keys.push_back(s_Variant.key);
    }

    const std::vector<uint64_t> s_OwnRids = OwnRids(p_Part.index);
    const std::vector<uint64_t> s_FormerRids = FormerRids(p_Part.index);
    const auto s_Start = std::chrono::steady_clock::now();
    const auto s_Ms = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_Start)
            .count();
    };
    const pkg::PartitionPlan s_Plan = pkg::PlanPartition(s_Tables, p_Tables, p_Part.index, s_Variants, p_Version,
                                                         p_Indexes, p_Cached, s_Own, s_OwnRids, s_FormerRids);
    // Failed only because a game file could not be read.
    const auto s_ReadFailed = [](const fr::RecipeOutcome& p_Outcome, std::span<const pkg::SourceProblem> p_Problems) {
        return p_Outcome.failure != fr::RecipeFailure::None &&
               std::any_of(p_Problems.begin(), p_Problems.end(), [&](const pkg::SourceProblem& p_P) {
                   return p_P.readFailed && data::kSources[p_P.source].rid == p_Outcome.sourceRid;
               });
    };
    const auto s_Record = [&](std::span<const fr::RecipeOutcome> p_Outcomes,
                              std::span<const pkg::SourceProblem> p_Problems) {
        std::string s_States;

        for (size_t i = 0; i < s_Indexed.size() && i < p_Outcomes.size(); ++i) {
            m_Unbuildable[s_Indexed[i].second].store(p_Outcomes[i].failure != fr::RecipeFailure::None);
            m_ReadFailed[s_Indexed[i].second].store(s_ReadFailed(p_Outcomes[i], p_Problems));
            s_States += fmt::format("{}{} {}", s_States.empty() ? "" : ", ", s_Keys[i], DescribeOutcome(p_Outcomes[i]));
        }

        return s_States;
    };

    // Nothing changed since the last start: nothing is decoded.
    if (s_Plan.cached) {
        std::vector<fr::RecipeOutcome> s_Outcomes;

        for (const fr::CachedVariant& s_Variant : s_Plan.cached->variants)
            s_Outcomes.push_back(s_Variant.outcome);

        const std::string s_States = s_Record(s_Outcomes, {});
        LogTo(fr::LogTopic::MapVariants, "{}: up to date (from the cache in {} ms, no source decoded): {}", s_Log,
              s_Ms(), s_States);

        if (s_Plan.cached->packageHash)
            MarkInstalled(p_Part, s_Own, s_Plan.cached->fileSize);

        return s_Plan.cached;
    }

    const auto s_LeftAlone = [&](bool p_Readable) {
        LogTo(fr::LogTopic::MapVariants,
              "{}: {} exists and {}; left alone; its full versions cannot load, and their picks play the normal map",
              s_Log, s_Own.string(), LeftAloneWhy(p_Readable));
    };

    // Someone else's file there (or one that can't be read) is never replaced: nothing is decoded for it.
    if (fs::exists(s_Own, s_Error)) {
        if (const pkg::FileOwner s_Owner = pkg::HoldsOnlyFile(s_Own, s_OwnRids, s_FormerRids);
            s_Owner != pkg::FileOwner::Mod) {
            s_LeftAlone(s_Owner == pkg::FileOwner::Other);
            return std::nullopt;
        }
    }

    // Rebuilt from the game's files: each source decoded once, freed when the build returns.
    const pkg::PartitionBuild s_Build =
        pkg::BuildPartition(s_Tables, p_Part.index, s_Variants, s_Plan.sources, s_Plan.spots, s_Runtime);
    const std::string s_States = s_Record(s_Build.variants, s_Build.problems);
    LogTo(fr::LogTopic::MapVariants,
          "{}: built from the game's files in {} ms ({} of {} sources decoded, {} bytes): {}", s_Log, s_Ms(),
          s_Build.decoded, s_Plan.sources.size(), s_Build.decodedBytes, s_States);

    // A game file that could not be read may read fine next time: such an outcome is not cached.
    const auto s_Keep = [&](fr::PackageCacheLine p_Line) -> std::optional<fr::PackageCacheLine> {
        if (s_Build.Cacheable())
            return p_Line;

        LogTo(fr::LogTopic::MapVariants,
              "{}: not cached: a game file could not be read; the next start builds it again", s_Log);
        return std::nullopt;
    };

    const std::vector<uint8_t>& s_Wanted = s_Build.package;
    const bool s_Exists = fs::exists(s_Own, s_Error);
    bool s_Readable = true;
    std::vector<uint8_t> s_Current;

    // The mod's package is at most a few MB; anything over 64 MiB is not the mod's.
    if (s_Exists)
        s_Current = pkg::ReadFileBytes(s_Own, &s_Readable, size_t(64) << 20);

    const pkg::FileAction s_Action = pkg::DecidePackageFile(!s_Wanted.empty(), s_Exists ? &s_Current : nullptr,
                                                            s_Readable, s_OwnRids, s_Wanted, s_FormerRids);
    const auto s_Previous = std::find_if(p_Cached.begin(), p_Cached.end(), [&](const fr::PackageCacheLine& p_Line) {
        return p_Line.partition == p_Part.index;
    });
    std::optional<std::vector<fr::RecipeOutcome>> s_Kept;

    if (!s_Build.Cacheable() && s_Exists && s_Readable && s_Previous != p_Cached.end()) {
        std::vector<bool> s_Unread;

        for (const fr::RecipeOutcome& s_Outcome : s_Build.variants)
            s_Unread.push_back(s_ReadFailed(s_Outcome, s_Build.problems));

        s_Kept = fr::KeptOutcomes(&*s_Previous, s_Plan.recipeKey, s_Keys, s_Build.variants, s_Unread, s_Current);
    }

    const std::vector<fr::RecipeOutcome>& s_Final = s_Kept ? *s_Kept : s_Build.variants;

    for (size_t i = 0; i < s_Variants.size() && i < s_Final.size(); ++i) {
        const fr::RecipeOutcome& s_Outcome = s_Final[i];

        if (s_Outcome.failure == fr::RecipeFailure::None)
            continue;

        std::string s_Detail;

        // A kept outcome is the last good build's: this build's problems don't explain it.
        if (!s_Kept)
            for (const pkg::SourceProblem& s_Problem : s_Build.problems)
                if (data::kSources[s_Problem.source].rid == s_Outcome.sourceRid) {
                    const fr::RecipeSource& s_Source = data::kSources[s_Problem.source];
                    s_Detail = fmt::format(": {} {} {} in chunk {}'s packages: {}", s_Source.type, s_Source.name,
                                           fr::RecipeFailureName(s_Problem.failure), s_Source.chunk, s_Problem.detail);
                }

        Log("{}: full version can't be built from the game's files ({}){}; missions never pick it", s_Keys[i],
            DescribeOutcome(s_Outcome), s_Detail);
    }

    // A game file that could not be read may read fine next time: the package the last good build installed
    // stays as it is, its variants as that build had them; and a package of the mod's is never removed for it.
    if (s_Kept) {
        LogTo(fr::LogTopic::MapVariants,
              "{}: {} left as it is: a game file could not be read; the package the last good build installed "
              "serves: {}",
              s_Log, s_Own.string(), s_Record(*s_Kept, {}));
        MarkInstalled(p_Part, s_Own, s_Previous->fileSize);
        return *s_Previous;
    }

    if (s_Action == pkg::FileAction::Remove && !s_Build.Cacheable()) {
        LogTo(fr::LogTopic::MapVariants,
              "{}: {} left as it is: a game file could not be read, and it is not the package the last good build "
              "installed; its full versions are off until a start reads them",
              s_Log, s_Own.string());
        return std::nullopt;
    }

    if (s_Build.packageFailed) {
        LogError("{}: {} left as it is: building its package failed (a resource type is not 4 characters)", s_Log,
                 s_Own.string());
        return std::nullopt;
    }

    // Kept only once the file is as the line says (written, up to date, removed or never there).
    const fr::PackageCacheLine s_Empty =
        pkg::BuiltCacheLine(p_Part.index, s_Plan.sourceKey, s_Plan.recipeKey, s_Variants, s_Build.variants, {});

    switch (s_Action) {
        case pkg::FileAction::None: return s_Keep(s_Empty);
        case pkg::FileAction::NotOurs: s_LeftAlone(s_Readable); return std::nullopt;
        case pkg::FileAction::Remove: {
            std::error_code s_RemoveError;
            fs::remove(s_Own, s_RemoveError);

            if (s_RemoveError) {
                LogError("{}: removing {}, a package of the mod, failed: {}", s_Log, s_Own.string(),
                         s_RemoveError.message());
                return std::nullopt;
            }

            LogTo(fr::LogTopic::MapVariants, "{}: removed {}: none of its variants can be built", s_Log,
                  s_Own.string());
            return s_Keep(s_Empty);
        }
        case pkg::FileAction::UpToDate:
            LogTo(fr::LogTopic::MapVariants, "{}: {} is up to date ({} bytes)", s_Log, s_Own.string(), s_Wanted.size());
            break;
        case pkg::FileAction::Write: {
            std::string s_WriteError;

            if (!pkg::WriteFileAtomically(s_Own, s_Wanted, &s_WriteError)) {
                Log("{}: not installed: {}", s_Log, s_WriteError);
                return std::nullopt;
            }

            std::vector<uint64_t> s_Written;

            for (size_t i = 0; i < s_Variants.size() && i < s_Build.variants.size(); ++i)
                if (s_Build.variants[i].failure == fr::RecipeFailure::None)
                    s_Written.insert(s_Written.end(), s_Variants[i].resources.begin(), s_Variants[i].resources.end());

            std::sort(s_Written.begin(), s_Written.end());
            s_Written.erase(std::unique(s_Written.begin(), s_Written.end()), s_Written.end());
            LogTo(fr::LogTopic::MapVariants, "{}: wrote {} ({} bytes{}): {} resource{}: {}", s_Log, s_Own.string(),
                  s_Wanted.size(), s_Exists ? ", replacing a stale copy" : "", s_Written.size(),
                  s_Written.size() == 1 ? "" : "s", DescribeResources(s_Written));
            break;
        }
    }

    const fr::PackageCacheLine s_Line =
        pkg::BuiltCacheLine(p_Part.index, s_Plan.sourceKey, s_Plan.recipeKey, s_Variants, s_Build.variants, s_Wanted);
    MarkInstalled(p_Part, s_Own, s_Line.fileSize);
    return s_Keep(s_Line);
}

void LandslidePackage::MarkInstalled(Part& p_Part, const fs::path& p_Own, uintmax_t p_Size) {
    p_Part.own = p_Own;
    p_Part.ownSize = p_Size;

    // Each variant's entries (a layer's with its base's), for its build id.
    bool s_Read = false;
    const std::vector<uint8_t> s_Bytes = pkg::ReadFileBytes(p_Own, &s_Read, size_t(64) << 20);
    const pkg::ParsedPackage s_Package =
        s_Read && s_Bytes.size() == p_Size ? pkg::ParsePatchPackage(s_Bytes) : pkg::ParsedPackage {};
    const std::vector<std::pair<fr::RecipeVariant, size_t>> s_Indexed = PartitionVariants(p_Part.index);

    if (!s_Package.ok)
        LogError("{}: reading {} back failed{}; its full versions' crash sentinels name no entries", p_Part.log,
                 p_Own.string(), s_Read ? fmt::format(": {}", s_Package.error) : std::string());

    for (const auto& [s_Variant, s_Index] : s_Indexed) {
        std::vector<uint64_t> s_Rids(s_Variant.resources.begin(), s_Variant.resources.end());

        for (const auto& [s_Base, s_BaseIndex] : s_Indexed)
            if (!s_Variant.layerOf.empty() && s_Base.key == s_Variant.layerOf)
                s_Rids.insert(s_Rids.end(), s_Base.resources.begin(), s_Base.resources.end());

        m_VariantHashes[s_Index] =
            s_Package.ok ? std::optional<uint64_t>(pkg::VariantPackageHash(s_Package, s_Rids)) : std::nullopt;
    }

    std::string s_ListError;
    p_Part.otherPatches = ListOtherPatches(p_Own.parent_path(), p_Part.index, &s_ListError);

    if (!s_ListError.empty())
        LogError("{}: listing {} failed: {}", p_Part.log, p_Own.parent_path().string(), s_ListError);

    std::string s_Others;

    for (const int s_Number : p_Part.otherPatches)
        s_Others += fmt::format("{}{}", s_Others.empty() ? "" : ", ", s_Number);

    p_Part.target.store(kPatch);
    std::string s_Variants;

    for (const auto& [s_Variant, s_Index] : s_Indexed) {
        const data::Variant* s_Table = TableOf(*fr::FullVariants()[s_Index]);

        if (s_Table && !m_Unbuildable[s_Index].load())
            s_Variants += fmt::format("{}{} ({} resources, {} dependencies, {} fingerprints)",
                                      s_Variants.empty() ? "" : ", ", s_Table->name, s_Table->own.size(),
                                      s_Table->dependencies.size(), s_Table->fingerprints.size());
    }

    LogTo(fr::LogTopic::MapVariants,
          "{}: installed ({} bytes: {}); partition {} \"{}\" will be raised to {} when the game creates its "
          "partitions (other chunk{} patches in Runtime: [{}]{})",
          p_Part.log, p_Size, s_Variants, p_Part.index, p_Part.id, kPatch, p_Part.index, s_Others,
          m_MaxPatchLevel ? "; MaxPatchLevel is enabled in mods.ini and raises it instead" : "");
}

int32_t LandslidePackage::OnPartition(int32_t p_Index, std::string_view p_Id, int p_Type, int32_t p_Level) noexcept {
    try {
        Part* s_Part = PartOf(p_Index);

        if (!s_Part)
            return p_Level;

        int s_Target = s_Part->target.load();
        std::vector<int> s_Others = s_Part->otherPatches;
        std::string s_Refused;
        const std::string s_FileName = FileName(s_Part->index);

        // Again at raise time: the file may have gone or changed since Init,
        // or another patch of the chunk been added (which the raise would mount).
        if (s_Target > 0) {
            std::error_code s_Error;
            const uintmax_t s_Size = fs::file_size(s_Part->own, s_Error);

            if (s_Error || s_Size != s_Part->ownSize) {
                s_Refused = fmt::format(" (not raised: {} {} since game start)", s_FileName,
                                        s_Error ? "is gone" : fmt::format("changed size to {} bytes", s_Size));
                s_Target = 0;
            }
            else {
                std::string s_ListError;
                std::vector<int> s_Now = ListOtherPatches(s_Part->own.parent_path(), s_Part->index, &s_ListError);

                if (s_ListError.empty())
                    s_Others = std::move(s_Now);
            }
        }

        const pkg::RaiseDecision s_Decision = pkg::DecideRaise(p_Index, p_Id, p_Level, s_Part->index, s_Part->id,
                                                               s_Target, s_Others, m_MaxPatchLevel, s_FileName);

        s_Part->partitionSeen.store(true);
        s_Part->raisedTo.store(s_Decision.level);
        LogTo(fr::LogTopic::MapVariants, "{}: partition {} \"{}\" type {} patch level {}{}", s_Part->log, p_Index, p_Id,
              p_Type, p_Level, s_Refused.empty() ? s_Decision.note : s_Refused);
        return s_Decision.level;
    }
    catch (const std::exception& e) {
        LogError("landslide package: partition hook failed: {}", e.what());
    }
    catch (...) {
        LogError("landslide package: partition hook failed: unknown exception");
    }

    return p_Level;
}

fr::LandslideGate LandslidePackage::Gate(const fr::FullVariant& p_Variant, bool p_Forced) const noexcept {
    fr::LandslideGate s_Gate;
    s_Gate.builtIn = BuiltIn(p_Variant);
    s_Gate.switchedOff = SwitchedOff();
    s_Gate.buildable = Buildable(p_Variant);
    s_Gate.turnedOff = TurnedOff(p_Variant);
    const Part* s_Part = PartOf(p_Variant.partition);
    s_Gate.installed = s_Part && s_Part->target.load() > 0;
    s_Gate.partitionSeen = s_Part && s_Part->partitionSeen.load();
    s_Gate.raisedTo = s_Part ? s_Part->raisedTo.load() : -1;
    s_Gate.target = kPatch;
    s_Gate.maxPatchLevel = m_MaxPatchLevel;
    s_Gate.freelancerLayer = FreelancerLayer();
    s_Gate.forced = p_Forced;
    return s_Gate;
}

bool LandslidePackage::TurnedOff(const fr::FullVariant& p_Variant) const noexcept {
    const size_t s_Index = IndexOf(p_Variant);
    return s_Index < m_TurnedOff.size() && m_TurnedOff[s_Index].load();
}

fr::LandslideVerdict LandslidePackage::CheckResources(const fr::FullVariant& p_Variant) noexcept {
    try {
        const data::Variant* s_Table = TableOf(p_Variant);

        if (!s_Table)
            return {fr::LandslideBlock::NotBuiltIn, "this DLL has no recipe for this variant"};

        bool s_Container = true;
        const fr::ResourceLookup s_Lookup = [&s_Container](uint64_t p_Rid) { return Seen(p_Rid, &s_Container); };
        const std::vector<uint64_t> s_Own(s_Table->own.begin(), s_Table->own.end());
        const std::string s_FileName = FileName(p_Variant.partition);
        fr::LandslideVerdict s_Verdict = fr::CheckOwnResources(s_Own, s_Lookup, s_FileName);

        if (!s_Container)
            return {fr::LandslideBlock::ReadFault, "the resource container is not available"};

        if (s_Verdict.block == fr::LandslideBlock::None)
            s_Verdict = fr::CheckDependencies(s_Table->dependencies, s_Lookup);

        std::string s_Summary;

        if (s_Verdict.block == fr::LandslideBlock::None)
            s_Verdict = fr::CheckFingerprints(s_Table->fingerprints, s_Lookup, s_FileName, &s_Summary);

        if (s_Verdict.block == fr::LandslideBlock::ReadFault && m_FaultLogged.exchange(true))
            s_Verdict.why = "reading the resource container faulted (again; see the first time)";

        if (s_Verdict.block == fr::LandslideBlock::None)
            LogTo(fr::LogTopic::MapVariants,
                  "{}: full version: the package's {} resource{} load from {}; its {} dependencies are all there; {} "
                  "fingerprints match: {}",
                  p_Variant.key, s_Own.size(), s_Own.size() == 1 ? "" : "s", s_FileName, s_Table->dependencies.size(),
                  s_Table->fingerprints.size(), s_Summary);

        return s_Verdict;
    }
    catch (const std::exception& e) {
        return {fr::LandslideBlock::ReadFault, fmt::format("checking the package failed: {}", e.what())};
    }
    catch (...) {
        return {fr::LandslideBlock::ReadFault, "checking the package failed: unknown exception"};
    }
}

void LandslidePackage::WriteSentinel(const fr::FullVariant& p_Variant, std::string_view p_Scene) noexcept {
    try {
        std::lock_guard s_Lock(m_SentinelMutex);
        const fs::path s_Path = SentinelPath(p_Variant);
        const size_t s_Index = IndexOf(p_Variant);
        const std::string s_Text =
            fr::LandslideSentinelText(s_Index < m_Builds.size() ? m_Builds[s_Index] : "", LocalTime(), p_Scene);
        std::string s_Error = "the mod's folder was not found";

        if (s_Path.empty() ||
            !pkg::WriteFileAtomically(s_Path, std::vector<uint8_t>(s_Text.begin(), s_Text.end()), &s_Error))
            Log("{}: could not write the crash sentinel: {}; a crash during this load will not turn the full version "
                "off",
                p_Variant.key, s_Error);
    }
    catch (const std::exception& e) {
        LogError("{}: writing the crash sentinel failed: {}", p_Variant.key, e.what());
    }
    catch (...) {
        LogError("{}: writing the crash sentinel failed: unknown exception", p_Variant.key);
    }
}

void LandslidePackage::ClearSentinel(const fr::FullVariant& p_Variant) noexcept {
    try {
        std::lock_guard s_Lock(m_SentinelMutex);
        const fs::path s_Path = SentinelPath(p_Variant);
        std::error_code s_Error;

        if (s_Path.empty() || !fs::exists(s_Path, s_Error))
            return;

        if (fs::remove(s_Path, s_Error))
            LogTo(fr::LogTopic::MapVariants, "{}: the full version reached play; crash sentinel removed",
                  p_Variant.key);
        else
            Log("{}: could not remove the crash sentinel {}: {}; the next game start will turn the full version off",
                p_Variant.key, s_Path.string(), s_Error.message());
    }
    catch (const std::exception& e) {
        LogError("{}: removing the crash sentinel failed: {}", p_Variant.key, e.what());
    }
    catch (...) {
        LogError("{}: removing the crash sentinel failed: unknown exception", p_Variant.key);
    }
}

void LandslidePackage::Rearm(const fr::FullVariant& p_Variant) noexcept {
    try {
        std::lock_guard s_Lock(m_SentinelMutex);
        const fs::path s_Path = SentinelPath(p_Variant);
        const size_t s_Index = IndexOf(p_Variant);
        std::error_code s_Error;

        if (!s_Path.empty())
            fs::remove(s_Path, s_Error);

        if (s_Index < m_TurnedOff.size())
            m_TurnedOff[s_Index].store(false);

        LogTo(fr::LogTopic::MapVariants, "{}: full version turned back on in Settings{}; the next {} load tries it",
              p_Variant.key,
              s_Error ? fmt::format(" (the crash sentinel could not be removed: {})", s_Error.message()) : "",
              p_Variant.display);
    }
    catch (const std::exception& e) {
        LogError("{}: turning the full version back on failed: {}", p_Variant.key, e.what());
    }
    catch (...) {
        LogError("{}: turning the full version back on failed: unknown exception", p_Variant.key);
    }
}

// Runs for every partition the game creates, before its packages are mounted
// (added at Init only when a package was installed). Raises only the
// partitions with an installed package; another mod's detour may raise its
// own partition from the same hook, so the detours chain.
DEFINE_PLUGIN_DETOUR(FreelancerRandomizer, void*, IPackageManager_SPartitionInfo_SPartitionInfo,
                     IPackageManager::SPartitionInfo* th, int32_t index, ZString partitionID,
                     IPackageManager::EPartitionType type, int32_t patchLevel) {
    if (!m_LandslidePackage.Handles(index))
        return HookResult<void*>(HookAction::Continue());

    const int32_t s_Level =
        m_LandslidePackage.OnPartition(index, partitionID.ToStringView(), static_cast<int>(type), patchLevel);

    if (s_Level == patchLevel)
        return HookResult<void*>(HookAction::Continue());

    // The engine takes the level by value, so a raise must call the original
    // here; this ends the detour chain for that partition only when raising.
    return HookResult<void*>(HookAction::Return(), p_Hook->CallOriginal(th, index, partitionID, type, s_Level));
}
