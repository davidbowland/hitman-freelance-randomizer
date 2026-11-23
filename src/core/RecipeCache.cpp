#include "RecipeCache.h"

#include "LandslideFull.h"
#include "Rng.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace fr {

namespace {

// Exactly 16 hex digits (either case).
std::optional<uint64_t> ParseHex16(std::string_view p_Text) {
    if (p_Text.size() != 16)
        return std::nullopt;
    uint64_t s_Value = 0;
    const auto [s_End, s_Error] = std::from_chars(p_Text.data(), p_Text.data() + p_Text.size(), s_Value, 16);
    if (s_Error != std::errc() || s_End != p_Text.data() + p_Text.size())
        return std::nullopt;
    return s_Value;
}

// Decimal digits only, no sign.
std::optional<uint64_t> ParseDecimal(std::string_view p_Text) {
    if (p_Text.empty() || !std::all_of(p_Text.begin(), p_Text.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    uint64_t s_Value = 0;
    const auto [s_End, s_Error] = std::from_chars(p_Text.data(), p_Text.data() + p_Text.size(), s_Value);
    if (s_Error != std::errc() || s_End != p_Text.data() + p_Text.size())
        return std::nullopt;
    return s_Value;
}

// p_Text split at single spaces (an empty field where two meet).
std::vector<std::string_view> Fields(std::string_view p_Text) {
    std::vector<std::string_view> s_Out;
    size_t s_At = 0;
    for (size_t s_Space = p_Text.find(' '); s_Space != std::string_view::npos; s_Space = p_Text.find(' ', s_At)) {
        s_Out.push_back(p_Text.substr(s_At, s_Space - s_At));
        s_At = s_Space + 1;
    }
    s_Out.push_back(p_Text.substr(s_At));
    return s_Out;
}

std::optional<CachedVariant> ParseVariant(std::string_view p_Text) {
    const size_t s_Equals = p_Text.find('=');
    if (s_Equals == 0 || s_Equals == std::string_view::npos)
        return std::nullopt;
    CachedVariant s_Out;
    s_Out.key = std::string(p_Text.substr(0, s_Equals));
    const std::string_view s_State = p_Text.substr(s_Equals + 1);
    if (s_State == "ok")
        return s_Out;
    const size_t s_Colon = s_State.find(':');
    if (s_Colon == std::string_view::npos)
        return std::nullopt;
    const std::optional<RecipeFailure> s_Failure = RecipeFailureOfName(s_State.substr(0, s_Colon));
    const std::optional<uint64_t> s_Rid = ParseHex16(s_State.substr(s_Colon + 1));
    if (!s_Failure || *s_Failure == RecipeFailure::None || !s_Rid)
        return std::nullopt;
    s_Out.outcome = {*s_Failure, *s_Rid};
    return s_Out;
}

} // namespace

std::string Hex16(uint64_t p_Value) {
    char s_Text[17];
    std::snprintf(s_Text, sizeof(s_Text), "%016llX", static_cast<unsigned long long>(p_Value));
    return s_Text;
}

uint64_t PackageRecipeKey(uint64_t p_Tables, std::span<const RecipeVariant> p_Variants, std::string_view p_Version) {
    uint64_t s_Hash = RecipeHashWord(kFnv1a64Basis, p_Tables);
    s_Hash = RecipeHashWord(s_Hash, p_Variants.size());
    for (const RecipeVariant& s_Variant : p_Variants) {
        s_Hash = RecipeHashText(s_Hash, s_Variant.key);
        s_Hash = RecipeHashWord(s_Hash, s_Variant.resources.size());
        for (const uint64_t s_Rid : s_Variant.resources)
            s_Hash = RecipeHashWord(s_Hash, s_Rid);
        s_Hash = RecipeHashText(s_Hash, s_Variant.layerOf);
    }
    return RecipeHashText(s_Hash, p_Version);
}

uint64_t PackageSourceKey(uint64_t p_Tables, std::span<const RecipeVariant> p_Variants, std::string_view p_Version,
                          std::span<const SourceSpot> p_Sources) {
    uint64_t s_Hash = RecipeHashWord(PackageRecipeKey(p_Tables, p_Variants, p_Version), p_Sources.size());
    for (const SourceSpot& s_Spot : p_Sources) {
        s_Hash = RecipeHashWord(s_Hash, s_Spot.rid);
        if (!s_Spot.found) {
            s_Hash = RecipeHashText(s_Hash, "missing");
            continue;
        }
        s_Hash = RecipeHashText(s_Hash, s_Spot.package);
        s_Hash = RecipeHashWord(s_Hash, s_Spot.offset);
        s_Hash = RecipeHashWord(s_Hash, s_Spot.stored);
        s_Hash = RecipeHashWord(s_Hash, s_Spot.xored ? 1 : 0);
        s_Hash = RecipeHashWord(s_Hash, s_Spot.finalSize);
        s_Hash = RecipeHashWord(s_Hash, s_Spot.packageSize);
        s_Hash = RecipeHashWord(s_Hash, static_cast<uint64_t>(s_Spot.packageTime));
    }
    return s_Hash;
}

std::string FormatPackageCacheLine(const PackageCacheLine& p_Line) {
    std::string s_Out = std::to_string(p_Line.partition) + " " + Hex16(p_Line.sourceKey) + " " +
                        Hex16(p_Line.recipeKey) + " " + std::to_string(p_Line.fileSize) + " " +
                        PartitionIdText(p_Line.packageHash);
    for (const CachedVariant& s_Variant : p_Line.variants) {
        s_Out += " " + s_Variant.key + "=";
        s_Out +=
            s_Variant.outcome.failure == RecipeFailure::None
                ? std::string("ok")
                : std::string(RecipeFailureName(s_Variant.outcome.failure)) + ":" + Hex16(s_Variant.outcome.sourceRid);
    }
    return s_Out;
}

std::optional<PackageCacheLine> ParsePackageCacheLine(std::string_view p_Text) {
    const std::vector<std::string_view> s_Fields = Fields(p_Text);
    if (s_Fields.size() < 6)
        return std::nullopt;

    const std::optional<uint64_t> s_Partition = ParseDecimal(s_Fields[0]);
    const std::optional<uint64_t> s_Key = ParseHex16(s_Fields[1]);
    const std::optional<uint64_t> s_RecipeKey = ParseHex16(s_Fields[2]);
    const std::optional<uint64_t> s_Size = ParseDecimal(s_Fields[3]);
    if (!s_Partition || *s_Partition > 9999 || !s_Key || !s_RecipeKey || !s_Size)
        return std::nullopt;

    PackageCacheLine s_Line;
    s_Line.partition = static_cast<int>(*s_Partition);
    s_Line.sourceKey = *s_Key;
    s_Line.recipeKey = *s_RecipeKey;
    s_Line.fileSize = *s_Size;
    if (s_Fields[4] != "empty") {
        s_Line.packageHash = ParseHex16(s_Fields[4]);
        if (!s_Line.packageHash)
            return std::nullopt;
    }
    // No file has no size; a file always has some.
    if (s_Line.packageHash.has_value() != (s_Line.fileSize != 0))
        return std::nullopt;

    for (size_t i = 5; i < s_Fields.size(); ++i) {
        std::optional<CachedVariant> s_Variant = ParseVariant(s_Fields[i]);
        if (!s_Variant || std::any_of(s_Line.variants.begin(), s_Line.variants.end(),
                                      [&](const CachedVariant& p_V) { return p_V.key == s_Variant->key; }))
            return std::nullopt;
        s_Line.variants.push_back(std::move(*s_Variant));
    }
    return s_Line;
}

std::string FormatPackageCache(std::span<const PackageCacheLine> p_Lines) {
    std::string s_Out;
    for (const PackageCacheLine& s_Line : p_Lines)
        s_Out += FormatPackageCacheLine(s_Line) + "\n";
    return s_Out;
}

std::vector<PackageCacheLine> ParsePackageCache(std::string_view p_Text) {
    std::vector<PackageCacheLine> s_Out;
    while (!p_Text.empty()) {
        const size_t s_End = p_Text.find('\n');
        std::string_view s_Line = p_Text.substr(0, s_End);
        p_Text = s_End == std::string_view::npos ? std::string_view() : p_Text.substr(s_End + 1);
        if (!s_Line.empty() && s_Line.back() == '\r')
            s_Line.remove_suffix(1);
        std::optional<PackageCacheLine> s_Parsed = ParsePackageCacheLine(s_Line);
        if (s_Parsed && std::none_of(s_Out.begin(), s_Out.end(),
                                     [&](const PackageCacheLine& p_L) { return p_L.partition == s_Parsed->partition; }))
            s_Out.push_back(std::move(*s_Parsed));
    }
    return s_Out;
}

std::string PartitionIdText(const std::optional<uint64_t>& p_PackageHash) {
    return p_PackageHash ? Hex16(*p_PackageHash) : std::string("empty");
}

std::string VariantBuildId(std::string_view p_Version, const std::optional<uint64_t>& p_VariantHash) {
    const std::string s_Text = PartitionIdText(p_VariantHash);
    return LandslideBuildId(p_Version, std::span(reinterpret_cast<const uint8_t*>(s_Text.data()), s_Text.size()));
}

std::optional<std::vector<RecipeOutcome>> KeptOutcomes(const PackageCacheLine* p_Previous, uint64_t p_RecipeKey,
                                                       std::span<const std::string_view> p_Keys,
                                                       std::span<const RecipeOutcome> p_Outcomes,
                                                       const std::vector<bool>& p_ReadFailed,
                                                       std::span<const uint8_t> p_File) {
    if (!p_Previous || p_Previous->recipeKey != p_RecipeKey || !p_Previous->packageHash ||
        p_Previous->fileSize != p_File.size() || p_Previous->variants.size() != p_Keys.size() ||
        p_Outcomes.size() != p_Keys.size() || p_ReadFailed.size() != p_Keys.size())
        return std::nullopt;
    std::vector<RecipeOutcome> s_Out;
    for (size_t i = 0; i < p_Keys.size(); ++i) {
        const CachedVariant& s_Variant = p_Previous->variants[i];
        // A variant that was read and builds otherwise now: the game's files changed since.
        if (s_Variant.key != p_Keys[i] ||
            (!p_ReadFailed[i] && (s_Variant.outcome.failure != p_Outcomes[i].failure ||
                                  s_Variant.outcome.sourceRid != p_Outcomes[i].sourceRid)))
            return std::nullopt;
        s_Out.push_back(s_Variant.outcome);
    }
    if (Fnv1a64(p_File) != *p_Previous->packageHash)
        return std::nullopt;
    return s_Out;
}

bool PackageCacheHit(const PackageCacheLine& p_Line, uint64_t p_SourceKey, std::span<const std::string_view> p_Keys,
                     const InstalledPackage& p_File, const std::function<bool()>& p_Matches) {
    if (p_Line.sourceKey != p_SourceKey || p_Line.variants.size() != p_Keys.size())
        return false;
    for (size_t i = 0; i < p_Keys.size(); ++i)
        if (p_Line.variants[i].key != p_Keys[i])
            return false;
    if (!p_Line.packageHash)
        return !p_File.exists;
    return p_File.exists && p_File.size == p_Line.fileSize && p_Matches();
}

} // namespace fr
