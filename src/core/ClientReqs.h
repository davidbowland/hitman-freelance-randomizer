#pragma once

// Pure logic of the client requirements shuffle (client_reqs): the optional
// objectives ("client requirements", BonusReq_IDnn_<Name> in the safehouse
// brick) a Freelancer campaign gives each mild location, saved in the CPD as
// Mild<Loc>_ClientReq01..03. When a campaign is chosen, each location's picks
// are replaced by picks drawn from the campaign seed: never an excluded id,
// never one blacklisted for the location's destination, never two ids that
// conflict (kClientReqConflicts) in one campaign. In campaign mode (the
// default) the seed first draws one pool of ids for the whole campaign and the
// locations pick from it; in mission mode each location draws from every
// allowed id. The pool depends on the campaign type (the book's name, the
// CPD_CrimeSectorName the game sets when it is chosen: ClientReqCampaignKey),
// so each book gets its own pool, previewed before the pick
// (PreviewClientReqCampaignTypes).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Feature name of the picks in DialHash.
inline constexpr std::string_view kClientReqFeature = "client reqs";

// Feature name of the campaign ranking (the pool's draw and the conflict
// winners) in DialHash, under the scene name ClientReqCampaignKey(type)
// (kClientReqCampaignKey when the campaign type is unknown).
inline constexpr std::string_view kClientReqPoolFeature = "client reqs pool";
inline constexpr std::string_view kClientReqCampaignKey = "campaign";

// client_reqs_exclude's default: No Disguise (suit only), No Bodies Found,
// Doors Stay Locked.
inline constexpr std::string_view kDefaultClientReqExclude = "3,4,18";

// client_reqs_mode: campaign draws one pool per campaign and the locations
// pick from it; mission draws each location from every allowed id.
enum class ClientReqMode { Campaign, Mission };
inline constexpr ClientReqMode kDefaultClientReqMode = ClientReqMode::Campaign;

// "campaign" / "mission".
std::string_view ClientReqModeName(ClientReqMode mode);

// client_reqs_mode's value (whitespace-trimmed, any case), or empty when it
// is neither.
std::optional<ClientReqMode> ParseClientReqMode(std::string_view text);

// client_reqs_pool_size's default, also used for 0 or less: 10. The game's own
// campaigns use 6 to 12 distinct requirements over 3 to 6 locations, 7.5 at
// the median.
inline constexpr int32_t kDefaultClientReqPoolSize = 10;
// The smallest pool (one location's three slots).
inline constexpr size_t kMinClientReqPoolSize = 3;

// Two ids that never appear in one campaign: 02 Targets Only (kill targets
// only) and 55 No Pacify. Add pairs here.
struct ClientReqConflict {
    int32_t a;
    int32_t b;
};
std::span<const ClientReqConflict> ClientReqConflicts();

// True when a and b are a pair of ClientReqConflicts (either order).
bool ClientReqsConflict(int32_t a, int32_t b);

// The client requirement ids: those of the safehouse's BonusReq_IDnn_<Name>
// entities, ascending. The other
// ZEvergreenBonusRequirementDefinitionEntity ids (101..136, the prestige
// objectives) are not client requirements and are never picked.
std::span<const int32_t> ClientReqIds();
bool IsClientReqId(int32_t id);

// The requirement's in-game name (BonusReq_ID03_No_Disguise -> "Suit Only"),
// or empty when id is not a client
// requirement.
std::string_view ClientReqName(int32_t id);

// For the log: "03 Suit Only", "124 Shoot Target into Water (prestige
// objective)" (fr::PrestigeObjectiveName, Merces.h),
// "77 (not a client requirement)".
std::string ClientReqLabel(int32_t id);

// For the overlay: "Suit Only", "Shoot Target into Water (prestige
// objective)", "Unknown objective (77)". Never "(unknown)".
std::string ClientReqDisplayName(int32_t id);

// A payout table entry's row name in the overlay, by its objective number
// (fr::kObjectiveNumberPropertyId): a client requirement's name ("Suit
// Only"), a prestige objective's with " (prestige)" ("Silent Assassin
// (prestige)"), "Unused objective" for a number with neither (17 and 32: the
// table pays them, but the safehouse defines no such requirement, so no
// campaign offers them), "Unknown objective" when the number was not read.
// Never a bare number.
std::string PayoutObjectiveName(std::optional<int32_t> objective);

// The overlay's row order: client requirements, then prestige objectives,
// then the rest; by PayoutObjectiveName within each group, then by number.
bool PayoutRowBefore(std::optional<int32_t> a, std::optional<int32_t> b);

// A non-zero id the rules never allow: not a client requirement, or excluded.
bool InvalidClientReq(int32_t id, const std::set<int32_t>& excluded);

// Whether client_reqs_exclude leaves the shuffle enough to pick from: at
// least kMinClientReqPoolSize client requirements not excluded, the two of a
// conflicting pair counting as one (a campaign never gets both). A
// destination's blacklist can still leave one location too few; that
// location keeps the game's picks, as before.
bool ClientReqExcludeLeavesEnough(const std::set<int32_t>& excluded);

// The IEvergreenTerritoryDefinition.EDestination name ("Paris"), or empty.
std::string_view DestinationName(int32_t destination);

// A mild location: its codename (as in the CPD keys Mild<Code>_ClientReq01)
// and its ZEvergreenTerritoryStatusEntity in the safehouse brick
// (TerritoryStatus_<Code>).
struct ClientReqLocation {
    std::string_view code;
    uint64_t territoryStatusId;
};

// The 19 mild locations.
std::span<const ClientReqLocation> ClientReqLocations();

// The location whose TerritoryStatus entity has this id, or null.
const ClientReqLocation* FindClientReqLocation(uint64_t territoryStatusId);

// "Mild<code>_ClientReq0<slot>" (slot 1-based).
std::string ClientReqCpdKey(std::string_view code, int slot);

// The slot (1..3) of a board value ("<Loc> Req 0N", an entry of a
// TerritoryStatus' m_idsBonusRequirements) by its entity id, from the
// safehouse dump's names; 0 for any other entity (Req 04..08, unknown). The
// array is not in slot order: Solitaire's starts Req 08, Req 01, Req 02.
int ClientReqMirrorSlot(uint64_t mirrorEntityId);

// The CPD ints' entity ids seen for slots 1 and 2 (ClientReq01, ClientReq02
// inside Helper_Evergreen_ClientRequirementData_MildSolitaire); every
// location's helper is the same template. 0: not known.
uint64_t KnownClientReqCpdIntId(int slot);

// The locations (indexes into cpdIds) whose CPD int for some slot is not the
// one most locations share (each location's three CPD int entity ids, 0 for
// none): every location's helper is one template, so an odd one out is a
// mapping error. With a tie for the most common id, every location of the
// slot is an outlier.
std::set<size_t> ClientReqSlotOutliers(const std::vector<std::array<uint64_t, 3>>& cpdIds);

// The table's spelling of a campaign type name (trimmed; case, spaces and
// punctuation ignored: "PSY-OPS" -> "Psy Ops"), else the trimmed text when it
// can be one (printable, no '_', '[' or '/'), else empty.
std::string CanonicalCampaignTypeName(std::string_view text);

// True when the text names a table type (case, spaces and punctuation ignored).
bool IsKnownCampaignTypeName(std::string_view text);

// The table's name for a CPDCrimeSectorIndex, or empty.
std::string_view CampaignTypeNameForIndex(int32_t index);

// The DialHash scene name of a campaign's ranking: "campaign:Big Pharma" for
// a campaign type (CanonicalCampaignTypeName), kClientReqCampaignKey
// ("campaign") when it is unknown.
std::string ClientReqCampaignKey(const std::optional<std::string>& typeName);

// Which campaign type the chosen (or current) campaign is.
struct ClientReqTypeChoice {
    std::optional<std::string> name; // empty: unknown (the seed-only pool)
    std::string how;                 // how it was decided, or why it is unknown
};

// The strings of the CPD_CrimeSectorName helper before the pick (empty when
// there was no before, e.g. a later visit) and now, and CPDCrimeSectorIndex
// now. Names are the strings that can be one (CanonicalCampaignTypeName; the
// helper's own "CPD_CrimeSectorName" is not). The one name that is new since
// before wins; else, with no new name, the only name there is (the same type
// as the last campaign); else the index's table name. A name from the strings
// that is not a table name gives way to the index's table name, so a book
// whose spelling the table does not have still gets its previewed pool.
ClientReqTypeChoice ResolveClientReqCampaignType(std::span<const std::string> before, std::span<const std::string> now,
                                                 std::optional<int32_t> index);

// One ZEvergreenBonusRequirementDefinitionEntity, its destination blacklist
// resolved to IEvergreenTerritoryDefinition.EDestination values. (Its crime
// sector blacklist is not used: each campaign type already has its own list.)
struct ClientReqDef {
    int32_t id = 0;
    std::vector<int32_t> destinationBlacklist;
};

// A location that needs picks: its CPD key prefix ("MildBridge"), its
// destination, and the game's picks per slot (0: none).
struct ClientReqSite {
    std::string key;
    int32_t destination = -1;
    std::vector<int32_t> gamePicks;
};

struct ClientReqOptions {
    ClientReqMode mode = kDefaultClientReqMode;
    // The DialHash scene name of the campaign ranking (ClientReqCampaignKey).
    std::string campaignKey = std::string(kClientReqCampaignKey);
    // Campaign mode: the pool's size; 0 (or less) is kDefaultClientReqPoolSize.
    int32_t poolSize = kDefaultClientReqPoolSize;
    std::set<int32_t> excluded;
};

struct ClientReqPlan {
    std::string key;
    std::vector<int32_t> gamePicks;
    std::vector<int32_t> picks; // per slot, same length; a slot the game left 0 stays 0
    size_t needed = 0;          // non-zero game slots
    size_t allowed = 0;         // ids allowed here
    size_t fromPool = 0;        // campaign mode: pool ids allowed here
    size_t toppedUp = 0;        // campaign mode: slots filled from outside the pool
    bool fallback = false;      // too few allowed ids: picks are the game's
    std::string why;            // the fallback's reason
};

// A whole campaign's plan.
struct ClientReqCampaignPlan {
    ClientReqMode mode = kDefaultClientReqMode;
    size_t campaignAllowed = 0; // ids allowed in the campaign (client reqs, not excluded, conflict losers out)
    std::set<int32_t> blocked;  // conflict losers: never picked in this campaign
    // Campaign mode: the pool in draw order, the size asked for (after the
    // default and the clamp) and how it was decided.
    std::vector<int32_t> pool;
    size_t poolSize = 0;
    std::string poolSizeWhy;
    std::vector<ClientReqPlan> plans; // one per site, in the sites' order
};

// Plans a campaign. The campaign's ids: definitions of client requirements
// (IsClientReqId), not excluded.
// They are ranked by DialHash(seed, options.campaignKey,
// kClientReqPoolFeature, id), lowest first; walking that ranking, an id that
// conflicts with one already taken is blocked (never picked in this
// campaign, either mode).
//
// Campaign mode: the pool is the first poolSize ids of the ranking (blocked
// ones skipped); poolSize is options.poolSize (kDefaultClientReqPoolSize when
// <= 0), clamped to [kMinClientReqPoolSize, campaign's ids]. Each site then
// ranks the pool ids its destination allows by DialHash(seed, key,
// kClientReqFeature, id); when they cannot fill its slots, the rest come from
// the other allowed ids (not blocked) in the same ranking (toppedUp).
//
// Mission mode: each site ranks every allowed id (not blocked, destination
// allowed) by DialHash(seed, key, kClientReqFeature, id).
//
// Either way the lowest hashes win, in that order; each site's non-zero slots
// get distinct ids; a site with fewer allowed ids than slots keeps the game's
// picks (fallback, with the reason); a site with no non-zero slot is planned
// unchanged. Duplicate definitions of an id count as one with both
// blacklists. Independent of the order of defs and sites, and a site's picks
// do not depend on the other sites.
ClientReqCampaignPlan PlanClientReqCampaign(int32_t seed, std::span<const ClientReqDef> defs,
                                            std::span<const ClientReqSite> sites, const ClientReqOptions& options);

// PlanClientReqCampaign for a campaign of this type (campaignKey =
// ClientReqCampaignKey(typeName)). The preview, the applied plan and the
// repair all go through here, so a type's previewed pool is the pool its
// campaign gets.
ClientReqCampaignPlan PlanClientReqTypeCampaign(int32_t seed, std::span<const ClientReqDef> defs,
                                                std::span<const ClientReqSite> sites, const ClientReqOptions& base,
                                                const std::optional<std::string>& typeName);

// The campaigns the safehouse offers at once (ZEvergreenCampaignManager's
// eight ZEvergreenCampaignDefinitionAccessor, m_Index 0..7, "CampaignDefinition1..8";
// eight m_CrimeSectors): slots are 0..kMaxClientReqBooks-1.
inline constexpr int32_t kMaxClientReqBooks = 8;

// One property of a campaign entity as the walk read it (ReadClientReqBook).
struct ClientReqBookProperty {
    std::string name;                // "m_Index"
    std::optional<int32_t> number;   // an int32 or enum value
    std::optional<std::string> text; // a string value
    std::optional<int32_t> refSlot;  // a reference to a campaign accessor: its m_Index
};

// What one entity tells about a book: the campaign slot (0-based) and the
// crime sector (a table name), each when it tells.
struct ClientReqBook {
    std::optional<int32_t> slot;
    std::optional<std::string> type;
    std::string how; // where each came from, for the log
};

// slot: the first reference to a campaign accessor, else the first int32
// whose name has "index" (any case; not a crime sector's or a leader's) in
// [0, kMaxClientReqBooks). type: the first text naming a table type
// (IsKnownCampaignTypeName), else the first int32 whose name has
// "crimesector" or "crimetype" that is a table index.
ClientReqBook ReadClientReqBook(std::span<const ClientReqBookProperty> properties);

// What the preview lists, in order: label (what the overlay shows) and type
// (the pool's campaign type), and the book's slot when it came from a book.
struct ClientReqPreviewEntry {
    std::string label;
    std::string type;
    std::optional<int32_t> slot;
};
struct ClientReqPreviewOrder {
    std::vector<ClientReqPreviewEntry> entries;
    bool byBooks = false; // the books' own order and types (else every type, by index)
    std::string why;      // for the log
};

// The books with a slot and a type, one per slot (the first read), in slot
// order. When every book with a slot has a type and no slot was read with
// two types (byBooks), that is the list: the books on offer. Otherwise every
// table type not already listed follows, alphabetically, so whichever
// book is chosen, its type was previewed. Books without a slot are ignored.
ClientReqPreviewOrder OrderClientReqPreview(std::span<const ClientReqBook> books);

// One campaign type's plan before the pick (no sites).
struct ClientReqTypePreview {
    std::string name;  // the campaign type
    std::string label; // what the overlay shows
    std::optional<int32_t> slot;
    ClientReqCampaignPlan plan;
};

// PlanClientReqTypeCampaign(seed, defs, {}, base, type) for every entry of
// OrderClientReqPreview(books), in its order (no books: all eight types
// alphabetically).
std::vector<ClientReqTypePreview> PreviewClientReqCampaignTypes(int32_t seed, std::span<const ClientReqDef> defs,
                                                                const ClientReqOptions& base,
                                                                std::span<const ClientReqBook> books = {});

// The sites (indexes) holding an id the rules never allow
// (InvalidClientReq): the ones a repair rewrites.
std::vector<size_t> ClientReqSitesToRepair(std::span<const ClientReqSite> sites, const std::set<int32_t>& excluded);

// Parses client_reqs_exclude: comma-separated ids ("3, 18,38"); entries are
// whitespace-trimmed, empty ones ignored, others that are not a positive
// integer appended to badOut.
std::set<int32_t> ParseClientReqIds(std::string_view csv, std::vector<std::string>& badOut);

// "3, 18, 38".
std::string FormatClientReqIds(const std::set<int32_t>& ids);

// A CPD number as a CpdSet writes it ("45.000000", "45"), as an integer, or
// empty when it is not a whole number in int32 range.
std::optional<int32_t> ParseCpdInt(std::string_view value);

// Waiting for a campaign's picks. OnGenerate fires when the safehouse offers
// the next campaigns; the game writes Mild<Loc>_ClientReq0N only when the
// player chooses one, which can be minutes later (or in a later session).
// The mod re-reads the cached CPD values and feeds each read here.
enum class ClientReqWatchStep {
    Waiting,      // no pick yet
    Settling,     // picks there, not yet unchanged for settleMs
    Apply,        // picks unchanged for settleMs: plan and write now
    AlreadyThere, // picks there at the first read of a watch that needs a zero read first: leave them
};

// How long the picks must stay unchanged before they are replaced. The game
// writes every location of the campaign within ~10 ms of the pick; 3 s leaves
// room for a slower burst.
inline constexpr int64_t kClientReqSettleMs = 3'000;

class ClientReqPickWatch {
public:
    // acceptFirstRead: picks at the first read count (the campaign was
    // generated during this safehouse visit, so they can only be its own).
    // Otherwise (a generation carried over from an earlier visit or session)
    // a read without any pick must come first: picks there from the start
    // were made before the mod watched (by the game without the mod, or a
    // campaign already under way) and are left alone.
    explicit ClientReqPickWatch(bool acceptFirstRead = true, int64_t settleMs = kClientReqSettleMs);

    // One read of every watched slot (0: no pick), in a fixed order, at nowMs
    // (any monotonic clock). A change in the picks restarts the settle time.
    ClientReqWatchStep OnRead(std::span<const int32_t> picks, int64_t nowMs);

    // Reads so far, and whether one had no pick at all.
    int reads() const { return m_Reads; }
    bool sawEmpty() const { return m_SawEmpty; }
    // When the current picks were first read (valid while Settling).
    int64_t candidateSinceMs() const { return m_Since; }

private:
    bool m_AcceptFirst;
    int64_t m_SettleMs;
    int m_Reads = 0;
    bool m_SawEmpty = false;
    std::vector<int32_t> m_Candidate;
    int64_t m_Since = 0;
};

// After the writes: the slots must keep what was written. A read that differs
// (the game wrote again) is let settle (unchanged for settleMs), then the
// plan is applied again to what is there, at most maxRewrites times.
enum class ClientReqGuardStep {
    Holding,  // the slots hold what was written
    Settling, // changed; waiting for the change to settle
    Rewrite,  // changed and settled: plan and write again
    GaveUp,   // changed again after maxRewrites rewrites: left as they are
};

inline constexpr int kMaxClientReqRewrites = 3;

class ClientReqWriteGuard {
public:
    explicit ClientReqWriteGuard(std::vector<int32_t> expected, int maxRewrites = kMaxClientReqRewrites,
                                 int64_t settleMs = kClientReqSettleMs);

    ClientReqGuardStep OnRead(std::span<const int32_t> picks, int64_t nowMs);

    // After a rewrite: the slots now hold expected.
    void Rewritten(std::vector<int32_t> expected);

    int rewrites() const { return m_Rewrites; }
    const std::vector<int32_t>& expected() const { return m_Expected; }

private:
    std::vector<int32_t> m_Expected;
    int m_MaxRewrites;
    int64_t m_SettleMs;
    int m_Rewrites = 0;
    ClientReqPickWatch m_Settle;
    bool m_Settling = false;
};

// The persisted "client_reqs_pending" value (a generated campaign whose
// picks were not seen yet) when it still applies: a non-zero 32-bit integer
// equal to the persisted campaign seed. Empty otherwise (blank, bad, or
// another campaign was generated since).
std::optional<int32_t> ResumableClientReqSeed(std::string_view pendingText, std::optional<int32_t> persistedSeed);

// Whether a safehouse visit checks the campaign's locations for ids the rules
// never allow (and repairs them): client_reqs on, a known campaign seed
// (non-zero), no pick awaited, and the persisted "client_reqs_checked" text
// is not that seed (each campaign is checked until one check completes).
bool ClientReqCheckDue(bool on, std::optional<int32_t> campaignSeed, bool pickAwaited, std::string_view checkedText);

} // namespace fr
