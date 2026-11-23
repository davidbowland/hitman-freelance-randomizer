#pragma once

// Pure part of the enforcer shuffle: which
// actors' m_aEnforcedOutfits lists (the professions whose outfits an actor
// sees through) move to which other actors. Lists only move between actors of
// the same actor type (guard to guard, civilian to civilian), preferring the
// same group (brick, type and outfit repository id), then the same brick and
// type, then (when allowed) any brick; partners always share their start
// state (a start-disabled actor's list goes to another start-disabled actor);
// each move is one swap of a list with an empty one, so
// the number of enforcers (overall and per list) never changes. A share drawn
// per mission (at least kDefaultEnforcerShareMin) says how many of the
// scene's enforcers move. Deterministic in (seed, scene), independent of
// engine pointers and walk order.

#include "Guid.h"
#include "ShareRange.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// The Rng feature tag of the shuffle's rolls.
constexpr std::string_view kEnforcerFeature = "enforcers";

// The least share of a scene's enforcers moved per mission and the most
// the share is drawn uniformly between them. Settings
// enforcers_share_min, enforcers_share_max.
constexpr double kDefaultEnforcerShareMin = 0.55;
constexpr double kDefaultEnforcerShareMax = 0.9;

// Whether lists may move to actors of another brick (setting
// enforcers_any_brick). On by default: a header swap
// between bricks relies on both bricks staying loaded for the whole scene.
constexpr bool kDefaultEnforcerAnyBrick = true;

// Inclusions, each its own setting, all on by default:
// possible targets (enforcers_include_targets), start-disabled actors
// (enforcers_include_late) and sentries (enforcers_include_sentries) may lose
// their list or receive one. Off, each is rejected as before.
constexpr bool kDefaultEnforcerIncludeTargets = true;
constexpr bool kDefaultEnforcerIncludeLate = true;
constexpr bool kDefaultEnforcerIncludeSentries = true;

struct EnforcerInclude {
    bool targets = kDefaultEnforcerIncludeTargets;
    bool late = kDefaultEnforcerIncludeLate;
    bool sentries = kDefaultEnforcerIncludeSentries;

    // Every inclusion off.
    static constexpr EnforcerInclude None() { return {false, false, false}; }
};

// One scene actor. Every fact the rules need is passed in; an unread fact is
// empty and rejects the actor (it keeps whatever it has).
struct EnforcerActor {
    uint64_t handle = 0;              // identity within this load (the mod passes the entity pointer); 0 = none
    uint64_t brickRid = 0;            // the brick the walk found it in: sort key and mission-brick rule
    uint64_t entityId = 0;            // its sub-entity id: sort key
    std::optional<bool> startEnabled; // m_bStartEnabled
    std::optional<bool> crowd;        // m_bCrowdCharacter
    std::optional<int32_t> kind;      // its outfit kit's actor type (eAT_Civilian 0, eAT_Guard 1, ...)
    std::optional<Guid> outfit;       // m_OutfitRepositoryID
    std::optional<size_t> enforced;   // m_aEnforcedOutfits element count (0: not an enforcer)
    std::optional<std::string> role;  // a Freelancer role named by the actor (supplier, mule, ...), or why unknown
    bool inTargetPool = false;        // in a ZVsTargetPicker's m_aActors: a possible target
    bool sentry = false;              // in a ZSentryZoneEntity's m_aActors: a checkpoint sentry
    bool sharedList = false;          // its list storage is another actor's too
    bool removed = false;             // a loaded map variant switches it off (stranded; VariantCleanup.h)
};

enum class EnforcerReject {
    None,
    NoHandle,      // no entity
    Duplicate,     // the same entity was passed twice (only the first counts)
    Unreadable,    // a flag, the kind, the outfit or the list was not read
    StartDisabled, // not always present
    Crowd,         // a crowd character
    MissionBrick,  // in the mission brick: Freelancer's own dedicated actors
    RoleActor,     // a supplier, mule, lookout, assassin or suspect (or its name was unreadable)
    TargetPool,    // a possible target
    Sentry,        // a checkpoint sentry (its disguise checks are setpiece logic)
    NoOutfit,      // an empty outfit repository id
    SharedList,    // its list storage is shared, so moving it would move another actor's too
    Removed,       // the loaded map variant switches it off (stranded), whatever the inclusions
};

const char* EnforcerRejectName(EnforcerReject reject);

// How close a move's two actors are; every tier keeps the actor type.
enum class EnforcerTier {
    Group, // same brick, type and outfit
    Brick, // same brick and type, other outfit
    Kind,  // same type, other brick
};

const char* EnforcerTierName(EnforcerTier tier);

// The categories an inclusion setting lets in (an actor can be in several).
enum class EnforcerInclusion {
    Target, // a possible target (enforcers_include_targets)
    Late,   // start disabled (enforcers_include_late)
    Sentry, // a checkpoint sentry (enforcers_include_sentries)
};

const char* EnforcerInclusionName(EnforcerInclusion inclusion);

// Per inclusion: the eligible actors it let in, and the moves they took part in.
struct EnforcerInclusionCount {
    size_t actors = 0;    // eligible actors in the category
    size_t enforcers = 0; // of those, with a non-empty list
    size_t movedFrom = 0; // moves taking a list away from one of them
    size_t movedTo = 0;   // moves giving a list to one of them
};

// One move: actor `from`'s list goes to actor `to` and `to`'s empty list to
// `from` (indices into the actors passed in).
struct EnforcerMove {
    size_t from = 0;
    size_t to = 0;
    EnforcerTier tier = EnforcerTier::Group;
};

struct EnforcerPlan {
    std::vector<EnforcerMove> moves;           // by `from`, ascending
    std::vector<EnforcerReject> verdicts;      // per actor passed in
    std::map<EnforcerReject, size_t> rejected; // counts, rejected actors only
    size_t eligible = 0;                       // actors passing every rule
    size_t eligibleEnforcers = 0;              // of those, with a non-empty list
    size_t keptEnforcers = 0;                  // eligible enforcers left in place
    size_t untouchedEnforcers = 0;             // rejected actors with a non-empty list (kept)
    size_t missionBrickActors = 0;             // actors in the mission brick, whatever their verdict (but no
                                               // handle or a duplicate): the showdown backstop, independent of
                                               // the include settings
    std::map<EnforcerInclusion, EnforcerInclusionCount> included; // only categories with eligible actors
    size_t groups = 0;                                            // groups of eligible actors
    size_t groupsWithMoves = 0;                                   // groups with a Group-tier move kept
    double share = 1.0;                                           // the share asked for (clamped to [0, 1])
    size_t wanted = 0;                          // share of all enforcers (eligible and untouched), rounded
    size_t available = 0;                       // moves possible in all tiers (each enforcer at most once)
    std::map<EnforcerTier, size_t> movesByTier; // of the moves made
};

// The share of a mission's enforcers to move: uniform in the range (clamped
// as ClampEnforcerShares) from (seed, scene).
double DrawEnforcerShare(int32_t seed, std::string_view scene, ShareRange range);

// Pairs the eligible actors' enforcers with non-enforcers in three tiers: in
// each (brickRid, kind, start state, outfit) group, in a canonical order ((brickRid,
// entityId), then input order), a seeded shuffle of the enforcers and one of
// the non-enforcers pair them up (min of the two); the enforcers and others
// left over are paired the same way by (brickRid, kind, start state), and what
// is still left by (kind, start state) (only with anyBrick). The start state
// is part of every key, so the number of enforcers present from the start
// stays the same too. Each group draws from its own stream, so a
// group's pairing never depends on other groups (the subset kept does). Then `wanted` = share of all enforcers
// (eligible and untouched) moves are kept: Group-tier moves first, then Brick,
// then Kind, a seeded random subset of the last tier needed. Every actor
// appears in at most one move.
EnforcerPlan PlanEnforcerShuffle(int32_t seed, std::string_view scene, std::span<const EnforcerActor> actors,
                                 uint64_t missionBrickRid, double share = 1.0, bool anyBrick = true,
                                 const EnforcerInclude& include = {});

// The least and most share, clamped (ClampShareRange; NaN: the defaults).
ShareRange ClampEnforcerShares(double minShare, double maxShare);

} // namespace fr
