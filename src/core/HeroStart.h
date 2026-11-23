#pragma once

// Pure part of the start fix. 47 starts at the scene's hero spawn (ZHeroSpawn)
// whose entrance id is the entrance the contract asks for. A brick places a
// hero spawn as an instance of levelflow.template's herospawn.entitytemplate
// (a ZEntity root holding a ZHeroSpawn, a ZOrientationEntity and a few value
// entities); the instance's m_sId, m_bDefaultHeroSpawn and m_mTransform are
// aliases. At runtime the entrance is the inner ZHeroSpawn's m_sRepositoryId
// (kHeroSpawnEntranceProperty); it has no m_sId. A map
// variant can delete that spawn: snow_festival's scenario_mamushi deletes
// agencymarkup_snowcrane's HeroSpawn_Restaurant (entrance be6045fd-...) and
// adds HeroSpawn_Mamushi on the same spot with another entrance id, marked
// default. The game does NOT fall back to the default spawn then: 47 stays at location.brick's placeholder Agent47_Default, in
// the sky. So once the scene's entities exist, when no spawn answers the
// requested entrance, one spawn is re-pointed to it (DecideHeroStart): the one
// standing where the deleted spawn stood, if that spot is known, else a
// default spawn (the variant's own first), else any spawn.
//
// reflection's scenario_bellini deletes agencymarkup_snowcrane's whole
// HeroSpawns_Snowcrane group (all nine spawns) and keeps one spawn of its own,
// HeroSpawn_Bellini (default, entrance 6ad6d556-...), with a
// HeroSpawnDirector and a spawn sequence: 47 starts in the "Recruitment
// Dream" room of Bellini's Mirror World, 179 m under the hotel, and reaches
// the hotel through the mirror portals. That start is the variant's intended
// one; the fix only re-points HeroSpawn_Bellini to the requested entrance, so
// the game reports the start as the contract's (47 still starts there).
//
// dartmoor's showdown scene under the sloth variant keeps one spawn of the
// garden brick (44bc69efb5aa958d) and adds its own default one,
// 22f118f0776459a5, at (-509.04, -0.10, 499.99): a staging spot 500 m up, off
// the playable map (re-pointed, 47 would start there and see no NPCs). A
// spawn that stands off the map
// (SpawnPlaceOf) is never re-pointed: the next one in the order above is, and
// when none is left nothing changes (StartAction::OffMapOnly).
//
// A variant can also keep a Freelancer spawn but close the area it stands in
// (its own navmesh has no floor there; tools/landslide/variant_audit.py's
// "seal" findings): the Garden Show closes Dartmoor's mansion with
// Evergreen_AlexaBalcony in it, Illusions of Grandeur closes Mumbai's barge and
// the Bollywood set with HeroSpawn_Barge and HeroSpawn_Bollywood_Dancer. A
// variant can also make the area around a spawn off limits (a trespassing or
// hostile area it adds) and put its own people right beside it, so 47 (never
// disguised in Freelancer) is seen at once: A Bitter Pill's for-sale house
// (Evergreen_Spawn_House), The Undying's and The Undying Returns' Kronstad
// room (Start_Kronstad_Engineer), Embrace of the Serpent's jungle
// (HeroSpawn_Jungle), Illusions of Grandeur's chawl (HeroSpawn_Kashmirian).
// An off-limits start with no one beside it stays. Such a
// spawn (KnownSealedSpawns, by the option and the spawn instance's entity id)
// never answers the requested entrance and is never re-pointed: when it is the
// requested one, another spawn is re-pointed to its entrance as for a deleted
// one, and the sealed spawn takes that spawn's old entrance (a swap), so only
// one spawn answers the request.

#include "Guid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace fr {

// ZHeroSpawn's entrance id property (ZRepositoryID, CRC-32 dd4034e6; seen in
// the safehouse's spawn, measured in game). The bricks' m_sId is the
// herospawn template's alias for it.
constexpr std::string_view kHeroSpawnEntranceProperty = "m_sRepositoryId";

struct StartPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f; // up
};

// 3D distance in meters.
double StartDistance(StartPoint a, StartPoint b);

// A spawn this close (m, 3D) to where a deleted spawn stood takes its place.
constexpr double kSameSpotMeters = 3.0;

// After this many seconds of play, 47's position is checked against the spawns.
constexpr double kStartCheckPlaySeconds = 5.0;

// 47 farther than this (m, 3D) from every spawn did not start at one.
constexpr double kHeroFarMeters = 50.0;

// One ZHeroSpawn of the loaded scene, as read once its entities exist.
struct HeroSpawnFacts {
    std::optional<Guid> entrance;    // m_sRepositoryId; empty when unreadable
    bool isDefault = false;          // m_bDefaultHeroSpawn
    std::optional<StartPoint> where; // world position, when read
    bool inVariantBrick = false;     // in one of the loaded map variant's bricks
    uint64_t id = 0;                 // the herospawn instance's entity id in its brick; 0 when unread
    bool sealed = false;             // FindSealedSpawn: out of reach under the loaded variant, never used
};

// Off the playable map: a spawn this far up or down (m, |z|). Dartmoor's
// sloth staging spawn stands at z 500; reflection's HeroSpawn_Bellini, a real
// start in the Mirror World at z -179, stays under it.
constexpr double kOffMapHeightMeters = 250.0;

// Off the playable map: a spawn this far out (m, |x| or |y|).
constexpr double kOffMapReachMeters = 3000.0;

// With at least kOffMapPeers other spawns on the map (positions read, within
// the bounds above), a spawn this far up or down (m) from their median height
// is off the map too.
constexpr double kOffMapPeerHeightMeters = 150.0;
constexpr size_t kOffMapPeers = 2;

// A position that was really read: finite, and not (0, 0, 0) (a stale cached
// transform reads all zeros).
bool IsSpawnPointRead(StartPoint point);

// A spawn known to stand off the playable map (by its instance's entity id),
// for when positions cannot be read in time.
struct OffMapSpawn {
    uint64_t id;
    std::string_view name;
    StartPoint where;
};

// The spawns known so far to stand off the map (dartmoor sloth's staging spawn).
std::span<const OffMapSpawn> KnownOffMapSpawns();

enum class SpawnPlace {
    Unknown, // no position read (and not a known off-map spawn)
    OnMap,
    OffMap, // far up, down or out (kOffMap*), or a known off-map spawn
};

// Where spawns[index] stands, judged against the bounds and the other spawns.
SpawnPlace SpawnPlaceOf(std::span<const HeroSpawnFacts> spawns, size_t index,
                        std::span<const OffMapSpawn> offMap = KnownOffMapSpawns());

// "on the map", "off the map", "position unknown".
std::string_view SpawnPlaceText(SpawnPlace place);

// A vanilla spawn a map variant deletes: its entrance id and where it stood
// (world position, from the bricks).
struct DeletedSpawn {
    std::string_view entrance; // "xxxxxxxx-xxxx-..."
    std::string_view name;
    StartPoint where;
};

// The deleted spawns known so far (snow_festival's HeroSpawn_Restaurant).
std::span<const DeletedSpawn> KnownDeletedSpawns();

// A Freelancer spawn a variant keeps but seals off (by its herospawn
// instance's entity id), under that variant only.
struct SealedSpawn {
    std::string_view option; // the variant option ("garden_show")
    uint64_t id;
    std::string_view name;
    std::string_view why;
};

// The sealed spawns known so far (the Garden Show's, Illusions of Grandeur's).
std::span<const SealedSpawn> KnownSealedSpawns();

// The sealed spawn with this instance id under this option, or null.
const SealedSpawn* FindSealedSpawn(std::string_view option, uint64_t id,
                                   std::span<const SealedSpawn> known = KnownSealedSpawns());

enum class StartAction {
    NoRequest,  // the contract's entrance is unknown (empty): nothing to do
    Matched,    // a spawn answers the requested entrance
    Repoint,    // none does: re-point `spawn` to it
    NoSpawn,    // none does, and no spawn can be re-pointed (none found, or none readable)
    OffMapOnly, // none does, and every spawn that could be re-pointed stands off the map: change nothing
    SealedOnly, // only a sealed spawn does, and no other can be re-pointed: change nothing
};

// Why that spawn was picked to re-point.
enum class RepointWhy {
    None,
    SameSpot,       // it stands where the deleted spawn stood
    VariantDefault, // a default spawn of the variant's bricks
    SceneDefault,   // a default spawn of another brick
    AnySpawn,       // no default spawn: the first readable one (the variant's own first)
};

struct StartDecision {
    StartAction action = StartAction::NoRequest;
    size_t spawn = 0; // Matched: the first match; Repoint: the one to re-point
    RepointWhy why = RepointWhy::None;
    size_t matches = 0; // spawns answering the requested entrance
    size_t offMap = 0;  // Repoint, OffMapOnly: spawns with a readable entrance passed over as off the map
    // A sealed spawn answers the requested entrance (the first, `sealedSpawn`): with Repoint, it takes the
    // re-pointed spawn's old entrance.
    bool sealedRequest = false;
    size_t sealedSpawn = 0;
};

// What to do about the requested entrance given the scene's spawns. Only a
// spawn whose entrance id was read is ever re-pointed, and never one off the
// map (SpawnPlaceOf; one whose position is unknown still can be) or a sealed
// one; a sealed spawn never counts as answering the request.
StartDecision DecideHeroStart(std::span<const HeroSpawnFacts> spawns, const std::optional<Guid>& requested,
                              std::span<const DeletedSpawn> deleted = KnownDeletedSpawns(),
                              std::span<const OffMapSpawn> offMap = KnownOffMapSpawns());

// "it stands where HeroSpawn_Restaurant stood", "the variant's default spawn", ...
std::string_view RepointWhyText(RepointWhy why);

// Where a spawn's position was read: the ZHeroSpawn itself, or (when it has
// none, e.g. it is not spatial) another entity of its herospawn template
// instance, which stands on the instance's transform.
enum class SpawnPointSource {
    None,
    Own,
    TemplateMate,
};

struct SpawnPoint {
    std::optional<StartPoint> where;
    SpawnPointSource source = SpawnPointSource::None;
    size_t mate = 0; // TemplateMate: index into mates
};

// The spawn's own position if read, else the first template mate's that was.
SpawnPoint PickSpawnPoint(const std::optional<StartPoint>& own, std::span<const std::optional<StartPoint>> mates);

// The spawn nearest to 47 (index into points; points without a position
// are skipped), or empty when none has one.
struct NearestStart {
    size_t index = 0;
    double distance = 0.0;
};

std::optional<NearestStart> NearestSpawn(std::span<const std::optional<StartPoint>> points, StartPoint hero);

} // namespace fr
