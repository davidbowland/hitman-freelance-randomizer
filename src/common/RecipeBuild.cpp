#include "RecipeBuild.h"

#include "LandslideFull.h"
#include "PatchPackage.h"
#include "Rng.h"
#include "TextNoCase.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace pkg {

namespace fs = std::filesystem;

namespace {

// The name is chunk p_Chunk's own package or one of its IOI patches.
bool IoiPackageOf(int p_Chunk, std::string_view p_Name) {
    const std::string s_Chunk = "chunk" + std::to_string(p_Chunk);
    const std::optional<int> s_Patch = fr::PackagePatchNumber(p_Name);
    return s_Patch && *s_Patch < fr::kFirstModPatch && fr::StartsWithNoCase(p_Name, s_Chunk) &&
           p_Name.size() > s_Chunk.size() &&
           (p_Name[s_Chunk.size()] == '.' || fr::LowerAscii(p_Name[s_Chunk.size()]) == 'p');
}

// A source's bytes, or why it can't be used.
struct Decoded {
    std::vector<uint8_t> bytes;
    fr::RecipeFailure failure = fr::RecipeFailure::None;
};

} // namespace

const ChunkPackages& RuntimeIndexes::Chunk(int p_Chunk) {
    auto s_It = m_Chunks.find(p_Chunk);

    if (s_It != m_Chunks.end())
        return s_It->second;

    const auto s_Start = std::chrono::steady_clock::now();
    ChunkPackages s_Out;
    std::error_code s_Error;

    for (const fs::directory_entry& s_Entry : fs::directory_iterator(m_Runtime, s_Error)) {
        const std::string s_Name = s_Entry.path().filename().string();

        if (!IoiPackageOf(p_Chunk, s_Name))
            continue;

        std::ifstream s_In(s_Entry.path(), std::ios::binary);
        RpkgIndex s_Index;

        if (s_In)
            s_Index = ReadRpkgIndex(s_In, fr::PackagePatchNumber(s_Name).value_or(0) > 0);
        else
            s_Index.error = "it can't be opened";

        std::error_code s_StampError;
        const uint64_t s_Size = fs::file_size(s_Entry.path(), s_StampError);
        const int64_t s_Time = fs::last_write_time(s_Entry.path(), s_StampError).time_since_epoch().count();
        s_Out.stamps[s_Name] = {s_StampError ? 0 : s_Size, s_StampError ? 0 : s_Time};
        s_Out.indexes.emplace_back(s_Name, std::move(s_Index));
    }

    if (s_Error)
        s_Out.error = "listing " + m_Runtime.string() + " failed: " + s_Error.message();

    m_PackagesRead += s_Out.indexes.size();
    m_ReadTime += std::chrono::steady_clock::now() - s_Start;
    return m_Chunks.emplace(p_Chunk, std::move(s_Out)).first->second;
}

std::vector<uint16_t> PartitionSources(const RecipeTables& p_Tables, int p_Partition) {
    std::vector<uint16_t> s_Out;

    for (const fr::RecipeResource& s_Resource : p_Tables.resources)
        if (s_Resource.partition == p_Partition)
            for (const uint16_t s_Index : s_Resource.sources)
                if (s_Index < p_Tables.sources.size())
                    s_Out.push_back(s_Index);

    std::sort(s_Out.begin(), s_Out.end());
    s_Out.erase(std::unique(s_Out.begin(), s_Out.end()), s_Out.end());
    return s_Out;
}

std::vector<fr::SourceSpot> LocateSources(const RecipeTables& p_Tables, std::span<const uint16_t> p_Sources,
                                          RuntimeIndexes& p_Indexes) {
    std::vector<fr::SourceSpot> s_Out;

    for (const uint16_t s_Index : p_Sources) {
        const fr::RecipeSource& s_Source = p_Tables.sources[s_Index];
        fr::SourceSpot& s_Spot = s_Out.emplace_back();
        s_Spot.rid = s_Source.rid;
        const ChunkPackages& s_Packages = p_Indexes.Chunk(s_Source.chunk);

        if (!s_Packages.error.empty()) {
            s_Spot.error = s_Packages.error;
            s_Spot.readFailed = true;
            continue;
        }

        const LocateResult s_Located = LocateNewest(s_Packages.indexes, s_Source.chunk, s_Source.rid);

        if (!s_Located.found) {
            s_Spot.error = !s_Located.error.empty()
                               ? s_Located.error
                               : "no package of chunk " + std::to_string(s_Source.chunk) + " holds it";
            // An index that can't be read hides where the copy is.
            s_Spot.readFailed = std::any_of(s_Packages.indexes.begin(), s_Packages.indexes.end(),
                                            [](const auto& p_Package) { return !p_Package.second.ok; });
            continue;
        }

        s_Spot.found = true;
        s_Spot.package = s_Located.found->package;
        s_Spot.offset = s_Located.found->entry.offset;
        s_Spot.stored = s_Located.found->entry.stored;
        s_Spot.xored = s_Located.found->entry.xored;
        s_Spot.finalSize = s_Located.found->entry.finalSize;

        if (const auto s_Stamp = s_Packages.stamps.find(s_Spot.package); s_Stamp != s_Packages.stamps.end()) {
            s_Spot.packageSize = s_Stamp->second.first;
            s_Spot.packageTime = s_Stamp->second.second;
        }
    }

    return s_Out;
}

PartitionBuild BuildPartition(const RecipeTables& p_Tables, int p_Partition,
                              std::span<const fr::RecipeVariant> p_Variants, std::span<const uint16_t> p_Sources,
                              std::span<const fr::SourceSpot> p_Spots, const fs::path& p_Runtime) {
    PartitionBuild s_Out;
    std::map<uint64_t, Decoded> s_Decoded; // by rid, each read and checked once
    std::map<std::string, std::ifstream> s_Files;

    for (size_t i = 0; i < p_Sources.size() && i < p_Spots.size(); ++i) {
        const fr::RecipeSource& s_Source = p_Tables.sources[p_Sources[i]];
        const fr::SourceSpot& s_Spot = p_Spots[i];
        Decoded& s_Entry = s_Decoded[s_Source.rid];
        const auto s_Problem = [&](fr::RecipeFailure p_Failure, std::string p_Detail, bool p_ReadFailed) {
            s_Entry.failure = p_Failure;
            s_Entry.bytes = {};
            s_Out.problems.push_back({p_Sources[i], p_Failure, std::move(p_Detail), p_ReadFailed});
        };

        if (!s_Spot.found) {
            s_Problem(fr::RecipeFailure::SourceMissing, s_Spot.error, s_Spot.readFailed);
            continue;
        }

        auto s_File = s_Files.find(s_Spot.package);

        if (s_File == s_Files.end())
            s_File = s_Files.emplace(s_Spot.package, std::ifstream(p_Runtime / s_Spot.package, std::ios::binary)).first;

        RpkgEntry s_Where;
        s_Where.hash = s_Spot.rid;
        s_Where.offset = s_Spot.offset;
        s_Where.stored = s_Spot.stored;
        s_Where.xored = s_Spot.xored;
        s_Where.finalSize = s_Spot.finalSize;
        std::optional<std::vector<uint8_t>> s_Bytes;

        if (s_File->second)
            s_Bytes = ReadRpkgResource(s_File->second, s_Where);

        if (!s_Bytes) {
            s_Problem(fr::RecipeFailure::SourceMissing, s_Spot.package + "'s copy can't be read or decoded", true);
            continue;
        }

        ++s_Out.decoded;
        s_Out.decodedBytes += s_Bytes->size();
        const fr::RecipeFailure s_Failure = fr::VerifyRecipeSource(s_Source, *s_Bytes);

        if (s_Failure == fr::RecipeFailure::SourceSize)
            s_Problem(s_Failure,
                      s_Spot.package + " holds " + std::to_string(s_Bytes->size()) + " bytes, the recipe needs " +
                          std::to_string(s_Source.size),
                      false);
        else if (s_Failure != fr::RecipeFailure::None)
            s_Problem(s_Failure,
                      s_Spot.package + "'s copy changed (hash " + fr::Hex16(fr::Fnv1a64(*s_Bytes)) +
                          ", the recipe needs " + fr::Hex16(s_Source.hash) + ")",
                      false);
        else
            s_Entry.bytes = std::move(*s_Bytes);
    }

    s_Files.clear();

    // Each resource a variant names, rebuilt once.
    std::map<uint64_t, fr::RecipeResult> s_Built;
    const auto s_ResourceOf = [&](uint64_t p_Rid) -> const fr::RecipeResource* {
        for (const fr::RecipeResource& s_Resource : p_Tables.resources)
            if (s_Resource.rid == p_Rid && s_Resource.partition == p_Partition)
                return &s_Resource;
        return nullptr;
    };
    const auto s_Rebuild = [&](uint64_t p_Rid) -> fr::RecipeOutcome {
        if (const auto s_It = s_Built.find(p_Rid); s_It != s_Built.end())
            return {s_It->second.failure, s_It->second.sourceRid};

        const fr::RecipeResource* s_Resource = s_ResourceOf(p_Rid);

        if (!s_Resource)
            return {fr::RecipeFailure::SourceMissing, p_Rid};

        // A source that failed its check fails the resource with its own reason.
        for (const uint16_t s_Index : s_Resource->sources) {
            if (s_Index >= p_Tables.sources.size())
                continue;
            const uint64_t s_Rid = p_Tables.sources[s_Index].rid;
            const auto s_It = s_Decoded.find(s_Rid);
            if (s_It != s_Decoded.end() && s_It->second.failure != fr::RecipeFailure::None) {
                s_Built[p_Rid] = {s_It->second.failure, s_Rid, {}};
                return {s_It->second.failure, s_Rid};
            }
        }

        const fr::RecipeResult& s_Result = s_Built[p_Rid] =
            fr::ApplyRecipe(*s_Resource, p_Tables.sources, p_Tables.literals,
                            [&](uint64_t p_Source) -> std::optional<fr::RecipeSourceBytes> {
                                const auto s_It = s_Decoded.find(p_Source);
                                if (s_It == s_Decoded.end())
                                    return std::nullopt;
                                return fr::RecipeSourceBytes {s_It->second.bytes, true};
                            });
        return {s_Result.failure, s_Result.sourceRid};
    };

    s_Out.variants = fr::ResolveBuildable(p_Variants, s_Rebuild);
    s_Decoded.clear(); // every resource is rebuilt: the sources go before the package is built

    std::vector<PackageResource> s_Resources;

    for (const fr::RecipeResource& s_Resource : p_Tables.resources) {
        const bool s_Wanted = [&] {
            for (size_t i = 0; i < p_Variants.size(); ++i)
                if (s_Out.variants[i].failure == fr::RecipeFailure::None &&
                    std::find(p_Variants[i].resources.begin(), p_Variants[i].resources.end(), s_Resource.rid) !=
                        p_Variants[i].resources.end())
                    return true;
            return false;
        }();
        const auto s_It = s_Built.find(s_Resource.rid);

        if (s_Resource.partition != p_Partition || !s_Wanted || s_It == s_Built.end())
            continue;

        PackageResource& s_Package = s_Resources.emplace_back();
        s_Package.hash = s_Resource.rid;
        s_Package.type = std::string(s_Resource.type);
        s_Package.data = std::move(s_It->second.bytes);
        s_Package.memSize = s_Resource.memSize;
        s_Package.vmemSize = s_Resource.vmemSize;

        for (size_t i = 0; i < s_Resource.refFlags.size() && i < s_Resource.refHashes.size(); ++i)
            s_Package.refs.push_back({s_Resource.refFlags[i], s_Resource.refHashes[i]});
    }

    if (!s_Resources.empty()) {
        s_Out.package = BuildPatchPackage(s_Resources);
        s_Out.packageFailed = s_Out.package.empty();
    }

    return s_Out;
}

bool PartitionBuild::Cacheable() const {
    return std::none_of(problems.begin(), problems.end(), [](const SourceProblem& p_P) { return p_P.readFailed; });
}

FileOwner HoldsOnlyFile(const fs::path& p_Path, std::span<const uint64_t> p_Own, std::span<const uint64_t> p_Former) {
    constexpr size_t kMaxOwn = size_t(64) << 20; // the mod's package is at most a few MB
    std::error_code s_Error;
    const uintmax_t s_Size = fs::file_size(p_Path, s_Error);

    if (s_Error)
        return FileOwner::Unreadable;

    if (s_Size > kMaxOwn)
        return FileOwner::Other;

    // Read in full first: a read error is then told from a file that isn't a package.
    bool s_Read = false;
    const std::vector<uint8_t> s_Bytes = ReadFileBytes(p_Path, &s_Read, kMaxOwn);

    if (!s_Read)
        return FileOwner::Unreadable;

    std::istringstream s_In(std::string(s_Bytes.begin(), s_Bytes.end()), std::ios::binary);
    return HoldsOnly(ReadRpkgIndex(s_In, true), p_Own, p_Former) ? FileOwner::Mod : FileOwner::Other;
}

PartitionPlan PlanPartition(const RecipeTables& p_Tables, uint64_t p_TableHash, int p_Partition,
                            std::span<const fr::RecipeVariant> p_Variants, std::string_view p_Version,
                            RuntimeIndexes& p_Indexes, std::span<const fr::PackageCacheLine> p_Cached,
                            const fs::path& p_Own, std::span<const uint64_t> p_OwnRids,
                            std::span<const uint64_t> p_FormerRids) {
    PartitionPlan s_Plan;
    s_Plan.sources = PartitionSources(p_Tables, p_Partition);
    s_Plan.spots = LocateSources(p_Tables, s_Plan.sources, p_Indexes);
    s_Plan.sourceKey = fr::PackageSourceKey(p_TableHash, p_Variants, p_Version, s_Plan.spots);
    s_Plan.recipeKey = fr::PackageRecipeKey(p_TableHash, p_Variants, p_Version);

    const auto s_Line = std::find_if(p_Cached.begin(), p_Cached.end(), [&](const fr::PackageCacheLine& p_Line) {
        return p_Line.partition == p_Partition;
    });

    if (s_Line == p_Cached.end())
        return s_Plan;

    std::error_code s_Error;
    fr::InstalledPackage s_File;
    s_File.exists = fs::exists(p_Own, s_Error);
    s_File.size = s_File.exists ? fs::file_size(p_Own, s_Error) : 0;

    // There, but its size can't be told: never a hit.
    if (s_Error)
        return s_Plan;

    std::vector<std::string_view> s_Keys;

    for (const fr::RecipeVariant& s_Variant : p_Variants)
        s_Keys.push_back(s_Variant.key);

    // The mod's file, with the very bytes the line names (the mod's package is at most a few MB).
    if (fr::PackageCacheHit(*s_Line, s_Plan.sourceKey, s_Keys, s_File, [&] {
            if (HoldsOnlyFile(p_Own, p_OwnRids, p_FormerRids) != FileOwner::Mod)
                return false;
            bool s_Read = false;
            const std::vector<uint8_t> s_Bytes = ReadFileBytes(p_Own, &s_Read, size_t(64) << 20);
            return s_Read && s_Line->packageHash && fr::Fnv1a64(s_Bytes) == *s_Line->packageHash;
        }))
        s_Plan.cached = *s_Line;

    return s_Plan;
}

fr::PackageCacheLine BuiltCacheLine(int p_Partition, uint64_t p_SourceKey, uint64_t p_RecipeKey,
                                    std::span<const fr::RecipeVariant> p_Variants,
                                    std::span<const fr::RecipeOutcome> p_Outcomes,
                                    std::span<const uint8_t> p_Installed) {
    fr::PackageCacheLine s_Line;
    s_Line.partition = p_Partition;
    s_Line.sourceKey = p_SourceKey;
    s_Line.recipeKey = p_RecipeKey;

    for (size_t i = 0; i < p_Variants.size() && i < p_Outcomes.size(); ++i)
        s_Line.variants.push_back({std::string(p_Variants[i].key), p_Outcomes[i]});

    if (!p_Installed.empty()) {
        s_Line.fileSize = p_Installed.size();
        s_Line.packageHash = fr::Fnv1a64(p_Installed);
    }

    return s_Line;
}

uint64_t VariantPackageHash(const ParsedPackage& p_Package, std::span<const uint64_t> p_Rids) {
    uint64_t s_Hash = fr::RecipeHashWord(fr::kFnv1a64Basis, p_Rids.size());

    for (const uint64_t s_Rid : p_Rids) {
        s_Hash = fr::RecipeHashWord(s_Hash, s_Rid);
        const auto s_It = std::find_if(p_Package.resources.begin(), p_Package.resources.end(),
                                       [&](const PackageResource& p_R) { return p_R.hash == s_Rid; });

        if (s_It == p_Package.resources.end()) {
            s_Hash = fr::RecipeHashText(s_Hash, "missing");
            continue;
        }

        s_Hash = fr::RecipeHashText(s_Hash, s_It->type);
        s_Hash = fr::RecipeHashWord(s_Hash, s_It->memSize);
        s_Hash = fr::RecipeHashWord(s_Hash, s_It->vmemSize);
        s_Hash = fr::RecipeHashWord(s_Hash, s_It->refs.size());

        for (const PackageRef& s_Ref : s_It->refs) {
            s_Hash = fr::RecipeHashWord(s_Hash, s_Ref.flags);
            s_Hash = fr::RecipeHashWord(s_Hash, s_Ref.hash);
        }

        s_Hash = fr::RecipeHashWord(s_Hash, s_It->data.size());
        s_Hash = fr::Fnv1a64(s_It->data, s_Hash);
    }

    return s_Hash;
}

} // namespace pkg
