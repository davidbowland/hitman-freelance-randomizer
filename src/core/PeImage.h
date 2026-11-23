#pragma once

// Pure reads of a loaded PE image (the game's exe) through a memory reader:
// its sections, byte-pattern matches and MSVC RTTI (x64). Used to check the
// SDK's ZWorldInventory pointer and ZWorldInventory::RequestNewItem pattern
// before calling them (the briefcase delivery), and to find the live
// ZWorldInventory by its class when the SDK's pointer is not it. Host-
// testable with a fake memory.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fr {

// Reads size bytes at address into out; false when unreadable.
using MemReader = std::function<bool(uint64_t address, void* out, size_t size)>;

struct ImageSection {
    std::string name;
    uint64_t start = 0; // absolute
    uint64_t size = 0;  // VirtualSize
    bool executable = false;
    bool writable = false;
};

struct ImageInfo {
    uint64_t base = 0;
    uint64_t size = 0; // SizeOfImage
    uint64_t sizeOfCode = 0;
    std::vector<ImageSection> sections;

    bool Contains(uint64_t address) const { return address >= base && address < base + size; }
};

// The PE32+ headers at base; nullopt when they are not one.
std::optional<ImageInfo> ParseImage(const MemReader& read, uint64_t base);

// The section holding address, or nullptr.
const ImageSection* SectionOf(const ImageInfo& image, uint64_t address);

// "<section>+0x<offset>" or "outside the image".
std::string DescribeAddress(const ImageInfo& image, uint64_t address);

// Addresses in [start, start + size) where pattern matches ('x' in mask: the
// byte must match; anything else: any byte), at most max of them.
std::vector<uint64_t> FindPattern(const MemReader& read, uint64_t start, uint64_t size, std::string_view pattern,
                                  std::string_view mask, size_t max);

// The MSVC RTTI type name (".?AVZWorldInventory@@") of the object at address,
// through its vtable's complete object locator; nullopt when the object has
// no vtable in the image or the locator is not one.
std::optional<std::string> RttiTypeName(const MemReader& read, const ImageInfo& image, uint64_t object);

// The vtables (absolute addresses) whose complete object locator names the
// mangled type at offset 0 (the class's primary vtable).
std::vector<uint64_t> RttiVtables(const MemReader& read, const ImageInfo& image, std::string_view mangled);

inline constexpr std::string_view kWorldInventoryRtti = ".?AVZWorldInventory@@";

// One place the live ZWorldInventory may be.
struct InventoryCandidate {
    std::string how; // "Globals::WorldInventory", ...
    uint64_t address = 0;
    std::optional<std::string> rtti; // its RTTI type name, when read
    bool vtableMatches = false;      // its vtable is a ZWorldInventory vtable
    int64_t entries = -1;            // m_aItems' size (-1: unread)
    int64_t items = -1;              // of them ZHM5Items (-1: unread)
};

struct InventoryChoice {
    std::optional<size_t> index; // into the candidates; nullopt: none may be used
    std::string why;
};

// The candidate to call ZWorldInventory::RequestNewItem on: the first whose
// RTTI says ZWorldInventory (or whose vtable is one); with no RTTI in the
// image (rttiAvailable false), the first with at least one ZHM5Item in
// m_aItems (in a mission the live one holds 47's items). Never one whose
// RTTI names another class, never one with an empty or unreadable item list
// when RTTI cannot vouch for it.
InventoryChoice ChooseWorldInventory(const std::vector<InventoryCandidate>& candidates, bool rttiAvailable);

std::string DescribeCandidate(const InventoryCandidate& candidate);

} // namespace fr
