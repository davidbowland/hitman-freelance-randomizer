#pragma once

// Pure part of the safe spot log (log_safe_spots, on by default, log
// only). Each Freelancer map places several instances of the safe setpiece
// (setpiece_evergreen_container_items_safe_unique, LootMerces.h), and the game
// picks one of them per mission. A map variant's set dressing can bury the
// picked one, so the log lists every spot, which one the game picked, and the
// variant's placed entities near each spot.
//
// Which spot is picked: measured in game (about 60 missions on several maps),
// exactly one safe instance per mission had, by 5 s of play, its random route
// kSafeRouteId's m_nMaxOutPinNum go
// from 1 to 5 and its values kSafeStateId and kSafeCountId go from 0 to 3; in
// every one where the player opened a safe, it was that instance (its
// kSafeStateId drops back at Evergreen_Safe_Open). The other instances keep
// 1, 0 and 0.

#include "SpotPoint.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace fr {

// Sub-entity ids inside the safe template, the same in every instance.
constexpr uint64_t kSafeRouteId = 0x3d3aaceee8b3ca6bull; // ZSignalRandomRouteEntity_void: m_nMaxOutPinNum 1 -> 5
constexpr uint64_t kSafeStateId = 0xb62b52eac35db0e6ull; // ZValueInt: m_nValue 0 -> 3 (0 again once opened)
constexpr uint64_t kSafeCountId = 0x5bf5cc3ead539422ull; // ZValueInt: m_nValue 0 -> 3

constexpr double kSafeNearRadius = 2.5;        // m, 3D: a variant's entity this close to a spot is listed
constexpr size_t kSafeNearMax = 10;            // entities listed per spot
constexpr double kSafeCheckSeconds = 10.0;     // s of play: the pick is settled (5 s in every log)
constexpr double kSafeLateCheckSeconds = 60.0; // s of play: again, only when nothing looked picked at the first check
constexpr size_t kSafeMaxSpots = 32;           // spots logged per report
constexpr size_t kSafeMaxPieces = 100'000;     // a variant's placed entities read per load

// The values that tell a picked spot, each empty when unread.
struct SafeSignals {
    std::optional<int32_t> routeOuts; // kSafeRouteId's m_nMaxOutPinNum
    std::optional<int32_t> state;     // kSafeStateId's m_nValue
    std::optional<int32_t> count;     // kSafeCountId's m_nValue
};

enum class SafeActivity {
    Active,  // any value says picked
    Idle,    // the values read all say not picked
    Unknown, // none read
};

SafeActivity ActivityOf(const SafeSignals& signals);

// "picked", "not picked", "unknown (values unread)".
const char* ActivityName(SafeActivity activity);

// "route outs 5, state 3, count 3" ("?" for an unread value).
std::string SignalsText(const SafeSignals& signals);

// Whether a read position can stand for a real one: finite and not exactly
// (0, 0, 0), the value of a world transform the engine never computed (read
// from a stale cache).
bool IsReadPoint(SpotPoint point);

// One safe spot as read.
struct SafeSpot {
    uint64_t id = 0;             // the instance's sub-entity id in its brick
    uint64_t brickRid = 0;       // the brick that places it
    std::optional<SpotPoint> at; // set only to a point IsReadPoint accepts
    std::string noPosition;      // why `at` is empty
    SafeSignals signals;
};

// "%016llx".
std::string HexId(uint64_t id);

// "(x, y, z)" with two decimals.
std::string PointText(SpotPoint point);

// "spot 3/8 entity 61afbf6e1e2ee784 in brick 00598B6364BC39F5 at (1.00, 2.00, 3.00): picked (route outs 5,
// state 3, count 3)" (1-based index).
std::string SafeSpotText(size_t index, size_t total, const SafeSpot& spot);

// "8 spots, picked: spot 3 (61afbf6e1e2ee784)"; "8 spots, none picked yet"; "8 spots, 2 look picked: ...";
// "no safe spots".
std::string SafeSummaryText(std::span<const SafeSpot> spots);

// One placed entity of a map variant's bricks.
struct PlacedPiece {
    uint64_t id = 0;
    std::string what; // "ZGeomEntity", or "ZCompositeEntity template 00AB..."
    SpotPoint at;
};

// What of p_Pieces lies within radius of safe, nearest first, at most max:
// "2 from holiday_hoarders within 2.5 m, nearest first: 0.84 m ZGeomEntity 0123... at (x, y, z); 2.10 m ..."
// (", +N more" past max), or "nothing from holiday_hoarders within 2.50 m (nearest 4.20 m ZGeomEntity ...)".
// Pieces whose position IsReadPoint rejects are left out ("; 3 with position unread, not compared"); a
// rejected safe position gives "position unread; not compared".
std::string SafeNearText(SpotPoint safe, std::string_view option, std::span<const PlacedPiece> pieces,
                         double radius = kSafeNearRadius, size_t max = kSafeNearMax);

} // namespace fr
