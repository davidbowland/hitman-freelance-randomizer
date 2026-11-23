#include "doctest.h"

#include "PeImage.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kBase = 0x140000000ull;
constexpr uint64_t kHeap = 0x20000000ull;

// A fake process: a small PE32+ image (.text, .rdata, .data) and a heap.
struct FakeMemory {
    std::vector<uint8_t> image = std::vector<uint8_t>(0x6000);
    std::vector<uint8_t> heap = std::vector<uint8_t>(0x1000);

    template <class T>
    void Put(uint64_t address, T value) {
        std::memcpy(At(address), &value, sizeof(T));
    }

    void PutBytes(uint64_t address, const void* bytes, size_t size) { std::memcpy(At(address), bytes, size); }

    uint8_t* At(uint64_t address) {
        if (address >= kBase && address < kBase + image.size())
            return image.data() + (address - kBase);
        if (address >= kHeap && address < kHeap + heap.size())
            return heap.data() + (address - kHeap);
        return nullptr;
    }

    fr::MemReader Reader() {
        return [this](uint64_t address, void* out, size_t size) {
            for (const auto* region : {&image, &heap}) {
                const uint64_t start = region == &image ? kBase : kHeap;
                if (address >= start && address + size <= start + region->size()) {
                    std::memcpy(out, region->data() + (address - start), size);
                    return true;
                }
            }
            return false;
        };
    }

    void Section(uint64_t header, const char* name, uint32_t rva, uint32_t size, uint32_t characteristics) {
        char text[8] {};
        std::strncpy(text, name, 8);
        PutBytes(header, text, 8);
        Put<uint32_t>(header + 8, size);
        Put<uint32_t>(header + 12, rva);
        Put<uint32_t>(header + 36, characteristics);
    }

    // A class with RTTI: type descriptor in .data, locator and vtable in .rdata.
    uint64_t Class(const std::string& mangled, uint32_t descriptorRva, uint32_t locatorRva, uint32_t vtableRva) {
        PutBytes(kBase + descriptorRva + 16, mangled.c_str(), mangled.size() + 1);
        const uint32_t locator[6] {1, 0, 0, descriptorRva, 0, locatorRva};
        PutBytes(kBase + locatorRva, locator, sizeof(locator));
        Put<uint64_t>(kBase + vtableRva - 8, kBase + locatorRva);
        Put<uint64_t>(kBase + vtableRva, kBase + 0x1000); // a method in .text
        return kBase + vtableRva;
    }
};

FakeMemory MakeImage() {
    FakeMemory memory;
    memory.Put<uint16_t>(kBase, 0x5A4D);
    memory.Put<int32_t>(kBase + 0x3C, 0x80);
    const uint64_t nt = kBase + 0x80;
    memory.Put<uint32_t>(nt, 0x00004550);
    memory.Put<uint16_t>(nt + 6, 3);            // sections
    memory.Put<uint16_t>(nt + 20, 0xF0);        // SizeOfOptionalHeader
    memory.Put<uint16_t>(nt + 24, 0x20B);       // PE32+
    memory.Put<uint32_t>(nt + 24 + 4, 0x1000);  // SizeOfCode
    memory.Put<uint32_t>(nt + 24 + 56, 0x6000); // SizeOfImage
    const uint64_t sections = nt + 24 + 0xF0;
    memory.Section(sections, ".text", 0x1000, 0x1000, 0x60000020);
    memory.Section(sections + 40, ".rdata", 0x2000, 0x2000, 0x40000040);
    memory.Section(sections + 80, ".data", 0x4000, 0x2000, 0xC0000040);
    return memory;
}

} // namespace

TEST_CASE("pe image: PE sections and addresses") {
    FakeMemory memory = MakeImage();
    const auto image = fr::ParseImage(memory.Reader(), kBase);
    REQUIRE(image);
    CHECK(image->size == 0x6000);
    CHECK(image->sizeOfCode == 0x1000);
    REQUIRE(image->sections.size() == 3);
    CHECK(image->sections[0].name == ".text");
    CHECK(image->sections[0].executable);
    CHECK(!image->sections[1].writable);
    CHECK(image->sections[2].writable);
    CHECK(fr::DescribeAddress(*image, kBase + 0x4010) == ".data+0x10");
    CHECK(fr::DescribeAddress(*image, kHeap) == "outside the image");

    CHECK(!fr::ParseImage(memory.Reader(), kHeap)); // no MZ
}

TEST_CASE("pe image: pattern matches, an ambiguous pattern is seen") {
    FakeMemory memory = MakeImage();
    const uint8_t prologue[] {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x6B, 0x08};
    memory.PutBytes(kBase + 0x1100, prologue, sizeof(prologue));
    memory.PutBytes(kBase + 0x1800, prologue, sizeof(prologue));

    const std::string pattern("\x4C\x8B\xDC\x49\x89\x6B\x08", 7);
    const auto found = fr::FindPattern(memory.Reader(), kBase, 0x1000 + 0x1000, pattern, "xxxxxxx", 16);
    REQUIRE(found.size() == 2);
    CHECK(found[0] == kBase + 0x1100);
    CHECK(found[1] == kBase + 0x1800);

    // Wildcards.
    const auto wild =
        fr::FindPattern(memory.Reader(), kBase + 0x1000, 0x1000, std::string("\x4C\x00\xDC", 3), "x?x", 16);
    CHECK(wild.size() == 2);
    CHECK(fr::FindPattern(memory.Reader(), kBase + 0x1000, 0x1000, pattern, "xxxxxxx", 1).size() == 1);
}

TEST_CASE("pe image: RTTI names the object and finds the class's vtables") {
    FakeMemory memory = MakeImage();
    const uint64_t worldVtable = memory.Class(".?AVZWorldInventory@@", 0x4100, 0x2100, 0x2208);
    const uint64_t otherVtable = memory.Class(".?AVZSomethingElse@@", 0x4200, 0x2140, 0x2308);
    const auto image = fr::ParseImage(memory.Reader(), kBase);
    REQUIRE(image);

    // A heap ZWorldInventory, a static other object, and a pointer to the
    // heap one in .data.
    memory.Put<uint64_t>(kHeap + 0x100, worldVtable);
    memory.Put<uint64_t>(kBase + 0x4800, otherVtable);
    memory.Put<uint64_t>(kBase + 0x4900, kHeap + 0x100);

    CHECK(fr::RttiTypeName(memory.Reader(), *image, kHeap + 0x100) ==
          std::optional<std::string>(".?AVZWorldInventory@@"));
    CHECK(fr::RttiTypeName(memory.Reader(), *image, kBase + 0x4800) ==
          std::optional<std::string>(".?AVZSomethingElse@@"));
    CHECK(!fr::RttiTypeName(memory.Reader(), *image, kBase + 0x4900)); // holds a heap pointer, not a vtable
    CHECK(!fr::RttiTypeName(memory.Reader(), *image, kHeap + 0x200));  // zeroes

    const auto vtables = fr::RttiVtables(memory.Reader(), *image, fr::kWorldInventoryRtti);
    REQUIRE(vtables.size() == 1);
    CHECK(vtables[0] == worldVtable);
    CHECK(fr::RttiVtables(memory.Reader(), *image, ".?AVZNothing@@").empty());
}

TEST_CASE("pe image: choosing the world inventory") {
    using fr::InventoryCandidate;

    // The SDK's pointer is zeroed memory (no RTTI, empty list); the deref'd
    // one is the class: the class wins.
    {
        std::vector<InventoryCandidate> candidates(2);
        candidates[0].how = "Globals::WorldInventory";
        candidates[0].entries = 0;
        candidates[0].items = 0;
        candidates[1].how = "*Globals::WorldInventory";
        candidates[1].rtti = std::string(fr::kWorldInventoryRtti);
        const auto choice = fr::ChooseWorldInventory(candidates, true);
        REQUIRE(choice.index);
        CHECK(*choice.index == 1);
    }

    // RTTI in the image but no candidate is the class: none.
    {
        std::vector<InventoryCandidate> candidates(1);
        candidates[0].how = "Globals::WorldInventory";
        candidates[0].items = 40;
        CHECK(!fr::ChooseWorldInventory(candidates, true).index);
    }

    // Another class is never used, even with items.
    {
        std::vector<InventoryCandidate> candidates(1);
        candidates[0].rtti = std::string(".?AVZOther@@");
        candidates[0].items = 40;
        CHECK(!fr::ChooseWorldInventory(candidates, false).index);
    }

    // No RTTI at all: only a list holding ZHM5Items vouches.
    {
        std::vector<InventoryCandidate> candidates(2);
        candidates[0].items = 0;
        candidates[1].items = 12;
        const auto choice = fr::ChooseWorldInventory(candidates, false);
        REQUIRE(choice.index);
        CHECK(*choice.index == 1);
        candidates[1].items = -1;
        CHECK(!fr::ChooseWorldInventory(candidates, false).index);
    }
}
