#include "PeImage.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fr {

namespace {

template <class T>
bool Read(const MemReader& read, uint64_t address, T& out) {
    return read(address, &out, sizeof(T));
}

constexpr uint32_t kScnExecute = 0x20000000;
constexpr uint32_t kScnWrite = 0x80000000;
constexpr size_t kChunk = 1 << 20;

std::string Hex(uint64_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

// Calls body(chunkStart, bytes, count) over [start, start + size) in chunks
// that overlap by overlap bytes (so a match across a boundary is seen once).
template <class F>
void Chunks(const MemReader& read, uint64_t start, uint64_t size, size_t overlap, F&& body) {
    std::vector<uint8_t> buffer;
    uint64_t at = start;
    const uint64_t end = start + size;

    while (at < end) {
        const size_t count = static_cast<size_t>(std::min<uint64_t>(kChunk + overlap, end - at));
        buffer.resize(count);
        if (read(at, buffer.data(), count)) {
            if (!body(at, buffer.data(), count))
                return;
        }
        if (count <= overlap || at + count >= end)
            return;
        at += count - overlap;
    }
}

bool IsData(const ImageSection& section) {
    return !section.executable;
}

} // namespace

std::optional<ImageInfo> ParseImage(const MemReader& read, uint64_t base) {
    uint16_t mz = 0;
    if (!Read(read, base, mz) || mz != 0x5A4D)
        return std::nullopt;

    int32_t ntOffset = 0;
    if (!Read(read, base + 0x3C, ntOffset) || ntOffset <= 0 || ntOffset > 0x10000)
        return std::nullopt;

    const uint64_t nt = base + static_cast<uint64_t>(ntOffset);
    uint32_t signature = 0;
    if (!Read(read, nt, signature) || signature != 0x00004550)
        return std::nullopt;

    uint16_t sectionCount = 0;
    uint16_t optionalSize = 0;
    if (!Read(read, nt + 4 + 2, sectionCount) || !Read(read, nt + 4 + 16, optionalSize))
        return std::nullopt;

    const uint64_t optional = nt + 24;
    uint16_t magic = 0;
    if (!Read(read, optional, magic) || magic != 0x20B) // PE32+
        return std::nullopt;

    ImageInfo image;
    image.base = base;
    uint32_t sizeOfImage = 0;
    uint32_t sizeOfCode = 0;
    if (!Read(read, optional + 56, sizeOfImage) || !Read(read, optional + 4, sizeOfCode))
        return std::nullopt;
    image.size = sizeOfImage;
    image.sizeOfCode = sizeOfCode;

    uint64_t header = optional + optionalSize;
    for (uint16_t i = 0; i < sectionCount && i < 96; ++i, header += 40) {
        char name[9] {};
        uint32_t virtualSize = 0;
        uint32_t virtualAddress = 0;
        uint32_t characteristics = 0;
        if (!read(header, name, 8) || !Read(read, header + 8, virtualSize) ||
            !Read(read, header + 12, virtualAddress) || !Read(read, header + 36, characteristics))
            return std::nullopt;
        ImageSection section;
        section.name = name;
        section.start = base + virtualAddress;
        section.size = virtualSize;
        section.executable = (characteristics & kScnExecute) != 0;
        section.writable = (characteristics & kScnWrite) != 0;
        image.sections.push_back(std::move(section));
    }

    return image;
}

const ImageSection* SectionOf(const ImageInfo& image, uint64_t address) {
    for (const ImageSection& section : image.sections) {
        if (address >= section.start && address < section.start + section.size)
            return &section;
    }
    return nullptr;
}

std::string DescribeAddress(const ImageInfo& image, uint64_t address) {
    if (const ImageSection* section = SectionOf(image, address))
        return section->name + "+" + Hex(address - section->start);
    if (image.Contains(address))
        return "image+" + Hex(address - image.base);
    return "outside the image";
}

std::vector<uint64_t> FindPattern(const MemReader& read, uint64_t start, uint64_t size, std::string_view pattern,
                                  std::string_view mask, size_t max) {
    std::vector<uint64_t> found;
    const size_t length = std::min(pattern.size(), mask.size());
    if (length == 0 || max == 0)
        return found;

    Chunks(read, start, size, length - 1, [&](uint64_t at, const uint8_t* bytes, size_t count) {
        for (size_t i = 0; i + length <= count; ++i) {
            bool match = true;
            for (size_t j = 0; j < length && match; ++j)
                match = mask[j] != 'x' || bytes[i + j] == static_cast<uint8_t>(pattern[j]);
            if (!match)
                continue;
            const uint64_t address = at + i;
            if (found.empty() || found.back() != address)
                found.push_back(address);
            if (found.size() >= max)
                return false;
        }
        return true;
    });

    return found;
}

std::optional<std::string> RttiTypeName(const MemReader& read, const ImageInfo& image, uint64_t object) {
    uint64_t vtable = 0;
    if (!Read(read, object, vtable) || !image.Contains(vtable))
        return std::nullopt;

    uint64_t locator = 0;
    if (!Read(read, vtable - 8, locator) || !image.Contains(locator))
        return std::nullopt;

    uint32_t fields[6] {};
    if (!read(locator, fields, sizeof(fields)) || fields[0] != 1)
        return std::nullopt;

    // pSelf: the locator's own RVA (x64 locators are image-relative).
    if (image.base + fields[5] != locator)
        return std::nullopt;

    const uint64_t descriptor = image.base + fields[3];
    if (!image.Contains(descriptor))
        return std::nullopt;

    char name[257] {};
    if (!read(descriptor + 16, name, 256)) {
        // Near the image's end: read what fits.
        const uint64_t room = image.base + image.size - (descriptor + 16);
        if (room == 0 || room > 256 || !read(descriptor + 16, name, static_cast<size_t>(room)))
            return std::nullopt;
    }

    const std::string text(name, strnlen(name, 256));
    if (!text.starts_with(".?A"))
        return std::nullopt;
    return text;
}

std::vector<uint64_t> RttiVtables(const MemReader& read, const ImageInfo& image, std::string_view mangled) {
    std::vector<uint64_t> vtables;
    std::string needle(mangled);
    needle.push_back('\0');
    const std::string mask(needle.size(), 'x');

    // The type descriptors: the name, 16 bytes into one.
    std::vector<uint32_t> descriptorRvas;
    for (const ImageSection& section : image.sections) {
        if (!IsData(section))
            continue;
        for (const uint64_t at : FindPattern(read, section.start, section.size, needle, mask, 8)) {
            if (at >= image.base + 16)
                descriptorRvas.push_back(static_cast<uint32_t>(at - 16 - image.base));
        }
    }

    if (descriptorRvas.empty())
        return vtables;

    // The complete object locators naming one at offset 0.
    std::vector<uint64_t> locators;
    for (const ImageSection& section : image.sections) {
        if (!IsData(section) || section.writable)
            continue;
        Chunks(read, section.start, section.size, 24, [&](uint64_t at, const uint8_t* bytes, size_t count) {
            const uint64_t aligned = (at + 3) & ~uint64_t(3);
            for (size_t i = static_cast<size_t>(aligned - at); i + 24 <= count; i += 4) {
                uint32_t fields[6];
                std::memcpy(fields, bytes + i, sizeof(fields));
                if (fields[0] != 1 || fields[1] != 0)
                    continue;
                if (std::find(descriptorRvas.begin(), descriptorRvas.end(), fields[3]) == descriptorRvas.end())
                    continue;
                if (image.base + fields[5] != at + i)
                    continue;
                locators.push_back(at + i);
            }
            return true;
        });
    }

    std::sort(locators.begin(), locators.end());
    locators.erase(std::unique(locators.begin(), locators.end()), locators.end());

    // The vtables: right after a pointer to a locator.
    for (const ImageSection& section : image.sections) {
        if (!IsData(section) || section.writable)
            continue;
        Chunks(read, section.start, section.size, 8, [&](uint64_t at, const uint8_t* bytes, size_t count) {
            const uint64_t aligned = (at + 7) & ~uint64_t(7);
            for (size_t i = static_cast<size_t>(aligned - at); i + 8 <= count; i += 8) {
                uint64_t value;
                std::memcpy(&value, bytes + i, sizeof(value));
                if (std::find(locators.begin(), locators.end(), value) != locators.end())
                    vtables.push_back(at + i + 8);
            }
            return true;
        });
    }

    std::sort(vtables.begin(), vtables.end());
    vtables.erase(std::unique(vtables.begin(), vtables.end()), vtables.end());
    return vtables;
}

InventoryChoice ChooseWorldInventory(const std::vector<InventoryCandidate>& candidates, bool rttiAvailable) {
    for (size_t i = 0; i < candidates.size(); ++i) {
        const InventoryCandidate& candidate = candidates[i];
        if (candidate.vtableMatches || (candidate.rtti && *candidate.rtti == kWorldInventoryRtti))
            return {i, candidate.how + ": its RTTI says ZWorldInventory"};
    }

    if (rttiAvailable)
        return {std::nullopt, "no candidate's RTTI says ZWorldInventory"};

    for (size_t i = 0; i < candidates.size(); ++i) {
        const InventoryCandidate& candidate = candidates[i];
        if (candidate.rtti)
            continue; // another class
        if (candidate.items > 0)
            return {i, candidate.how + ": no RTTI in the image; its m_aItems holds " + std::to_string(candidate.items) +
                           " ZHM5Items"};
    }

    return {std::nullopt, "no RTTI in the image and no candidate's m_aItems holds a ZHM5Item"};
}

std::string DescribeCandidate(const InventoryCandidate& candidate) {
    std::string text = candidate.how + " " + Hex(candidate.address) + ": RTTI " +
                       (candidate.rtti ? "\"" + *candidate.rtti + "\"" : std::string("none"));
    if (candidate.vtableMatches)
        text += " (a ZWorldInventory vtable)";
    text += ", m_aItems ";
    text += candidate.entries < 0 ? std::string("unread") : std::to_string(candidate.entries) + " entries";
    if (candidate.items >= 0)
        text += " (" + std::to_string(candidate.items) + " ZHM5Items)";
    return text;
}

} // namespace fr
