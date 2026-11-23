#include "ItemGrants.h"

#include "Scene.h" // ToLower

#include <algorithm>
#include <cctype>
#include <string>

namespace fr {

namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

bool Has(const std::string& lower, std::string_view word) {
    return lower.find(word) != std::string::npos;
}

// Known ids per kind (the given one first).
constexpr std::array<std::string_view, 1> kExplosiveIds = {kMakeshiftExplosiveId};
constexpr std::array<std::string_view, 5> kPoisonIds = {
    kLethalPoisonVialId,
    "67637973-ff21-4b00-88c3-304f8405dbb7", // Lethal Poison Vial ("Lethal Poison FataLIDOMIDE")
    "49765e76-dea7-4ad4-b502-2bad7727a15f", // Lethal Pills ("Lethal Pills S3")
    "ef63eda6-6411-4ce0-b35b-143fc5767fc0", // Lethal Pills
    "a1f89118-d345-4367-9423-620c3ef5dfba", // Lethal Poison Pill Jar
};
constexpr std::array<std::string_view, 3> kCoinIds = {
    kClassicCoinId,
    "dda002e9-02b1-4208-82a5-cf059f3c79cf", // Coin (the safehouse's bird's nest coin, SafehouseCoin.h)
    "b9b0c5f2-41d1-4087-a57b-48a67731b699", // ICA Outstanding Service Coin
};

template <size_t N>
bool AnyOf(const std::array<std::string_view, N>& ids, const Guid& id) {
    return std::any_of(ids.begin(), ids.end(), [&](std::string_view text) { return ParseGuid(text) == id; });
}

} // namespace

const char* GrantItemSetting(GrantItem item) {
    switch (item) {
        case GrantItem::MakeshiftExplosive: return "grant_makeshift_explosive";
        case GrantItem::LethalPoison: return "grant_lethal_poison";
        case GrantItem::Coin: return "grant_coin";
    }
    return "?";
}

const char* GrantItemName(GrantItem item) {
    switch (item) {
        case GrantItem::MakeshiftExplosive: return "Makeshift Explosive";
        case GrantItem::LethalPoison: return "Lethal Poison Vial";
        case GrantItem::Coin: return "Classic Coin";
    }
    return "?";
}

const char* GrantKindName(GrantItem item) {
    switch (item) {
        case GrantItem::MakeshiftExplosive: return "makeshift explosive";
        case GrantItem::LethalPoison: return "lethal poison";
        case GrantItem::Coin: return "coin";
    }
    return "?";
}

Guid GrantItemId(GrantItem item) {
    switch (item) {
        case GrantItem::MakeshiftExplosive: return *ParseGuid(kMakeshiftExplosiveId);
        case GrantItem::LethalPoison: return *ParseGuid(kLethalPoisonVialId);
        case GrantItem::Coin: return *ParseGuid(kClassicCoinId);
    }
    return {};
}

size_t GrantItemIndex(GrantItem item) {
    return static_cast<size_t>(item);
}

bool IsGrantKindId(GrantItem item, const Guid& id) {
    switch (item) {
        case GrantItem::MakeshiftExplosive: return AnyOf(kExplosiveIds, id);
        case GrantItem::LethalPoison: return AnyOf(kPoisonIds, id);
        case GrantItem::Coin: return AnyOf(kCoinIds, id);
    }
    return false;
}

bool HasTrait(std::string_view traits, std::string_view trait) {
    const std::string want = ToLower(Trim(trait));

    while (!traits.empty()) {
        const size_t comma = traits.find(',');

        if (ToLower(Trim(traits.substr(0, comma))) == want)
            return true;
        if (comma == std::string_view::npos)
            break;

        traits.remove_prefix(comma + 1);
    }

    return false;
}

bool IsGrantKindItem(GrantItem item, std::string_view title, std::string_view commonName, std::string_view traits) {
    const std::string names = ToLower(title) + "|" + ToLower(commonName);
    const bool noTraits = Trim(traits).empty();

    switch (item) {
        case GrantItem::MakeshiftExplosive:
            return Has(names, "makeshift") && Has(names, "explosive") &&
                   !HasTrait(traits, "detonator_explosive_device");

        case GrantItem::LethalPoison:
            if (!Has(names, "lethal"))
                return false;
            if (!noTraits)
                return HasTrait(traits, "consumable_poison");
            return (Has(names, "poison") || Has(names, "pill")) && !Has(names, "syringe");

        case GrantItem::Coin: return Has(names, "coin");
    }

    return false;
}

GrantVerdict DecideGrant(bool on, size_t ownOfKind) {
    if (!on)
        return GrantVerdict::Off;

    return ownOfKind > 0 ? GrantVerdict::Carried : GrantVerdict::Give;
}

GrantStartVerdict DecideGrantStart(const GrantStartInput& input) {
    if (input.timer < kGrantDelaySeconds)
        return GrantStartVerdict::Wait;
    if (input.listDone && !input.loadoutPending)
        return GrantStartVerdict::Go;
    return input.timer >= kGrantLoadoutWaitSeconds ? GrantStartVerdict::GoTimedOut : GrantStartVerdict::Wait;
}

const char* GrantStartVerdictName(GrantStartVerdict verdict) {
    switch (verdict) {
        case GrantStartVerdict::Wait: return "waiting";
        case GrantStartVerdict::Go: return "ready";
        case GrantStartVerdict::GoTimedOut: return "timed out waiting (given anyway)";
    }
    return "?";
}

} // namespace fr
