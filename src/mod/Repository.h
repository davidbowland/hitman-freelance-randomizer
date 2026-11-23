#pragma once

#include "Guid.h"

#include <Glacier/ZResource.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

class ZString;

// The "Value" field of one pro.repo entry.
enum class RepoValueKind : uint8_t {
    None,    // the entry is missing, not a key-value object, or has no "Value" key
    Float32, // written as float32
    Float64, // written as float64
    Other,   // some other type: never written
};

// One GUID looked up in pro.repo. valueData and entry point into engine
// memory and are used only right after the lookup that produced them.
struct RepoLookup {
    bool entryFound = false;    // the GUID is a key of pro.repo
    bool entryIsObject = false; // its value is a key-value object
    bool hasValue = false;      // the object has a "Value" key
    RepoValueKind kind = RepoValueKind::None;
    char typeName[48] = {}; // engine type name of "Value", when hasValue
    double value = 0.0;     // when kind is Float32 or Float64
    void* valueData = nullptr;
    bool hasToggle = false; // a bool "Toggle" key
    bool toggle = false;
    bool* toggleData = nullptr;  // when hasToggle
    const void* entry = nullptr; // the entry's ZDynamicObject
};

// One pro.repo entry with a "Parameter" key whose value is a ZString, copied
// out of engine memory by ProRepository::ScanParameters. valueData points into
// engine memory and is used only right after the scan that produced it.
struct RepoParamEntry {
    fr::Guid guid;
    char parameter[96]; // the Parameter text (cut to fit; see parameterCut)
    bool parameterCut;  // the text was longer than the buffer
    bool hasValue;      // a "Value" key
    RepoValueKind kind; // of Value
    double value;       // when kind is Float32 or Float64
    void* valueData;    // when kind is Float32 or Float64
    char valueType[40]; // engine type name of Value
    bool hasToggle;     // a bool "Toggle"
    bool toggle;
    bool* toggleData; // when hasToggle
};

// One pro.repo item (an entry with an "ItemType" or "InventoryCategoryIcon"
// key), copied out of engine memory by ProRepository::ScanItems. A text that
// is not a ZString holds its engine type name in angle brackets.
struct RepoItemEntry {
    fr::Guid guid;
    char title[80];
    char icon[40]; // InventoryCategoryIcon
    char itemType[48];
};

// One pro.repo entry with an "Evergreen_Rarity" key (the weapon rarity
// shuffle), copied out of engine memory by ProRepository::ScanRarityItems. The
// value pointers point into engine memory and are used only right after the
// scan that produced them. Texts that are not ZStrings are left empty.
struct RepoRarityEntry {
    fr::Guid guid;
    char title[80];           // Title, else CommonName
    char icon[40];            // InventoryCategoryIcon
    char audioHead[48];       // AudioHeadType
    char audioTail[48];       // AudioTailType
    char audioExotic[48];     // AudioExoticType ("eWBC_AudioExoticType_TranquilizerDartPistol": a dart gun)
    RepoValueKind rarityKind; // of Evergreen_Rarity
    double rarity;
    void* rarityData;
    RepoValueKind capacityKind; // of Evergreen_CapacityCost (None when absent)
    double capacity;
    void* capacityData;
};

// One pro.repo item for the random loadout (an entry with an
// "Evergreen_CapacityCost", "InventoryCategoryIcon" or "OnlineTraits" key),
// copied out of engine memory by ProRepository::ScanLoadoutItems. entry
// points into engine memory and is used only right after the scan (DumpPairs).
// Texts that are not ZStrings are left empty.
struct RepoLoadoutEntry {
    fr::Guid guid;
    char title[80]; // Title, else CommonName
    char commonName[80];
    char icon[40];        // InventoryCategoryIcon
    char itemType[48];    // ItemType
    char audioHead[48];   // AudioHeadType
    char audioTail[48];   // AudioTailType
    char audioExotic[48]; // AudioExoticType (a dart gun, fr::IsDartGun)
    char traits[160];     // OnlineTraits, comma-separated (cut to fit)
    char perks[96];       // Perks, comma-separated (cut to fit): "suppressor", "piercing,damage", ...
    char itemSize[48];    // ItemSize, if the key exists and is a string
    char grip[48];        // GripAnimType, if a string ("IGT_Melee_1H_Stick": fr::JudgeCrateItemFit)
    char holster[48];     // HolsterAbility, if the key exists and is a string
    char runtime[24];     // Runtime (the item's runtime resource id in decimal), if a string
    char entitlement[48]; // Evergreen_UnlockEntitlement, if a string (empty: none needed)
    int8_t loseOnWounded; // Evergreen_LoseOnWounded: 1 true, 0 false, -1 missing or not a bool
    bool hasCapacity;
    double capacity; // Evergreen_CapacityCost (float32/float64)
    bool hasRarity;
    double rarity; // Evergreen_Rarity
    const void* entry;
};

// One pro.repo item (an entry with an "InventoryCategoryIcon" key) for the
// concealable mace, copied out of engine memory by
// ProRepository::ScanConcealItems. The ZString pointers point into engine
// memory (the entry's ItemSize / HolsterAbility values, null when the key is
// missing or not a ZString) and are used only right after the scan.
struct RepoConcealEntry {
    fr::Guid guid;
    char title[80];
    char commonName[80];
    char icon[40];     // InventoryCategoryIcon
    char itemSize[48]; // ItemSize (a ZString), else empty
    char holster[48];  // HolsterAbility (a ZString), else empty
    int8_t canConceal; // HitmanCanConceal: 1 true, 0 false, -1 missing or not a bool
    int8_t frisked;    // IsCheckedWhenFrisked: the same
    ZString* itemSizeValue;
    ZString* holsterValue;
};

// One key of an entry and its value as short text, copied out by
// ProRepository::DumpPairs.
struct RepoPairText {
    char key[48];
    char value[72];
};

// Short text for a lookup that cannot be written: "not in pro.repo",
// "entry is not an object", "no Value key", "Value is <type>". Empty when
// it can be written.
std::string RepoUnwritableReason(const RepoLookup& p_Lookup);

// Guarded access to the loaded repository ([assembly:/repository/pro.repo].pc_repo):
// the resource data is a THashMap<ZRepositoryID, ZDynamicObject>, each entry a
// TArray<SDynamicObjectKeyValuePair>.
// Every engine read and write goes through a guarded step (Guard.h).
class ProRepository {
public:
    // Returned by Lookup and every Scan* pass (in place of an exception code)
    // when a pass visited more nodes than the map holds plus one
    // (fr::RepoPassOverran) and stopped: the map's links are broken.
    static constexpr uint32_t kOverrunCode = 0xE0F70001;

    // True when pro.repo is loaded (acquiring the resource the first time);
    // otherwise false with the reason in p_Why.
    bool Ready(std::string& p_Why);

    // Number of entries, or 0 when not ready or unreadable.
    size_t EntryCount();

    // One pass over every entry; p_Out[i] describes p_Keys[i]. Returns 0, or
    // the structured-exception code when the pass faulted (p_Out is then
    // partial). Call Ready first.
    uint32_t Lookup(std::span<const fr::Guid> p_Keys, std::vector<RepoLookup>& p_Out);

    // Writes p_Value into a looked-up Float32/Float64 "Value" (as that type).
    // Returns 0, or the fault code; nothing is written for any other kind.
    uint32_t Write(const RepoLookup& p_Lookup, double p_Value);
    uint32_t Write(const RepoParamEntry& p_Entry, double p_Value);

    // Writes p_Value into a looked-up or scanned bool "Toggle" (footstep
    // hearing: a difficulty entity's m_bToggle is built from it). Returns 0,
    // or the fault code; nothing is written when the entry has no Toggle.
    uint32_t WriteToggle(const RepoLookup& p_Lookup, bool p_Value);
    uint32_t WriteToggle(const RepoParamEntry& p_Entry, bool p_Value);

    // One pass over every entry, collecting each key-value entry that has a
    // ZString "Parameter" (at most kMaxParamEntries; the rest are counted in
    // p_Overflow). p_Scanned gets the number of entries visited. Returns 0, or
    // the fault code (p_Out then holds what was collected before the fault).
    // Call Ready first.
    static constexpr size_t kMaxParamEntries = 16'384;
    uint32_t ScanParameters(std::vector<RepoParamEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned);

    // One pass over every entry, collecting each item (at most
    // kMaxItemEntries; the rest are counted in p_Overflow). p_Scanned gets the
    // number of entries visited. Returns 0, or the fault code (p_Out then holds
    // what was collected before the fault). Call Ready first.
    static constexpr size_t kMaxItemEntries = 16'384;
    uint32_t ScanItems(std::vector<RepoItemEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned);

    // One pass over every entry, collecting each key-value entry with an
    // "Evergreen_Rarity" key (at most kMaxItemEntries; the rest are counted in
    // p_Overflow). Same return and contract as ScanItems.
    uint32_t ScanRarityItems(std::vector<RepoRarityEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned);

    // One pass over every entry, collecting each item of RepoLoadoutEntry's
    // kind (at most kMaxItemEntries). Same return and contract as ScanItems.
    uint32_t ScanLoadoutItems(std::vector<RepoLoadoutEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned);

    // Every key of an entry found by ScanLoadoutItems (at most
    // kMaxDumpPairs) with its value as text: strings, bools, numbers, arrays
    // of strings joined with commas, other arrays and objects as their size.
    // Returns 0, or the fault code (p_Out then holds what was read).
    static constexpr size_t kMaxDumpPairs = 192;
    uint32_t DumpPairs(const void* p_Entry, std::vector<RepoPairText>& p_Out);

    // Writes p_Value into a float32/float64 value found by a scan (as that
    // type). Returns 0, or the fault code; nothing is written for other kinds.
    uint32_t WriteValue(void* p_Data, RepoValueKind p_Kind, double p_Value);

    // One pass over every entry, collecting each entry with an
    // "InventoryCategoryIcon" key (at most kMaxItemEntries). Same return and
    // contract as ScanItems.
    uint32_t ScanConcealItems(std::vector<RepoConcealEntry>& p_Out, size_t* p_Overflow, size_t* p_Scanned);

    // *p_To = *p_From with ZString's own copy (the source's refcount taken,
    // the old text's released), guarded: either may be engine memory (a
    // pro.repo value from ScanConcealItems) or a ZString this mod holds.
    // Returns 0, or the fault code; nothing is done when either is null.
    uint32_t CopyString(ZString* p_To, const ZString* p_From);

private:
    void* MapPointer(); // the resource data, or nullptr

    ZResourcePtr m_Resource;
    bool m_LoggedFault = false;
};
