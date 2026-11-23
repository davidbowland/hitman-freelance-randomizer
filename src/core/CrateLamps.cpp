#include "CrateLamps.h"

#include "LandslideFull.h"
#include "Variants.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace fr {

namespace {

// Sub-entity ids from the template's blueprint (TBLU 00430B7BE6F18F94).
constexpr std::array<CrateLampEntity, kCrateLampEntityCount> kEntities {{
    {0x834100cee5e680edull, "FX_Logic_LampCore_01", CrateLampPart::LampCore, "DiffusePower"},
    {0xb667d37a9d333f1full, "Light_Area_Side_04", CrateLampPart::Center, "m_fDiffusePower"},
    {0x623b6598fcbc59d9ull, "Light_Env_05", CrateLampPart::Environment, "m_fDiffusePower"},
    {0x9b3f9129928ae948ull, "Light_Area_Side", CrateLampPart::Side, "m_fDiffusePower"},
    {0x4aaaeb824186bb86ull, "Light_Area_Side_01", CrateLampPart::Side, "m_fDiffusePower"},
    {0xbbcedd4f15192887ull, "Light_Area_Side_02", CrateLampPart::Side, "m_fDiffusePower"},
    {0x415ea38858ceed0aull, "Light_Area_Side_03", CrateLampPart::Side, "m_fDiffusePower"},
}};

// Night caps: a modest glow next to the day placements' 25 to 7000. The side
// lights get what the lamp core sends them.
constexpr float kLampCoreNightCap = 5.0f;
constexpr float kCenterNightCap = 3.0f;
constexpr float kEnvironmentNightCap = 3.0f;

// Above the cap by more than this: a float read back from the engine.
constexpr float kCapTolerance = 0.01f;

std::string Number(float p_Value) {
    char s_Text[32];
    std::snprintf(s_Text, sizeof(s_Text), "%g", static_cast<double>(p_Value));
    return s_Text;
}

} // namespace

bool IsNightTimeOfDayBrick(std::string_view p_Name) {
    return p_Name.starts_with("tod_") && p_Name.find("night") != std::string_view::npos;
}

std::string_view NightTimeOfDayOf(const VariantOption& p_Option) {
    for (const VariantBrick* s_Brick : p_Option.bricks) {
        if (s_Brick && IsNightTimeOfDayBrick(s_Brick->name))
            return s_Brick->name;
    }

    return {};
}

NightVerdict JudgeNightLoad(std::string_view p_Applied, bool p_Full) {
    if (p_Applied.empty())
        return {false, "no map variant on this load (Freelancer's maps are all day)"};

    std::string s_Who;
    const VariantOption* s_Option = nullptr;

    if (p_Full) {
        const FullVariant* s_Full = FullVariantOfKey(p_Applied);

        if (!s_Full)
            return {false, "full version \"" + std::string(p_Applied) + "\" is not known"};

        s_Who = std::string(s_Full->fullName);
        s_Option = FindVariantOption(s_Full->option);
    }
    else {
        s_Who = "map variant " + std::string(p_Applied);
        s_Option = FindVariantOption(p_Applied);
    }

    if (!s_Option)
        return {false, s_Who + ": its option is not known"};

    const std::string_view s_Night = NightTimeOfDayOf(*s_Option);

    if (s_Night.empty())
        return {false, s_Who + " loads no night time of day"};

    return {true, s_Who + " loads " + std::string(s_Night)};
}

std::span<const CrateLampEntity> CrateLampEntities() {
    return kEntities;
}

float CrateLampNightCap(CrateLampPart p_Part) {
    switch (p_Part) {
        case CrateLampPart::LampCore:
        case CrateLampPart::Side: return kLampCoreNightCap;
        case CrateLampPart::Center: return kCenterNightCap;
        case CrateLampPart::Environment: return kEnvironmentNightCap;
    }

    return kCenterNightCap;
}

std::optional<float> CrateLampNightValue(CrateLampPart p_Part, float p_Value) {
    const float s_Cap = CrateLampNightCap(p_Part);

    // Not a number (a bad read) is not a power to lower.
    if (!(p_Value > s_Cap + kCapTolerance))
        return std::nullopt;

    return s_Cap;
}

std::string_view CrateLampPartName(CrateLampPart p_Part) {
    switch (p_Part) {
        case CrateLampPart::LampCore: return "lamp core";
        case CrateLampPart::Center: return "center light";
        case CrateLampPart::Environment: return "environment light";
        case CrateLampPart::Side: return "side light";
    }

    return "?";
}

std::string CrateLampValueSpan(std::span<const float> p_Values) {
    if (p_Values.empty())
        return "none";

    const auto [s_Low, s_High] = std::minmax_element(p_Values.begin(), p_Values.end());

    if (*s_Low == *s_High)
        return Number(*s_Low);

    return Number(*s_Low) + " to " + Number(*s_High);
}

} // namespace fr
