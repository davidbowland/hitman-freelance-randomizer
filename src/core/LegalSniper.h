#pragma once

// Pure logic of legal sniper rifles (legal_sniper_rifles; glue in
// mod/LegalSniperKits.cpp). Whether a visible weapon is legal in a disguise
// is not in pro.repo: its outfit entries carry no weapon data at all (every
// one of the 1,365 has only ID_, CommonName, Name, Description, Image,
// ImageTransparent, Category, TokenID, HeroDisguiseAvailable, IsHitmanSuit,
// some Features_; the Dubai guards' entries look like Bangkok's hotel
// security). It is in the outfit's ZGlobalOutfitKit entity: m_bWeaponsAllowed
// (visible firearms are legal) with m_aWeaponsAllowedExceptionKeys /
// m_aWeaponsAllowedExceptionKeywords (weapons that stay illegal even so), and
// m_aAllowedWeaponKeys / m_aAllowedWeaponKeywords (weapons allowed in an
// outfit that has no m_bWeaponsAllowed). The items' keywords are pro.repo's
// AdditionalKeywords: every sniper rifle but a few carries
// INVENTORY_TYPE_SNIPER (44 items; pistols carry INVENTORY_TYPE_PISTOL,
// assault rifles INVENTORY_TYPE_ASSAULTRIFLE, ...).
//
// With the setting on, every kit with m_bWeaponsAllowed loses the sniper
// rifles from its two exception lists (a keyword entity whose keyword is
// INVENTORY_TYPE_SNIPER, an item key whose pro.repo item is a sniper rifle),
// so a sniper rifle is as legal there as any other firearm. An exception
// entry the mod cannot read (a keyword entity whose keyword it cannot find,
// an item key it cannot read) is never taken for "no sniper exception": such
// a kit is logged as unknown and left alone.
//
// The keyword lists are TArray<TEntityRef<ZKeywordEntity>> (SDK ZOutfit.h,
// 16-byte elements). A ZKeywordEntity has no ZString property, only
// m_rHolder (ZEntityRef) and m_aHolders (TArray<ZEntityRef>). Its keyword is looked up in the game's
// ZGameKeywordManager (keyword id -> string): the keyword entity as a holder
// or a keyword channel there, or a registered keyword id stored in the
// entity's own memory (KeywordIdSlots, PickKeywordId). The lists are
// edited in place (no allocation): the sniper entries move behind the kept
// ones and the array's count shrinks; the vanilla order and count are put
// back at the scene's clear.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

inline constexpr std::string_view kSniperKeyword = "INVENTORY_TYPE_SNIPER";
// The keyword guard outfits list as their sniper rifle exception (seen on Dartmoor's mansion guards).
inline constexpr std::string_view kSniperRifleKeyword = "ITEM_SNIPER_RIFLE";

// A keyword entity's text that names sniper rifles: INVENTORY_TYPE_SNIPER,
// any case (not MAGAZINE_SNIPER_STEALTH, the stealth magazine's).
bool IsSniperKeyword(std::string_view keyword);

// A pro.repo item that is a sniper rifle: InventoryCategoryIcon "sniperrifle"
// or an ItemType starting with "eSniper" (any case).
bool IsSniperItem(std::string_view inventoryCategoryIcon, std::string_view itemType);

// What one list entry names, as far as the mod could read it.
enum class EntryMatch {
    Sniper,  // names sniper rifles
    Other,   // read, and names something else
    Unknown, // not read: could be anything
};

// A keyword entity's texts: Sniper when any names sniper rifles, Unknown when
// there are none.
EntryMatch MatchKeywordTexts(const std::vector<std::string>& texts);

// What one outfit kit says about visible sniper rifles.
struct KitSniperFacts {
    bool weaponsAllowed = false;  // m_bWeaponsAllowed
    size_t sniperExceptions = 0;  // sniper entries in the two exception lists
    size_t sniperAllowed = 0;     // sniper entries in the two allowed lists
    size_t unknownExceptions = 0; // exception entries (or lists) the mod could not read
};

enum class KitSniper {
    NoFirearms,         // no visible firearm is legal (left alone)
    NoFirearmsSniperOk, // no m_bWeaponsAllowed, yet a sniper rifle is on its allowed list (left alone)
    AlreadyLegal, // firearms legal, every exception read and none a sniper rifle (Dubai's guards, Berlin's bikers)
    Legalize,     // firearms legal but sniper rifles excepted: the exceptions are removed
    Unknown,      // firearms legal, no sniper exception read, but an exception unreadable (left alone)
};

inline constexpr size_t kKitSniperVerdicts = 5;

KitSniper ClassifyKit(const KitSniperFacts& facts);
std::string_view KitSniperText(KitSniper verdict);

// The new order of one exception list: the kept elements (in their order),
// then the sniper ones; kept is the count the array shrinks to.
struct ExceptionEdit {
    std::vector<size_t> order;
    size_t kept = 0;

    bool Changes() const { return kept < order.size(); }
};

ExceptionEdit PlanExceptionEdit(const std::vector<bool>& isSniper);

// A raw TArray's three qwords: begin, end, and the allocation end (or, with
// bit 62 set, the inline flags whose low byte is the inline count).
struct RawArrayHeader {
    uint64_t begin = 0;
    uint64_t end = 0;
    uint64_t third = 0;

    bool operator==(const RawArrayHeader&) const = default;
};

bool IsInlineArray(const RawArrayHeader& header, size_t elementSize);

// The header of the same array holding only its first newCount elements:
// inline, the low byte of the flags; else end = begin + newCount elements.
// Empty when newCount is more than the array holds or the header is
// implausible (an end before begin, a span of partial elements).
std::optional<RawArrayHeader> ShrinkArrayHeader(const RawArrayHeader& header, size_t elementSize, size_t newCount);

// --- A keyword id inside a keyword entity ---
//
// A window of the entity's own memory (from its ZEntityImpl start to the end
// of its last property, so never a neighbor entity's) is scanned for 4-byte
// slots holding a keyword id the game registered. Skipped: the property
// storage (skip, [begin, end) offsets in the window) and both halves of any
// 8-aligned qword that looks like a pointer (a pointer's high half is a small
// number, as keyword ids are).

struct ByteRange {
    size_t begin = 0;
    size_t end = 0;
};

struct KeywordIdSlot {
    size_t offset = 0; // in the window
    int32_t id = 0;

    bool operator==(const KeywordIdSlot&) const = default;
};

// A user-mode heap or image address (0x10000 up to 2^47).
bool LooksLikePointer(uint64_t value);

std::vector<KeywordIdSlot> KeywordIdSlots(const std::vector<uint8_t>& window, const std::vector<ByteRange>& skip,
                                          const std::vector<int32_t>& registeredIds);

// The keyword id the slots name: the one distinct id among them; empty when
// there is none, or several (ambiguous: unknown).
std::optional<int32_t> PickKeywordId(const std::vector<KeywordIdSlot>& slots);

// Text that can be a keyword: letters, digits and underscores, 2 to 96 long.
bool IsKeywordLikeText(std::string_view text);

} // namespace fr
