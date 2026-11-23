#include "Recipe.h"

#include "Rng.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace fr {

namespace {

RecipeResult Fail(RecipeFailure p_Failure, uint64_t p_SourceRid) {
    return {p_Failure, p_SourceRid, {}};
}

// Fits p_Length bytes at p_Offset inside p_Size bytes (no overflow).
bool Fits(uint64_t p_Offset, uint64_t p_Length, uint64_t p_Size) {
    return p_Offset <= p_Size && p_Length <= p_Size - p_Offset;
}

constexpr std::array<std::string_view, 8> kFailureNames {
    "None", "SourceMissing", "SourceSize", "SourceHash", "OpOutOfRange", "OutputSize", "OutputHash", "BaseNotBuildable",
};

} // namespace

uint64_t RecipeHashWord(uint64_t p_Hash, uint64_t p_Value) {
    std::array<uint8_t, 8> s_Bytes {};
    for (size_t i = 0; i < s_Bytes.size(); ++i)
        s_Bytes[i] = static_cast<uint8_t>(p_Value >> (8 * i));
    return Fnv1a64(s_Bytes, p_Hash);
}

uint64_t RecipeHashText(uint64_t p_Hash, std::string_view p_Text) {
    p_Hash = RecipeHashWord(p_Hash, p_Text.size());
    return Fnv1a64(std::span(reinterpret_cast<const uint8_t*>(p_Text.data()), p_Text.size()), p_Hash);
}

std::string_view RecipeFailureName(RecipeFailure p_Failure) {
    const size_t s_Index = static_cast<size_t>(p_Failure);
    return s_Index < kFailureNames.size() ? kFailureNames[s_Index] : std::string_view("Unknown");
}

std::optional<RecipeFailure> RecipeFailureOfName(std::string_view p_Name) {
    for (size_t i = 0; i < kFailureNames.size(); ++i)
        if (kFailureNames[i] == p_Name)
            return static_cast<RecipeFailure>(i);
    return std::nullopt;
}

RecipeFailure VerifyRecipeSource(const RecipeSource& p_Source, std::span<const uint8_t> p_Bytes) {
    if (p_Bytes.size() != p_Source.size)
        return RecipeFailure::SourceSize;
    if (Fnv1a64(p_Bytes) != p_Source.hash)
        return RecipeFailure::SourceHash;
    return RecipeFailure::None;
}

RecipeResult ApplyRecipe(const RecipeResource& p_Resource, std::span<const RecipeSource> p_AllSources,
                         std::span<const uint8_t> p_Literals,
                         const std::function<std::optional<RecipeSourceBytes>(uint64_t p_Rid)>& p_Source) {
    std::vector<std::span<const uint8_t>> s_Sources;
    std::vector<uint64_t> s_SourceRids;
    s_Sources.reserve(p_Resource.sources.size());
    s_SourceRids.reserve(p_Resource.sources.size());
    for (uint16_t s_Index : p_Resource.sources) {
        if (s_Index >= p_AllSources.size())
            return Fail(RecipeFailure::OpOutOfRange, 0);
        const RecipeSource& s_Source = p_AllSources[s_Index];
        const std::optional<RecipeSourceBytes> s_Bytes = p_Source(s_Source.rid);
        if (!s_Bytes)
            return Fail(RecipeFailure::SourceMissing, s_Source.rid);
        if (!s_Bytes->verified) {
            const RecipeFailure s_Failure = VerifyRecipeSource(s_Source, s_Bytes->bytes);
            if (s_Failure != RecipeFailure::None)
                return Fail(s_Failure, s_Source.rid);
        }
        s_Sources.push_back(s_Bytes->bytes);
        s_SourceRids.push_back(s_Source.rid);
    }

    std::vector<uint8_t> s_Out(p_Resource.size);
    uint64_t s_At = 0;
    for (const RecipeOp& s_Op : p_Resource.ops) {
        std::span<const uint8_t> s_From;
        uint64_t s_FromRid = 0;
        if (s_Op.kind == OpKind::Copy) {
            if (s_Op.source >= s_Sources.size())
                return Fail(RecipeFailure::OpOutOfRange, 0);
            s_From = s_Sources[s_Op.source];
            s_FromRid = s_SourceRids[s_Op.source];
        }
        else if (s_Op.kind == OpKind::Literal) {
            s_From = p_Literals;
        }
        else {
            return Fail(RecipeFailure::OpOutOfRange, 0);
        }
        if (!Fits(s_Op.offset, s_Op.length, s_From.size()))
            return Fail(RecipeFailure::OpOutOfRange, s_FromRid);
        if (!Fits(s_At, s_Op.length, s_Out.size()))
            return Fail(RecipeFailure::OutputSize, 0);
        if (s_Op.length > 0)
            std::memcpy(s_Out.data() + s_At, s_From.data() + s_Op.offset, s_Op.length);
        s_At += s_Op.length;
    }
    if (s_At != s_Out.size())
        return Fail(RecipeFailure::OutputSize, 0);
    if (Fnv1a64(s_Out) != p_Resource.hash)
        return Fail(RecipeFailure::OutputHash, 0);

    return {RecipeFailure::None, 0, std::move(s_Out)};
}

uint64_t RecipeTableHash(std::span<const RecipeSource> p_Sources, std::span<const uint8_t> p_Literals,
                         std::span<const RecipeResource> p_Resources) {
    uint64_t s_Hash = RecipeHashWord(kFnv1a64Basis, p_Sources.size());
    for (const RecipeSource& s_Source : p_Sources) {
        s_Hash = RecipeHashWord(s_Hash, s_Source.rid);
        s_Hash = RecipeHashText(s_Hash, s_Source.type);
        s_Hash = RecipeHashWord(s_Hash, s_Source.size);
        s_Hash = RecipeHashWord(s_Hash, s_Source.hash);
        s_Hash = RecipeHashWord(s_Hash, s_Source.chunk);
    }
    s_Hash = RecipeHashWord(s_Hash, p_Literals.size());
    s_Hash = Fnv1a64(p_Literals, s_Hash);
    s_Hash = RecipeHashWord(s_Hash, p_Resources.size());
    for (const RecipeResource& s_Resource : p_Resources) {
        s_Hash = RecipeHashWord(s_Hash, s_Resource.rid);
        s_Hash = RecipeHashText(s_Hash, s_Resource.type);
        s_Hash = RecipeHashText(s_Hash, s_Resource.path);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.sourceRid);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.blueprint);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.memSize);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.vmemSize);
        s_Hash = RecipeHashWord(s_Hash, static_cast<uint32_t>(s_Resource.partition));
        s_Hash = RecipeHashWord(s_Hash, s_Resource.refFlags.size());
        s_Hash = Fnv1a64(s_Resource.refFlags, s_Hash);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.refHashes.size());
        for (uint64_t s_Ref : s_Resource.refHashes)
            s_Hash = RecipeHashWord(s_Hash, s_Ref);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.sources.size());
        for (uint16_t s_Index : s_Resource.sources)
            s_Hash = RecipeHashWord(s_Hash, s_Index);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.ops.size());
        for (const RecipeOp& s_Op : s_Resource.ops) {
            s_Hash = RecipeHashWord(s_Hash, static_cast<uint8_t>(s_Op.kind));
            s_Hash = RecipeHashWord(s_Hash, s_Op.source);
            s_Hash = RecipeHashWord(s_Hash, s_Op.offset);
            s_Hash = RecipeHashWord(s_Hash, s_Op.length);
        }
        s_Hash = RecipeHashWord(s_Hash, s_Resource.size);
        s_Hash = RecipeHashWord(s_Hash, s_Resource.hash);
    }

    return s_Hash;
}

std::vector<RecipeOutcome> ResolveBuildable(std::span<const RecipeVariant> p_Variants,
                                            const std::function<RecipeOutcome(uint64_t p_Rid)>& p_Resource) {
    std::vector<RecipeOutcome> s_Out(p_Variants.size());
    for (size_t i = 0; i < p_Variants.size(); ++i) {
        for (uint64_t s_Rid : p_Variants[i].resources) {
            const RecipeOutcome s_Resource = p_Resource(s_Rid);
            if (s_Resource.failure != RecipeFailure::None) {
                s_Out[i] = s_Resource;
                break;
            }
        }
    }

    // Follow each layer down to its innermost base; a chain longer than the
    // list is a cycle, and a key two variants share names no base.
    std::vector<RecipeOutcome> s_Resolved = s_Out;
    for (size_t i = 0; i < p_Variants.size(); ++i) {
        if (s_Out[i].failure != RecipeFailure::None)
            continue;
        std::string_view s_Base = p_Variants[i].layerOf;
        for (size_t s_Depth = 0; !s_Base.empty(); ++s_Depth) {
            const auto s_IsBase = [&](const RecipeVariant& p_V) { return p_V.key == s_Base; };
            const auto s_It = std::find_if(p_Variants.begin(), p_Variants.end(), s_IsBase);
            if (s_It == p_Variants.end() || std::count_if(p_Variants.begin(), p_Variants.end(), s_IsBase) > 1 ||
                s_Depth >= p_Variants.size()) {
                s_Resolved[i] = {RecipeFailure::BaseNotBuildable, 0};
                break;
            }
            const RecipeOutcome& s_BaseOutcome = s_Out[static_cast<size_t>(s_It - p_Variants.begin())];
            if (s_BaseOutcome.failure != RecipeFailure::None) {
                s_Resolved[i] = {RecipeFailure::BaseNotBuildable, s_BaseOutcome.sourceRid};
                break;
            }
            s_Base = s_It->layerOf;
        }
    }

    return s_Resolved;
}

} // namespace fr
