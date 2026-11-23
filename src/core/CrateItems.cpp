#include "CrateItems.h"

#include "Rng.h"
#include "WeaponRarity.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <optional>
#include <utility>

namespace fr {

bool IsCrateOptionKeyId(uint64_t id) {
    return std::find(kCrateOptionKeyIds.begin(), kCrateOptionKeyIds.end(), id) != kCrateOptionKeyIds.end();
}

std::vector<size_t> CrateToolSlots(std::span<const CrateSlotSummary> selectors) {
    std::vector<size_t> s_Slots;

    for (size_t i = 0; i < selectors.size(); ++i) {
        const CrateSlotSummary& s_Selector = selectors[i];

        if (s_Selector.options > 0 && s_Selector.firearms == 0 && s_Selector.merces == 0)
            s_Slots.push_back(i);
    }

    return s_Slots;
}

bool IsMercesItem(std::string_view name, std::string_view icon) {
    return icon == "evergreen_payout" || name.find("Mercer_Pickup") != std::string_view::npos;
}

// --- crate_items ------------------------------------------------------------------------------------------------

namespace {

constexpr std::string_view kCratePlanFeature = "crate_items_plan";
// Sub-draws of one open's plan (DialHash param: open << 8 | sub).
constexpr uint64_t kSubKind = 1;
constexpr uint64_t kSubGun = 2;
constexpr uint64_t kSubFirst = 4; // a regular crate's first draw (regular and large items together)
constexpr uint64_t kSubSlots = 8; // + slot (regular items), + 3 + slot (tools)

double PlanUniform(int32_t seed, std::string_view scene, uint32_t openIndex, uint64_t sub) {
    return UniformFromHash(DialHash(seed, scene, kCratePlanFeature, (static_cast<uint64_t>(openIndex) << 8) | sub));
}

size_t IndexOf(double u, size_t size) {
    return std::min(static_cast<size_t>(std::floor(u * static_cast<double>(size))), size - 1);
}

bool ContainsLower(std::string_view text, std::string_view needle) {
    if (needle.size() > text.size())
        return false;

    for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
        bool s_Match = true;

        for (size_t k = 0; k < needle.size() && s_Match; ++k) {
            char c = text[i + k];

            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');

            s_Match = c == needle[k];
        }

        if (s_Match)
            return true;
    }

    return false;
}

double Clamp01(double value, double fallback) {
    if (std::isnan(value))
        return fallback;

    return std::clamp(value, 0.0, 1.0);
}

// A draw from pool without the candidates in taken; empty when nothing is left.
std::optional<size_t> DrawDistinct(double u, std::span<const size_t> pool, std::span<const size_t> taken) {
    std::vector<size_t> s_Left;

    for (const size_t i : pool) {
        if (std::find(taken.begin(), taken.end(), i) == taken.end())
            s_Left.push_back(i);
    }

    if (s_Left.empty())
        return std::nullopt;

    return s_Left[IndexOf(u, s_Left.size())];
}

// "Apple (regular, vanilla crate)"; an item that comes alone says why: "Hatchet (regular, level; no size in the game
// files; ...)".
std::string PickText(std::span<const CrateCandidate> candidates, std::optional<size_t> index, CratePickPool pool) {
    if (!index || *index >= candidates.size())
        return "(none)";

    const CrateCandidate& s_Item = candidates[*index];
    const std::string s_Why = s_Item.large && !s_Item.aloneWhy.empty() ? "; " + s_Item.aloneWhy : std::string();
    return s_Item.name + " (" + CratePickPoolName(pool) + ", " + CrateItemOrigin(s_Item) + s_Why + ")";
}

} // namespace

const char* CrateItemOrigin(const CrateCandidate& item) {
    switch ((item.inCrate ? 4 : 0) | (item.onMap ? 2 : 0) | (item.onLevel ? 1 : 0)) {
        case 7: return "vanilla crate, map and level";
        case 6: return "vanilla crate and map";
        case 5: return "vanilla crate and level";
        case 4: return "vanilla crate";
        case 3: return "map and level";
        case 2: return "map";
        case 1: return "level";
        default: break;
    }

    return item.loaded ? "loaded" : "not loaded";
}

bool IsFreelancerTool(bool loseOnWounded, std::string_view icon, std::string_view name) {
    return loseOnWounded && !IsFirearmIcon(icon) && !IsMercesItem(name, icon);
}

bool IsLargeCrateItem(std::string_view itemSize, bool vanillaCrateItem) {
    return !vanillaCrateItem && ContainsLower(itemSize, "large");
}

namespace {

// The grips of a long one-handed melee item (pro.repo GripAnimType), for an item with no size in the game files.
constexpr std::string_view kLongGrips[] {"igt_melee_1h_stick", "igt_melee_1h_hammer", "igt_melee_1h_baton",
                                         "igt_melee_1h_sword"};

std::string LowerAscii(std::string_view text) {
    std::string s_Out(text);

    for (char& c : s_Out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }

    return s_Out;
}

} // namespace

CrateItemFit JudgeCrateItemFit(std::string_view itemSize, std::string_view grip, std::string_view icon,
                               std::optional<CrateItemLength> length, bool vanillaCrateItem) {
    CrateItemFit s_Fit;
    s_Fit.length = length;

    if (vanillaCrateItem)
        return s_Fit;

    if (IsLargeCrateItem(itemSize, false)) {
        s_Fit.alone = CrateAlone::ItemSize;
        return s_Fit;
    }

    if (IsFirearmIcon(icon))
        return s_Fit;

    if (length) {
        if (std::isfinite(length->meters) && length->meters > kCrateSlotLengthMeters)
            s_Fit.alone = CrateAlone::TooLong;

        return s_Fit;
    }

    const std::string s_Grip = LowerAscii(grip);

    if (LowerAscii(icon) == "melee" && ContainsLower(itemSize, "medium") &&
        std::find(std::begin(kLongGrips), std::end(kLongGrips), s_Grip) != std::end(kLongGrips))
        s_Fit.alone = CrateAlone::UnknownLong;

    return s_Fit;
}

std::string DescribeCrateItemFit(const CrateItemFit& fit, std::string_view grip) {
    char s_Buf[160];

    switch (fit.alone) {
        case CrateAlone::No: return {};
        case CrateAlone::ItemSize: return "ItemSize large";
        case CrateAlone::TooLong:
            std::snprintf(s_Buf, sizeof(s_Buf), "too long for a slot: %.2f m (%s)",
                          fit.length ? fit.length->meters : 0.0,
                          fit.length && fit.length->source == CrateLengthSource::Model ? "its model"
                                                                                       : "its box in the game files");
            return s_Buf;
        case CrateAlone::UnknownLong: {
            // "IGT_Melee_1H_Stick" -> "stick".
            const std::string s_Grip = LowerAscii(grip);
            const size_t s_Last = s_Grip.rfind('_');
            const std::string s_Kind = s_Last == std::string::npos ? s_Grip : s_Grip.substr(s_Last + 1);
            std::snprintf(s_Buf, sizeof(s_Buf),
                          "no size in the game files; a medium melee item held like a %s, which mostly are longer "
                          "than a slot's %.2f m",
                          s_Kind.empty() ? "stick" : s_Kind.c_str(), kCrateSlotLengthMeters);
            return s_Buf;
        }
    }

    return "?";
}

std::optional<uint64_t> ParseRuntimeId(std::string_view text) {
    if (text.empty() || text.size() > 20)
        return std::nullopt;

    uint64_t s_Value = 0;

    for (const char c : text) {
        if (c < '0' || c > '9')
            return std::nullopt;

        const auto s_Digit = static_cast<uint64_t>(c - '0');

        if (s_Value > (UINT64_MAX - s_Digit) / 10)
            return std::nullopt;

        s_Value = s_Value * 10 + s_Digit;
    }

    if (s_Value == 0)
        return std::nullopt;

    return s_Value;
}

bool CrateGunOwned(std::optional<bool> flagsEligible, std::optional<bool> listed) {
    if (!flagsEligible && !listed)
        return true;

    if (flagsEligible && !*flagsEligible)
        return true;

    return listed.value_or(false);
}

std::string FoldCrateItemName(std::string_view name) {
    const auto s_Space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };

    while (!name.empty() && s_Space(name.front()))
        name.remove_prefix(1);

    while (!name.empty() && s_Space(name.back()))
        name.remove_suffix(1);

    std::string s_Out(name);

    for (char& c : s_Out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }

    return s_Out;
}

CrateOwnedItems MakeCrateOwnedItems(std::span<const Guid> listed, std::span<const std::pair<Guid, std::string>> named) {
    CrateOwnedItems s_Owned;
    s_Owned.ids.assign(listed.begin(), listed.end());
    std::sort(s_Owned.ids.begin(), s_Owned.ids.end());
    s_Owned.ids.erase(std::unique(s_Owned.ids.begin(), s_Owned.ids.end()), s_Owned.ids.end());

    for (const auto& [s_Id, s_Name] : named) {
        if (!std::binary_search(s_Owned.ids.begin(), s_Owned.ids.end(), s_Id))
            continue;

        std::string s_Folded = FoldCrateItemName(s_Name);

        if (!s_Folded.empty())
            s_Owned.names.push_back(std::move(s_Folded));
    }

    std::sort(s_Owned.names.begin(), s_Owned.names.end());
    s_Owned.names.erase(std::unique(s_Owned.names.begin(), s_Owned.names.end()), s_Owned.names.end());
    return s_Owned;
}

CrateOwnedBy CrateItemListed(const CrateOwnedItems& owned, const Guid& item, std::string_view name) {
    if (std::binary_search(owned.ids.begin(), owned.ids.end(), item))
        return CrateOwnedBy::Id;

    const std::string s_Folded = FoldCrateItemName(name);

    if (!s_Folded.empty() && std::binary_search(owned.names.begin(), owned.names.end(), s_Folded))
        return CrateOwnedBy::Name;

    return CrateOwnedBy::None;
}

CrateItemsConfig SanitizeCrateItemsConfig(CrateItemsConfig cfg) {
    const CrateItemsConfig s_Defaults;
    cfg.toolsChance = Clamp01(cfg.toolsChance, s_Defaults.toolsChance);
    cfg.mercesChance = Clamp01(cfg.mercesChance, s_Defaults.mercesChance);
    return cfg;
}

CrateKindChances CrateKindChancesOf(const CrateItemsConfig& cfg, const CrateGunsConfig& guns) {
    CrateKindChances s_Chances;
    s_Chances.gun = guns.on ? Clamp01(guns.poolChance, 0.0) : 0.0;
    s_Chances.tools = Clamp01(cfg.toolsChance, 0.0);
    s_Chances.merces = Clamp01(cfg.mercesChance, 0.0);
    const double s_Sum = s_Chances.gun + s_Chances.tools + s_Chances.merces;

    // Over 100%: each scaled down by the same factor, so the bands still add up to the whole roll.
    if (s_Sum > 1.0) {
        s_Chances.scale = 1.0 / s_Sum;
        s_Chances.gun *= s_Chances.scale;
        s_Chances.tools *= s_Chances.scale;
        s_Chances.merces *= s_Chances.scale;
    }

    return s_Chances;
}

std::string DescribeCrateItemsConfig(const CrateItemsConfig& cfg, const CrateGunsConfig& guns) {
    const CrateKindChances s_Chances = CrateKindChancesOf(cfg, guns);
    char s_Scaled[96] = "";

    if (s_Chances.scale < 1.0)
        std::snprintf(s_Scaled, sizeof(s_Scaled), " (set chances add up to %g%%: scaled by %.3f)",
                      100.0 / s_Chances.scale, s_Chances.scale);

    char s_Buf[512];
    std::snprintf(s_Buf, sizeof(s_Buf), "gun %g%%%s, tools %g%%, merces %g%%, regular %g%%%s, %s, %s, %s",
                  100.0 * s_Chances.gun, guns.on ? "" : " (crate_guns off)", 100.0 * s_Chances.tools,
                  100.0 * s_Chances.merces, 100.0 * s_Chances.regular(), s_Scaled,
                  cfg.unloaded ? "gun and tool crates take items the map has not loaded too"
                               : "gun and tool crates take items the map has loaded only",
                  cfg.evenBoxes ? "selector boxes one size" : "selector boxes as the game sizes them",
                  cfg.levelItems ? "items around the level too" : "no items from around the level");
    return s_Buf;
}

CratePools BuildCratePools(std::span<const CrateCandidate> candidates, const CrateItemsConfig& cfg, bool guns,
                           std::span<const Guid> exclude) {
    CratePools s_Pools;
    std::vector<size_t> s_Order(candidates.size());

    for (size_t i = 0; i < s_Order.size(); ++i)
        s_Order[i] = i;

    std::sort(s_Order.begin(), s_Order.end(),
              [&](size_t a, size_t b) { return candidates[a].item < candidates[b].item; });

    std::vector<Guid> s_Seen;

    // Each item once, however many option helpers hold it (a vanilla crate's item that is on the map too).
    for (const size_t i : s_Order) {
        const CrateCandidate& s_Item = candidates[i];

        if (s_Item.item.IsEmpty() || std::find(s_Seen.begin(), s_Seen.end(), s_Item.item) != s_Seen.end())
            continue;

        s_Seen.push_back(s_Item.item);

        if (s_Item.name.empty()) {
            ++s_Pools.unnamed;
            continue;
        }

        if (s_Item.merces) {
            ++s_Pools.merces;
            continue;
        }

        // A mission's quest item (an NPC's ID card, an event pass; any map's): never a crate's, but for a usable
        // prop (the Fuse Cell, Gun Powder: IsUsableQuestProp).
        if (IsQuestItemIcon(s_Item.icon)) {
            if (!IsUsableQuestProp(s_Item.item)) {
                ++s_Pools.quest;
                continue;
            }

            ++s_Pools.usableProps;
        }

        // Only guns and tools come from beyond the map, and only with unloaded on.
        if (!s_Item.loaded && (!cfg.unloaded || (!s_Item.firearm && !s_Item.tool))) {
            ++s_Pools.notLoaded;
            continue;
        }

        const bool s_Unique = s_Item.firearm || s_Item.large;

        if (s_Unique && std::find(exclude.begin(), exclude.end(), s_Item.item) != exclude.end()) {
            ++s_Pools.taken;
            continue;
        }

        if (s_Item.firearm) {
            if (!guns)
                ++s_Pools.gunsOff;
            else if (s_Item.owned)
                ++s_Pools.owned;
            else
                s_Pools.guns.push_back(i);

            continue;
        }

        // Any other item you own (the CPD's owned lists).
        if (s_Item.owned) {
            ++s_Pools.ownedItems;
            s_Pools.ownedTools += s_Item.tool ? 1 : 0;
            continue;
        }

        if (s_Item.large) {
            if (s_Item.loaded)
                s_Pools.large.push_back(i);

            continue;
        }

        if (s_Item.loaded)
            s_Pools.regular.push_back(i);

        if (s_Item.tool)
            s_Pools.tools.push_back(i);
    }

    return s_Pools;
}

std::string DescribeCratePools(const CratePools& pools) {
    char s_Usable[64] = "";

    if (pools.usableProps)
        std::snprintf(s_Usable, sizeof(s_Usable), "; kept: %zu usable prop%s (quest items)", pools.usableProps,
                      pools.usableProps == 1 ? "" : "s");

    char s_Buf[512];
    std::snprintf(s_Buf, sizeof(s_Buf),
                  "regular %zu, tools %zu, guns %zu, large %zu; left out: %zu unnamed, %zu merces, %zu quest items, "
                  "%zu not loaded, %zu owned guns, %zu owned items (%zu Freelancer tools), %zu guns (crate_guns off), "
                  "%zu already planned%s%s",
                  pools.regular.size(), pools.tools.size(), pools.guns.size(), pools.large.size(), pools.unnamed,
                  pools.merces, pools.quest, pools.notLoaded, pools.owned, pools.ownedItems, pools.ownedTools,
                  pools.gunsOff, pools.taken, pools.oneItem ? "" : ", no one-item crate", s_Usable);
    return s_Buf;
}

// --- items around the level ---------------------------------------------------------------------------------------

const char* LevelItemVerdictName(LevelItemVerdict verdict) {
    switch (verdict) {
        case LevelItemVerdict::Kept: return "kept";
        case LevelItemVerdict::Unnamed: return "unnamed";
        case LevelItemVerdict::NotItem: return "not inventory items";
        case LevelItemVerdict::Firearm: return "firearms (gun crates only)";
        case LevelItemVerdict::Merces: return "merces";
        case LevelItemVerdict::Container: return "containers";
        case LevelItemVerdict::Ammo: return "ammo";
        case LevelItemVerdict::NoModel: return "without a model (no Runtime id)";
        case LevelItemVerdict::Story: return "story items";
        case LevelItemVerdict::NotPickup: return "with no pick-up (drinks and food to poison)";
        case LevelItemVerdict::Quest: return "quest items (a mission's props)";
    }

    return "?";
}

namespace {

// The items the player gets no pick-up for (IsNotPickupItem), by repository id.
constexpr const char* kNotPickupItems[] {
#include "NotPickupItems.inc"
};

} // namespace

bool IsQuestItemIcon(std::string_view icon) {
    return icon.size() == 9 && ContainsLower(icon, "questitem");
}

namespace {

// QuestItems 47 uses on the level (IsUsableQuestProp), by repository id. Of the 42 QuestItems found on the 15 maps
// (tools/rpkg/level_items.py --walk) the other 38 are story items: NPCs' ID cards, invitations and event
// passes, notes, memos, a diary, safe combinations, access codes and dongles, the USB Kill List, a bag of drugs for a
// dealer, a joint, Bangkok's faulty studio microphone (found on Mendoza, nothing there to use it on).
constexpr const char* kUsableQuestProps[] {
    "daa20d0d-5da4-4ab0-9c7b-39bcee192241", // Fuse Cell (Fuse_Cell): a fuse box's cell
    "e65953cb-f954-4d21-9f11-52b454cac15e", // Gun Powder (QuestItem_GunPowder_Pouch)
    "01048280-0358-4f0a-95b7-1f9f665c1648", // Insecticide
    "ac77e98d-4ffa-4755-80fc-cd6e7adc63fb", // Expired Canned Bolognese (Sick): eCC_Brick, melee_nonlethal
};

} // namespace

bool IsUsableQuestProp(const Guid& item) {
    return std::any_of(std::begin(kUsableQuestProps), std::end(kUsableQuestProps),
                       [&](const char* p_Row) { return ParseGuid(p_Row) == std::optional<Guid>(item); });
}

bool IsNotPickupItem(const Guid& item) {
    return std::any_of(std::begin(kNotPickupItems), std::end(kNotPickupItems),
                       [&](const char* p_Row) { return ParseGuid(p_Row) == std::optional<Guid>(item); });
}

LevelItemVerdict ClassifyLevelItem(std::string_view name, std::string_view commonName, std::string_view icon,
                                   std::string_view itemType, bool hasRuntime, bool canPickUp) {
    if (name.empty())
        return LevelItemVerdict::Unnamed;

    if (icon.empty() || ContainsLower(icon, "invalid"))
        return LevelItemVerdict::NotItem;

    if (IsFirearmIcon(icon))
        return LevelItemVerdict::Firearm;

    if (IsMercesItem(name, icon) || IsMercesItem(commonName, icon) || ContainsLower(name, "merces") ||
        ContainsLower(icon, "payout") || ContainsLower(icon, "money"))
        return LevelItemVerdict::Merces;

    for (const std::string_view text : {icon, itemType, name, commonName}) {
        for (const std::string_view part : {"container", "suitcase", "briefcase"}) {
            if (ContainsLower(text, part))
                return LevelItemVerdict::Container;
        }
    }

    if (ContainsLower(itemType, "ammo") || ContainsLower(icon, "ammo"))
        return LevelItemVerdict::Ammo;

    if (!canPickUp)
        return LevelItemVerdict::NotPickup;

    if (!hasRuntime)
        return LevelItemVerdict::NoModel;

    for (const std::string_view text : {name, commonName}) {
        for (const std::string_view part : {"intel", "evidence", "dossier", "document"}) {
            if (ContainsLower(text, part))
                return LevelItemVerdict::Story;
        }
    }

    // A mission's quest item (an NPC's ID card, an event pass): not a crate's to hand out.
    if (IsQuestItemIcon(icon))
        return LevelItemVerdict::Quest;

    return LevelItemVerdict::Kept;
}

std::string LevelItemCategory(std::string_view icon, std::string_view itemType) {
    if (ContainsLower(icon, "key") || ContainsLower(itemType, "keycard"))
        return "key";

    if (icon.empty())
        return "?";

    std::string s_Lower(icon);

    for (char& c : s_Lower) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }

    return s_Lower;
}

LevelItems CollectLevelItems(std::span<const LevelSpawner> spawners) {
    LevelItems s_Items;
    s_Items.spawners = spawners.size();
    std::vector<size_t> s_First; // one spawner per distinct item

    for (size_t i = 0; i < spawners.size(); ++i) {
        const LevelSpawner& s_Spawner = spawners[i];

        if (!s_Spawner.item || s_Spawner.item->IsEmpty()) {
            ++s_Items.noItem;
            continue;
        }

        if (s_Spawner.freelancerBrick) {
            ++s_Items.freelancer;
            continue;
        }

        if (!s_Spawner.spawnOnStart) {
            ++s_Items.startUnread;
            continue;
        }

        if (!*s_Spawner.spawnOnStart) {
            ++s_Items.notAtStart;
            continue;
        }

        ++s_Items.atStart;
        const bool s_Known =
            std::any_of(s_First.begin(), s_First.end(), [&](size_t k) { return *spawners[k].item == *s_Spawner.item; });

        if (!s_Known)
            s_First.push_back(i);
    }

    // Sorted by GUID: the pools do not depend on the walk order.
    std::sort(s_First.begin(), s_First.end(),
              [&](size_t a, size_t b) { return *spawners[a].item < *spawners[b].item; });
    s_Items.distinct = s_First.size();

    for (const size_t i : s_First) {
        const LevelSpawner& s_Spawner = spawners[i];
        LevelItemVerdict s_Verdict =
            ClassifyLevelItem(s_Spawner.name, s_Spawner.commonName, s_Spawner.icon, s_Spawner.itemType,
                              s_Spawner.hasRuntime, !IsNotPickupItem(*s_Spawner.item));

        // A quest item 47 uses on the level (the Fuse Cell, Gun Powder): kept, logged as a usable prop.
        if (s_Verdict == LevelItemVerdict::Quest && IsUsableQuestProp(*s_Spawner.item)) {
            s_Verdict = LevelItemVerdict::Kept;
            s_Items.usable.push_back(i);
        }

        ++s_Items.verdicts[static_cast<size_t>(s_Verdict)];

        if (s_Verdict == LevelItemVerdict::Kept)
            s_Items.kept.push_back(i);
        else if (s_Verdict == LevelItemVerdict::Story)
            s_Items.story.push_back(i);
        else if (s_Verdict == LevelItemVerdict::NotPickup)
            s_Items.notPickup.push_back(i);
        else if (s_Verdict == LevelItemVerdict::Quest)
            s_Items.quest.push_back(i);
    }

    return s_Items;
}

std::string DescribeLevelItems(std::span<const LevelSpawner> spawners, const LevelItems& items, size_t maxNames) {
    std::string s_Text =
        std::to_string(items.spawners) + " item spawners outside the option helpers: " + std::to_string(items.atStart) +
        " spawn at start (" + std::to_string(items.notAtStart) + " on demand, " + std::to_string(items.startUnread) +
        " unread, " + std::to_string(items.freelancer) + " in evergreen_setup, " + std::to_string(items.noItem) +
        " without an item key); " + std::to_string(items.distinct) +
        " distinct items: " + std::to_string(items.kept.size()) + " kept";

    // By category, most first (a tie: by name).
    std::vector<std::pair<std::string, size_t>> s_Categories;

    for (const size_t i : items.kept) {
        if (i >= spawners.size())
            continue;

        const std::string s_Category = LevelItemCategory(spawners[i].icon, spawners[i].itemType);
        const auto s_Found = std::find_if(s_Categories.begin(), s_Categories.end(),
                                          [&](const auto& c) { return c.first == s_Category; });

        if (s_Found == s_Categories.end())
            s_Categories.emplace_back(s_Category, 1);
        else
            ++s_Found->second;
    }

    std::sort(s_Categories.begin(), s_Categories.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });

    if (!s_Categories.empty()) {
        s_Text += " (";

        for (size_t k = 0; k < s_Categories.size(); ++k)
            s_Text += (k ? ", " : "") + s_Categories[k].first + " " + std::to_string(s_Categories[k].second);

        s_Text += ")";
    }

    if (!items.kept.empty()) {
        s_Text += ": ";

        for (size_t k = 0; k < items.kept.size() && k < maxNames; ++k) {
            if (items.kept[k] < spawners.size())
                s_Text += (k ? ", " : "") + spawners[items.kept[k]].name;
        }

        if (items.kept.size() > maxNames)
            s_Text += " (+" + std::to_string(items.kept.size() - maxNames) + " more)";
    }

    s_Text += "; left out:";

    for (size_t v = 1; v < kLevelItemVerdicts; ++v)
        s_Text += (v > 1 ? ", " : " ") + std::to_string(items.verdicts[v]) + " " +
                  LevelItemVerdictName(static_cast<LevelItemVerdict>(v));

    if (!items.story.empty()) {
        s_Text += "; story items left out:";

        for (size_t k = 0; k < items.story.size(); ++k) {
            if (items.story[k] < spawners.size())
                s_Text += (k ? ", " : " ") + spawners[items.story[k]].name;
        }
    }

    if (!items.notPickup.empty()) {
        s_Text += "; no pick-up, left out:";

        for (size_t k = 0; k < items.notPickup.size(); ++k) {
            if (items.notPickup[k] < spawners.size())
                s_Text += (k ? ", " : " ") + spawners[items.notPickup[k]].name;
        }
    }

    if (!items.quest.empty()) {
        s_Text += "; quest items left out:";

        for (size_t k = 0; k < items.quest.size(); ++k) {
            if (items.quest[k] < spawners.size())
                s_Text += (k ? ", " : " ") + spawners[items.quest[k]].name;
        }
    }

    if (!items.usable.empty()) {
        s_Text += "; kept: usable prop:";

        for (size_t k = 0; k < items.usable.size(); ++k) {
            if (items.usable[k] < spawners.size())
                s_Text += (k ? ", " : " ") + spawners[items.usable[k]].name;
        }
    }

    return s_Text;
}

const char* CratePlanKindName(CratePlanKind kind) {
    switch (kind) {
        case CratePlanKind::Vanilla: return "vanilla";
        case CratePlanKind::Items: return "regular";
        case CratePlanKind::Tools: return "tools";
        case CratePlanKind::Gun: return "gun";
        case CratePlanKind::Merces: return "merces";
        case CratePlanKind::Large: return "large item";
    }

    return "?";
}

const char* CratePickPoolName(CratePickPool pool) {
    switch (pool) {
        case CratePickPool::None: return "none";
        case CratePickPool::Regular: return "regular";
        case CratePickPool::Tools: return "Freelancer tools";
        case CratePickPool::Gun: return "gun";
    }

    return "?";
}

CratePlan PlanCrate(int32_t seed, std::string_view scene, uint32_t openIndex,
                    std::span<const CrateCandidate> candidates, const CratePools& pools, const CrateItemsConfig& cfg,
                    const CrateGunsConfig& guns) {
    CratePlan s_Plan;
    s_Plan.roll = PlanUniform(seed, scene, openIndex, kSubKind);
    const CrateKindChances s_Chances = CrateKindChancesOf(cfg, guns);
    const double s_GunEnd = s_Chances.gun;
    const double s_ToolsEnd = s_GunEnd + s_Chances.tools;
    const double s_MercesEnd = s_ToolsEnd + s_Chances.merces;
    std::string s_Fell; // kinds the roll fell through ("gun: you own every gun left; ")
    char s_Buf[256];
    const std::string s_Scaled = s_Chances.scale < 1.0
                                     ? "; chances scaled by " + std::to_string(s_Chances.scale) + " to fit 100%"
                                     : std::string();

    const auto s_Why = [&](const char* p_Text) { s_Plan.why = s_Fell + p_Text + s_Scaled; };

    // Distinct items of p_Pool into the empty slots (p_Taken: already placed); false when the pool runs out.
    const auto s_Fill = [&](std::span<const size_t> p_Pool, uint64_t p_Sub, CratePickPool p_From,
                            std::vector<size_t>& p_Taken) {
        for (size_t s = 0; s < 3; ++s) {
            if (s_Plan.slots[s])
                continue;

            const std::optional<size_t> s_Pick =
                DrawDistinct(PlanUniform(seed, scene, openIndex, p_Sub + s), p_Pool, p_Taken);

            if (!s_Pick)
                return false;

            s_Plan.slots[s] = s_Pick;
            s_Plan.from[s] = p_From;
            p_Taken.push_back(*s_Pick);
        }

        return true;
    };

    // A gun crate: one gun you don't own, alone in the weapon pool's one-item option, whatever its size.
    if (s_Plan.roll < s_GunEnd) {
        if (!pools.guns.empty() && pools.oneItem) {
            // By rarity: the vanilla rarity value times its multiplier.
            const auto s_Weight = [&](size_t i) {
                const int32_t r = candidates[i].rarity;
                return CrateRarityValue(r) * guns.rarityMult[CrateRarityIndex(r)];
            };
            double s_Total = 0.0;

            for (const size_t i : pools.guns)
                s_Total += s_Weight(i);

            const double s_U = PlanUniform(seed, scene, openIndex, kSubGun);
            size_t s_GunPick = pools.guns[IndexOf(s_U, pools.guns.size())];

            if (s_Total > 0.0) {
                double s_At = s_U * s_Total;
                s_GunPick = pools.guns.back();

                for (const size_t i : pools.guns) {
                    s_At -= s_Weight(i);

                    if (s_At < 0.0) {
                        s_GunPick = i;
                        break;
                    }
                }
            }

            s_Plan.kind = CratePlanKind::Gun;
            s_Plan.single = s_GunPick;
            s_Plan.singleFrom = CratePickPool::Gun;
            std::snprintf(s_Buf, sizeof(s_Buf), "roll %.4f < gun %g: one gun alone (%zu guns you don't own)",
                          s_Plan.roll, s_GunEnd, pools.guns.size());
            s_Why(s_Buf);
            return s_Plan;
        }

        s_Fell += pools.guns.empty() ? "gun: no gun you don't own left to offer, a regular crate instead; "
                                     : "gun: no one-item crate now, a regular crate instead; ";
    }
    // A tool crate: up to three Freelancer tools you don't own, a slot left over takes a regular item.
    else if (s_Plan.roll < s_ToolsEnd) {
        if (!pools.tools.empty()) {
            std::vector<size_t> s_Taken;
            s_Fill(pools.tools, kSubSlots + 3, CratePickPool::Tools, s_Taken);
            const size_t s_Tools = s_Taken.size();

            if (s_Fill(pools.regular, kSubSlots, CratePickPool::Regular, s_Taken)) {
                s_Plan.kind = CratePlanKind::Tools;
                std::snprintf(s_Buf, sizeof(s_Buf),
                              "roll %.4f in tools [%g, %g): %zu Freelancer tools (%zu you don't own, %zu owned left "
                              "out), %zu topped up from the regular pool",
                              s_Plan.roll, s_GunEnd, s_ToolsEnd, s_Tools, pools.tools.size(), pools.ownedTools,
                              3 - s_Tools);
                s_Why(s_Buf);
                return s_Plan;
            }

            s_Plan.slots = {};
            s_Plan.from = {};
            s_Fell += "tools: too few regular items to top them up; ";
        }
        else {
            s_Fell += "tools: no Freelancer tool you don't own (" + std::to_string(pools.ownedTools) +
                      " owned left out), a regular crate instead; ";
        }
    }
    // A merces crate: the weapon pool's merces option with its own item.
    else if (s_Plan.roll < s_MercesEnd) {
        if (pools.oneItem) {
            s_Plan.kind = CratePlanKind::Merces;
            std::snprintf(s_Buf, sizeof(s_Buf), "roll %.4f in merces [%g, %g)", s_Plan.roll, s_ToolsEnd, s_MercesEnd);
            s_Why(s_Buf);
            return s_Plan;
        }

        s_Fell += "merces: no one-item crate now, a regular crate instead; ";
    }

    // A regular crate: the first item from the regular and large items together (a large one comes alone), the
    // others from the regular items.
    std::vector<size_t> s_First(pools.regular.begin(), pools.regular.end());
    s_First.insert(s_First.end(), pools.large.begin(), pools.large.end());
    std::sort(s_First.begin(), s_First.end(),
              [&](size_t a, size_t b) { return candidates[a].item < candidates[b].item; });
    const char* s_Band = s_Plan.roll >= s_MercesEnd ? ">=" : "<"; // "<": a kind fell through (s_Fell says why)

    if (!s_First.empty()) {
        const size_t s_Pick = s_First[IndexOf(PlanUniform(seed, scene, openIndex, kSubFirst), s_First.size())];

        if (candidates[s_Pick].large) {
            s_Plan.kind = CratePlanKind::Large;
            s_Plan.single = s_Pick;
            s_Plan.singleFrom = CratePickPool::Regular;
            std::snprintf(s_Buf, sizeof(s_Buf),
                          "roll %.4f %s %g: regular, the first draw a large item: alone (%zu regular, %zu large)",
                          s_Plan.roll, s_Band, s_MercesEnd, pools.regular.size(), pools.large.size());
            s_Why(s_Buf);
            return s_Plan;
        }

        s_Plan.slots[0] = s_Pick;
        s_Plan.from[0] = CratePickPool::Regular;
    }

    std::vector<size_t> s_Taken;

    if (s_Plan.slots[0])
        s_Taken.push_back(*s_Plan.slots[0]);

    if (!s_Plan.slots[0] || !s_Fill(pools.regular, kSubSlots, CratePickPool::Regular, s_Taken)) {
        s_Plan.slots = {};
        s_Plan.from = {};
        s_Plan.kind = CratePlanKind::Vanilla;
        std::snprintf(s_Buf, sizeof(s_Buf), "roll %.4f %s %g: three regular items wanted, only %zu to choose from",
                      s_Plan.roll, s_Band, s_MercesEnd, pools.regular.size());
        s_Why(s_Buf);
        return s_Plan;
    }

    s_Plan.kind = CratePlanKind::Items;
    std::snprintf(s_Buf, sizeof(s_Buf), "roll %.4f %s %g: regular, three of %zu regular items (%zu large)", s_Plan.roll,
                  s_Band, s_MercesEnd, pools.regular.size(), pools.large.size());
    s_Why(s_Buf);
    return s_Plan;
}

std::string DescribeCratePlan(const CratePlan& plan, std::span<const CrateCandidate> candidates) {
    std::string s_Out = CratePlanKindName(plan.kind);

    if (plan.single) {
        s_Out += ": alone: " + PickText(candidates, plan.single, plan.singleFrom);
    }
    else if (plan.kind == CratePlanKind::Merces) {
        s_Out += ": the crate's own merces option, alone";
    }
    else if (plan.kind != CratePlanKind::Vanilla) {
        for (size_t s = 0; s < plan.slots.size(); ++s)
            s_Out += (s == 0 ? ": slot " : ", slot ") + std::to_string(s + 1) + " " +
                     PickText(candidates, plan.slots[s], plan.from[s]);
    }

    if (!plan.why.empty())
        s_Out += " (" + plan.why + ")";

    return s_Out;
}

std::vector<CrateOptionAction> CrateSlotActions(std::span<const std::optional<size_t>> held, size_t crate,
                                                bool picked) {
    std::vector<CrateOptionAction> s_Actions(held.size(), CrateOptionAction::Exclude);
    const auto s_Own = std::find(held.begin(), held.end(), std::optional<size_t>(crate));

    if (picked)
        return s_Actions;

    // Its own item still lies in it: nothing is drawn (drawn again, its option would spawn it a second time).
    if (s_Own != held.end()) {
        for (size_t i = 0; i < held.size(); ++i) {
            if (held[i] && held[i] != std::optional<size_t>(crate))
                s_Actions[i] = CrateOptionAction::Rewrite;
        }

        return s_Actions;
    }

    const auto s_Out =
        std::find_if(held.begin(), held.end(), [](const std::optional<size_t>& h) { return h.has_value(); });

    if (s_Out == held.end()) {
        std::fill(s_Actions.begin(), s_Actions.end(), CrateOptionAction::Offer);
        return s_Actions;
    }

    for (size_t i = 0; i < held.size(); ++i) {
        if (held[i])
            s_Actions[i] = CrateOptionAction::Rewrite;
    }

    s_Actions[static_cast<size_t>(s_Out - held.begin())] = CrateOptionAction::Reuse;
    return s_Actions;
}

// --- an item's rest ---------------------------------------------------------------------------------------------

namespace {

constexpr double kPi = 3.14159265358979323846;

// The built-in rests (CrateRestFallback), as seen in game. The axis is the spawner's
// (the item comes out as the spawner times its placement attach). The crate's own option for an item overrides it.
struct TableRest {
    const char* item;
    LocalAxis axis;
    double lift; // m; < 0: not known
};

constexpr TableRest kTableRests[] {
    // Pouch Flash (attach adds no turn): on end on Fish's +Z spawner, upside down on a spawner turned -Y up: +Y.
    {"f04bd2e9-3f3a-42cd-9150-b7845e952d67", {1, false}, -1.0},
    // ICA Device RubberDuck Remote Explosive Classic: stands up on Icicle's +Z spawner; its attach is a
    // quarter turn of yaw (its X along the spawner's Y), so spawner +Y lays it on the item's own X, a flank if the
    // model faces along its Y (a guess: which side is not seen yet).
    {"0bc37bb7-dcd8-4348-a338-22fd8676a416", {1, false}, -1.0},
    // The other RubberDuck explosives: one model, the same attach (the Devil one's is the Classic's quarter turn of
    // yaw), and the Devil one sits upright on Bust_Colombi's spawner turned +Z.
    {"7e52d861-481c-4f7c-87d2-6211d90586bf", {1, false}, -1.0}, // Remote Explosive Devil RubberDuck
    {"4ca96340-ae60-427b-a011-9e296cd67fd9", {1, false}, -1.0}, // Proximity Explosive RubberDuck
    {"2bdf5016-e70b-4ac9-a3d5-f35b6743c09a", {1, false}, -1.0}, // Remote Concussion RubberDuck
    {"67b6eb96-89c6-43ce-ba7a-526b092a55f9", {1, false}, -1.0}, // Remote Concussion RubberDuck Collectors
    {"4e6da33e-acc0-4f76-8158-c7b2cb7bb9b4", {1, false}, -1.0}, // ICA Device Sports A RubberDuck Remote Knockdown
    // (Grenade) Emetic (attach adds no turn, its origin 6 cm above the spawner: it stands on its base): stands
    // upright on Icicle's +Z spawner; +Y lays it on its side.
    {"b988422a-02a6-499e-b796-302a782be3d1", {1, false}, -1.0},
    // (Not the Explosive Phone: its box from the game files, 14.4 x 7.5 x 1.0 cm along X, Y, Z, lays it +Z up:
    // CrateBoxRestAxis.)
    // Napoleon figure: its own option's spawner, +Y up, 4 cm above the floor.
    {"0a5bebc8-0148-4745-90b2-f54b3c71116c", {1, false}, 0.04},
    // (Tool) Coin Classic: its own option's spawner, -Z up, 3 cm above the floor.
    {"98bf7fc1-7857-4999-bc99-586c49f24017", {2, true}, 0.03},
};

// The twins (CrateTwinOf), by option template (helper_evergreen_selectableitem_<name>): tools/rpkg/crate_poses.py
// --twins. Each item hangs on the supplier's wall and has no crate option; its
// twin is a crate tool-slot item whose box is the item's turned by the turn (sides within 6 mm, centers within 1 cm;
// most match to the tenth of a millimeter). turn[3 * j + i] = (twin axis j) . (item axis i), both as the wall hangs
// them. They come before the table: the RubberDuck explosives and (Grenade) Emetic lie as the crate's Proximity
// Explosive RubberDuck and Concussion Grenade lie (the same boxes), not on the side the table guessed.
struct TableTwin {
    const char* item;
    const char* twin;
    AxisTurn turn;
};

constexpr TableTwin kCrateTwins[] {
    // electronickeyhacker: as disposablescrambler
    {"6dadf34c-00f2-43f9-b4e8-6763502aa2c8", "b970a355-4296-4acc-9ec9-584e69a79eed", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // electronickeyhackermkiii: as disposablescrambler
    {"8b114fce-586b-4b06-b446-75d0bb4a4cfb", "b970a355-4296-4acc-9ec9-584e69a79eed", {1, 0, 0, 0, 0, 1, 0, -1, 0}},
    // emeticgrenade: as concussiongrenade
    {"b988422a-02a6-499e-b796-302a782be3d1", "59e407df-c49b-4abe-a1be-0806b026e47e", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // icaproximityconcussiondevicemkiii: as icaproximitytaser
    {"8e77e2c5-caa6-45fe-8594-6a75c21569ec", "0209f0b7-f6de-45c2-a730-4802abe35a75", {0, -1, 0, 1, 0, 0, 0, 0, 1}},
    // icaremoteconcussiondevice: as icaproximitytaser
    {"8afed6c5-a730-4f47-b02c-1e4608f2ae81", "0209f0b7-f6de-45c2-a730-4802abe35a75", {0, 1, 0, -1, 0, 0, 0, 0, 1}},
    // icaremotetaser: as icaproximitytaser
    {"021ed731-eebc-400a-9658-8f6fc5af9da6", "0209f0b7-f6de-45c2-a730-4802abe35a75", {0, 1, 0, -1, 0, 0, 0, 0, 1}},
    // modernemeticsyringe: as modernlethalsyringe
    {"1c50d6e0-11c8-4cbc-be05-f51a8e5013be", "af9ad679-6a7c-4f8e-9700-ceb5e6887666", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // modernsedativesyringe: as modernlethalsyringe
    {"c45e59f4-d8e1-4c37-b079-8b74b1fe9b24", "af9ad679-6a7c-4f8e-9700-ceb5e6887666", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // proximitysemtexdemoblockmkiii: as remotesemtexdemoblockmkiii
    {"ba5c5c48-3d2e-4d4d-9dbe-f57b95200b1a", "7488229b-3fa8-4539-90ba-a7bf65798568", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // remoteconcussioncollectorsduck: as proximityexplosiveduck
    {"67b6eb96-89c6-43ce-ba7a-526b092a55f9", "4ca96340-ae60-427b-a011-9e296cd67fd9", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // remoteconcussionrubberduck: as proximityexplosiveduck
    {"2bdf5016-e70b-4ac9-a3d5-f35b6743c09a", "4ca96340-ae60-427b-a011-9e296cd67fd9", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // remoteexplosiveclassicrubberduck: as proximityexplosiveduck
    {"0bc37bb7-dcd8-4348-a338-22fd8676a416", "4ca96340-ae60-427b-a011-9e296cd67fd9", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // remoteexplosivedevilrubberduck: as proximityexplosiveduck
    {"7e52d861-481c-4f7c-87d2-6211d90586bf", "4ca96340-ae60-427b-a011-9e296cd67fd9", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // thepaleduck: as proximityexplosiveduck
    {"9aabe1cf-2a11-49d5-8baa-e8ed3ef22c3e", "4ca96340-ae60-427b-a011-9e296cd67fd9", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // theredlightflashgrenade: as concussiongrenade
    {"bc9bf630-8a8b-438c-9b63-3dd5e9245595", "59e407df-c49b-4abe-a1be-0806b026e47e", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
    // thetrickshotduck: as proximityexplosiveduck
    {"4e6da33e-acc0-4f76-8158-c7b2cb7bb9b4", "4ca96340-ae60-427b-a011-9e296cd67fd9", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
};

// Each item's box center from its spawner, in the spawner's axes (CrateItemBoxCenter): the Boundary of its option
// template (helper_evergreen_selectableitem_<item>, the box the game draws over it, sized and placed for its model),
// by repository id: tools/rpkg/crate_poses.py --boxes, 296 templates, every one the crate, the supplier or a
// mission's option helper may place. An item centered by its box lies where the slot's items sit; the spawners'
// middle lies a few cm nearer the player (the Large items slot's by 4.8 cm).
struct TableBox {
    const char* item;
    std::array<double, 3> center;
    std::array<double, 3> size; // along the spawner's axes
};

constexpr TableBox kCrateItemBoxes[] {
#include "CrateItemBoxes.inc"
};

// The model size of each item with no option template whose model the local packages hold (CrateItemLongestSide):
// tools/rpkg/crate_poses.py --sizes, the bounding boxes of the PRIMs its Runtime template's item art
// places. Only for whether an item fits a slot; never a box center or a rest.
struct TableSize {
    const char* item;
    std::array<double, 3> size; // along the model's axes
};

constexpr TableSize kCrateItemModelSizes[] {
#include "CrateItemSizes.inc"
};

// Axis i of a rotation (a row), as doubles.
std::array<double, 3> Row(const WorldAxes& axes, size_t i) {
    return {axes[3 * i], axes[3 * i + 1], axes[3 * i + 2]};
}

double Dot(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<int, 3> AxisVector(LocalAxis axis) {
    std::array<int, 3> s_V {0, 0, 0};
    s_V[axis.index % 3] = axis.negative ? -1 : 1;
    return s_V;
}

std::array<int, 3> Cross(const std::array<int, 3>& a, const std::array<int, 3>& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// The rotation about the unit axis n by 90 degrees (sin 1, cos 0) or 180 (sin 0, cos -1): Rodrigues' formula,
// R = cos I + sin [n]x + (1 - cos) n n^T.
AxisTurn TurnAbout(const std::array<int, 3>& n, int sine, int cosine) {
    const int s_Cross[9] {0, -n[2], n[1], n[2], 0, -n[0], -n[1], n[0], 0};
    AxisTurn s_T {};

    for (size_t r = 0; r < 3; ++r) {
        for (size_t c = 0; c < 3; ++c)
            s_T[3 * r + c] =
                static_cast<int8_t>((r == c ? cosine : 0) + sine * s_Cross[3 * r + c] + (1 - cosine) * n[r] * n[c]);
    }

    return s_T;
}

} // namespace

std::string LocalAxisText(LocalAxis axis) {
    constexpr char kNames[3] {'X', 'Y', 'Z'};
    return std::string(1, axis.negative ? '-' : '+') + kNames[axis.index % 3];
}

std::optional<LocalAxis> UpLocalAxis(const WorldAxes& axes, double maxTiltDegrees) {
    const double s_Limit = std::cos(maxTiltDegrees * kPi / 180.0);

    for (uint8_t i = 0; i < 3; ++i) {
        const std::array<double, 3> s_Axis = Row(axes, i);
        const double s_Length = std::sqrt(Dot(s_Axis, s_Axis));

        if (std::isnan(s_Length) || std::isinf(s_Length) || s_Length < 1e-4)
            return std::nullopt;

        const double s_Up = s_Axis[2] / s_Length;

        if (std::fabs(s_Up) >= s_Limit)
            return LocalAxis {i, s_Up < 0.0};
    }

    return std::nullopt;
}

std::optional<CrateRest> LearnCrateRest(std::span<const CrateRestSample> samples) {
    std::vector<LocalAxis> s_Seen; // in first-seen order
    std::vector<size_t> s_Counts;  // per s_Seen
    std::vector<std::optional<LocalAxis>> s_Axes(samples.size());
    size_t s_Usable = 0;
    size_t s_Slots = 0;

    for (size_t i = 0; i < samples.size(); ++i) {
        const CrateRestSample& s_Sample = samples[i];

        if (s_Sample.where != CratePlacement::ToolSlot)
            continue;

        ++s_Slots;

        if (!s_Sample.axes || s_Sample.attach == std::optional<bool>(false))
            continue;

        s_Axes[i] = UpLocalAxis(*s_Sample.axes);

        if (!s_Axes[i])
            continue;

        ++s_Usable;
        const auto s_At = std::find(s_Seen.begin(), s_Seen.end(), *s_Axes[i]);

        if (s_At == s_Seen.end()) {
            s_Seen.push_back(*s_Axes[i]);
            s_Counts.push_back(1);
        }
        else {
            ++s_Counts[static_cast<size_t>(s_At - s_Seen.begin())];
        }
    }

    if (s_Seen.empty())
        return std::nullopt;

    // The most named, the first seen on a tie.
    const size_t s_Best = static_cast<size_t>(std::max_element(s_Counts.begin(), s_Counts.end()) - s_Counts.begin());
    CrateRest s_Rest;
    s_Rest.axis = s_Seen[s_Best];
    s_Rest.source = CrateRestSource::Learned;
    s_Rest.agree = s_Counts[s_Best];
    s_Rest.usable = s_Usable;
    s_Rest.samples = s_Slots;
    s_Rest.others = samples.size() - s_Slots;
    std::vector<double> s_Lifts;

    for (size_t i = 0; i < samples.size(); ++i) {
        if (s_Axes[i] != std::optional<LocalAxis>(s_Rest.axis))
            continue;

        // The pose: the first agreeing one's whole rotation.
        if (!s_Rest.pose) {
            s_Rest.pose = samples[i].axes;
            s_Rest.poseSample = i;
            s_Rest.poseFrom = samples[i].source;
        }

        if (samples[i].lift && !std::isnan(*samples[i].lift))
            s_Lifts.push_back(*samples[i].lift);
    }

    if (!s_Lifts.empty()) {
        std::sort(s_Lifts.begin(), s_Lifts.end());
        const size_t n = s_Lifts.size();
        s_Rest.lift = n % 2 ? s_Lifts[n / 2] : (s_Lifts[n / 2 - 1] + s_Lifts[n / 2]) / 2.0;
    }

    return s_Rest;
}

CrateRest CrateRestFallback(const Guid& item) {
    // Neither its own option nor a twin's: centered in the slot by its own box from the game files (the slot's
    // middle lies nearer the player than the slot's items).
    CrateRest s_Rest;
    s_Rest.center = CrateItemBoxCenter(item);
    s_Rest.boxSize = CrateItemBoxSize(item);

    for (const TableRest& s_Row : kTableRests) {
        if (ParseGuid(s_Row.item) != std::optional<Guid>(item))
            continue;

        s_Rest.axis = s_Row.axis;
        s_Rest.source = CrateRestSource::Table;

        if (s_Row.lift >= 0.0)
            s_Rest.lift = s_Row.lift;

        return s_Rest;
    }

    // Not in the table: on its widest face, as its box shows it (the box's thin side is the axis the crate lays up).
    if (s_Rest.boxSize) {
        if (const std::optional<LocalAxis> s_Axis = CrateBoxRestAxis(*s_Rest.boxSize)) {
            s_Rest.axis = *s_Axis;
            s_Rest.source = CrateRestSource::Box;
        }
    }

    return s_Rest;
}

std::optional<LocalAxis> CrateBoxRestAxis(const std::array<double, 3>& size) {
    for (const double s_Side : size) {
        if (!std::isfinite(s_Side) || s_Side < 0.0)
            return std::nullopt;
    }

    std::array<uint8_t, 3> s_Order {0, 1, 2};
    std::stable_sort(s_Order.begin(), s_Order.end(), [&](uint8_t a, uint8_t b) { return size[a] < size[b]; });

    if (!(size[s_Order[1]] > 0.0) || size[s_Order[0]] > kCrateBoxThinRatio * size[s_Order[1]])
        return std::nullopt;

    return LocalAxis {s_Order[0], false};
}

std::optional<CrateTwin> CrateTwinOf(const Guid& item) {
    for (const TableTwin& s_Row : kCrateTwins) {
        if (ParseGuid(s_Row.item) != std::optional<Guid>(item))
            continue;

        const std::optional<Guid> s_Twin = ParseGuid(s_Row.twin);

        if (!s_Twin)
            return std::nullopt;

        return CrateTwin {*s_Twin, s_Row.turn};
    }

    return std::nullopt;
}

std::optional<std::array<double, 3>> CrateItemBoxCenter(const Guid& item) {
    for (const TableBox& s_Row : kCrateItemBoxes) {
        if (ParseGuid(s_Row.item) == std::optional<Guid>(item))
            return s_Row.center;
    }

    return std::nullopt;
}

std::optional<std::array<double, 3>> CrateItemBoxSize(const Guid& item) {
    for (const TableBox& s_Row : kCrateItemBoxes) {
        if (ParseGuid(s_Row.item) == std::optional<Guid>(item))
            return s_Row.size;
    }

    return std::nullopt;
}

std::optional<CrateItemLength> CrateItemLongestSide(const Guid& item) {
    if (const std::optional<std::array<double, 3>> s_Box = CrateItemBoxSize(item))
        return CrateItemLength {*std::max_element(s_Box->begin(), s_Box->end()), CrateLengthSource::Box};

    for (const TableSize& s_Row : kCrateItemModelSizes) {
        if (ParseGuid(s_Row.item) == std::optional<Guid>(item))
            return CrateItemLength {*std::max_element(s_Row.size.begin(), s_Row.size.end()), CrateLengthSource::Model};
    }

    return std::nullopt;
}

CrateRest TwinCrateRest(const CrateRest& twinRest, const std::array<int8_t, 9>& turn, std::string twinName) {
    CrateRest s_Rest = twinRest;
    s_Rest.source = CrateRestSource::Twin;
    s_Rest.twin = std::move(twinName);
    s_Rest.twinTurn = turn;
    // Never the twin's spot: the item is centered in its slot by its box (FitSpawner). A turned twin is another
    // model: its height is not the item's (Remote Taze and the Remote Concussion Device lie right at the floor).
    s_Rest.offset.reset();
    s_Rest.center.reset();

    if (turn != kNoTurn)
        s_Rest.lift.reset();

    // The twin's rest axis a in the item's axes: the item axis i with turn[a][i] != 0 (twin axis a = sum_i
    // turn[a][i] item axis i).
    const size_t a = twinRest.axis.index % 3;

    for (uint8_t i = 0; i < 3; ++i) {
        const int s_T = turn[3 * a + i];

        if (s_T != 0) {
            s_Rest.axis = LocalAxis {i, (s_T < 0) != twinRest.axis.negative};
            break;
        }
    }

    if (twinRest.pose) {
        s_Rest.pose = TurnAxes(*twinRest.pose, turn);

        if (const std::optional<LocalAxis> s_Up = UpLocalAxis(*s_Rest.pose))
            s_Rest.axis = *s_Up;
    }

    return s_Rest;
}

CrateRest CrateRestFor(const Guid& item, std::span<const CrateRestSample> samples,
                       std::span<const CrateRestSample> twinSamples, std::string twinName) {
    if (const std::optional<CrateRest> s_Learned = LearnCrateRest(samples))
        return *s_Learned;

    const size_t s_Slots = static_cast<size_t>(std::count_if(
        samples.begin(), samples.end(), [](const CrateRestSample& s) { return s.where == CratePlacement::ToolSlot; }));
    CrateRest s_Rest;
    const std::optional<CrateTwin> s_Twin = CrateTwinOf(item);
    const std::optional<CrateRest> s_TwinRest = s_Twin ? LearnCrateRest(twinSamples) : std::nullopt;

    if (s_TwinRest)
        s_Rest = TwinCrateRest(*s_TwinRest, s_Twin->turn, std::move(twinName));
    else
        s_Rest = CrateRestFallback(item);

    s_Rest.samples = s_Slots;
    s_Rest.others = samples.size() - s_Slots;
    return s_Rest;
}

namespace {

// "; centered by its box (center (+0.00, +0.00, +0.01) m from its spawner, from its option template)", "; centered by
// its geometry (center (...) m from its spawner, measured where it spawned earlier this load)" (UseMeasuredCenter), or
// "; no box in the game files (at the slot's middle; centered by its geometry once it spawns)".
std::string TemplateCenterText(const CrateRest& rest) {
    if (!rest.center)
        return "; no box in the game files (at the slot's middle; centered by its geometry once it spawns)";

    char s_Spot[160];
    std::snprintf(s_Spot, sizeof(s_Spot), "; centered by its %s (center (%+.2f, %+.2f, %+.2f) m from its spawner, %s)",
                  rest.centerMeasured ? "geometry" : "box", (*rest.center)[0], (*rest.center)[1], (*rest.center)[2],
                  rest.centerMeasured ? "measured where it spawned earlier this load" : "from its option template");
    return s_Spot;
}

} // namespace

std::string DescribeCrateRest(const CrateRest& rest) {
    std::string s_Out = LocalAxisText(rest.axis) + " up (";

    switch (rest.source) {
        case CrateRestSource::Learned:
            s_Out += "learned from the crate's own option for it: " + std::to_string(rest.agree) + " of " +
                     std::to_string(rest.usable) + " agree";
            if (rest.usable < rest.samples)
                s_Out += ", " + std::to_string(rest.samples - rest.usable) + " more unread or not upright";
            if (rest.pose)
                s_Out += "; pose and yaw of " + (rest.poseFrom.empty() ? std::string("that option") : rest.poseFrom);
            if (rest.offset) {
                char s_Spot[96];
                std::snprintf(s_Spot, sizeof(s_Spot), ", its spawner (%+.2f, %+.2f) m from its slot's middle",
                              (*rest.offset)[0], (*rest.offset)[1]);
                s_Out += s_Spot;
            }
            break;
        case CrateRestSource::Twin:
            s_Out += (rest.samples ? "its " + std::to_string(rest.samples) + " options in the crate were unreadable"
                                   : std::string("no option of its own in the crate")) +
                     "; laid as its twin " + (rest.twin.empty() ? std::string("(no name)") : rest.twin) +
                     " (the same box in the game files), learned from the crate's own option for the twin: " +
                     std::to_string(rest.agree) + " of " + std::to_string(rest.usable) + " agree";
            if (rest.pose)
                s_Out += "; pose and yaw of " + (rest.poseFrom.empty() ? std::string("that option") : rest.poseFrom) +
                         ", turned to the item's axes";
            if (rest.twinTurn && *rest.twinTurn != kNoTurn)
                s_Out += "; another model (turned): not the twin's height";
            if (rest.center) {
                char s_Spot[96];
                std::snprintf(s_Spot, sizeof(s_Spot),
                              "; centered by its box (center (%+.2f, %+.2f, %+.2f) m from its spawner)",
                              (*rest.center)[0], (*rest.center)[1], (*rest.center)[2]);
                s_Out += s_Spot;
            }
            break;
        case CrateRestSource::Table:
            s_Out += rest.samples ? "its " + std::to_string(rest.samples) +
                                        " options in the crate were unreadable; the built-in table"
                                  : std::string("no option of its own in the crate; the built-in table");
            s_Out += TemplateCenterText(rest);
            break;
        case CrateRestSource::Box: {
            s_Out += rest.samples ? "its " + std::to_string(rest.samples) +
                                        " options in the crate were unreadable and it is not in the built-in table"
                                  : std::string("no option of its own in the crate, not in the built-in table");
            s_Out += "; on its widest face by its box in the game files";

            if (rest.boxSize) {
                char s_Size[96];
                std::snprintf(s_Size, sizeof(s_Size), " (%.3f x %.3f x %.3f m along its spawner's axes)",
                              (*rest.boxSize)[0], (*rest.boxSize)[1], (*rest.boxSize)[2]);
                s_Out += s_Size;
            }

            s_Out += TemplateCenterText(rest);
            break;
        }
        case CrateRestSource::Measured:
            s_Out += "no option of its own in the crate; it stood upright at an earlier spawn this load: laid on its "
                     "widest face as its model measured";
            s_Out += TemplateCenterText(rest);
            break;
        case CrateRestSource::Default:
            s_Out += rest.samples ? "its " + std::to_string(rest.samples) +
                                        " options in the crate were unreadable and it is not in the built-in table; "
                                        "lying flat as vanilla"
                                  : std::string("no option of its own in the crate, not in the built-in table; lying "
                                                "flat as vanilla");
            s_Out += TemplateCenterText(rest);
            break;
    }

    if (rest.others)
        s_Out += "; " + std::to_string(rest.others) + " other placement" + (rest.others == 1 ? "" : "s") +
                 " not learned from";

    char s_Buf[64];

    if (rest.lift)
        std::snprintf(s_Buf, sizeof(s_Buf), "; %.2f m above the floor)", *rest.lift);
    else
        std::snprintf(s_Buf, sizeof(s_Buf), "; height not learned)");

    return s_Out + s_Buf;
}

std::optional<AxisTurn> TurnToPose(const WorldAxes& vanilla, const WorldAxes& pose) {
    // TurnAxes(vanilla, T) row i = sum_j T[j][i] vanilla_j = pose_i, so T[j][i] = vanilla_j . pose_i (both
    // orthonormal), each rounded to -1, 0 or 1.
    const double s_Limit = std::cos(kCrateRestMaxTiltDegrees * kPi / 180.0);
    std::array<std::array<double, 3>, 3> s_V {};
    std::array<std::array<double, 3>, 3> s_P {};

    for (size_t i = 0; i < 3; ++i) {
        s_V[i] = Row(vanilla, i);
        s_P[i] = Row(pose, i);

        for (std::array<double, 3>* s_Axis : {&s_V[i], &s_P[i]}) {
            const double s_Length = std::sqrt(Dot(*s_Axis, *s_Axis));

            if (std::isnan(s_Length) || std::isinf(s_Length) || s_Length < 1e-4)
                return std::nullopt;

            for (double& c : *s_Axis)
                c /= s_Length;
        }
    }

    AxisTurn s_T {};
    std::array<int, 3> s_RowHits {};
    std::array<int, 3> s_ColumnHits {};

    for (size_t j = 0; j < 3; ++j) {
        for (size_t i = 0; i < 3; ++i) {
            const double s_Cos = Dot(s_V[j], s_P[i]);

            if (std::fabs(s_Cos) >= s_Limit) {
                s_T[3 * j + i] = static_cast<int8_t>(s_Cos < 0.0 ? -1 : 1);
                ++s_RowHits[j];
                ++s_ColumnHits[i];
            }
        }
    }

    for (size_t k = 0; k < 3; ++k) {
        if (s_RowHits[k] != 1 || s_ColumnHits[k] != 1)
            return std::nullopt;
    }

    // A turn, never a mirror (a mirrored pose, or a mirrored spawner, is no turn away).
    const int s_Det = s_T[0] * (s_T[4] * s_T[8] - s_T[5] * s_T[7]) - s_T[1] * (s_T[3] * s_T[8] - s_T[5] * s_T[6]) +
                      s_T[2] * (s_T[3] * s_T[7] - s_T[4] * s_T[6]);

    if (s_Det != 1)
        return std::nullopt;

    return s_T;
}

AxisTurn TurnRestToUp(LocalAxis rest, LocalAxis up) {
    const std::array<int, 3> s_A = AxisVector(rest);
    const std::array<int, 3> s_U = AxisVector(up);

    if (s_A == s_U)
        return kNoTurn;

    if (rest.index == up.index) {
        // Opposite: half a turn about an axis across them (local Z, or X for a rest on Z).
        const std::array<int, 3> s_About =
            rest.index == 2 ? std::array<int, 3> {1, 0, 0} : std::array<int, 3> {0, 0, 1};
        return TurnAbout(s_About, 0, -1);
    }

    // A quarter turn about rest x up takes rest to up.
    return TurnAbout(Cross(s_A, s_U), 1, 0);
}

AxisTurn InverseTurn(const AxisTurn& turn) {
    AxisTurn s_T {};

    for (size_t r = 0; r < 3; ++r) {
        for (size_t c = 0; c < 3; ++c)
            s_T[3 * r + c] = turn[3 * c + r];
    }

    return s_T;
}

WorldAxes TurnAxes(const WorldAxes& axes, const AxisTurn& turn) {
    WorldAxes s_Out {};

    for (size_t i = 0; i < 3; ++i) {
        for (size_t k = 0; k < 3; ++k) {
            double s_Sum = 0.0;

            for (size_t j = 0; j < 3; ++j)
                s_Sum += turn[3 * j + i] * static_cast<double>(axes[3 * j + k]);

            s_Out[3 * i + k] = static_cast<float>(s_Sum);
        }
    }

    return s_Out;
}

namespace {

// The world move of a slide along the vanilla spawner's axes.
std::array<double, 3> SlideMove(const WorldAxes& vanilla, const std::array<double, 3>& slide) {
    std::array<double, 3> s_Move {};

    for (size_t i = 0; i < 3; ++i) {
        for (size_t k = 0; k < 3; ++k)
            s_Move[k] += slide[i] * vanilla[3 * i + k];
    }

    return s_Move;
}

} // namespace

SpatialTransform ApplySpawnerTurn(const SpatialTransform& vanilla, const SpawnerTurn& change) {
    SpatialTransform s_Out = vanilla;
    s_Out.axes = TurnAxes(vanilla.axes, change.turn);
    const std::array<double, 3> s_Move = SlideMove(vanilla.axes, change.slide);

    for (size_t k = 0; k < 3; ++k)
        s_Out.at[k] = static_cast<float>(vanilla.at[k] + s_Move[k] + (k == 2 ? change.lift : 0.0));

    return s_Out;
}

SpatialTransform UndoSpawnerTurn(const SpatialTransform& now, const SpawnerTurn& change) {
    SpatialTransform s_Out = now;
    s_Out.axes = TurnAxes(now.axes, InverseTurn(change.turn));
    const std::array<double, 3> s_Move = SlideMove(s_Out.axes, change.slide);

    for (size_t k = 0; k < 3; ++k)
        s_Out.at[k] = static_cast<float>(now.at[k] - s_Move[k] - (k == 2 ? change.lift : 0.0));

    return s_Out;
}

SpatialTransform LocalTransform(const SpatialTransform& parent, const SpatialTransform& world) {
    SpatialTransform s_Out;
    const std::array<double, 3> s_Offset {world.at[0] - static_cast<double>(parent.at[0]),
                                          world.at[1] - static_cast<double>(parent.at[1]),
                                          world.at[2] - static_cast<double>(parent.at[2])};

    for (size_t k = 0; k < 3; ++k) {
        const std::array<double, 3> s_P = Row(parent.axes, k);
        const double s_Square = Dot(s_P, s_P);
        const double s_Scale = s_Square > 1e-8 ? 1.0 / s_Square : 0.0;

        for (size_t i = 0; i < 3; ++i)
            s_Out.axes[3 * i + k] = static_cast<float>(Dot(Row(world.axes, i), s_P) * s_Scale);

        s_Out.at[k] = static_cast<float>(Dot(s_Offset, s_P) * s_Scale);
    }

    return s_Out;
}

SpatialTransform WorldTransform(const SpatialTransform& parent, const SpatialTransform& local) {
    SpatialTransform s_Out;

    for (size_t k = 0; k < 3; ++k) {
        double s_At = parent.at[k];

        for (size_t j = 0; j < 3; ++j) {
            s_At += static_cast<double>(local.at[j]) * parent.axes[3 * j + k];

            double s_Axis = 0.0;

            for (size_t m = 0; m < 3; ++m)
                s_Axis += static_cast<double>(local.axes[3 * j + m]) * parent.axes[3 * m + k];

            s_Out.axes[3 * j + k] = static_cast<float>(s_Axis);
        }

        s_Out.at[k] = static_cast<float>(s_At);
    }

    return s_Out;
}

bool SameTransform(const SpatialTransform& a, const SpatialTransform& b) {
    for (size_t i = 0; i < 9; ++i) {
        if (!(std::fabs(a.axes[i] - b.axes[i]) <= 0.02f))
            return false;
    }

    for (size_t i = 0; i < 3; ++i) {
        if (!(std::fabs(a.at[i] - b.at[i]) <= 0.01f))
            return false;
    }

    return true;
}

std::optional<double> CrateFloorHeight(std::span<const SpatialTransform> spawners) {
    std::vector<double> s_Heights;

    for (const SpatialTransform& s_Spawner : spawners) {
        if (UpLocalAxis(s_Spawner.axes, kCrateFlatMaxTiltDegrees) == std::optional<LocalAxis>(kLocalUp) &&
            !std::isnan(s_Spawner.at[2]))
            s_Heights.push_back(s_Spawner.at[2]);
    }

    if (s_Heights.empty())
        return std::nullopt;

    std::sort(s_Heights.begin(), s_Heights.end());
    const size_t n = s_Heights.size();
    return n % 2 ? s_Heights[n / 2] : (s_Heights[n / 2 - 1] + s_Heights[n / 2]) / 2.0;
}

std::optional<WorldPoint> CrateSlotMiddle(std::span<const SpatialTransform> spawners) {
    WorldPoint s_Sum {};
    size_t s_Count = 0;

    for (const SpatialTransform& s_Spawner : spawners) {
        if (std::isnan(s_Spawner.at[0]) || std::isnan(s_Spawner.at[1]) || std::isnan(s_Spawner.at[2]))
            continue;

        for (size_t k = 0; k < 3; ++k)
            s_Sum[k] += s_Spawner.at[k];

        ++s_Count;
    }

    if (!s_Count)
        return std::nullopt;

    for (double& c : s_Sum)
        c /= static_cast<double>(s_Count);

    return s_Sum;
}

std::optional<WorldPoint> CrateSlotBoxMiddle(std::span<const SpatialTransform> boxes,
                                             std::optional<WorldPoint> spawnerMiddle) {
    std::optional<WorldPoint> s_Middle = CrateSlotMiddle(boxes);

    if (!s_Middle)
        return std::nullopt;

    if (spawnerMiddle) {
        if (std::hypot((*s_Middle)[0] - (*spawnerMiddle)[0], (*s_Middle)[1] - (*spawnerMiddle)[1]) >
            kCrateBoxMiddleMaxMeters)
            return std::nullopt;

        (*s_Middle)[2] = (*spawnerMiddle)[2];
    }

    return s_Middle;
}

namespace {

// The box axis nearest world up (its row's z the largest), or none for a NaN or zero rotation.
std::optional<size_t> UpRow(const WorldAxes& axes) {
    std::optional<size_t> s_Best;
    double s_Up = 0.0;

    for (size_t k = 0; k < 3; ++k) {
        const std::array<double, 3> s_Row = Row(axes, k);
        const double s_Length = std::sqrt(Dot(s_Row, s_Row));

        if (std::isnan(s_Length) || std::isinf(s_Length) || s_Length < 1e-4)
            return std::nullopt;

        if (std::fabs(s_Row[2]) / s_Length > s_Up) {
            s_Up = std::fabs(s_Row[2]) / s_Length;
            s_Best = k;
        }
    }

    return s_Best;
}

double Median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const size_t n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

} // namespace

std::optional<CrateBoxSize> CommonCrateBoxSize(std::span<const CrateBox> boxes) {
    std::vector<double> s_Sides;
    std::vector<double> s_Heights;

    for (const CrateBox& s_Box : boxes) {
        const std::optional<size_t> s_Up = UpRow(s_Box.world.axes);

        if (!s_Up || std::any_of(s_Box.size.begin(), s_Box.size.end(), [](double v) { return !(v >= 0.0); }))
            continue;

        for (size_t k = 0; k < 3; ++k)
            (k == *s_Up ? s_Heights : s_Sides).push_back(s_Box.size[k]);
    }

    if (s_Heights.empty())
        return std::nullopt;

    return CrateBoxSize {Median(s_Sides), Median(s_Heights)};
}

std::array<double, 3> CrateBoxExtent(const WorldAxes& axes, const CrateBoxSize& size) {
    const size_t s_Up = UpRow(axes).value_or(2);
    std::array<double, 3> s_Out {};

    for (size_t k = 0; k < 3; ++k)
        s_Out[k] = k == s_Up ? size.height : size.side;

    return s_Out;
}

std::array<double, 3> PointInSpawner(const SpatialTransform& spawner, const WorldPoint& point) {
    const std::array<double, 3> s_Offset {point[0] - spawner.at[0], point[1] - spawner.at[1], point[2] - spawner.at[2]};
    std::array<double, 3> s_Out {};

    for (size_t i = 0; i < 3; ++i) {
        const std::array<double, 3> s_Axis = Row(spawner.axes, i);
        const double s_Square = Dot(s_Axis, s_Axis);
        s_Out[i] = s_Square > 1e-8 ? Dot(s_Offset, s_Axis) / s_Square : 0.0;
    }

    return s_Out;
}

WorldPoint PointFromSpawner(const SpatialTransform& spawner, const std::array<double, 3>& local) {
    WorldPoint s_Out {spawner.at[0], spawner.at[1], spawner.at[2]};

    for (size_t i = 0; i < 3; ++i) {
        for (size_t k = 0; k < 3; ++k)
            s_Out[k] += local[i] * spawner.axes[3 * i + k];
    }

    return s_Out;
}

std::array<double, 3> TurnPoint(const std::array<double, 3>& twinLocal, const std::array<int8_t, 9>& turn) {
    std::array<double, 3> s_Out {};

    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j)
            s_Out[i] += turn[3 * j + i] * twinLocal[j];
    }

    return s_Out;
}

WorldPoint CrateBoxCenter(const SpatialTransform& spawnerVanilla, const SpatialTransform& spawnerNow,
                          const WorldPoint& boxVanilla, const std::optional<std::array<double, 3>>& itemCenter) {
    if (itemCenter)
        return PointFromSpawner(spawnerNow, *itemCenter);

    if (SameTransform(spawnerVanilla, spawnerNow))
        return boxVanilla;

    return WorldPoint {spawnerNow.at[0], spawnerNow.at[1], spawnerNow.at[2]};
}

std::string DescribeCrateBoxSize(const CrateBoxSize& size) {
    char s_Buf[64];
    std::snprintf(s_Buf, sizeof(s_Buf), "%.2f x %.2f x %.2f m", size.side, size.side, size.height);
    return s_Buf;
}

CrateCenterCheck CheckItemCenter(const SpatialTransform& item, const ItemBounds& bounds,
                                 const SpatialTransform& spawner, const WorldPoint& target, double slack) {
    CrateCenterCheck s_Check;
    std::array<double, 3> s_Middle {};
    bool s_Sized = true;
    double s_Largest = 0.0;

    for (size_t i = 0; i < 3; ++i) {
        s_Check.size[i] = bounds.max[i] - bounds.min[i];
        s_Middle[i] = (bounds.min[i] + bounds.max[i]) / 2.0;
        s_Sized = s_Sized && std::isfinite(s_Check.size[i]) && std::isfinite(s_Middle[i]) && s_Check.size[i] >= 0.0 &&
                  s_Check.size[i] <= kCrateItemMaxSideMeters;
        s_Largest = std::max(s_Largest, s_Sized ? s_Check.size[i] : 0.0);
    }

    for (size_t i = 0; i < 9 && s_Sized; ++i)
        s_Sized = std::isfinite(item.axes[i]) && std::isfinite(spawner.axes[i]);

    for (size_t i = 0; i < 3 && s_Sized; ++i)
        s_Sized = std::isfinite(item.at[i]) && std::isfinite(spawner.at[i]) && std::isfinite(target[i]);

    if (!s_Sized || s_Largest <= 0.0)
        return s_Check;

    // Its middle is a point in its own axes: carried to the world as a spawner's local point is.
    s_Check.center = PointFromSpawner(item, s_Middle);
    s_Check.centerLocal = PointInSpawner(spawner, s_Check.center);
    s_Check.shift = {target[0] - s_Check.center[0], target[1] - s_Check.center[1], 0.0};
    s_Check.miss = std::hypot(s_Check.shift[0], s_Check.shift[1]);
    s_Check.verdict = s_Check.miss > kCrateCenterMaxMissMeters ? CrateCentering::TooFar
                      : s_Check.miss > slack                   ? CrateCentering::Moved
                                                               : CrateCentering::Centered;
    return s_Check;
}

std::string DescribeCenterCheck(const CrateCenterCheck& check) {
    if (check.verdict == CrateCentering::Unsized)
        return "its bounds do not read as an item's (left as it lies)";

    const char* s_Verdict = check.verdict == CrateCentering::Moved    ? "moved"
                            : check.verdict == CrateCentering::TooFar ? "too far to trust, left as it lies"
                                                                      : "centered already";
    char s_Buf[192];
    std::snprintf(s_Buf, sizeof(s_Buf),
                  "%.2f x %.2f x %.2f m, its middle (%+.2f, %+.2f, %+.2f) m from its spawner, %.2f m off along the "
                  "floor: %s",
                  check.size[0], check.size[1], check.size[2], check.centerLocal[0], check.centerLocal[1],
                  check.centerLocal[2], check.miss, s_Verdict);
    return s_Buf;
}

bool UseMeasuredCenter(CrateRest& rest, const std::array<double, 3>& center) {
    if (rest.source == CrateRestSource::Learned || rest.source == CrateRestSource::Twin)
        return false;

    rest.center = center;
    rest.centerMeasured = true;
    return true;
}

namespace {

// A model's sides and middle (its own axes), when its bounds read as a small item's (CheckItemCenter's test).
bool ModelSides(const ItemBounds& bounds, std::array<double, 3>& sides, std::array<double, 3>& middle) {
    double s_Largest = 0.0;

    for (size_t i = 0; i < 3; ++i) {
        sides[i] = bounds.max[i] - bounds.min[i];
        middle[i] = (bounds.min[i] + bounds.max[i]) / 2.0;

        if (!std::isfinite(sides[i]) || !std::isfinite(middle[i]) || sides[i] < 0.0 ||
            sides[i] > kCrateItemMaxSideMeters)
            return false;

        s_Largest = std::max(s_Largest, sides[i]);
    }

    return s_Largest > 0.0;
}

bool FiniteAxes(const WorldAxes& axes) {
    return std::all_of(axes.begin(), axes.end(), [](float v) { return std::isfinite(v); });
}

// The model's thinnest axis when the item does not lie on its widest face (see MeasuredRestAxis), with its sides and
// middle.
std::optional<uint8_t> StandingThinAxis(const SpatialTransform& item, const ItemBounds& bounds,
                                        std::array<double, 3>& sides, std::array<double, 3>& middle) {
    if (!ModelSides(bounds, sides, middle) || !FiniteAxes(item.axes))
        return std::nullopt;

    std::array<uint8_t, 3> s_Order {0, 1, 2};
    std::stable_sort(s_Order.begin(), s_Order.end(), [&](uint8_t a, uint8_t b) { return sides[a] < sides[b]; });
    const uint8_t s_Thin = s_Order[0];

    if (!(sides[s_Order[1]] > 0.0))
        return std::nullopt;

    // Lying on its widest face already: its thinnest axis points up (or down).
    const std::array<double, 3> s_ThinAxis = Row(item.axes, s_Thin);
    const double s_Length = std::sqrt(Dot(s_ThinAxis, s_ThinAxis));

    if (!(s_Length > 1e-4) || std::fabs(s_ThinAxis[2]) / s_Length >= std::cos(kCrateRestMaxTiltDegrees * kPi / 180.0))
        return std::nullopt;

    // Its height: each side along world up.
    double s_Height = 0.0;

    for (size_t i = 0; i < 3; ++i) {
        const std::array<double, 3> s_Axis = Row(item.axes, i);
        const double s_L = std::sqrt(Dot(s_Axis, s_Axis));

        if (!(s_L > 1e-4))
            return std::nullopt;

        s_Height += std::fabs(s_Axis[2] / s_L) * sides[i];
    }

    // Taller than its thinnest side by any real amount (a cube, a ball, a can on its side: it lies so already).
    if (s_Height <= kCrateLayRatio * sides[s_Thin] || s_Height <= sides[s_Thin] + kCrateLaySlackMeters)
        return std::nullopt;

    return s_Thin;
}

} // namespace

std::optional<LocalAxis> MeasuredRestAxis(const SpatialTransform& item, const ItemBounds& bounds,
                                          const SpatialTransform& spawner) {
    std::array<double, 3> s_Sides {};
    std::array<double, 3> s_Middle {};
    const std::optional<uint8_t> s_Thin = StandingThinAxis(item, bounds, s_Sides, s_Middle);

    if (!s_Thin || !FiniteAxes(spawner.axes))
        return std::nullopt;

    // The spawner's local axis along the model's thinnest one.
    std::array<double, 3> s_Dir = Row(item.axes, *s_Thin);
    const double s_Length = std::sqrt(Dot(s_Dir, s_Dir));

    for (double& v : s_Dir)
        v /= s_Length;

    const double s_Limit = std::cos(kCrateRestMaxTiltDegrees * kPi / 180.0);

    for (uint8_t j = 0; j < 3; ++j) {
        const std::array<double, 3> s_Axis = Row(spawner.axes, j);
        const double s_L = std::sqrt(Dot(s_Axis, s_Axis));

        if (!(s_L > 1e-4))
            return std::nullopt;

        const double s_Cos = Dot(s_Dir, s_Axis) / s_L;

        if (std::fabs(s_Cos) >= s_Limit)
            return LocalAxis {j, s_Cos < 0.0};
    }

    return std::nullopt;
}

std::optional<SpatialTransform> LayItemFlat(const SpatialTransform& item, const ItemBounds& bounds) {
    std::array<double, 3> s_Sides {};
    std::array<double, 3> s_Middle {};
    const std::optional<uint8_t> s_Thin = StandingThinAxis(item, bounds, s_Sides, s_Middle);
    const std::optional<LocalAxis> s_Up = s_Thin ? UpLocalAxis(item.axes) : std::nullopt;

    if (!s_Thin || !s_Up || s_Up->index == *s_Thin)
        return std::nullopt;

    // Its bottom now: its middle less half its height.
    const WorldPoint s_Was = PointFromSpawner(item, s_Middle);
    double s_HeightWas = 0.0;

    for (size_t i = 0; i < 3; ++i)
        s_HeightWas += std::fabs(item.axes[3 * i + 2]) * s_Sides[i];

    // A quarter turn in its own frame: the thinnest axis where its up axis was.
    SpatialTransform s_Flat;
    s_Flat.axes = TurnAxes(item.axes, TurnRestToUp(LocalAxis {*s_Thin, false}, *s_Up));
    s_Flat.at = item.at;
    double s_HeightNow = 0.0;

    for (size_t i = 0; i < 3; ++i)
        s_HeightNow += std::fabs(s_Flat.axes[3 * i + 2]) * s_Sides[i];

    // Its middle back over where it was along the floor, its bottom at the same height.
    const WorldPoint s_Now = PointFromSpawner(s_Flat, s_Middle);
    const double s_Want[3] {s_Was[0], s_Was[1], s_Was[2] - s_HeightWas / 2.0 + s_HeightNow / 2.0};

    for (size_t k = 0; k < 3; ++k)
        s_Flat.at[k] = static_cast<float>(item.at[k] + (s_Want[k] - s_Now[k]));

    return s_Flat;
}

bool UseMeasuredRest(CrateRest& rest, LocalAxis axis) {
    if (rest.source == CrateRestSource::Learned || rest.source == CrateRestSource::Twin ||
        rest.source == CrateRestSource::Measured)
        return false;

    rest.axis = axis;
    rest.source = CrateRestSource::Measured;
    rest.lift.reset();
    rest.center.reset();
    rest.centerMeasured = false;
    rest.pose.reset();
    rest.offset.reset();
    return true;
}

namespace {

// To the millimeter (the same change comes out at every open: the vanilla transform is read back each time), and
// a few millimeters are left out.
double Millimeters(double meters) {
    const double s_Rounded = std::round(meters * 1000.0) / 1000.0;
    return std::fabs(s_Rounded) < kCrateMinSlideMeters ? 0.0 : s_Rounded;
}

} // namespace

SpawnerFit FitSpawner(const SpatialTransform& vanilla, std::optional<double> floor, const CrateRest& rest,
                      std::optional<WorldPoint> slotMiddle, std::optional<WorldPoint> slotBoxMiddle) {
    SpawnerFit s_Fit;
    s_Fit.up = UpLocalAxis(vanilla.axes);
    const std::optional<AxisTurn> s_ToPose = rest.pose ? TurnToPose(vanilla.axes, *rest.pose) : std::nullopt;
    bool s_Height = true; // the height goes to the rest's

    if (s_ToPose) {
        // The pose of the crate's own option for the item: its rest axis and its yaw.
        s_Fit.posed = true;
        s_Fit.change.turn = *s_ToPose;
    }
    else {
        if (!s_Fit.up)
            return s_Fit;

        // Its up axis is the rest axis: the rotation and height are right for the item (the option's own item lies
        // on the same axis).
        if (*s_Fit.up == rest.axis)
            s_Height = false;
        else
            s_Fit.change.turn = TurnRestToUp(rest.axis, *s_Fit.up);
    }

    if (floor && s_Height) {
        const double s_Target = rest.lift ? *rest.lift : rest.axis == kLocalUp ? 0.0 : kCrateSideLiftMeters;
        s_Fit.change.lift = Millimeters(s_Target - (vanilla.at[2] - *floor));
    }

    // Where in the slot: the item's own spot (its own option's offset from its slot's middle) with its pose, else
    // its box centered on the slot's box middle (a twin's: the spawners' middle lies 3-4 cm nearer the player than
    // where the slot's items sit), else the slot's middle (the option's spot is tuned for the
    // option's own item). Along the floor only, kept along the vanilla spawner's axes.
    if (slotMiddle) {
        s_Fit.ownSpot = s_Fit.posed && rest.offset.has_value();
        WorldPoint s_Target {(*slotMiddle)[0], (*slotMiddle)[1], 0.0};

        if (s_Fit.ownSpot) {
            s_Target[0] += (*rest.offset)[0];
            s_Target[1] += (*rest.offset)[1];
        }
        else if (rest.center && slotBoxMiddle &&
                 std::all_of(rest.center->begin(), rest.center->end(), [](double c) { return std::fabs(c) < 1.0; })) {
            // The box center from the spawner as turned: sum_i center_i (new axis i).
            const WorldAxes s_Axes = TurnAxes(vanilla.axes, s_Fit.change.turn);
            s_Fit.centered = true;
            s_Target = {(*slotBoxMiddle)[0], (*slotBoxMiddle)[1], 0.0};

            for (size_t i = 0; i < 3; ++i) {
                for (size_t k = 0; k < 2; ++k)
                    s_Target[k] -= (*rest.center)[i] * s_Axes[3 * i + k];
            }
        }

        const std::array<double, 3> s_Move {s_Target[0] - vanilla.at[0], s_Target[1] - vanilla.at[1], 0.0};

        if (std::hypot(s_Move[0], s_Move[1]) >= kCrateMinSlideMeters) {
            for (size_t i = 0; i < 3; ++i)
                s_Fit.change.slide[i] = std::round(Dot(Row(vanilla.axes, i), s_Move) * 1000.0) / 1000.0;
        }
    }

    s_Fit.fits = s_Fit.change == SpawnerTurn {};
    return s_Fit;
}

CrateFit OptionFit(std::span<const CrateFit> spawners) {
    for (const CrateFit s_Fit : {CrateFit::Unfit, CrateFit::Turned, CrateFit::Fits}) {
        if (std::find(spawners.begin(), spawners.end(), s_Fit) != spawners.end())
            return s_Fit;
    }

    return CrateFit::Unread;
}

CrateFitPick KeepUnfitOut(std::span<CrateOptionAction> actions, std::span<const CrateFit> fits) {
    CrateFitPick s_Pick;
    const auto s_Fit = [&](size_t i) { return i < fits.size() ? fits[i] : CrateFit::Unread; };
    size_t s_Offers = 0;
    size_t s_Unfit = 0;

    for (size_t i = 0; i < actions.size(); ++i) {
        const bool s_Drawn = actions[i] == CrateOptionAction::Offer || actions[i] == CrateOptionAction::Reuse;
        s_Pick.turned += s_Drawn && s_Fit(i) == CrateFit::Turned ? 1 : 0;

        if (actions[i] == CrateOptionAction::Reuse && s_Fit(i) == CrateFit::Unfit)
            ++s_Pick.unfitReused;

        if (actions[i] != CrateOptionAction::Offer)
            continue;

        ++s_Offers;
        s_Unfit += s_Fit(i) == CrateFit::Unfit ? 1 : 0;
        s_Pick.unread += s_Fit(i) == CrateFit::Unread ? 1 : 0;
    }

    if (s_Unfit == 0)
        return s_Pick;

    if (s_Unfit == s_Offers) {
        s_Pick.fallback = true;
        return s_Pick;
    }

    for (size_t i = 0; i < actions.size(); ++i) {
        if (actions[i] == CrateOptionAction::Offer && s_Fit(i) == CrateFit::Unfit) {
            actions[i] = CrateOptionAction::Exclude;
            ++s_Pick.keptOut;
        }
    }

    return s_Pick;
}

CrateBranchPick CrateItemsBranch(const CratePlan* plan, bool oneItemHeld, bool toolItemOut) {
    using S = CrateBranchSteer;
    using R = CrateBranchReason;

    if (!plan)
        return {S::Vanilla, R::NoPlan};

    if (plan->single)
        return {S::Weapon, R::OneItem};

    if (plan->kind == CratePlanKind::Merces)
        return {S::Weapon, R::Merces};

    // Three items: the weapon pool would make it the game's merces crate, which only the merces chance gives.
    if (plan->kind != CratePlanKind::Vanilla)
        return {S::Tool, R::ToolPlan};

    if (oneItemHeld)
        return {S::Tool, R::OneItemHeld};

    if (toolItemOut)
        return {S::Tool, R::ToolSlotItemOut};

    return {S::Vanilla, R::Other};
}

std::optional<size_t> CratePickupSource(std::span<const CrateHold> holds) {
    std::optional<size_t> s_Best;

    for (size_t i = 0; i < holds.size(); ++i) {
        if (!s_Best || holds[i].lastOpen > holds[*s_Best].lastOpen)
            s_Best = i;
    }

    return s_Best;
}

void DropOneItemCrates(CratePools& pools) {
    pools.large.clear();
    pools.oneItem = false;
}

} // namespace fr
