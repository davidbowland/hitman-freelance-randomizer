#include "ClientReqs.h"

#include "Merces.h" // PrestigeObjectiveName
#include "Rng.h"
#include "Scene.h" // ToLower
#include "TextNoCase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <map>
#include <utility>

namespace fr {

namespace {

// The safehouse's BonusReq_IDnn_<Name> entities (snug/mission_vanilla.brick);
// ID59 has two entities. Ascending. The names are the game's own,
// matched by the entity names (in the comments where they differ). 41 is not
// on the wiki's lists; its name follows the others'.
constexpr std::array<std::pair<int32_t, std::string_view>, 61> kNames {{
    {1, "Hide Target Bodies"}, // Hide_Bodies
    {2, "Kill Only Targets"},  // Targets_Only
    {3, "Suit Only"},          // No_Disguise
    {4, "No Bodies Found"},
    {5, "Do not Get Spotted"}, // Spotted
    {6, "Accident Kill"},
    {7, "Neck Breaker"}, // Unarmed_Kill
    {8, "Poison Kill"},
    {9, "Explosive Kill"},      // Explosive_Kill_Generic
    {10, "Deadly Throw"},       // Deadly_Throw_Kill
    {11, "Electrocution Kill"}, // Electric_Kill
    {12, "Sniper Rifle Kill"},  // Sniper_Kill
    {13, "Shotgun Kill"},
    {14, "Sub Machine Gun Kill"}, // SMG_Kill
    {15, "Silenced Pistol Kill"},
    {16, "Unsilenced Pistol Kill"},
    {18, "Doors Stay Locked"},
    {19, "Take No Damage"},            // DontTakeDamage
    {20, "Perfect Shooter"},           // NoMissedShots
    {21, "Headshot Kills Only"},       // Headshots_Only
    {22, "Do not Blow Your Disguise"}, // Disguise_Blown
    {23, "No Combat"},
    {24, "Fiberwire Kill"},
    {25, "Assault Rifle Kill"},
    {26, "Melee Weapon Kill"},          // Melee_Kill
    {27, "Trap Kill - Oil Canister"},   // Oil_Trap_kill
    {28, "Trap Kill - Water Canister"}, // Water_Trap_Kill
    {29, "Burning Kill - Accident"},    // Accident_Burn_Kill
    {30, "Drowning Kill"},              // Drown_Kill
    {31, "Push Kill"},
    {33, "Destroy Cameras"},
    {34, "Destroy Surveillance Recorder"},       // Destroy_Recorder
    {35, "Do not Get Caught on Camera"},         // Camera_Caught
    {36, "Pacify Guards Unnoticed"},             // Silent_Takedown_3x
    {37, "Silent Target Takedown - Rare Melee"}, // Silent_Takedown_Rare
    {38, "Only Use One Disguise"},               // One_Disguise
    {39, "Poison Target - Emetic"},
    {40, "Poison Target - Sedative"},
    {41, "Poison Target - Any Poison"}, // Poison_Target_Any (not on the wiki)
    {42, "Poison Guard"},               // Poison_Guard_Any
    {43, "Poison Guard - Syringe"},
    {44, "Pacify Guards - Concussive Explosives"},  // Pacify_Guards_Concussive
    {45, "Explosive Kill - Remote"},                // Remote_Explosive_Kill
    {51, "Explosive Kill - Proximity"},             // Proximity_Explosive_Kill
    {52, "Explosive Kill - Fragmentation Grenade"}, // Grenade_Kill
    {53, "Silenced Firearm Kill"},
    {55, "Do Not Pacify"}, // No_Pacify
    {56, "Blind Target"},
    {57, "Blind Guard"},
    {59, "Poison Target - Dart Gun"},         // Dart_Gun_Target
    {62, "Eliminate Guards - Assault Rifle"}, // KillGuard_AR
    {63, "Eliminate Guards - Shotgun"},       // KillGuard_Shotgun
    {64, "Eliminate Guards - SMG"},           // KillGuard_SMG
    {65, "Eliminate Guards - Sniper"},        // KillGuard_Sniper
    {66, "Silenced Sniper Rifle Kill"},       // Silenced_Sniper
    {67, "Axe Kill"},
    {68, "Knife Kill"},
    {69, "No Witnesses"},
    {70, "Make Target Slip"},
    {71, "Distract Target"},
    {72, "No Firearms"},
}};

constexpr std::array<int32_t, kNames.size()> MakeIds() {
    std::array<int32_t, kNames.size()> ids {};
    for (size_t i = 0; i < kNames.size(); ++i)
        ids[i] = kNames[i].first;
    return ids;
}
constexpr std::array<int32_t, kNames.size()> kIds = MakeIds();

constexpr std::array<ClientReqLocation, 19> kLocations {{
    {"Bingo", 0x0677e8f204325180ull},     {"Blackjack", 0xaf627bc05636e463ull}, {"Bridge", 0x2ccf1eac76bc96a1ull},
    {"Chess", 0xee63eb5f96852b67ull},     {"Craps", 0xbbbb40d2b468e99dull},     {"Hilo", 0x24aba9bb8310728cull},
    {"Ludo", 0x1d53cd63c43c5addull},      {"Mahjong", 0xd5d1b07869ff92b1ull},   {"Mancala", 0x593b5e57c720f3eeull},
    {"Meyer", 0x1c09223ec1bbab9bull},     {"Pinochle", 0x4ca31e982ee9b809ull},  {"Poker", 0x67a13a4f33e73bd4ull},
    {"Sapo", 0xca0f62f14d578a91ull},      {"Shogi", 0x972335f6d8d451dbull},     {"Sicbo", 0xf23b703af80c1ad6ull},
    {"Solitaire", 0x3c3d96cc94b9e170ull}, {"Spades", 0xae4fe014b8830867ull},    {"Trompo", 0x00b08f517348a6f9ull},
    {"Whist", 0xb4e91cb7b0895aa0ull},
}};

// Every "<Loc> Req 01..03" board value of the safehouse dump (g_snug.txt) by
// entity id. Craps' names were copied: 1 "Craps Req 01", 13 each of 02 and
// 03; the slot is in the name's suffix all the same.
constexpr std::array<std::pair<uint64_t, int>, 81> kMirrorSlots {{
    {0x16243cac2e9ca8ebull, 1}, // Bingo Req 01
    {0xfffc9b4a84e1e2c4ull, 2}, // Bingo Req 02
    {0x6d313adc684ca04eull, 3}, // Bingo Req 03
    {0xf7766df049c20928ull, 1}, // Blackjack Req 01
    {0xefc273ec0480b7edull, 2}, // Blackjack Req 02
    {0x83e4cbdd4c24d30eull, 3}, // Blackjack Req 03
    {0x7c3dee568fc31dbdull, 1}, // Bridge Req 01
    {0x1b90ace9772ef30eull, 2}, // Bridge Req 02
    {0xfeb6038eff920958ull, 3}, // Bridge Req 03
    {0xceecb5f41d8235daull, 1}, // Chess Req 01
    {0x7eb63a78bfb339adull, 2}, // Chess Req 02
    {0xf065c24da4d2e747ull, 3}, // Chess Req 03
    {0xc9f1b339c316a92aull, 1}, // Craps Req 01
    {0x24a7e728d264ad4cull, 2}, // Craps Req 02
    {0x37ed969039f48949ull, 2}, // Craps Req 02
    {0x544a3db328c4ce3dull, 2}, // Craps Req 02
    {0x67c1b6029c2f38a2ull, 2}, // Craps Req 02
    {0x8e1df09a435995e5ull, 2}, // Craps Req 02
    {0x9fac14c5b7b0a4f1ull, 2}, // Craps Req 02
    {0xa118edf4cdfcc6c4ull, 2}, // Craps Req 02
    {0xb092b2657eca8076ull, 2}, // Craps Req 02
    {0xb90dc89e6e63c79dull, 2}, // Craps Req 02
    {0xbd0a732402fe8df8ull, 2}, // Craps Req 02
    {0xc4524e7b9b0e9724ull, 2}, // Craps Req 02
    {0xdeb77ece92478bedull, 2}, // Craps Req 02
    {0xedd886d173804c52ull, 2}, // Craps Req 02
    {0x196d4396182aee5dull, 3}, // Craps Req 03
    {0x24c82a6ec4f2e6daull, 3}, // Craps Req 03
    {0x2a027259926d09aeull, 3}, // Craps Req 03
    {0x3397e9df149e9559ull, 3}, // Craps Req 03
    {0x33bb71504ce1d485ull, 3}, // Craps Req 03
    {0x54b2182a24ac8a2dull, 3}, // Craps Req 03
    {0x5c077882e20fedb1ull, 3}, // Craps Req 03
    {0x906baf5462c438dcull, 3}, // Craps Req 03
    {0xcc766666d1f88985ull, 3}, // Craps Req 03
    {0xd5efff1392e264f4ull, 3}, // Craps Req 03
    {0xe17a895e14c68726ull, 3}, // Craps Req 03
    {0xe242bd7d91a179bcull, 3}, // Craps Req 03
    {0xe34f728278cba1edull, 3}, // Craps Req 03
    {0x6f3ca1908cf6a3deull, 1}, // Hilo Req 01
    {0xfb7fadf36e6b18f2ull, 2}, // Hilo Req 02
    {0x08a18748966b7997ull, 3}, // Hilo Req 03
    {0xd28e3d94afe839c6ull, 1}, // Ludo Req 01
    {0x35eb954d1e3f290eull, 2}, // Ludo Req 02
    {0x57356f346195e1cbull, 3}, // Ludo Req 03
    {0xbf5c03179696c316ull, 1}, // Mahjong Req 01
    {0x51396f67d087eeb1ull, 2}, // Mahjong Req 02
    {0xbbdae582a3255a50ull, 3}, // Mahjong Req 03
    {0x536f18694e2f03fcull, 1}, // Mancala Req 01
    {0xbae013c990137bd6ull, 2}, // Mancala Req 02
    {0xf5a685f580a8a46cull, 3}, // Mancala Req 03
    {0xfae1c06bd93229b2ull, 1}, // Meyer Req 01
    {0x27a08b70a35ff3a7ull, 2}, // Meyer Req 02
    {0x21639cd20470247cull, 3}, // Meyer Req 03
    {0x7b69500d1680390dull, 1}, // Pinochle Req 01
    {0x55b45214102b3b6eull, 2}, // Pinochle Req 02
    {0x5b82548006b25aa3ull, 3}, // Pinochle Req 03
    {0xddc9026914e955b2ull, 1}, // Poker Req 01
    {0xf2af8536007bda39ull, 2}, // Poker Req 02
    {0xe87f6a69b0c995dcull, 3}, // Poker Req 03
    {0x7d2fd279f466ff1bull, 1}, // Sapo Req 01
    {0xd54f93edc7054f99ull, 2}, // Sapo Req 02
    {0x8c5e820b6f7232f8ull, 3}, // Sapo Req 03
    {0xe15f1dc35b1d673eull, 1}, // Shogi Req 01
    {0x074383d2722f5c8cull, 2}, // Shogi Req 02
    {0x1147299c156635d8ull, 3}, // Shogi Req 03
    {0x55fd52b801e7e390ull, 1}, // Sicbo Req 01
    {0x5691007b02da8f45ull, 2}, // Sicbo Req 02
    {0x07c153dbff62cf29ull, 3}, // Sicbo Req 03
    {0xf1bbafb870a105c5ull, 1}, // Solitaire Req 01
    {0x844f8e0ee23abdc1ull, 2}, // Solitaire Req 02
    {0x1fb100ef5e82cc31ull, 3}, // Solitaire Req 03
    {0x492e640ba94ac5f4ull, 1}, // Spades Req 01
    {0x0dea3a7d573f4551ull, 2}, // Spades Req 02
    {0x3eeb7d15a8765786ull, 3}, // Spades Req 03
    {0x8ce9b7ffde0cb0b9ull, 1}, // Trompo Req 01
    {0xdea7e3723bab1ae6ull, 2}, // Trompo Req 02
    {0x75452bea059201e5ull, 3}, // Trompo Req 03
    {0x007b2bad6bba6982ull, 1}, // Whist Req 01
    {0x1358c90df4ecba4eull, 2}, // Whist Req 02
    {0x720d88d063a47d2dull, 3}, // Whist Req 03
}};

// ClientReq01, ClientReq02 of a Helper_Evergreen_ClientRequirementData
// (the CPD ints behind Solitaire Req 01 and Req 02).
constexpr std::array<uint64_t, 3> kKnownCpdIntIds {0x7ff3346576b348b3ull, 0xf439b561579924b1ull, 0};

// Never together in one campaign (ClientReqConflicts).
constexpr std::array<ClientReqConflict, 1> kConflicts {{
    {2, 55}, // Targets Only (kill targets only) / No Pacify
}};

// IEvergreenTerritoryDefinition.EDestination (SDK Glacier/Enums.h).
constexpr std::array<std::string_view, 20> kDestinations {
    "AmbroseIsland", "Bangkok",   "Berlin",   "Chongqing",    "Colorado",  "Dartmoor",        "Dubai",
    "HavenIsland",   "HawkesBay", "Hokkaido", "IsleOfSgail",  "Marrakesh", "Mendoza",         "Miami",
    "Mumbai",        "NewYork",   "Paris",    "SantaFortuna", "Sapienza",  "WhittletonCreek",
};

// A campaign type (the crime sector on a book): the CPDCrimeSectorIndex the
// game sets when it is chosen (the CrimeType its challenges count by, 0..7)
// and the CPD_CrimeSectorName that goes with it.
struct ClientReqCampaignType {
    int32_t index;
    std::string_view name;
};

// The eight crime sectors by CrimeType, the index the game's own Freelancer
// challenges count campaigns by (Evergreen_Mission_Data.CrimeType, one
// "<sector>" challenge per type, Peacock contractdata/SNUG/_SNUG_CHALLENGES.json;
// names from their UI_EVERGREEN_CHALLENGES_<SECTOR>_TITLE keys). The
// CPD_CrimeSectorName / CPDCrimeSectorIndex pairs in saves agree for 1, 2 and
// 3; the other spellings are not seen in a save yet (CanonicalCampaignTypeName ignores case,
// spaces and punctuation, and the index decides a spelling not in the table).
constexpr std::array<ClientReqCampaignType, 8> kCampaignTypes {{
    {0, "Arms Trafficking"},
    {1, "Assassination"},
    {2, "Organ Trafficking"},
    {3, "Big Pharma"},
    {4, "Eco Crime"},
    {5, "Psy Ops"},
    {6, "Espionage"},
    {7, "Sick Games"},
}};

// The crime sector indexes in alphabetical order, for a list without the
// books' order: the shelf is shuffled each time and
// the game's entities do not tell which book shows which sector.
constexpr std::array<int32_t, 8> kAlphabeticalOrder {0, 1, 3, 4, 6, 2, 5, 7};

// "%02d" of an id.
std::string TwoDigits(int32_t id) {
    char text[16];
    std::snprintf(text, sizeof(text), "%02d", static_cast<int>(id));
    return text;
}

std::string_view Trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    const size_t first = s.find_first_not_of(ws);
    if (first == std::string_view::npos)
        return {};
    return s.substr(first, s.find_last_not_of(ws) - first + 1);
}

// ASCII letters and digits only, lower case: "Psy-Ops" and "psy ops" -> "psyops".
std::string Folded(std::string_view s) {
    std::string out;
    for (const char c : s) {
        const char lower = LowerAscii(c);
        if ((lower >= 'a' && lower <= 'z') || (c >= '0' && c <= '9'))
            out += lower;
    }
    return out;
}

// The table entry whose name folds to the same letters, or null.
const ClientReqCampaignType* FindTypeByName(std::string_view text) {
    const std::string folded = Folded(text);
    if (folded.empty())
        return nullptr;
    for (const ClientReqCampaignType& type : kCampaignTypes) {
        if (Folded(type.name) == folded)
            return &type;
    }
    return nullptr;
}

bool InTable(const std::string& name) {
    return std::any_of(kCampaignTypes.begin(), kCampaignTypes.end(),
                       [&](const ClientReqCampaignType& type) { return type.name == name; });
}

bool Contains(const std::vector<int32_t>& list, int32_t value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

// Client requirement definitions, duplicates of an id merged (their
// destination blacklists); other ids dropped.
std::map<int32_t, ClientReqDef> MergeDefs(std::span<const ClientReqDef> defs) {
    std::map<int32_t, ClientReqDef> merged;
    for (const ClientReqDef& def : defs) {
        if (!IsClientReqId(def.id))
            continue;
        ClientReqDef& into = merged[def.id];
        into.id = def.id;
        into.destinationBlacklist.insert(into.destinationBlacklist.end(), def.destinationBlacklist.begin(),
                                         def.destinationBlacklist.end());
    }
    return merged;
}

// Ids ranked by DialHash(seed, scene, feature, id), lowest first.
std::vector<int32_t> Ranked(int32_t seed, std::string_view scene, std::string_view feature,
                            const std::vector<int32_t>& ids) {
    std::vector<std::pair<uint64_t, int32_t>> ranked;
    ranked.reserve(ids.size());
    for (const int32_t id : ids)
        ranked.emplace_back(DialHash(seed, scene, feature, id), id);
    std::sort(ranked.begin(), ranked.end());
    std::vector<int32_t> out;
    out.reserve(ranked.size());
    for (const auto& [hash, id] : ranked)
        out.push_back(id);
    return out;
}

// The strings of the CPD_CrimeSectorName helper that can be a campaign type's
// name, canonical, distinct, in first-seen order.
std::vector<std::string> TypeNames(std::span<const std::string> strings) {
    std::vector<std::string> names;
    for (const std::string& text : strings) {
        std::string name = CanonicalCampaignTypeName(text);
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(std::move(name));
    }
    return names;
}

std::string QuotedNames(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names)
        out += (out.empty() ? "\"" : ", \"") + name + "\"";
    return out;
}

} // namespace

std::string_view ClientReqModeName(ClientReqMode mode) {
    return mode == ClientReqMode::Mission ? "mission" : "campaign";
}

std::optional<ClientReqMode> ParseClientReqMode(std::string_view text) {
    const std::string lower = ToLower(Trim(text));
    if (lower == "campaign")
        return ClientReqMode::Campaign;
    if (lower == "mission")
        return ClientReqMode::Mission;
    return std::nullopt;
}

std::span<const ClientReqConflict> ClientReqConflicts() {
    return kConflicts;
}

bool ClientReqsConflict(int32_t a, int32_t b) {
    return std::any_of(kConflicts.begin(), kConflicts.end(), [&](const ClientReqConflict& conflict) {
        return (conflict.a == a && conflict.b == b) || (conflict.a == b && conflict.b == a);
    });
}

std::span<const int32_t> ClientReqIds() {
    return kIds;
}

bool IsClientReqId(int32_t id) {
    return std::binary_search(kIds.begin(), kIds.end(), id);
}

std::string_view ClientReqName(int32_t id) {
    for (const auto& [known, name] : kNames) {
        if (known == id)
            return name;
    }
    return {};
}

std::string ClientReqLabel(int32_t id) {
    if (const std::string_view name = ClientReqName(id); !name.empty())
        return TwoDigits(id) + " " + std::string(name);
    if (const char* name = PrestigeObjectiveName(id))
        return TwoDigits(id) + " " + name + " (prestige objective)";
    return TwoDigits(id) + " (not a client requirement)";
}

std::string ClientReqDisplayName(int32_t id) {
    if (const std::string_view name = ClientReqName(id); !name.empty())
        return std::string(name);
    if (const char* name = PrestigeObjectiveName(id))
        return std::string(name) + " (prestige objective)";
    return "Unknown objective (" + std::to_string(id) + ")";
}

std::string PayoutObjectiveName(std::optional<int32_t> objective) {
    if (!objective)
        return "Unknown objective";
    if (const std::string_view name = ClientReqName(*objective); !name.empty())
        return std::string(name);
    if (const char* name = PrestigeObjectiveName(*objective))
        return std::string(name) + " (prestige)";
    return "Unused objective";
}

bool PayoutRowBefore(std::optional<int32_t> a, std::optional<int32_t> b) {
    const auto group = [](std::optional<int32_t> objective) {
        if (objective && !ClientReqName(*objective).empty())
            return 0;
        return objective && PrestigeObjectiveName(*objective) ? 1 : 2;
    };
    const int groupA = group(a);
    const int groupB = group(b);

    if (groupA != groupB)
        return groupA < groupB;

    const std::string nameA = PayoutObjectiveName(a);
    const std::string nameB = PayoutObjectiveName(b);

    if (nameA != nameB)
        return nameA < nameB;

    return a.value_or(-1) < b.value_or(-1);
}

bool InvalidClientReq(int32_t id, const std::set<int32_t>& excluded) {
    return id != 0 && (!IsClientReqId(id) || excluded.contains(id));
}

bool ClientReqExcludeLeavesEnough(const std::set<int32_t>& excluded) {
    size_t allowed = 0;

    for (const int32_t id : kIds)
        allowed += excluded.contains(id) ? 0 : 1;

    for (const ClientReqConflict& pair : ClientReqConflicts()) {
        if (IsClientReqId(pair.a) && IsClientReqId(pair.b) && !excluded.contains(pair.a) && !excluded.contains(pair.b))
            --allowed;
    }

    return allowed >= kMinClientReqPoolSize;
}

std::string_view DestinationName(int32_t destination) {
    if (destination < 0 || static_cast<size_t>(destination) >= kDestinations.size())
        return {};
    return kDestinations[static_cast<size_t>(destination)];
}

std::span<const ClientReqLocation> ClientReqLocations() {
    return kLocations;
}

const ClientReqLocation* FindClientReqLocation(uint64_t territoryStatusId) {
    for (const ClientReqLocation& location : kLocations) {
        if (location.territoryStatusId == territoryStatusId)
            return &location;
    }
    return nullptr;
}

std::string ClientReqCpdKey(std::string_view code, int slot) {
    return "Mild" + std::string(code) + "_ClientReq" + TwoDigits(slot);
}

int ClientReqMirrorSlot(uint64_t mirrorEntityId) {
    for (const auto& [id, slot] : kMirrorSlots) {
        if (id == mirrorEntityId)
            return slot;
    }
    return 0;
}

uint64_t KnownClientReqCpdIntId(int slot) {
    if (slot < 1 || slot > static_cast<int>(kKnownCpdIntIds.size()))
        return 0;
    return kKnownCpdIntIds[static_cast<size_t>(slot - 1)];
}

std::set<size_t> ClientReqSlotOutliers(const std::vector<std::array<uint64_t, 3>>& cpdIds) {
    std::set<size_t> outliers;
    for (size_t slot = 0; slot < 3; ++slot) {
        std::map<uint64_t, size_t> counts;
        for (const auto& ids : cpdIds) {
            if (ids[slot])
                ++counts[ids[slot]];
        }
        uint64_t common = 0;
        size_t best = 0;
        bool tie = false;
        for (const auto& [id, count] : counts) {
            if (count > best) {
                common = id;
                best = count;
                tie = false;
            }
            else if (count == best) {
                tie = true;
            }
        }
        for (size_t i = 0; i < cpdIds.size(); ++i) {
            if (tie || cpdIds[i][slot] != common)
                outliers.insert(i);
        }
    }
    return outliers;
}

std::string CanonicalCampaignTypeName(std::string_view text) {
    text = Trim(text);
    if (text.empty() || text.find_first_of("_[/") != std::string_view::npos ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= 0x20 && c < 0x7F; }))
        return {};
    if (const ClientReqCampaignType* type = FindTypeByName(text))
        return std::string(type->name);
    return std::string(text);
}

bool IsKnownCampaignTypeName(std::string_view text) {
    return FindTypeByName(text) != nullptr;
}

std::string_view CampaignTypeNameForIndex(int32_t index) {
    for (const ClientReqCampaignType& type : kCampaignTypes) {
        if (type.index == index)
            return type.name;
    }
    return {};
}

std::string ClientReqCampaignKey(const std::optional<std::string>& typeName) {
    const std::string name = typeName ? CanonicalCampaignTypeName(*typeName) : std::string();
    if (name.empty())
        return std::string(kClientReqCampaignKey);
    return std::string(kClientReqCampaignKey) + ":" + name;
}

ClientReqTypeChoice ResolveClientReqCampaignType(std::span<const std::string> before, std::span<const std::string> now,
                                                 std::optional<int32_t> index) {
    ClientReqTypeChoice choice;
    const std::vector<std::string> was = TypeNames(before);
    const std::vector<std::string> names = TypeNames(now);
    std::vector<std::string> fresh;
    for (const std::string& name : names) {
        if (std::find(was.begin(), was.end(), name) == was.end())
            fresh.push_back(name);
    }

    const std::string_view byIndex = index ? CampaignTypeNameForIndex(*index) : std::string_view();
    // A name from the strings: the table's own wins; a spelling not in the
    // table gives way to the index's table name (the one previewed).
    const auto fromString = [&](const std::string& name, std::string how) {
        if (!InTable(name) && !byIndex.empty()) {
            choice.name = std::string(byIndex);
            choice.how = "CPDCrimeSectorIndex " + std::to_string(*index) + " (" + how + " \"" + name +
                         "\" is not a name in the table)";
            return choice;
        }
        choice.name = name;
        choice.how = std::move(how);
        if (!byIndex.empty() && byIndex != name)
            choice.how +=
                "; CPDCrimeSectorIndex " + std::to_string(*index) + " would be \"" + std::string(byIndex) + "\"";
        return choice;
    };

    if (fresh.size() == 1)
        return fromString(fresh.front(), was.empty() ? "the CPD_CrimeSectorName string"
                                                     : "the CPD_CrimeSectorName string new since the walk");
    if (fresh.empty() && names.size() == 1)
        return fromString(names.front(),
                          "the only CPD_CrimeSectorName string (unchanged since the walk: the same type as before)");

    const std::string byName = names.empty()
                                   ? "no CPD_CrimeSectorName string reads as a name"
                                   : "the CPD_CrimeSectorName strings are ambiguous (" + QuotedNames(names) + ")";
    if (!byIndex.empty()) {
        choice.name = std::string(byIndex);
        choice.how = "CPDCrimeSectorIndex " + std::to_string(*index) + " (" + byName + ")";
        return choice;
    }
    choice.how = byName + "; CPDCrimeSectorIndex " + (index ? std::to_string(*index) : std::string("not read")) +
                 " is not in the table";
    return choice;
}

ClientReqCampaignPlan PlanClientReqCampaign(int32_t seed, std::span<const ClientReqDef> defs,
                                            std::span<const ClientReqSite> sites, const ClientReqOptions& options) {
    const std::map<int32_t, ClientReqDef> merged = MergeDefs(defs);
    ClientReqCampaignPlan campaign;
    campaign.mode = options.mode;

    // The campaign's ids in the campaign ranking; conflict losers blocked.
    std::vector<int32_t> candidates;
    for (const auto& [id, def] : merged) {
        if (options.excluded.contains(id))
            continue;
        candidates.push_back(id);
    }
    std::vector<int32_t> order;
    for (const int32_t id : Ranked(seed, options.campaignKey, kClientReqPoolFeature, candidates)) {
        if (std::any_of(order.begin(), order.end(), [&](int32_t taken) { return ClientReqsConflict(id, taken); }))
            campaign.blocked.insert(id);
        else
            order.push_back(id);
    }
    campaign.campaignAllowed = order.size();

    std::set<int32_t> pool;
    if (options.mode == ClientReqMode::Campaign) {
        size_t size = 0;
        if (options.poolSize > 0) {
            size = static_cast<size_t>(options.poolSize);
            campaign.poolSizeWhy = "client_reqs_pool_size " + std::to_string(options.poolSize);
        }
        else {
            size = static_cast<size_t>(kDefaultClientReqPoolSize);
            campaign.poolSizeWhy = "default " + std::to_string(size);
        }
        const size_t clamped = std::min(std::max(size, kMinClientReqPoolSize), order.size());
        if (clamped != size)
            campaign.poolSizeWhy += ", clamped to " + std::to_string(clamped);
        campaign.poolSize = clamped;
        campaign.pool.assign(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(clamped));
        pool.insert(campaign.pool.begin(), campaign.pool.end());
    }

    for (const ClientReqSite& site : sites) {
        ClientReqPlan plan;
        plan.key = site.key;
        plan.gamePicks = site.gamePicks;
        plan.picks = site.gamePicks;
        plan.needed = static_cast<size_t>(
            std::count_if(site.gamePicks.begin(), site.gamePicks.end(), [](int32_t id) { return id != 0; }));

        std::vector<int32_t> inPool;
        std::vector<int32_t> outside;
        for (const int32_t id : order) {
            if (Contains(merged.at(id).destinationBlacklist, site.destination))
                continue;
            (options.mode == ClientReqMode::Mission || pool.contains(id) ? inPool : outside).push_back(id);
        }
        plan.allowed = inPool.size() + outside.size();
        if (options.mode == ClientReqMode::Campaign)
            plan.fromPool = inPool.size();

        if (plan.needed == 0) {
            campaign.plans.push_back(std::move(plan));
            continue;
        }

        if (plan.allowed < plan.needed) {
            plan.fallback = true;
            plan.why =
                "only " + std::to_string(plan.allowed) + " allowed ids for " + std::to_string(plan.needed) + " slots";
            campaign.plans.push_back(std::move(plan));
            continue;
        }

        std::vector<int32_t> ranked = Ranked(seed, site.key, kClientReqFeature, inPool);
        if (ranked.size() < plan.needed) {
            const std::vector<int32_t> extra = Ranked(seed, site.key, kClientReqFeature, outside);
            plan.toppedUp = plan.needed - ranked.size();
            ranked.insert(ranked.end(), extra.begin(), extra.begin() + static_cast<std::ptrdiff_t>(plan.toppedUp));
        }

        size_t next = 0;
        for (int32_t& slot : plan.picks) {
            if (slot != 0)
                slot = ranked[next++];
        }
        campaign.plans.push_back(std::move(plan));
    }

    return campaign;
}

ClientReqCampaignPlan PlanClientReqTypeCampaign(int32_t seed, std::span<const ClientReqDef> defs,
                                                std::span<const ClientReqSite> sites, const ClientReqOptions& base,
                                                const std::optional<std::string>& typeName) {
    ClientReqOptions options = base;
    options.campaignKey = ClientReqCampaignKey(typeName);
    return PlanClientReqCampaign(seed, defs, sites, options);
}

ClientReqBook ReadClientReqBook(std::span<const ClientReqBookProperty> properties) {
    ClientReqBook book;
    std::optional<int32_t> refSlot;
    std::string refHow;
    std::string slotHow;
    std::string typeHow;

    for (const ClientReqBookProperty& property : properties) {
        const std::string name = ToLower(property.name);
        const bool sectorInt =
            name.find("crimesector") != std::string::npos || name.find("crimetype") != std::string::npos;

        if (!refSlot && property.refSlot && *property.refSlot >= 0 && *property.refSlot < kMaxClientReqBooks) {
            refSlot = property.refSlot;
            refHow = property.name + " -> a campaign accessor's m_Index " + std::to_string(*property.refSlot);
        }

        if (!book.type && property.text) {
            if (const ClientReqCampaignType* type = FindTypeByName(*property.text)) {
                book.type = std::string(type->name);
                typeHow = property.name + " \"" + *property.text + "\"";
            }
        }

        if (!property.number)
            continue;

        if (sectorInt && !book.type) {
            if (const std::string_view type = CampaignTypeNameForIndex(*property.number); !type.empty()) {
                book.type = std::string(type);
                typeHow = property.name + " " + std::to_string(*property.number);
            }
        }
        else if (!sectorInt && name.find("leader") == std::string::npos && name.find("index") != std::string::npos &&
                 !book.slot && *property.number >= 0 && *property.number < kMaxClientReqBooks) {
            book.slot = property.number;
            slotHow = property.name + " " + std::to_string(*property.number);
        }
    }

    if (refSlot) {
        book.slot = refSlot;
        slotHow = refHow;
    }

    book.how = "slot " + (book.slot ? "from " + slotHow : std::string("not read")) + ", type " +
               (book.type ? "from " + typeHow : std::string("not read"));
    return book;
}

ClientReqPreviewOrder OrderClientReqPreview(std::span<const ClientReqBook> books) {
    ClientReqPreviewOrder order;
    std::map<int32_t, std::string> bySlot;
    size_t unnamed = 0;
    size_t clashes = 0;

    for (const ClientReqBook& book : books) {
        if (!book.slot)
            continue;
        if (!book.type) {
            ++unnamed;
            continue;
        }
        const auto [at, added] = bySlot.emplace(*book.slot, *book.type);
        if (!added && at->second != *book.type)
            ++clashes;
    }

    std::set<std::string> listed;
    for (const auto& [slot, type] : bySlot) {
        order.entries.push_back({type, type, slot});
        listed.insert(type);
    }

    order.byBooks = !bySlot.empty() && unnamed == 0 && clashes == 0;
    if (order.byBooks) {
        order.why = std::to_string(bySlot.size()) + " books read, in campaign order";
        return order;
    }

    order.why =
        bySlot.empty()
            ? std::string("no book's crime sector read")
            : std::to_string(bySlot.size()) + " books read" +
                  (unnamed ? ", " + std::to_string(unnamed) + " without a crime sector" : std::string()) +
                  (clashes ? ", " + std::to_string(clashes) + " slots read with two crime sectors" : std::string());
    order.why += "; every crime sector listed, alphabetically";

    for (const int32_t index : kAlphabeticalOrder) {
        const std::string name(kCampaignTypes[static_cast<size_t>(index)].name);
        if (!listed.contains(name))
            order.entries.push_back({name, name, std::nullopt});
    }
    return order;
}

std::vector<ClientReqTypePreview> PreviewClientReqCampaignTypes(int32_t seed, std::span<const ClientReqDef> defs,
                                                                const ClientReqOptions& base,
                                                                std::span<const ClientReqBook> books) {
    std::vector<ClientReqTypePreview> previews;
    for (const ClientReqPreviewEntry& entry : OrderClientReqPreview(books).entries)
        previews.push_back(
            {entry.type, entry.label, entry.slot, PlanClientReqTypeCampaign(seed, defs, {}, base, entry.type)});
    return previews;
}

std::vector<size_t> ClientReqSitesToRepair(std::span<const ClientReqSite> sites, const std::set<int32_t>& excluded) {
    std::vector<size_t> out;
    for (size_t i = 0; i < sites.size(); ++i) {
        if (std::any_of(sites[i].gamePicks.begin(), sites[i].gamePicks.end(),
                        [&](int32_t id) { return InvalidClientReq(id, excluded); }))
            out.push_back(i);
    }
    return out;
}

std::set<int32_t> ParseClientReqIds(std::string_view csv, std::vector<std::string>& badOut) {
    std::set<int32_t> ids;
    while (true) {
        const size_t comma = csv.find(',');
        const std::string_view entry = Trim(csv.substr(0, comma));
        if (!entry.empty()) {
            int32_t id = 0;
            const auto [end, error] = std::from_chars(entry.data(), entry.data() + entry.size(), id);
            if (error == std::errc() && end == entry.data() + entry.size() && id > 0)
                ids.insert(id);
            else
                badOut.emplace_back(entry);
        }
        if (comma == std::string_view::npos)
            break;
        csv.remove_prefix(comma + 1);
    }
    return ids;
}

std::string FormatClientReqIds(const std::set<int32_t>& ids) {
    std::string out;
    for (const int32_t id : ids)
        out += (out.empty() ? "" : ", ") + std::to_string(id);
    return out;
}

std::optional<int32_t> ParseCpdInt(std::string_view value) {
    value = Trim(value);
    double number = 0.0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
    if (value.empty() || error != std::errc() || end != value.data() + value.size() || !std::isfinite(number) ||
        number != std::floor(number) || number < -2147483648.0 || number > 2147483647.0)
        return std::nullopt;
    return static_cast<int32_t>(number);
}

ClientReqPickWatch::ClientReqPickWatch(bool acceptFirstRead, int64_t settleMs)
    : m_AcceptFirst(acceptFirstRead), m_SettleMs(settleMs) {}

ClientReqWatchStep ClientReqPickWatch::OnRead(std::span<const int32_t> picks, int64_t nowMs) {
    const bool first = m_Reads == 0;
    ++m_Reads;

    if (std::none_of(picks.begin(), picks.end(), [](int32_t id) { return id != 0; })) {
        m_SawEmpty = true;
        m_Candidate.clear();
        return ClientReqWatchStep::Waiting;
    }

    if (first && !m_AcceptFirst)
        return ClientReqWatchStep::AlreadyThere;

    if (m_Candidate.empty() || !std::equal(picks.begin(), picks.end(), m_Candidate.begin(), m_Candidate.end())) {
        m_Candidate.assign(picks.begin(), picks.end());
        m_Since = nowMs;
    }

    return nowMs - m_Since >= m_SettleMs ? ClientReqWatchStep::Apply : ClientReqWatchStep::Settling;
}

ClientReqWriteGuard::ClientReqWriteGuard(std::vector<int32_t> expected, int maxRewrites, int64_t settleMs)
    : m_Expected(std::move(expected)), m_MaxRewrites(maxRewrites), m_SettleMs(settleMs), m_Settle(true, settleMs) {}

ClientReqGuardStep ClientReqWriteGuard::OnRead(std::span<const int32_t> picks, int64_t nowMs) {
    if (std::equal(picks.begin(), picks.end(), m_Expected.begin(), m_Expected.end())) {
        if (m_Settling)
            m_Settle = ClientReqPickWatch(true, m_SettleMs);
        m_Settling = false;
        return ClientReqGuardStep::Holding;
    }

    if (m_Rewrites >= m_MaxRewrites)
        return ClientReqGuardStep::GaveUp;

    m_Settling = true;
    // All slots cleared counts as a change that settles like any other.
    std::vector<int32_t> marked(picks.begin(), picks.end());
    marked.push_back(1); // never all zero, so the pick watch never reports Waiting
    const ClientReqWatchStep step = m_Settle.OnRead(marked, nowMs);
    return step == ClientReqWatchStep::Apply ? ClientReqGuardStep::Rewrite : ClientReqGuardStep::Settling;
}

void ClientReqWriteGuard::Rewritten(std::vector<int32_t> expected) {
    m_Expected = std::move(expected);
    ++m_Rewrites;
    m_Settling = false;
    m_Settle = ClientReqPickWatch(true, m_SettleMs);
}

std::optional<int32_t> ResumableClientReqSeed(std::string_view pendingText, std::optional<int32_t> persistedSeed) {
    pendingText = Trim(pendingText);
    int32_t seed = 0;
    const auto [end, error] = std::from_chars(pendingText.data(), pendingText.data() + pendingText.size(), seed);

    if (pendingText.empty() || error != std::errc() || end != pendingText.data() + pendingText.size() || seed == 0 ||
        persistedSeed != seed)
        return std::nullopt;

    return seed;
}

bool ClientReqCheckDue(bool on, std::optional<int32_t> campaignSeed, bool pickAwaited, std::string_view checkedText) {
    if (!on || !campaignSeed || *campaignSeed == 0 || pickAwaited)
        return false;
    return Trim(checkedText) != std::to_string(*campaignSeed);
}

} // namespace fr
