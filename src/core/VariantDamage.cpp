#include "VariantDamage.h"

#include "Scene.h" // ToLower

#include <cmath>
#include <cstdio>

namespace fr {

namespace {

// "10", "0.01", "?".
std::string Number(std::optional<float> value) {
    if (!value)
        return "?";

    char text[32];
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(*value));
    return text;
}

} // namespace

bool IsDamageOverTimeClass(std::string_view className) {
    return ToLower(className) == ToLower(kDamageOverTimeClass);
}

std::string DamageBeforeText(const DamageBefore& before) {
    return Number(before.damage) + " every " + Number(before.interval) + " s";
}

bool DamageReadBackOk(std::optional<float> readBack, float expected) {
    if (!readBack)
        return true;

    return std::fabs(*readBack - expected) <= std::fabs(expected) * 1.0e-6f;
}

void AddDamage(DamageTally& tally, const DamageBefore& before, bool ok) {
    if (ok)
        ++tally.switchedOff;
    else
        ++tally.failed;

    tally.details += (tally.details.empty() ? "" : ", ") + DamageBeforeText(before);
}

std::string DamageTallyText(const DamageTally& tally) {
    if (tally.switchedOff == 0 && tally.failed == 0)
        return "timed damage: none in this variant's bricks";

    std::string text = std::string("timed damage ") +
                       (tally.switchedOff > 0 ? "switched off (" : "not switched off (") +
                       std::to_string(tally.switchedOff + tally.failed) + " " + std::string(kDamageOverTimeClass);

    if (tally.failed > 0)
        text += ", " + std::to_string(tally.failed) + " could not be switched off: 47 may still take damage";

    return text + "; they dealt " + tally.details + "; set to 0 damage, never ticking)";
}

} // namespace fr
