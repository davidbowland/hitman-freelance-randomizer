#include "Loadout.h"

#include "ClientReqs.h"
#include "ConcealableMace.h"
#include "JsonScan.h"
#include "LootMerces.h" // JsonStringField
#include "Merces.h"     // PrestigeObjectiveName
#include "Rng.h"
#include "Scene.h"

#include <algorithm>
#include <iterator>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <set>
#include <utility>

namespace fr {

namespace {

const std::vector<std::string> kFirearmKinds {"pistol", "smg", "assaultrifle", "shotgun", "sniperrifle", "sniper"};

// Non-lethal "explosives" (flash, emetic, concussion and knockdown, sedative,
// EMP, audio, smoke, taser).
const std::vector<std::string> kNonLethalBlast {"flash", "emetic", "concussi", "knockdown", "sedative", "emp ",
                                                "emp_",  " emp",   "audio",    "smoke",     "taze",     "tase"};

// Rarity minimums (Evergreen_Rarity; every objective compares with >=, so an
// epic or legendary item meets a "rare" rule and a legendary one an "epic" rule).
constexpr int32_t kRare = 2;
constexpr int32_t kEpic = 3;
constexpr int32_t kLegendary = 4;

bool Contains(const std::vector<std::string>& list, std::string_view value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// The name keywords look at the names only, never ItemType: that is the
// game's animation class, shared across items (eCC_BaseballBat is the Mace's,
// the Croquet Mallet's, the Police Baton's and the Fish's too; eCC_Katana the
// Sabre's; eCC_Knife the scissors' and shurikens'), so "baseballbat" matched
// the Mace for prestige 125.
std::string NameText(const LoadoutItem& item) {
    return ToLower(item.name);
}

bool ContainsAny(std::string_view text, const std::vector<std::string>& parts) {
    return std::any_of(parts.begin(), parts.end(),
                       [&](const std::string& part) { return text.find(part) != std::string_view::npos; });
}

bool HasKind(const LoadoutItem& item, std::string_view kind) {
    return item.icon == kind || Contains(item.traits, kind) || Contains(item.perks, kind);
}

ItemNeed Need(std::string label, std::vector<std::string> kinds, std::vector<std::string> nameAny = {},
              std::vector<std::string> nameNone = {}, int32_t minRarity = 0, std::optional<bool> silenced = {}) {
    ItemNeed need;
    need.label = std::move(label);
    need.kinds = std::move(kinds);
    need.nameAny = std::move(nameAny);
    need.nameNone = std::move(nameNone);
    need.minRarity = minRarity;
    need.silenced = silenced;
    return need;
}

ItemNeed Prefer(ItemNeed need, std::vector<ItemNeed> prefer) {
    need.prefer = std::move(prefer);
    return need;
}

ItemNeed Cheapest(ItemNeed need) {
    need.cheapest = true;
    return need;
}

// The safehouse pickups (kSpawnedItems) may meet it.
ItemNeed Spawnable(ItemNeed need) {
    need.spawned = true;
    return need;
}

ItemNeed Ordered(ItemNeed need, NeedOrder order) {
    need.order = order;
    return need;
}

// Every firearm need is a kill with it (Unsilenced Pistol Kill, Eliminate
// Guards - SMG, ...), which a dart gun can't do (ItemNeed::kill).
ItemNeed Firearm(std::string label, std::string kind, int32_t minRarity = 0, std::optional<bool> silenced = {}) {
    std::vector<std::string> kinds {kind};
    if (kind == "sniperrifle")
        kinds.push_back("sniper");
    ItemNeed need = Need(std::move(label), std::move(kinds), {}, {}, minRarity, silenced);
    need.kill = true;
    return need;
}

ItemNeed AnyFirearm(std::string label, int32_t minRarity = 0, std::optional<bool> silenced = {}) {
    ItemNeed need = Need(std::move(label), kFirearmKinds, {}, {}, minRarity, silenced);
    need.kill = true;
    return need;
}

ItemNeed LethalExplosive() {
    return Need("a lethal explosive", {"explosive", "explosives"}, {}, kNonLethalBlast);
}

ItemNeed LethalMelee(std::string label, int32_t minRarity = 0) {
    return Need(std::move(label), {"melee_lethal"}, {}, {}, minRarity);
}

// The game's melee kill counts a fiberwire kill too (26 Melee Kill, 102 Epic
// Melee Kill).
ItemNeed MeleeKill(std::string label, int32_t minRarity = 0) {
    return Need(std::move(label), {"melee_lethal", "fiberwire"}, {}, {}, minRarity);
}

ItemNeed NonLethalMelee(std::string label, int32_t minRarity = 0) {
    return Need(std::move(label), {"melee_nonlethal"}, {}, {}, minRarity);
}

ItemNeed Knife() {
    return Need("a knife", {"melee_lethal"}, {"knife", "dagger", "kukri", "cleaver"});
}

ItemNeed Axe() {
    return Need("an axe", {"melee_lethal"}, {"axe", "hatchet"});
}

// A remotely triggered lethal explosive (C4, rubber duck, phone, breaching
// charge, ...): not a proximity mine, grenade or a non-lethal blast.
ItemNeed RemoteExplosive() {
    std::vector<std::string> none = kNonLethalBlast;
    for (const char* part : {"proximity", "tripwire", "grenade", "molotov"})
        none.emplace_back(part);
    return Need("a remote explosive", {"explosive_device"}, {}, std::move(none));
}

ItemNeed Poison(std::string label, std::vector<std::string> nameAny = {}) {
    return Need(std::move(label), {"poison"}, std::move(nameAny));
}

// A poison that prefers a syringe of its kind (trait syringe_poison: "Lethal
// Poison Syringe Antique", "Emetic Poison Syringe", "Sedative Syringe", ...)
// over pills and vials (consumable_poison).
ItemNeed PoisonPreferSyringe(std::string label, std::string syringe, std::vector<std::string> nameAny = {}) {
    return Prefer(Poison(std::move(label), std::move(nameAny)), {Need(std::move(syringe), {"syringe_poison"})});
}

ItemNeed ByName(std::string label, std::vector<std::string> nameAny, std::vector<std::string> nameNone = {}) {
    return Need(std::move(label), {}, std::move(nameAny), std::move(nameNone));
}

// The Freelancer oil canister ("Evergreen OilSpill Canister"): a trap to slip
// on, and 47's only way to start a fire for a burn accident (spilled, then
// shot or lit).
ItemNeed OilCanister(std::string label = "an oil canister") {
    return ByName(std::move(label), {"oil canister", "oilspill", "oil spill", "fuel canister"});
}

// A taser device for an electrocution: the ICA Remote / Proximity Taze and
// their Micro variants ("Remote Taze / ICA Device Modular Remote Taze", ...;
// tool icon, trait "none", so by name).
ItemNeed Taser() {
    return ByName("a taser device", {"taze", "taser", "tazer"});
}

// Blind Target / Blind Guard: a flash device (the Flash Phone, "(Flash) Robot
// Lil' Flashy", the Remote Flash), the flash grenade ("(Grenade) Flash S3";
// "Pouch Flash" is taken for one too) only when none of those fits.
ItemNeed FlashDevice() {
    return Prefer(ByName("a flash device", {"flash"}),
                  {ByName("a flash device that is not a grenade", {"flash"}, {"grenade", "pouch"})});
}

ItemNeed Spill() {
    return ByName("an oil or water canister",
                  {"oil canister", "water canister", "oilspill", "waterspill", "oil spill", "water spill"});
}

// Silent Assassin, No Firearms (prestige) and No Firearms (client requirement).
constexpr int32_t kPrestigeNoFirearms = 116;
constexpr int32_t kClientReqNoFirearms = 72;

std::string_view Trim(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

std::string_view Unquote(std::string_view text) {
    text = Trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    return text;
}

// "*_ClientReq01" .. but not "Hot_ClientReqsFailed".
bool IsClientReqKey(std::string_view key) {
    const size_t at = key.find("_ClientReq");
    if (at == std::string_view::npos)
        return false;
    const std::string_view rest = key.substr(at + 10);
    return !rest.empty() && std::all_of(rest.begin(), rest.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string JoinGuids(const std::vector<Guid>& ids) {
    std::string text;
    for (const Guid& id : ids) {
        if (!text.empty())
            text += ',';
        text += FormatGuid(id);
    }
    return text;
}

// The plan's random stream.
class Stream {
public:
    explicit Stream(uint64_t seed) : m_State(seed) {}

    double Next() { return UniformFromHash(SplitMix64(m_State)); }

private:
    uint64_t m_State;
};

} // namespace

bool IsLockpickId(const Guid& id) {
    static const Guid s_Mk3 = *ParseGuid(kLockpickMk3Id);
    static const Guid s_Collectors = *ParseGuid(kCollectorsLockpickId);
    return id == s_Mk3 || id == s_Collectors;
}

const char* LoadoutSourceName(LoadoutSource source) {
    switch (source) {
        case LoadoutSource::Persistent: return "gear wall (PersistentItems)";
        case LoadoutSource::Transient: return "Freelancer tools (TransientItems)";
        case LoadoutSource::Spawned: return "safehouse pickups (spawned, not owned)";
    }
    return "?";
}

int32_t SlotCost(const LoadoutItem& item) {
    if (IsLockpickId(item.id))
        return 1;
    if (item.source == LoadoutSource::Spawned) {
        for (const SpawnedItem& spawned : kSpawnedItems) {
            if (ParseGuid(spawned.id) == item.id)
                return spawned.cost;
        }
    }
    return std::max(0, item.capacity);
}

bool IsFirearmItem(const LoadoutItem& item) {
    return std::any_of(kFirearmKinds.begin(), kFirearmKinds.end(),
                       [&](const std::string& kind) { return HasKind(item, kind); });
}

bool IsHandsOnlyHolster(std::string_view holster) {
    return !holster.empty() && holster != "eHolsteringAllowed";
}

bool IsLargeIconGuess(std::string_view icon) {
    const std::string lower = ToLower(icon);
    return lower == "sniperrifle" || lower == "sniper" || lower == "assaultrifle" || lower == "shotgun";
}

bool IsBriefcaseItem(const LoadoutItem& item, bool useItemSize) {
    if (Contains(item.traits, "case"))
        return true;
    // HolsterAbility decides when the item has one (the Sawed Off Bartoli 12G is
    // ITEMSIZE_LARGE yet eHolsteringAllowed, and 47 holsters it in game).
    if (!item.holster.empty())
        return IsHandsOnlyHolster(item.holster);
    return useItemSize ? IsLargeItemSize(item.itemSize) : IsLargeIconGuess(item.icon);
}

bool Matches(const ItemNeed& need, const LoadoutItem& item) {
    if (!need.kinds.empty() && std::none_of(need.kinds.begin(), need.kinds.end(),
                                            [&](const std::string& kind) { return HasKind(item, kind); }))
        return false;

    const std::string name = NameText(item);

    if (!need.nameAny.empty() && !ContainsAny(name, need.nameAny))
        return false;

    if (ContainsAny(name, need.nameNone))
        return false;

    if (need.kill && item.dartGun)
        return false;

    if (item.rarity < need.minRarity)
        return false;

    if (need.silenced && (!IsFirearmItem(item) || item.silenced != *need.silenced))
        return false;

    return std::all_of(need.also.begin(), need.also.end(), [&](const ItemNeed& other) { return Matches(other, item); });
}

std::string_view PrestigeName(int32_t objective) {
    const char* name = PrestigeObjectiveName(objective);
    return name ? std::string_view(name) : std::string_view();
}

std::vector<ItemNeed> PrestigeNeeds(int32_t objective) {
    switch (objective) {
        case 102: return {MeleeKill("an epic or legendary lethal melee weapon or fiberwire", kEpic)};
        case 103: return {Firearm("an epic or legendary sniper rifle", "sniperrifle", kEpic)};
        case 104: return {Firearm("an epic or legendary shotgun", "shotgun", kEpic)};
        case 105: return {Firearm("an epic or legendary SMG", "smg", kEpic)};
        case 106: return {Firearm("an epic or legendary silenced pistol", "pistol", kEpic, true)};
        case 107: return {Firearm("an epic or legendary unsilenced pistol", "pistol", kEpic, false)};
        case 108: return {Firearm("an epic or legendary assault rifle", "assaultrifle", kEpic)};
        case 109: return {AnyFirearm("a legendary firearm", kLegendary)};
        case 110: return {ByName("an ice pick", {"icepick", "ice pick", "ice_pick"})};
        case 111: return {ByName("a katana or tanto", {"katana", "tanto"})};
        case 112: return {ByName("a shuriken", {"shuriken"})};
        case 113: return {Axe()};
        case 114: return {Knife()};
        // A shotgun's spread or a piercing round (pro.repo Perks "piercing")
        // hits two at once more easily; any firearm when neither is owned.
        case 121:
            return {
                Prefer(AnyFirearm("a firearm"), {Need("a shotgun or a piercing firearm", {"shotgun", "piercing"})})};
        case 123: return {LethalExplosive()};
        // 124 Shoot Target into Water is not in the game (PrestigeCards.h): no need.
        case 125:
            return {ByName("a baseball bat", {"baseball bat", "baseball_bat", "baseballbat"},
                           {"fish", "baton", "tonfa", "prize"})};
        case 131: return {Spill()};
        case 134: return {LethalExplosive()};
        default: return {};
    }
}

std::vector<ItemNeed> ClientReqNeeds(int32_t requirement) {
    switch (requirement) {
        case 8: return {PoisonPreferSyringe("a lethal poison", "a lethal poison syringe", {"lethal"})};
        case 9: return {LethalExplosive()};
        case 10:
            return {LethalMelee("a lethal throwable melee weapon")}; // kill method throw
        // 47 carries nothing else that electrocutes (car batteries and fuse
        // boxes are the level's).
        case 11: return {Taser()};
        case 12: return {Firearm("a sniper rifle", "sniperrifle")};
        case 13: return {Firearm("a shotgun", "shotgun")};
        case 14: return {Firearm("an SMG", "smg")};
        case 15: return {Firearm("a silenced pistol", "pistol", 0, true)};
        case 16: return {Firearm("an unsilenced pistol", "pistol", 0, false)};
        case 24: return {Need("a fiber wire", {"fiberwire"})};
        case 25: return {Firearm("an assault rifle", "assaultrifle")};
        case 26: return {MeleeKill("a lethal melee weapon or fiberwire")};
        case 27: return {OilCanister()};
        // A taser device can electrify the spilled water: helpful, not
        // required, so it comes after every objective's items.
        case 28:
            return {ByName("a water canister", {"water canister", "waterspill", "water spill"}),
                    Ordered(Taser(), NeedOrder::Helper)};
        case 29:
            return {OilCanister("an oil canister (to set alight)")}; // Burn Accident
        // A target pacified unnoticed with an item of rarity >= rare; the
        // requirement reads "a Rare, Epic or Legendary non-lethal Melee Item"
        // (a lethal one like the shuriken kills instead).
        case 37: return {NonLethalMelee("a rare, epic or legendary non-lethal melee item", kRare)};
        case 39: return {PoisonPreferSyringe("an emetic poison", "an emetic poison syringe", {"emetic"})};
        case 40: return {PoisonPreferSyringe("a sedative poison", "a sedative poison syringe", {"sedative"})};
        case 41: return {PoisonPreferSyringe("a poison", "a poison syringe")};
        case 42: return {PoisonPreferSyringe("a poison", "a poison syringe")};
        case 43: return {Need("a poison syringe", {"syringe_poison"})};
        case 44: return {Need("a concussive explosive", {"explosive", "explosives"}, {"concussi", "knockdown"})};
        case 45: return {RemoteExplosive()};
        case 51: return {ByName("a proximity explosive", {"proximity", "tripwire"}, kNonLethalBlast)};
        case 52: return {ByName("a fragmentation grenade", {"fragmentation", "frag grenade"}, kNonLethalBlast)};
        case 53: return {AnyFirearm("a silenced firearm", 0, true)};
        case 56: return {FlashDevice()};
        case 57: return {FlashDevice()};
        case 59: return {ByName("a dart gun", {"dart", "tranquilizer"})};
        case 62: return {Firearm("an assault rifle", "assaultrifle")};
        case 63: return {Firearm("a shotgun", "shotgun")};
        case 64: return {Firearm("an SMG", "smg")};
        case 65: return {Firearm("a sniper rifle", "sniperrifle")};
        case 66: return {Firearm("a silenced sniper rifle", "sniperrifle", 0, true)};
        case 67: return {Axe()};
        case 68: return {Knife()};
        // A grape bunch ("GrapeVine / Melee_GrapeVine"; not the "(S3)
        // Melee_GrapePicking_Knife"), the SPF 47 sunscreen ("(SUMMER)
        // Melee_Sun_Cream": no "spf" in its name) or a banana, whichever
        // takes the fewest slots. Oil and water spills make no one slip.
        // Grapes and bananas are safehouse pickups no one owns: spawned
        // (kSpawnedItems), at 1 and 2 slots.
        case 70:
            return {Spawnable(Cheapest(
                ByName("a grape bunch, SPF 47 or a banana",
                       {"grape", "spf", "sun_cream", "sun cream", "suncream", "sunscreen", "banana"}, {"knife"})))};
        // Not strictly needed (any noise works), but 47 has nothing to throw
        // without one: the least important need, placed after all others.
        case 71: return {Ordered(Need("a distraction item", {"distraction"}), NeedOrder::Last)};
        default: return {};
    }
}

std::optional<std::string> ClientReqPrefixForScene(std::string_view scene) {
    const std::string lower = ToLower(scene);
    const size_t slash = lower.find_last_of('/');
    const std::string file = slash == std::string::npos ? lower : lower.substr(slash + 1);

    for (const std::string_view marker : {std::string_view("_mild_"), std::string_view("_hot_")}) {
        const size_t at = file.rfind(marker);
        if (at == std::string::npos)
            continue;

        std::string code = file.substr(at + marker.size());
        const size_t dot = code.find('.');
        if (dot != std::string::npos)
            code.resize(dot);

        for (const ClientReqLocation& location : ClientReqLocations()) {
            if (ToLower(location.code) != code)
                continue;
            if (marker == "_hot_")
                return std::string("Hot");
            return "Mild" + std::string(location.code);
        }
    }

    return std::nullopt;
}

std::string FillExclusion(const LoadoutItem& item) {
    if (!item.known)
        return "not in pro.repo";
    if (SlotCost(item) <= 0)
        return "free (cost 0: no weight)";

    const std::string icon = ToLower(item.icon);
    const std::string kind = ToLower(item.kind);

    if (icon == "questitem" || kind.find("quest") != std::string::npos)
        return "quest item";
    if (icon.find("payout") != std::string::npos || icon.find("money") != std::string::npos ||
        ToLower(item.name).find("merces") != std::string::npos)
        return "money";
    if (kind.find("ammo") != std::string::npos)
        return "ammo";
    if (icon == "key" || icon == "keycard")
        return "a key";

    return {};
}

std::string MissingPartner(const LoadoutItem& item, std::span<const LoadoutItem* const> loadout) {
    const bool detonator = Contains(item.traits, "detonator_explosive_device") &&
                           ToLower(item.name).find("detonator") != std::string::npos;

    if (!detonator)
        return {};

    const bool partner = std::any_of(loadout.begin(), loadout.end(), [](const LoadoutItem* other) {
        return Contains(other->traits, "explosive_device");
    });

    return partner ? std::string() : std::string("an explosive device (detonators are useless alone)");
}

std::string LoadoutCategory(const LoadoutItem& item) {
    std::string icon = ToLower(Trim(item.icon));
    if (icon == "sniper")
        icon = "sniperrifle";
    return icon;
}

LoadoutPlan PlanLoadout(const LoadoutInput& input) {
    LoadoutPlan plan;
    plan.total = input.totalSlots;

    std::set<Guid> taken;
    std::vector<const LoadoutItem*> loadout;
    int32_t used = 0;
    int briefcases = 0;

    for (const LoadoutItem& item : input.carried) {
        taken.insert(item.id);
        loadout.push_back(&item);
        used += SlotCost(item);
        briefcases += item.briefcase ? 1 : 0;
    }

    plan.computedUsed = used;

    if (input.reportedUsed && *input.reportedUsed > used) {
        plan.notes.push_back("slots: the game reported " + std::to_string(*input.reportedUsed) +
                             " used, the packed items' costs add up to " + std::to_string(used) +
                             "; the larger is used");
        used = *input.reportedUsed;
    }

    plan.usedBefore = used;
    plan.briefcasesBefore = briefcases;

    // Owned candidates: one per id, not packed, in id order (the order the
    // lists come in never matters).
    std::vector<size_t> candidates;
    {
        std::set<Guid> seen;
        for (size_t i = 0; i < input.owned.size(); ++i) {
            const Guid& id = input.owned[i].id;
            if (taken.contains(id) || !seen.insert(id).second)
                continue;
            candidates.push_back(i);
        }
        std::sort(candidates.begin(), candidates.end(),
                  [&](size_t a, size_t b) { return input.owned[a].id < input.owned[b].id; });
    }

    Stream stream(DialHash(input.seed, input.scene, kLoadoutFeature, input.startCount));

    const auto left = [&]() { return input.totalSlots - used; };

    // Empty when it fits, else why not.
    const auto whyNot = [&](const LoadoutItem& item) -> std::string {
        if (item.briefcase && !input.briefcaseItems)
            return "needs a briefcase (random_loadout_holsterable_only is on)";
        if (SlotCost(item) > left())
            return "needs " + std::to_string(SlotCost(item)) + " slots, " + std::to_string(std::max(0, left())) +
                   " left";
        if (item.briefcase && briefcases >= 1)
            return "a briefcase item is already in the loadout";
        return {};
    };

    const auto take = [&](size_t index, LoadoutRule rule, std::string why) {
        const LoadoutItem& item = input.owned[index];
        taken.insert(item.id);
        loadout.push_back(&item);
        used += SlotCost(item);
        briefcases += item.briefcase ? 1 : 0;
        plan.picks.push_back({index, rule, std::move(why), SlotCost(item)});
    };

    const auto loadoutMatch = [&](const ItemNeed& need) -> const LoadoutItem* {
        for (const LoadoutItem* item : loadout) {
            if (Matches(need, *item))
                return item;
        }
        return nullptr;
    };

    const auto isPacked = [&](const LoadoutItem* item) {
        return std::any_of(input.carried.begin(), input.carried.end(),
                           [&](const LoadoutItem& carried) { return &carried == item; });
    };

    // A spawned item only for a need that allows it (ItemNeed::spawned).
    const auto offered = [](const ItemNeed& need, const LoadoutItem& item) {
        return item.source != LoadoutSource::Spawned || need.spawned;
    };

    // Silent Assassin, No Firearms: no rule picks a firearm (packed ones stay).
    const bool noFirearmsAtAll = input.prestige && *input.prestige == kPrestigeNoFirearms;

    // Rules 2 and 3: one need.
    const auto satisfy = [&](const ItemNeed& need, LoadoutRule rule, const std::string& what) {
        if (const LoadoutItem* met = loadoutMatch(need)) {
            plan.notes.push_back(what + ": " + need.label + ": met by " + met->name + " (" +
                                 (isPacked(met) ? "packed" : "picked") + ")");
            return;
        }

        std::vector<size_t> matching;
        std::vector<size_t> fitting;
        std::string firstWhy;
        size_t firearmOnly = 0;   // matching firearms under Silent Assassin, No Firearms
        size_t briefcaseOnly = 0; // matching briefcase items when none may be brought
        std::string firstBriefcase;

        for (const size_t index : candidates) {
            const LoadoutItem& item = input.owned[index];
            if (taken.contains(item.id) || !offered(need, item) || !Matches(need, item))
                continue;
            if (noFirearmsAtAll && IsFirearmItem(item)) {
                ++firearmOnly;
                continue;
            }
            if (item.briefcase && !input.briefcaseItems) {
                if (briefcaseOnly++ == 0)
                    firstBriefcase = item.name;
                continue;
            }
            matching.push_back(index);
            const std::string why = whyNot(item);
            if (why.empty())
                fitting.push_back(index);
            else if (firstWhy.empty())
                firstWhy = item.name + " " + why;
        }

        if (matching.empty() && briefcaseOnly == 0 && firearmOnly > 0) {
            plan.notes.push_back(what + ": " + need.label + ": needs a firearm; none brought (Silent Assassin, No " +
                                 "Firearms); " + std::to_string(firearmOnly) + " owned firearms match");
            return;
        }

        if (matching.empty() && briefcaseOnly > 0) {
            plan.notes.push_back(what + ": " + need.label + ": needs a briefcase item; none delivered (" +
                                 std::to_string(briefcaseOnly) + " owned items match, all briefcase items, such as " +
                                 firstBriefcase + "; random_loadout_holsterable_only is on)");
            return;
        }

        if (matching.empty()) {
            plan.notes.push_back(what + ": " + need.label + ": no owned item matches; nothing brought");
            return;
        }

        if (fitting.empty()) {
            plan.notes.push_back(what + ": " + need.label + ": " + std::to_string(matching.size()) +
                                 " owned items match but none fits (" + firstWhy + "); nothing brought");
            return;
        }

        // The best preferred items that fit, else every fitting match.
        const ItemNeed* preferred = nullptr;
        std::vector<size_t> pool;
        for (const ItemNeed& better : need.prefer) {
            for (const size_t index : fitting) {
                if (Matches(better, input.owned[index]))
                    pool.push_back(index);
            }
            if (!pool.empty()) {
                preferred = &better;
                break;
            }
        }
        if (!preferred)
            pool = fitting;
        size_t pricier = 0;
        if (need.cheapest) {
            int32_t least = SlotCost(input.owned[pool.front()]);
            for (const size_t index : pool)
                least = std::min(least, SlotCost(input.owned[index]));
            const auto kept = std::remove_if(pool.begin(), pool.end(),
                                             [&](size_t index) { return SlotCost(input.owned[index]) > least; });
            pricier = static_cast<size_t>(pool.end() - kept);
            pool.erase(kept, pool.end());
        }

        const size_t pick = pool[std::min(pool.size() - 1, static_cast<size_t>(stream.Next() * pool.size()))];
        std::string why = what + ": " + need.label;
        if (preferred)
            why += ": " + preferred->label + " preferred";
        why += " (1 of " + std::to_string(pool.size()) + " fitting";
        if (preferred && pool.size() < fitting.size())
            why += "; " + std::to_string(fitting.size() - pool.size()) + " other matches";
        else if (!preferred && !need.prefer.empty())
            why += "; no preferred item fits or is owned";
        if (pricier > 0)
            why += "; " + std::to_string(pricier) + " taking more slots skipped";
        if (firearmOnly > 0)
            why += "; " + std::to_string(firearmOnly) + " firearms skipped (Silent Assassin, No Firearms)";
        if (briefcaseOnly > 0)
            why += "; " + std::to_string(briefcaseOnly) + " briefcase items skipped";
        take(pick, rule, why + ")");
    };

    // Rule 1: the lockpick.
    if (!input.lockpickFirst) {
        plan.notes.push_back("lockpick: not planned first (random_loadout_lockpick_first = false); the random fill "
                             "may still draw one");
    }
    else if (const auto packed = std::find_if(loadout.begin(), loadout.end(),
                                              [](const LoadoutItem* item) { return IsLockpickId(item->id); });
             packed != loadout.end()) {
        plan.notes.push_back("lockpick: already packed: " + (*packed)->name);
    }
    else {
        std::optional<size_t> lockpick;
        const Guid mk3 = *ParseGuid(kLockpickMk3Id);
        const Guid collectors = *ParseGuid(kCollectorsLockpickId);

        for (const Guid& wanted : {mk3, collectors}) {
            for (const size_t index : candidates) {
                if (input.owned[index].id == wanted) {
                    lockpick = index;
                    break;
                }
            }
            if (lockpick)
                break;
        }

        if (!lockpick) {
            plan.notes.push_back("lockpick: owns neither Lockpick Mk III nor the Collector's Lockpick");
        }
        else if (const std::string why = whyNot(input.owned[*lockpick]); !why.empty()) {
            plan.notes.push_back("lockpick: " + input.owned[*lockpick].name + " " + why + "; not brought");
        }
        else {
            take(*lockpick, LoadoutRule::Lockpick,
                 input.owned[*lockpick].id == mk3 ? "lockpick: Lockpick Mk III (Freelancer tool)"
                                                  : "lockpick: the Collector's Lockpick (no Lockpick Mk III owned)");
        }
    }

    // Rules 2 and 3 collect their needs, then place them by ItemNeed::order:
    // every needed item first, then the helpful ones, then the least important.
    struct PendingNeed {
        ItemNeed need;
        LoadoutRule rule;
        std::string what;
    };
    std::vector<PendingNeed> pending;

    // Rule 2: the prestige objective.
    if (!input.objectiveItems) {
        plan.notes.push_back("objectives: no items planned for the prestige objective or client requirements "
                             "(random_loadout_objective_items = false)");
    }
    else if (input.prestige && *input.prestige != 0) {
        const std::string_view name = PrestigeName(*input.prestige);
        const std::string what = "prestige " + std::to_string(*input.prestige) + " " +
                                 (name.empty() ? std::string("(unknown)") : std::string(name));
        const std::vector<ItemNeed> needs = PrestigeNeeds(*input.prestige);

        if (needs.empty())
            plan.notes.push_back(what + ": needs no item");

        for (const ItemNeed& need : needs)
            pending.push_back({need, LoadoutRule::Prestige, what});
    }
    else {
        plan.notes.push_back("prestige: none chosen");
    }

    // Rule 3: the client requirements, the highest payout first (the amount
    // the player sees for it in this mission, LoadoutInput::clientReqPayouts),
    // so the slots go to the objective that pays most when not every need
    // fits; equal payouts keep the CPD's order, and an unknown payout comes
    // after every known one. Each tier below keeps this order. None with
    // LoadoutInput::objectiveItems off.
    std::set<int32_t> seenReqs;
    std::vector<int32_t> reqs;
    for (const int32_t req : input.clientReqs) {
        if (input.objectiveItems && req != 0 && seenReqs.insert(req).second)
            reqs.push_back(req);
    }

    const auto payoutOf = [&](int32_t req) -> std::optional<int32_t> {
        const auto found = input.clientReqPayouts.find(req);
        return found == input.clientReqPayouts.end() ? std::nullopt : std::optional<int32_t>(found->second);
    };
    std::stable_sort(reqs.begin(), reqs.end(), [&](int32_t a, int32_t b) {
        const std::optional<int32_t> payA = payoutOf(a);
        const std::optional<int32_t> payB = payoutOf(b);
        if (payA.has_value() != payB.has_value())
            return payA.has_value();
        return payA && *payA > *payB;
    });
    const auto payoutText = [&](int32_t req) {
        const std::optional<int32_t> pay = payoutOf(req);
        return pay ? "pays " + std::to_string(*pay) : std::string("payout unknown");
    };

    if (!reqs.empty()) {
        std::string order;
        for (const int32_t req : reqs)
            order += (order.empty() ? "" : ", ") + ClientReqLabel(req) + " (" + payoutText(req) + ")";
        plan.notes.push_back("client requirements, highest payout first: " + order);
    }

    for (const int32_t req : reqs) {
        const std::string what = "client requirement " + ClientReqLabel(req) + " (" + payoutText(req) + ")";
        const std::vector<ItemNeed> needs = ClientReqNeeds(req);

        if (needs.empty())
            plan.notes.push_back(what + ": needs no item");

        for (const ItemNeed& need : needs)
            pending.push_back({need, LoadoutRule::ClientReq, what});
    }

    // Two objectives one owned item can meet (a legendary shotgun for a
    // legendary firearm and a shotgun kill; an epic fiber wire for Melee Kill -
    // Epic and Fiberwire Kill): that item first, when one fits; both needs then
    // find it below.
    for (size_t i = 0; i < pending.size(); ++i) {
        for (size_t j = i + 1; j < pending.size(); ++j) {
            const ItemNeed& a = pending[i].need;
            const ItemNeed& b = pending[j].need;
            ItemNeed both = a;
            both.label = a.label + " that is also " + b.label;
            both.also.push_back(b);
            both.spawned = a.spawned && b.spawned;
            both.cheapest = a.cheapest || b.cheapest;
            if (loadoutMatch(a) || loadoutMatch(b) || std::none_of(candidates.begin(), candidates.end(), [&](size_t k) {
                    const LoadoutItem& item = input.owned[k];
                    return !taken.contains(item.id) && offered(both, item) && Matches(both, item) &&
                           !(noFirearmsAtAll && IsFirearmItem(item)) && whyNot(item).empty();
                }))
                continue;
            satisfy(both, pending[i].rule, pending[i].what + " and " + pending[j].what);
        }
    }

    for (const NeedOrder order : {NeedOrder::Required, NeedOrder::Helper, NeedOrder::Last}) {
        for (const PendingNeed& one : pending) {
            if (one.need.order == order)
                satisfy(one.need, one.rule, one.what);
        }
    }

    // Rule 4: random fill, weighted by slot cost; items of a category not yet
    // in the loadout (packed and picked items count) first, any category
    // only when none of those fits. Never a spawned item (not owned).
    size_t filled = 0;
    size_t duplicates = 0;
    if (!input.briefcaseItems)
        plan.notes.push_back("random fill: briefcase items are not drawn (random_loadout_holsterable_only is on)");

    // A no-firearms objective keeps the fill off firearms; with common
    // objective 72 alone a firearm another objective needs was still given
    // above, so the player can choose (prestige 116 gave none).
    const bool noFirearms = noFirearmsAtAll || std::find(input.clientReqs.begin(), input.clientReqs.end(),
                                                         kClientReqNoFirearms) != input.clientReqs.end();
    if (noFirearms)
        plan.notes.push_back("random fill: no firearms (a no-firearms objective)");

    for (;;) {
        std::set<std::string> present;
        for (const LoadoutItem* item : loadout) {
            if (std::string category = LoadoutCategory(*item); !category.empty())
                present.insert(std::move(category));
        }

        std::vector<size_t> pool;
        std::vector<size_t> fresh; // of a category not yet in the loadout
        int64_t totalWeight = 0;
        int64_t freshWeight = 0;

        for (const size_t index : candidates) {
            const LoadoutItem& item = input.owned[index];
            if (taken.contains(item.id) || item.source == LoadoutSource::Spawned ||
                (noFirearms && IsFirearmItem(item)) || !FillExclusion(item).empty() || !whyNot(item).empty() ||
                !MissingPartner(item, loadout).empty())
                continue;
            pool.push_back(index);
            totalWeight += SlotCost(item);
            if (const std::string category = LoadoutCategory(item); category.empty() || !present.contains(category)) {
                fresh.push_back(index);
                freshWeight += SlotCost(item);
            }
        }

        if (pool.empty() || totalWeight <= 0)
            break;

        const size_t fitCount = pool.size();
        const bool varied = !fresh.empty() && freshWeight > 0;
        if (varied) {
            pool = std::move(fresh);
            totalWeight = freshWeight;
        }

        double target = stream.Next() * static_cast<double>(totalWeight);
        size_t pick = pool.back();
        for (const size_t index : pool) {
            target -= SlotCost(input.owned[index]);
            if (target < 0) {
                pick = index;
                break;
            }
        }

        const int32_t cost = SlotCost(input.owned[pick]);
        const std::string category = LoadoutCategory(input.owned[pick]);
        std::string why = "random fill: weight " + std::to_string(cost) + " of " + std::to_string(totalWeight) + " (" +
                          std::to_string(fitCount) + " items fit";
        if (varied)
            why += ", " + std::to_string(pool.size()) + " of a category not yet in the loadout";
        else
            why += "; every one's category is already in the loadout, so " +
                   (category.empty() ? std::string("a duplicate category") : category) + " again";
        take(pick, LoadoutRule::Fill, why + ")");
        ++filled;
        duplicates += varied ? 0 : 1;
    }

    plan.notes.push_back("random fill: " + std::to_string(filled) + " items (" + std::to_string(duplicates) +
                         " of a category already in the loadout); " + std::to_string(std::max(0, left())) +
                         " slots left unused");
    plan.usedAfter = used;
    plan.briefcasesAfter = briefcases;
    return plan;
}

// --- LoadoutState ------------------------------------------------------------

bool LoadoutState::Empty() const {
    return lists.empty() && ints.empty() && !slotsTotal && !slotsUsed;
}

const std::vector<Guid>* LoadoutState::List(std::string_view key) const {
    const auto it = lists.find(std::string(key));
    return it == lists.end() ? nullptr : &it->second;
}

std::optional<int32_t> LoadoutState::Int(std::string_view key) const {
    const auto it = ints.find(std::string(key));
    return it == ints.end() ? std::nullopt : std::optional<int32_t>(it->second);
}

std::vector<std::string> JsonStringArray(std::string_view json) {
    std::vector<std::string> out;
    json = Trim(json);

    if (json.size() < 2 || json.front() != '[' || json.back() != ']')
        return out;

    size_t i = 1;
    while (i < json.size() - 1) {
        const char c = json[i];
        if (c == '"') {
            std::string value;
            ++i;
            while (i < json.size() - 1 && json[i] != '"') {
                if (json[i] == '\\' && i + 1 < json.size() - 1)
                    ++i;
                value += json[i++];
            }
            ++i; // the closing quote
            out.push_back(std::move(value));
        }
        else if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        }
        else {
            return {}; // not an array of strings
        }
    }

    return out;
}

CpdMerge MergeCpdSet(LoadoutState& state, std::string_view eventJson) {
    static constexpr std::array<std::string_view, 4> kParts {"Items", "OptinPayoutObjective", "CPD_CampaignStep",
                                                             "_ClientReq"};
    CpdMerge merge;

    for (const JsonMember& member : JsonMembersWithKeyContaining(eventJson, kParts)) {
        const std::string key(member.key);
        const std::string_view value = Trim(member.value);

        if (key.size() > 5 && key.ends_with("Items") && value.starts_with("[")) {
            std::vector<Guid> ids;
            for (const std::string& text : JsonStringArray(value)) {
                if (const auto id = ParseGuid(text))
                    ids.push_back(*id);
                else
                    ++merge.badIds;
            }
            merge.lists.push_back(key + " (" + std::to_string(ids.size()) + ")");
            state.lists[key] = std::move(ids);
            continue;
        }

        if (key == "OptinPayoutObjective" || key == "CPD_CampaignStep" || IsClientReqKey(key)) {
            if (const auto number = ParseCpdInt(Unquote(value))) {
                state.ints[key] = *number;
                merge.ints.push_back(key + " " + std::to_string(*number));
            }
        }
    }

    return merge;
}

std::optional<int32_t> GearSlotsValue(std::string_view eventJson) {
    static constexpr std::array<std::string_view, 1> kParts {"Value"};

    for (const JsonMember& member : JsonMembersWithKeyContaining(eventJson, kParts)) {
        if (member.key == "Value")
            return ParseCpdInt(Unquote(member.value));
    }

    return std::nullopt;
}

namespace {

constexpr std::string_view kStateHeader = "freelancerrandomizer loadout state 1";

} // namespace

std::string FormatLoadoutState(const LoadoutState& state, std::string_view section) {
    std::string text;
    const std::string prefix = std::string(section) + ".";

    for (const auto& [key, ids] : state.lists)
        text += prefix + "list." + key + "=" + JoinGuids(ids) + "\n";
    for (const auto& [key, value] : state.ints)
        text += prefix + "int." + key + "=" + std::to_string(value) + "\n";
    if (state.slotsTotal)
        text += prefix + "slots_total=" + std::to_string(*state.slotsTotal) + "\n";
    if (state.slotsUsed)
        text += prefix + "slots_used=" + std::to_string(*state.slotsUsed) + "\n";
    text += prefix + "updated=" + std::to_string(state.updatedUnix) + "\n";
    return text;
}

std::optional<LoadoutState> ParseLoadoutState(std::string_view text, std::string_view section) {
    if (text.find(kStateHeader) == std::string_view::npos)
        return std::nullopt;

    LoadoutState state;
    bool any = false;
    const std::string prefix = std::string(section) + ".";

    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = Trim(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);

        if (!line.starts_with(prefix))
            continue;

        const size_t eq = line.find('=');
        if (eq == std::string_view::npos)
            continue;

        const std::string_view key = line.substr(prefix.size(), eq - prefix.size());
        const std::string_view value = line.substr(eq + 1);

        if (key.starts_with("list.")) {
            std::vector<Guid> ids;
            size_t at = 0;
            while (at < value.size()) {
                const size_t comma = value.find(',', at);
                const std::string_view part =
                    value.substr(at, comma == std::string_view::npos ? value.npos : comma - at);
                if (const auto id = ParseGuid(Trim(part)))
                    ids.push_back(*id);
                if (comma == std::string_view::npos)
                    break;
                at = comma + 1;
            }
            state.lists[std::string(key.substr(5))] = std::move(ids);
            any = true;
        }
        else if (key.starts_with("int.")) {
            if (const auto number = ParseCpdInt(value)) {
                state.ints[std::string(key.substr(4))] = *number;
                any = true;
            }
        }
        else if (key == "slots_total" || key == "slots_used") {
            if (const auto number = ParseCpdInt(value)) {
                (key == "slots_total" ? state.slotsTotal : state.slotsUsed) = *number;
                any = true;
            }
        }
        else if (key == "updated") {
            int64_t unix = 0;
            const auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), unix);
            if (error == std::errc() && ptr == value.data() + value.size())
                state.updatedUnix = unix;
        }
    }

    if (!any)
        return std::nullopt;

    return state;
}

std::string_view LoadoutStateHeader() {
    return kStateHeader;
}

const char* CreateItemTypeName(int32_t type) {
    switch (type) {
        case 0: return "support";
        case 1: return "debug";
        case 2: return "contract";
        case 3: return "carried";
        case 4: return "container";
        default: return "unknown";
    }
}

LoadoutItemTypeChoice ChooseLoadoutItemType(std::optional<int32_t> packedType) {
    if (packedType)
        return {*packedType, "the type the game used for a packed item in this mission"};
    return {kCreateItemTypeContract,
            "no packed item was seen being created; contract, as the SDK's Assets mod adds items to 47's inventory"};
}

bool NeedsDelivery(bool heroHasIt, bool requested) {
    return !heroHasIt && !requested;
}

BriefcaseVerdict JudgeBriefcase(const BriefcaseProgress& progress, double settleSeconds, double timeoutSeconds) {
    // The owner, when read: 47 before any place means loose (once placed, 47
    // may be reported as the owner of what his briefcase holds, so it does not
    // decide then).
    if (progress.ownerIsHero && *progress.ownerIsHero && !progress.placeSent)
        return BriefcaseVerdict::Loose;
    if (progress.ownerIsBriefcase && *progress.ownerIsBriefcase && progress.placeSent)
        return BriefcaseVerdict::InBriefcase;
    // The game's own path places the item in the briefcase's storage entity.
    if (progress.ownerIsStorage && *progress.ownerIsStorage && progress.briefcaseSpawned && progress.containerSet)
        return BriefcaseVerdict::InBriefcase;

    // Picked up by 47 before the briefcase took it: loose.
    if (progress.itemPickedUp && !progress.itemPickedUpAfterPlace)
        return BriefcaseVerdict::Loose;

    if (progress.placeSent && progress.briefcaseSpawned && progress.secondsSincePlace >= settleSeconds) {
        // A pickup of the item right after the place, with no owner read to
        // tell otherwise, is taken as loose (never leave a loose item).
        if (progress.itemPickedUpAfterPlace && !(progress.ownerIsBriefcase && *progress.ownerIsBriefcase))
            return BriefcaseVerdict::Loose;
        return BriefcaseVerdict::InBriefcase;
    }

    if (progress.seconds >= timeoutSeconds)
        return BriefcaseVerdict::Failed;

    return BriefcaseVerdict::Pending;
}

const char* BriefcaseVerdictName(BriefcaseVerdict verdict) {
    switch (verdict) {
        case BriefcaseVerdict::Pending: return "pending";
        case BriefcaseVerdict::InBriefcase: return "IN THE BRIEFCASE";
        case BriefcaseVerdict::Loose: return "LOOSE (not in a briefcase)";
        case BriefcaseVerdict::Failed: return "FAILED (not placed in a briefcase in time)";
    }
    return "?";
}

bool IsHeroStateMaskSane(uint64_t mask) {
    return (mask >> kHeroStateCount) == 0;
}

std::string HeroStateText(uint64_t mask) {
    // EHeroGameState, ZHMModSDK Enums.h, in order.
    static constexpr std::array<const char*, kHeroStateCount> kNames {
        "eHGS_Reserved_QueryALL",
        "eHGS_Reserved_QueryAny",
        "eHGS_InstinctModeActive",
        "eHGS_IsHardToSee",
        "eHGS_Sneaking",
        "eHGS_InSequence",
        "eHGS_InCloseCombat",
        "eHGS_ItemHandlingActive",
        "eHGS_CamSwitched",
        "eHGS_SniperMode",
        "eHGS_OTSMode",
        "eHGS_DualWielding",
        "eHGS_PreciseAim",
        "eHGS_ActivatingAltFire",
        "eHGS_FiredLastFrFocusBurnRate",
        "eHGS_FiredLastFrFocusGainValue",
        "eHGS_BurningFocus",
        "eHGS_InsideCloset",
        "eHGS_Peeking",
        "eHGS_FocusedInteraction",
        "eHGS_HoldsUtility",
        "eHGS_FacingRight",
        "eHGS_ThrowAiming",
        "eHGS_PlaceItemAiming",
        "eHGS_Reloading",
        "eHGS_FiberWirePrimedAndReady",
        "eHGS_OperatingDoorVRHandPush",
        "eHGS_MvAgility",
        "eHGS_MvCover",
        "eHGS_MvDragging",
        "eHGS_MvDrainPipe",
        "eHGS_MvLedgeWalk",
        "eHGS_MvLedgeHang",
        "eHGS_MvLocomotion",
        "eHGS_MvCloseCombat",
        "eHGS_MvSurrendering",
        "eHGS_MvFriskComplying",
        "eHGS_MvShowItem",
        "eHGS_MvThrow",
        "eHGS_MvFiberWireKill",
        "eHGS_MvAlign",
        "eHGS_MvTakedown",
        "eHGS_MvSilentTakedown",
        "eHGS_MvSubAction",
        "eHGS_MvNewFullBody",
        "eHGS_MvDisguiseSafeZone",
        "eHGS_MvPlaceItem",
    };

    std::string text;
    for (int bit = 0; bit < 64; ++bit) {
        if (!(mask & HeroStateBit(bit)))
            continue;
        if (!text.empty())
            text += ", ";
        text += bit < kHeroStateCount ? std::string(kNames[static_cast<size_t>(bit)]) : "bit " + std::to_string(bit);
    }
    return text.empty() ? std::string("none") : text;
}

std::string MovementTypeName(int32_t type) {
    // EBaseMovementType, ZHMModSDK Enums.h, from eMovementDead (0).
    static constexpr std::array<const char*, 39> kNames {
        "eMovementDead",
        "eMovementNewFullBody",
        "eMovementAgility",
        "eMovementAlign",
        "eMovementCover",
        "eMovementDrainPipe",
        "eMovementLadder",
        "eMovementPullVictimFromWindow",
        "eMovementFiberWireKill",
        "eMovementDumpBody",
        "eMovementThrowBodyOverRail",
        "eMovementDumpBodyOverLedge",
        "eMovementOperateCPDoor",
        "eMovementDisguiseSafeZone",
        "eMovementHideInCloset",
        "eMovementTakeDown",
        "eMovementCloseCombat",
        "eMovementRecoveryFinisher",
        "eMovementContextAction",
        "eMovementSubaction",
        "eMovementGrabVictim",
        "eMovementPushVictimThroughWindowAndRail",
        "eMovementContextKill",
        "eMovementKickVictimOverLedge",
        "eMovementDragBody",
        "eMovementTakeClothes",
        "eMovementCoupDeGrace",
        "eMovementThrow",
        "eMovementPlace",
        "eMovementSurrender",
        "eMovementFrisk",
        "eMovementShowItem",
        "eMovementPeek",
        "eMovementFocusedInteraction",
        "eMovementSilentTakedown",
        "eMovementSnapNeck",
        "eMovementLocomotion",
        "eMovementLast",
        "eMovementPickupItem",
    };

    if (type == -1)
        return "eMovementNone";
    if (type >= 0 && static_cast<size_t>(type) < kNames.size())
        return kNames[static_cast<size_t>(type)];
    return "type " + std::to_string(type);
}

BlendHoldVerdict JudgeBlendHold(const std::optional<uint64_t>& mask, double clearSeconds, double unreadSeconds,
                                double clearNeeded, double unreadLimit) {
    if (!mask || !IsHeroStateMaskSane(*mask))
        return unreadSeconds >= unreadLimit ? BlendHoldVerdict::Unknown : BlendHoldVerdict::Unread;

    if (*mask & kBlendHoldStates)
        return BlendHoldVerdict::InBlend;

    return clearSeconds >= clearNeeded ? BlendHoldVerdict::Clear : BlendHoldVerdict::Settling;
}

bool BlendHoldGives(BlendHoldVerdict verdict) {
    return verdict == BlendHoldVerdict::Clear || verdict == BlendHoldVerdict::Unknown;
}

const char* BlendHoldVerdictName(BlendHoldVerdict verdict) {
    switch (verdict) {
        case BlendHoldVerdict::InBlend: return "47 is in a blend spot or an act";
        case BlendHoldVerdict::Settling: return "47 is just out of a blend spot or an act";
        case BlendHoldVerdict::Clear: return "47 is not in a blend spot or an act";
        case BlendHoldVerdict::Unread: return "47's state could not be read (waiting)";
        case BlendHoldVerdict::Unknown: return "47's state could not be read";
    }
    return "?";
}

std::string BriefcaseProgressText(const BriefcaseProgress& progress) {
    const auto yes = [](bool value) { return value ? "yes" : "no"; };
    const auto maybe = [](const std::optional<bool>& value) { return value ? (*value ? "yes" : "no") : "unknown"; };
    std::string text;
    text += std::string("item spawned ") + yes(progress.itemSpawned);
    text += std::string(", SetItem sent ") + yes(progress.setItemSent);
    text += std::string(", pocket failed ") + yes(progress.pocketFailed);
    text += std::string(", briefcase spawned ") + yes(progress.briefcaseSpawned);
    text += std::string(", container set ") + yes(progress.containerSet);
    text += std::string(", place sent ") + yes(progress.placeSent);
    text += std::string(", briefcase picked up ") + yes(progress.briefcasePickedUp);
    text += std::string(", item picked up loose ") +
            (progress.itemPickedUp ? (progress.itemPickedUpAfterPlace ? "after the place" : "YES") : "no");
    text += std::string(", owner 47 ") + maybe(progress.ownerIsHero);
    text += std::string(", owner the briefcase ") + maybe(progress.ownerIsBriefcase);
    text += std::string(", owner a storage ") + maybe(progress.ownerIsStorage);
    char seconds[64];
    std::snprintf(seconds, sizeof(seconds), "; %.1f s", progress.seconds);
    text += seconds;
    return text;
}

bool IsPocketDeliveryItem(const LoadoutItem& item) {
    return !item.briefcase && item.holster == "eHolsteringAllowed" && IsLargeItemSize(item.itemSize);
}

std::optional<HeldWeapon> ParseHoldingIllegalWeapon(std::string_view eventJson) {
    constexpr std::string_view key = "\"IsHoldingIllegalWeapon\"";
    const size_t at = eventJson.find(key);

    if (at == std::string_view::npos)
        return std::nullopt;

    size_t pos = at + key.size();
    while (pos < eventJson.size() && (eventJson[pos] == ' ' || eventJson[pos] == ':'))
        ++pos;

    HeldWeapon held;
    const std::string_view rest = eventJson.substr(pos);

    if (rest.starts_with("true"))
        held.holding = true;
    else if (!rest.starts_with("false"))
        return std::nullopt;

    // WeaponEquipped (only when holding) carries the event's only RepositoryId.
    if (held.holding) {
        if (const auto text = JsonStringField(eventJson, "RepositoryId"))
            held.id = ParseGuid(*text);
    }

    return held;
}

PocketVerdict JudgePocket(const PocketProgress& progress, double settleSeconds, double timeoutSeconds) {
    if (progress.inHands) {
        if (!progress.retrySent)
            return PocketVerdict::Retry;
        return progress.secondsSinceRetry >= settleSeconds ? PocketVerdict::InHands : PocketVerdict::Pending;
    }

    const bool held = (progress.ownerIsHero && *progress.ownerIsHero) || progress.itemPickedUp || progress.everInHands;

    if (progress.pocketFailed && !held)
        return PocketVerdict::Refused;

    if (held && progress.secondsSinceHeld >= settleSeconds)
        return PocketVerdict::Pocketed;

    if (!held && progress.seconds >= timeoutSeconds)
        return PocketVerdict::Failed;

    return PocketVerdict::Pending;
}

const char* PocketVerdictName(PocketVerdict verdict) {
    switch (verdict) {
        case PocketVerdict::Pending: return "pending";
        case PocketVerdict::Pocketed: return "POCKETED";
        case PocketVerdict::Retry: return "IN 47'S HANDS (pocket call sent once more)";
        case PocketVerdict::InHands: return "WRONG: IN 47'S HANDS";
        case PocketVerdict::Refused: return "REFUSED (the game would not pocket it)";
        case PocketVerdict::Failed: return "FAILED (47 did not get it in time)";
    }
    return "?";
}

std::string PocketProgressText(const PocketProgress& progress) {
    const auto yes = [](bool value) { return value ? "yes" : "no"; };
    std::string text;
    text += std::string("SetItem sent ") + yes(progress.setItemSent);
    text += std::string(", pocket failed ") + yes(progress.pocketFailed);
    text += std::string(", picked up ") + yes(progress.itemPickedUp);
    text += std::string(", owner 47 ") + (progress.ownerIsHero ? (*progress.ownerIsHero ? "yes" : "no") : "unknown");
    text += std::string(", in 47's hands ") + (progress.inHands ? "YES" : progress.everInHands ? "no (it was)" : "no");
    text += std::string(", pocket call sent again ") + yes(progress.retrySent);
    char seconds[64];
    std::snprintf(seconds, sizeof(seconds), "; %.1f s", progress.seconds);
    text += seconds;
    return text;
}

SaveVerdict JudgeSave(std::optional<bool> inInventory, std::optional<bool> inSource, bool removedFromSource,
                      std::optional<bool> heroHolds) {
    if (inSource && *inSource && inInventory && *inInventory)
        return SaveVerdict::Duplicated;

    if (inSource && *inSource)
        return removedFromSource ? SaveVerdict::RemovalLost : SaveVerdict::StaysOwned;

    if (inInventory && *inInventory)
        return SaveVerdict::Carried;

    if (inInventory && !*inInventory) {
        if (heroHolds && *heroHolds)
            return SaveVerdict::MissingFromSave;
        if (!removedFromSource && inSource && !*inSource)
            return SaveVerdict::Vanished;
        return removedFromSource ? SaveVerdict::Lost : SaveVerdict::NotDelivered;
    }

    return SaveVerdict::Partial;
}

const char* SaveVerdictText(SaveVerdict verdict) {
    switch (verdict) {
        case SaveVerdict::Carried: return "OK: carried out: saved in the loadout (InventoryItems), gone from its list";
        case SaveVerdict::Lost:
            return "OK if the mission failed or 47 lost it: neither carried nor owned (WRONG on a success with 47 "
                   "still holding it)";
        case SaveVerdict::StaysOwned:
            return "OK: never removed, still in its list (not carried out; a gear wall item is back on the wall)";
        case SaveVerdict::NotDelivered: return "OK: not delivered, not removed";
        case SaveVerdict::Duplicated: return "DUPLICATED: carried and still owned";
        case SaveVerdict::MissingFromSave: return "WRONG: 47 holds it but it is not saved as carried";
        case SaveVerdict::RemovalLost:
            return "CHECK: removed in the mission, back in its list and not carried (the game returned it, or the "
                   "removal did not reach the save)";
        case SaveVerdict::Vanished:
            return "WRONG: never removed, yet gone from its list and not carried (a lent gear wall item is put back "
                   "on the wall at the next mission: \"loadout wall loans\" lines)";
        case SaveVerdict::Partial: return "this CpdSet lacks InventoryItems";
    }
    return "?";
}

} // namespace fr
