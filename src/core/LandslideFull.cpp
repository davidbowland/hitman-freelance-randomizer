#include "LandslideFull.h"

#include "Scene.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace fr {

namespace {

// Missing dependencies named in the log line.
constexpr size_t kMaxNamed = 8;

std::string Hex(uint64_t p_Value) {
    char s_Text[17];
    std::snprintf(s_Text, sizeof(s_Text), "%016llX", static_cast<unsigned long long>(p_Value));
    return s_Text;
}

bool SameNoCase(std::string_view p_A, std::string_view p_B) {
    return p_A.size() == p_B.size() && ToLower(p_A) == ToLower(p_B);
}

LandslideVerdict Block(LandslideBlock p_Block, std::string p_Why) {
    return {p_Block, std::move(p_Why)};
}

LandslideVerdict Fault(const ResourceSeen& p_Seen, uint64_t p_Rid) {
    char s_Code[16];
    std::snprintf(s_Code, sizeof(s_Code), "0x%08X", p_Seen.fault);
    return Block(LandslideBlock::ReadFault,
                 "reading resource " + Hex(p_Rid) + " from the resource container faulted (" + s_Code + ")");
}

std::string_view Trim(std::string_view p_Text) {
    while (!p_Text.empty() && (p_Text.front() == ' ' || p_Text.front() == '\t' || p_Text.front() == '\r'))
        p_Text.remove_prefix(1);

    while (!p_Text.empty() && (p_Text.back() == ' ' || p_Text.back() == '\t' || p_Text.back() == '\r'))
        p_Text.remove_suffix(1);

    return p_Text;
}

} // namespace

std::string_view PackageFileName(std::string_view p_Package) {
    const size_t s_Slash = p_Package.find_last_of("/\\");
    return s_Slash == std::string_view::npos ? p_Package : p_Package.substr(s_Slash + 1);
}

std::optional<int> PackagePatchNumber(std::string_view p_Package) {
    const std::string s_Name = ToLower(PackageFileName(p_Package));
    constexpr std::string_view kPrefix = "chunk";
    constexpr std::string_view kSuffix = ".rpkg";

    if (s_Name.size() <= kPrefix.size() + kSuffix.size() || s_Name.compare(0, kPrefix.size(), kPrefix) != 0 ||
        s_Name.compare(s_Name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0)
        return std::nullopt;

    const std::string_view s_Rest =
        std::string_view(s_Name).substr(kPrefix.size(), s_Name.size() - kPrefix.size() - kSuffix.size());
    size_t i = 0;

    while (i < s_Rest.size() && s_Rest[i] >= '0' && s_Rest[i] <= '9')
        ++i;

    if (i == 0 || i > 4)
        return std::nullopt;

    if (i == s_Rest.size())
        return 0;

    constexpr std::string_view kPatch = "patch";
    const std::string_view s_Number = s_Rest.substr(i);

    if (s_Number.size() <= kPatch.size() || s_Number.substr(0, kPatch.size()) != kPatch ||
        s_Number.size() > kPatch.size() + 6)
        return std::nullopt;

    int s_Value = 0;

    for (const char c : s_Number.substr(kPatch.size())) {
        if (c < '0' || c > '9')
            return std::nullopt;

        s_Value = s_Value * 10 + (c - '0');
    }

    return s_Value;
}

FullAvailability JudgeFullAvailability(const FullAvailabilityInputs& p_In) {
    if (p_In.switchedOff)
        return FullAvailability::SwitchedOff;

    if (!p_In.builtIn || (!p_In.buildable && !p_In.readFailed))
        return FullAvailability::NotBuildable;

    if (!p_In.buildable || !p_In.installed)
        return FullAvailability::NotInstalled;

    return FullAvailability::Available;
}

FullAvailability FullOptionAvailability(std::string_view p_Option, const FullAvailabilityOf& p_Of) {
    for (const FullVariant* s_Variant : FullVariants()) {
        if (p_Option.empty() || s_Variant->option != p_Option)
            continue;

        if (const FullAvailability s_Why = p_Of(*s_Variant); s_Why != FullAvailability::Available)
            return s_Why;
    }

    return FullAvailability::Available;
}

LandslideVerdict CheckFullGate(const FullVariant& p_Variant, const LandslideGate& p_Gate) {
    if (!p_Gate.builtIn)
        return Block(LandslideBlock::NotBuiltIn, "this DLL has no recipe for its package");

    if (p_Gate.switchedOff)
        return Block(LandslideBlock::SwitchedOff, "Full map variants is off (full_map_variants = false)");

    if (!p_Gate.buildable)
        return Block(LandslideBlock::NotBuildable, "its package can't be built from your game files (see the \"" +
                                                       PackageLogName(p_Variant.partition, p_Variant.partitionId) +
                                                       ":\" lines at game start)");

    if (p_Gate.turnedOff)
        return Block(LandslideBlock::TurnedOff,
                     "it was turned off because " + std::string(p_Variant.aLoad) +
                         " did not finish (crash or quit while loading); turn it back on in Settings to try again");

    if (!p_Gate.installed)
        return Block(LandslideBlock::NotInstalled,
                     "the package was not installed at game start (see the \"landslide package:\" lines there)");

    if (!p_Gate.maxPatchLevel) {
        const std::string s_Partition = std::to_string(p_Variant.partition);

        if (!p_Gate.partitionSeen)
            return Block(LandslideBlock::NotRaised,
                         "the partition hook never saw " + std::string(p_Variant.location) + "'s partition " +
                             s_Partition +
                             " (the mod loaded after the game created its partitions?), so the package is not "
                             "mounted");

        if (p_Gate.raisedTo < p_Gate.target)
            return Block(LandslideBlock::NotRaised,
                         std::string(p_Variant.location) + "'s partition was created with patch level " +
                             std::to_string(p_Gate.raisedTo) + ", below " + std::to_string(p_Gate.target) +
                             ", so the package is not mounted (see the \"" +
                             PackageLogName(p_Variant.partition, p_Variant.partitionId) + ": partition " + s_Partition +
                             "\" line at game start)");
    }

    if (!p_Gate.freelancerLayer && !p_Gate.forced)
        return Block(LandslideBlock::TestOnly,
                     "this build's package has no Freelancer layer yet (no targets), so it is used only when " +
                         std::string(p_Variant.display) +
                         " is forced (variant_force = " + std::string(p_Variant.option) + ")");

    return {};
}

ContractVerdict CheckFullContract(const FullVariant& p_Variant, std::span<const std::string> p_Bricks) {
    ContractVerdict s_Out;
    size_t s_Mission = 0;
    bool s_Other = false;

    for (const std::string& s_Brick : p_Bricks) {
        if (SameNoCase(s_Brick, p_Variant.contractBrick))
            ++s_Mission;
        else if (std::any_of(p_Variant.droppable.begin(), p_Variant.droppable.end(),
                             [&](std::string_view p_Drop) { return SameNoCase(s_Brick, p_Drop); }))
            s_Out.dropped.push_back(s_Brick);
        else
            s_Other = true;
    }

    if (s_Mission == 1 && !s_Other && s_Out.dropped.size() <= 1)
        return s_Out;

    std::string s_List;

    for (const std::string& s_Brick : p_Bricks)
        s_List += (s_List.empty() ? "" : ", ") + s_Brick;

    std::string s_Expected = "[" + std::string(p_Variant.contractBrick) + "]";

    if (!p_Variant.droppable.empty()) {
        s_Expected += " (with at most one of ";

        for (size_t i = 0; i < p_Variant.droppable.size(); ++i)
            s_Expected += (i == 0 ? "" : ", ") + std::string(p_Variant.droppable[i]);

        s_Expected += ")";
    }

    s_Out.dropped.clear();
    s_Out.verdict = Block(LandslideBlock::Contract, "the contract lists [" + s_List + "] instead of " + s_Expected);
    return s_Out;
}

std::string DroppedBrickNote(std::string_view p_Brick, std::string_view p_Why) {
    // The brick's file name without its folder and extension.
    std::string_view s_Name =
        p_Brick.substr(p_Brick.find_last_of("/\\") == std::string_view::npos ? 0 : p_Brick.find_last_of("/\\") + 1);

    if (s_Name.size() > 6 && ToLower(s_Name.substr(s_Name.size() - 6)) == ".brick")
        s_Name.remove_suffix(6);

    const std::string s_Lower = ToLower(s_Name);
    const std::string_view s_Kind = s_Lower.find("_pro1") != std::string::npos    ? "hard-difficulty brick"
                                    : s_Lower.find("_easy_") != std::string::npos ? "easy-difficulty brick"
                                                                                  : "extra brick";
    return "the contract's " + std::string(s_Kind) + " (" + std::string(s_Name) +
           ") is left out: " + std::string(p_Why);
}

std::string PackageLogName(int p_Partition, std::string_view p_PartitionId) {
    return p_Partition == 26 ? std::string("landslide package") : std::string(p_PartitionId) + " package";
}

std::string FullPackageFileName(int p_Partition, int p_Patch) {
    return "chunk" + std::to_string(p_Partition) + "patch" + std::to_string(p_Patch) + ".rpkg";
}

LandslideVerdict CheckOwnResources(std::span<const uint64_t> p_Own, const ResourceLookup& p_Lookup,
                                   std::string_view p_OwnFile) {
    for (const uint64_t s_Rid : p_Own) {
        const ResourceSeen s_Seen = p_Lookup(s_Rid);

        if (s_Seen.fault)
            return Fault(s_Seen, s_Rid);

        if (!s_Seen.found)
            return Block(LandslideBlock::OwnResource, "the package's resource " + Hex(s_Rid) +
                                                          " is not in the resource container (" +
                                                          std::string(p_OwnFile) + " not mounted?)");

        if (!SameNoCase(PackageFileName(s_Seen.package), p_OwnFile))
            return Block(LandslideBlock::OwnResource, "the package's resource " + Hex(s_Rid) + " loads from " +
                                                          std::string(PackageFileName(s_Seen.package)) +
                                                          " instead of " + std::string(p_OwnFile));
    }

    return {};
}

LandslideVerdict CheckDependencies(std::span<const uint64_t> p_Dependencies, const ResourceLookup& p_Lookup) {
    size_t s_Missing = 0;
    std::string s_Named;

    for (const uint64_t s_Rid : p_Dependencies) {
        const ResourceSeen s_Seen = p_Lookup(s_Rid);

        if (s_Seen.fault)
            return Fault(s_Seen, s_Rid);

        if (s_Seen.found)
            continue;

        if (++s_Missing <= kMaxNamed)
            s_Named += (s_Named.empty() ? "" : ", ") + Hex(s_Rid);
    }

    if (s_Missing == 0)
        return {};

    return Block(LandslideBlock::Dependency,
                 "a game update removed resources the package uses (" + std::to_string(s_Missing) + " of the " +
                     std::to_string(p_Dependencies.size()) + " it references are missing: " + s_Named +
                     (s_Missing > kMaxNamed ? ", ..." : "") + ")");
}

LandslideVerdict CheckFingerprints(std::span<const LandslideFingerprint> p_Fingerprints, const ResourceLookup& p_Lookup,
                                   std::string_view p_OwnFile, std::string* p_Summary) {
    for (const LandslideFingerprint& s_Print : p_Fingerprints) {
        const ResourceSeen s_Seen = p_Lookup(s_Print.rid);

        if (s_Seen.fault)
            return Fault(s_Seen, s_Print.rid);

        const std::string s_Name(s_Print.name);
        const std::string s_Built = std::to_string(s_Print.size) + " bytes in " + std::string(s_Print.package);

        if (!s_Seen.found)
            return Block(LandslideBlock::GameChanged, "a game update changed " + s_Name + " (" + s_Built +
                                                          " when this mod was made, no longer in the game)");

        const std::string_view s_File = PackageFileName(s_Seen.package);
        const std::optional<int> s_Patch = PackagePatchNumber(s_File);

        if (s_Patch && *s_Patch >= kFirstModPatch && !SameNoCase(s_File, p_OwnFile))
            return Block(LandslideBlock::OtherMod, "another mod's package (" + std::string(s_File) + ") replaces " +
                                                       s_Name + " (" + std::to_string(s_Seen.size) + " bytes; " +
                                                       s_Built + " when this mod was made)");

        if (s_Seen.size != s_Print.size)
            return Block(LandslideBlock::GameChanged,
                         "a game update changed " + s_Name + " (" + s_Built + " when this mod was made, now " +
                             std::to_string(s_Seen.size) + " in " + std::string(s_File) + ")");

        if (p_Summary) {
            *p_Summary += (p_Summary->empty() ? "" : ", ") + s_Name + " " + std::to_string(s_Seen.size) + " " +
                          std::string(s_File);

            if (!SameNoCase(s_File, s_Print.package))
                *p_Summary += " (built against " + std::string(s_Print.package) + ")";
        }
    }

    return {};
}

std::string FullOverlay(const FullVariant& p_Variant, LandslideBlock p_Block) {
    const std::string s_Name(p_Variant.display);
    std::string s_Why;

    switch (p_Block) {
        case LandslideBlock::None: return s_Name;
        case LandslideBlock::NotBuiltIn:
        case LandslideBlock::TestOnly: s_Why = s_Name + " is not in this build of the mod"; break;
        case LandslideBlock::SwitchedOff: return "normal map (full map variants are off in Settings)";
        case LandslideBlock::NotBuildable: s_Why = s_Name + " doesn't work with your version of the game yet"; break;
        case LandslideBlock::TurnedOff:
            s_Why = s_Name + " is off after a load that did not finish; try it again in Settings";
            break;
        case LandslideBlock::Contract:
        case LandslideBlock::Dependency:
        case LandslideBlock::GameChanged:
            s_Why = s_Name + " could not be loaded: the game changed since this mod was made";
            break;
        case LandslideBlock::OtherMod:
            s_Why = s_Name + " could not be loaded: another mod changes " + std::string(p_Variant.location);
            break;
        case LandslideBlock::NotInstalled:
        case LandslideBlock::NotRaised:
        case LandslideBlock::NotMounted:
        case LandslideBlock::ReadFault:
        case LandslideBlock::OwnResource: s_Why = s_Name + " could not be loaded"; break;
    }

    return "normal map (" + s_Why + ")";
}

std::string LandslideBuildId(std::string_view p_Version, std::span<const uint8_t> p_Package) {
    // FNV-1a, 32 bits: enough to tell two builds' packages apart.
    uint32_t s_Hash = 2166136261u;

    for (const uint8_t s_Byte : p_Package) {
        s_Hash ^= s_Byte;
        s_Hash *= 16777619u;
    }

    char s_Text[9];
    std::snprintf(s_Text, sizeof(s_Text), "%08x", s_Hash);
    return std::string(p_Version) + "+" + s_Text;
}

std::string LandslideSentinelText(std::string_view p_Build, std::string_view p_Time, std::string_view p_Scene) {
    return "build " + std::string(p_Build) + "\nwritten " + std::string(p_Time) + "\nscene " + std::string(p_Scene) +
           "\n";
}

std::string LandslideSentinelBuild(std::string_view p_Text) {
    constexpr std::string_view kKey = "build";

    while (!p_Text.empty()) {
        const size_t s_End = std::min(p_Text.find('\n'), p_Text.size());
        const std::string_view s_Line = Trim(p_Text.substr(0, s_End));
        p_Text.remove_prefix(std::min(s_End + 1, p_Text.size()));

        if (s_Line.size() > kKey.size() && s_Line.substr(0, kKey.size()) == kKey &&
            (s_Line[kKey.size()] == ' ' || s_Line[kKey.size()] == '\t'))
            return std::string(Trim(s_Line.substr(kKey.size())));
    }

    return {};
}

SentinelAction JudgeSentinel(const std::string* p_Text, bool p_Readable, std::string_view p_Build) {
    if (!p_Text && p_Readable)
        return SentinelAction::None;

    if (!p_Readable)
        return SentinelAction::TurnOff;

    const std::string s_Build = LandslideSentinelBuild(*p_Text);

    // A sentinel without a build line (cut by the crash) counts as this build's.
    return s_Build.empty() || s_Build == p_Build ? SentinelAction::TurnOff : SentinelAction::Discard;
}

namespace {

constexpr std::array<LandslideNeverTarget, 1> kNeverTargets {{
    {"Marco Abiatti", "853b4070-99b1-45da-938b-6fa7947eb7cd", 0x33160A4D0FD92760ull}, // NPC/MainCharacters/Target
}};

// The full Author's: Craig Black, The Author's own target (the Author
// contract's target ab48a89d-...; his story ends in Event_Target_Escape after
// "Contemplating moon"). His entourage, Brother Akram (the contract's other
// kill, a setpiece of its own) and the cult may be targets.
constexpr std::array<LandslideNeverTarget, 1> kAuthorNeverTargets {{
    {"Craig Black", "ab48a89d-e8a7-4df4-ae72-f0fccaa65e7f", 0xC7CB1D860F5AB7BEull}, // NPC/Writer Main Loop
}};

// The full Icon's: Dino Bosco, The Icon's own target (his script runs the
// shoot, the Q&A, the VIP evacuation and the accident kills), and Sophia
// Wilde, his agent, scripted into his Q&A (mission_copperhead
// NPC_Target and Agent). The rest of the cast may be targets.
constexpr std::array<LandslideNeverTarget, 2> kIconNeverTargets {{
    {"Dino Bosco", "9c81b545-a234-46c5-9296-54f9f2a0bb6d", 0x0C285FADF7D81D2Dull}, // CHAR_Unique_LeadActor_Red
    {"Sophia Wilde", "005e3ab8-508c-471b-8426-53aca5c6ef4b",
     0x9C3AA306C142E51Bull}, // CHAR_Worker_MovieCrew_Female_Agent
}};

// Sapienza's difficulty bricks: each full version's override brick no longer
// loads octopus_pro1's day-level changes, so the swap leaves them out.
constexpr std::array<std::string_view, 2> kSapienzaDroppable {kSapienzaEasyBrick, kSapienzaHardBrick};

constexpr FullVariant kLandslideFull {
    "landslide",
    "Landslide",
    "landslide",
    "Sapienza mild",
    kLandslideScene,
    "a Landslide load",
    "the full Landslide",
    "Landslide is off: a Landslide mission did not finish loading last time. Until you try again, "
    "Sapienza missions that pick Landslide play the normal map.",
    kNeverTargets,
    false,
    26,
    "coastaltown",
    "Sapienza",
    kLandslideContractBrick,
    kSapienzaDroppable,
};

constexpr FullVariant kAuthorFull {
    "author",
    "The Author",
    "tod_ebola_night",
    "Sapienza mild",
    kAuthorScene,
    "a load of The Author",
    "the full Author",
    "The Author is off: a mission of The Author did not finish loading last time. Until you try again, "
    "Sapienza missions that pick The Author play the normal map.",
    kAuthorNeverTargets,
    true,
    26,
    "coastaltown",
    "Sapienza",
    kLandslideContractBrick,
    kSapienzaDroppable,
};

// The Icon's set has its own crowd (its NPCs and the night street crowd; none
// of mild Sapienza's day crowd), so it never shares the contract scene's
// mover rows.
constexpr FullVariant kIconFull {
    "icon",
    "The Icon",
    "lighting_copperhead",
    "Sapienza mild",
    kIconScene,
    "a load of The Icon",
    "the full Icon",
    "The Icon is off: a mission of The Icon did not finish loading last time. Until you try again, "
    "Sapienza missions that pick The Icon play the normal map.",
    kIconNeverTargets,
    false,
    26,
    "coastaltown",
    "Sapienza",
    kLandslideContractBrick,
    kSapienzaDroppable,
};

// The full Marrakesh night's: A House Built on Sand's two targets, Kong
// Tuo-Kwang (the Chinese contractor) and Matthieu Mendola (the architect),
// whose story lives in the night level's base (mission_marrakesh_python).
// The rest of the night's people (their bodyguards included) may be targets.
constexpr std::array<LandslideNeverTarget, 2> kMarrakeshNeverTargets {{
    {"Kong Tuo-Kwang", "23415c1d-6ee8-41a8-9975-523f3eede581", 0x57E229B4E7543CAAull},   // NPC (Contractor)
    {"Matthieu Mendola", "4d0953f0-7f19-4130-abae-610a8f5dddca", 0x67F6C22407232163ull}, // NPC (Employee)
}};

// The full Bad Boy's: The Icon's two (its story still runs under him) and
// Bartholomew Argus, The Bad Boy (the contract's one Kill objective,
// SAPIENZA/ELUSIVE/THEBADBOY.json), whose own story (his escape) runs in
// mission_caipirinha (his entity id there, as the layer check of
// tools/landslide/badboy_spec.py printed it). His people may be targets.
constexpr std::array<LandslideNeverTarget, 3> kBadBoyNeverTargets {{
    {"Dino Bosco", "9c81b545-a234-46c5-9296-54f9f2a0bb6d", 0x0C285FADF7D81D2Dull},
    {"Sophia Wilde", "005e3ab8-508c-471b-8426-53aca5c6ef4b", 0x9C3AA306C142E51Bull},
    {"Bartholomew Argus", "ab3fe438-acd0-49a4-a0f4-690c86a10324", 0x30685A26D9C1F190ull}, // mission_caipirinha
}};

constexpr std::array<std::string_view, 2> kMarrakeshDroppable {kMarrakeshEasyBrick, kMarrakeshHardBrick};

// The night level has its own crowds (the night market, the shisha café and
// mission_marrakesh_python's people), so it uses only its own mover rows. Its
// package is Marrakesh's (partition 25). No option of its own (the plain
// marrakesh_night option was removed): it is never picked, only the base of
// the full Entertainer (its scene, package bricks, mover rows and scene
// alias), so its gate, sentinel and off note never come up.
constexpr FullVariant kMarrakeshFull {
    "marrakesh",
    "Marrakesh at night",
    {},
    "Marrakesh mild",
    kMarrakeshScene,
    "a Marrakesh night load",
    "the full Marrakesh night",
    "Marrakesh at night is off: a Marrakesh night mission did not finish loading last time. Until you try "
    "again, Marrakesh missions that pick the night play the normal map.",
    kMarrakeshNeverTargets,
    false,
    25,
    "marrakesh",
    "Marrakesh",
    kMarrakeshContractBrick,
    kMarrakeshDroppable,
};

// The Bad Boy on The Icon's set: the full Icon (its scene and package bricks)
// with mission_caipirinha appended: The Icon's crowd plus Argus's people. Its
// mover rows are the scene's rows tagged with its key ("badboy",
// fr::MoverTableVariants), apart from the Icon's untagged ones; the Icon's
// apply until it has its own.
constexpr FullVariant kBadBoyFull {
    "badboy",
    "The Bad Boy",
    "the_bad_boy",
    "Sapienza mild",
    kIconScene,
    "a load of The Bad Boy",
    "the full Bad Boy",
    "The Bad Boy is off: a mission of The Bad Boy did not finish loading last time. Until you try again, "
    "Sapienza missions that pick The Bad Boy play the normal map.",
    kBadBoyNeverTargets,
    false,
    26,
    "coastaltown",
    "Sapienza",
    kLandslideContractBrick,
    kSapienzaDroppable,
    "icon",
};

// The full Patient Zero's: the outbreak's two named targets, Owen Cage and
// Klaus Liebleid ("patient zero"; the campaign contract's Kill objectives,
// HOKKAIDO/PATIENTZERO.json), whose story lives in mission_flu (their entity
// ids there, as tools/landslide/patientzero_spec.py has them). The infected
// are picked by the outbreak's story at run time; the full version switches
// that story off (IOI's Contracts-mode brick contractcreation_flu, loaded by
// the package's override brick), so nobody is infected. The rest of the
// outbreak's people (biosuit staff, doctors, quarantine guards) may be
// targets.
constexpr std::array<LandslideNeverTarget, 2> kPatientZeroNeverTargets {{
    {"Owen Cage", "3798fcf3-4e33-40db-a8a2-c160a3ec55bf", 0x1C3B8BC862DB9082ull},      // mission_flu
    {"Klaus Liebleid", "01822ba2-7b3b-4bbd-a9f3-cf8006ba945a", 0x01432E3710A37DA0ull}, // mission_flu
}};

constexpr std::array<std::string_view, 2> kHokkaidoDroppable {kHokkaidoEasyBrick, kHokkaidoHardBrick};

// The outbreak keeps the normal hotel's people (npc_mission_snowcrane), so
// mild Hokkaido's mover rows apply until it has rows of its own. Its package
// is Hokkaido's (partition 22).
constexpr FullVariant kPatientZeroFull {
    "patientzero",
    "Patient Zero",
    "patient_zero",
    "Hokkaido mild",
    kPatientZeroScene,
    "a Patient Zero load",
    "the full Patient Zero",
    "Patient Zero is off: a Patient Zero mission did not finish loading last time. Until you try again, "
    "Hokkaido missions that pick Patient Zero play the normal map.",
    kPatientZeroNeverTargets,
    true,
    22,
    "hokkaido",
    "Hokkaido",
    kHokkaidoContractBrick,
    kHokkaidoDroppable,
    {},
    "it was made for the normal hospital, not the outbreak",
};

// The full Entertainer's: the night's two (A House Built on Sand's story
// still runs under him) and the elusive target, "Philip Giggles", The
// Entertainer's one Kill objective (MARRAKESH/ELUSIVE/THEENTERTAINER.json),
// whose story runs in mission_marrakesh_maitai (his entity id there, from its
// JSON: entertainer_spec.py). His bodyguards and the café's people may be
// targets.
constexpr std::array<LandslideNeverTarget, 3> kEntertainerNeverTargets {{
    {"Kong Tuo-Kwang", "23415c1d-6ee8-41a8-9975-523f3eede581", 0x57E229B4E7543CAAull},
    {"Matthieu Mendola", "4d0953f0-7f19-4130-abae-610a8f5dddca", 0x67F6C22407232163ull},
    {"The Entertainer", "e63259c4-1a82-4299-9633-6ac54991e1e9", 0x28433E998F0F6321ull}, // mission_marrakesh_maitai
}};

// The Entertainer at night: the full Marrakesh night (its scene and package
// bricks) with mission_marrakesh_maitai appended: the night's crowds plus his
// people. Its mover rows are the scene's rows tagged "entertainer"
// (fr::MoverTableVariants); the night's apply until it has its own.
constexpr FullVariant kEntertainerFull {
    "entertainer",
    "The Entertainer",
    "the_entertainer",
    "Marrakesh mild",
    kMarrakeshScene,
    "a load of The Entertainer",
    "the full Entertainer",
    "The Entertainer is off: a mission of The Entertainer did not finish loading last time. Until you try again, "
    "Marrakesh missions that pick The Entertainer play the normal map.",
    kEntertainerNeverTargets,
    false,
    25,
    "marrakesh",
    "Marrakesh",
    kMarrakeshContractBrick,
    kMarrakeshDroppable,
    "marrakesh",
};

// Gluttony on Freelancer's Chongqing: the contract's scene and bricks with
// the vanilla scenario_azalea and the package's fr_gluttony appended (its
// deletes keep the set's props and Freelancer's apart). Mild Chongqing and
// its showdown both offer Gluttony; the contract's crowd stays, so its mover
// rows apply. Its package is Chongqing's (partition 6, "wet"). Nobody is a
// never-target: the set's people are the escalation's, without its
// objectives.
constexpr FullVariant kGluttonyFull {
    "gluttony",
    "Gluttony",
    "gluttony",
    "Chongqing mild",
    kChongqingMildScene,
    "a Gluttony load",
    "the full Gluttony",
    "Gluttony is off: a Gluttony mission did not finish loading last time. Until you try again, Chongqing "
    "missions that pick Gluttony play the normal map.",
    {},
    true,
    6,
    "wet",
    "Chongqing",
    kChongqingContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Chongqing hot",
};

// The spot fixes (tools/landslide/spotfix_spec.py): each appends the option's
// bricks and then the package's fix brick fr_<option> to the contract's own
// scene, as Gluttony; offered on the mild map and, where its option is, the
// showdown. Their crowds are the contract's (mover rows apply); nobody is a
// never-target beyond the option's own (Variants.cpp notTargets).
constexpr FullVariant kRevolutionaryFull {
    "revolutionary",
    "The Revolutionary",
    "revolutionary",
    "Santa Fortuna mild",
    kSantaFortunaMildScene,
    "a load of The Revolutionary",
    "the full Revolutionary",
    "The Revolutionary is off: a mission of The Revolutionary did not finish loading last time. Until you try "
    "again, Santa Fortuna missions that pick The Revolutionary play the normal map.",
    {},
    true,
    18,
    "colombia",
    "Santa Fortuna",
    kSantaFortunaContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Santa Fortuna hot",
};

constexpr FullVariant kSerpentFull {
    "serpent",
    "Embrace of the Serpent",
    "embrace_of_the_serpent",
    "Santa Fortuna mild",
    kSantaFortunaMildScene,
    "a load of Embrace of the Serpent",
    "the full Embrace of the Serpent",
    "Embrace of the Serpent is off: a mission of Embrace of the Serpent did not finish loading last time. Until "
    "you try again, Santa Fortuna missions that pick Embrace of the Serpent play the normal map.",
    {},
    true,
    18,
    "colombia",
    "Santa Fortuna",
    kSantaFortunaContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Santa Fortuna hot",
};

constexpr FullVariant kReflectionFull {
    "reflection",
    "The Reflection",
    "reflection",
    "Hokkaido mild",
    kHokkaidoMildScene,
    "a load of The Reflection",
    "the full Reflection",
    "The Reflection is off: a mission of The Reflection did not finish loading last time. Until you try again, "
    "Hokkaido missions that pick The Reflection play the normal map.",
    {},
    true,
    22,
    "hokkaido",
    "Hokkaido",
    kHokkaidoContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Hokkaido hot",
};

constexpr FullVariant kGardenShowFull {
    "gardenshow",
    "Garden Show",
    "garden_show",
    "Dartmoor mild",
    kDartmoorMildScene,
    "a Garden Show load",
    "the full Garden Show",
    "Garden Show is off: a Garden Show mission did not finish loading last time. Until you try again, Dartmoor "
    "missions that pick Garden Show play the normal map.",
    {},
    true,
    3,
    "ancestral",
    "Dartmoor",
    kDartmoorContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
};

constexpr FullVariant kSlothFull {
    "sloth",
    "Sloth",
    "sloth",
    "Dartmoor mild",
    kDartmoorMildScene,
    "a Sloth load",
    "the full Sloth",
    "Sloth is off: a Sloth mission did not finish loading last time. Until you try again, Dartmoor missions that "
    "pick Sloth play the normal map.",
    {},
    true,
    3,
    "ancestral",
    "Dartmoor",
    kDartmoorContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
};

constexpr FullVariant kIllusionsFull {
    "illusions",
    "Illusions of Grandeur",
    "illusions_of_grandeur",
    "Mumbai mild",
    kMumbaiMildScene,
    "a load of Illusions of Grandeur",
    "the full Illusions of Grandeur",
    "Illusions of Grandeur is off: a mission of Illusions of Grandeur did not finish loading last time. Until you "
    "try again, Mumbai missions that pick Illusions of Grandeur play the normal map.",
    {},
    true,
    17,
    "mongoose",
    "Mumbai",
    kMumbaiContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Mumbai hot",
};

constexpr FullVariant kUndyingFull {
    "undying",
    "The Undying",
    "the_undying",
    "Miami mild",
    kMiamiMildScene,
    "a load of The Undying",
    "the full Undying",
    "The Undying is off: a mission of The Undying did not finish loading last time. Until you try again, Miami "
    "missions that pick The Undying play the normal map.",
    {},
    true,
    19,
    "miami",
    "Miami",
    kMiamiContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Miami hot",
};

constexpr FullVariant kUndyingReturnsFull {
    "undyingreturns",
    "The Undying Returns",
    "the_undying_returns",
    "Miami mild",
    kMiamiMildScene,
    "a load of The Undying Returns",
    "the full Undying Returns",
    "The Undying Returns is off: a mission of The Undying Returns did not finish loading last time. Until you try "
    "again, Miami missions that pick The Undying Returns play the normal map.",
    {},
    true,
    19,
    "miami",
    "Miami",
    kMiamiContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Miami hot",
};

constexpr FullVariant kBankerFull {
    "banker",
    "The Banker",
    "the_banker",
    "Paris mild",
    kParisMildScene,
    "a load of The Banker",
    "the full Banker",
    "The Banker is off: a mission of The Banker did not finish loading last time. Until you try again, Paris "
    "missions that pick The Banker play the normal map.",
    {},
    true,
    27,
    "paris",
    "Paris",
    kParisContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Paris hot",
};

constexpr FullVariant kBitterPillFull {
    "bitterpill",
    "A Bitter Pill",
    "a_bitter_pill",
    "Whittleton Creek mild",
    kWhittletonMildScene,
    "a load of A Bitter Pill",
    "the full A Bitter Pill",
    "A Bitter Pill is off: a mission of A Bitter Pill did not finish loading last time. Until you try again, "
    "Whittleton Creek missions that pick A Bitter Pill play the normal map.",
    {},
    true,
    16,
    "skunk",
    "Whittleton Creek",
    kWhittletonContractBrick,
    {},
    {},
    "it belongs to the normal map",
    true,
    "Whittleton Creek hot",
};

// The package's order; the Bad Boy after the Icon, its base, and after the
// others so their sentinel indexes stay; Patient Zero, the Entertainer,
// Gluttony and then the spot fixes last for the same reason.
constexpr std::array<const FullVariant*, kFullVariantCount> kFullVariants {
    &kLandslideFull,   &kAuthorFull,    &kIconFull,          &kMarrakeshFull,      &kBadBoyFull,     &kPatientZeroFull,
    &kEntertainerFull, &kGluttonyFull,  &kRevolutionaryFull, &kSerpentFull,        &kReflectionFull, &kGardenShowFull,
    &kSlothFull,       &kIllusionsFull, &kUndyingFull,       &kUndyingReturnsFull, &kBankerFull,     &kBitterPillFull};

} // namespace

const FullVariant& LandslideFull() {
    return kLandslideFull;
}

std::string_view FullTurnedOffNote(const FullVariant& p_Variant) {
    return p_Variant.offNote;
}

std::span<const FullVariant* const> FullVariants() {
    return kFullVariants;
}

const FullVariant* FullVariantOf(std::string_view p_Option, std::string_view p_Map) {
    for (const FullVariant* s_Variant : kFullVariants) {
        if (!p_Option.empty() && s_Variant->option == p_Option &&
            (s_Variant->map == p_Map || (!s_Variant->alsoMap.empty() && s_Variant->alsoMap == p_Map)))
            return s_Variant;
    }

    return nullptr;
}

const FullVariant* FullVariantOfKey(std::string_view p_Key) {
    for (const FullVariant* s_Variant : kFullVariants) {
        if (!p_Key.empty() && s_Variant->key == p_Key)
            return s_Variant;
    }

    return nullptr;
}

const FullVariant* FullVariantOfScene(std::string_view p_Scene) {
    for (const FullVariant* s_Variant : kFullVariants) {
        if (s_Variant->layerOf.empty() && !s_Variant->appends && SameNoCase(s_Variant->scene, p_Scene))
            return s_Variant;
    }

    return nullptr;
}

const FullVariant* FullVariantOfLoad(std::string_view p_FullKey, std::string_view p_Scene) {
    if (const FullVariant* s_Keyed = FullVariantOfKey(p_FullKey);
        s_Keyed && !s_Keyed->appends && SameNoCase(s_Keyed->scene, p_Scene))
        return s_Keyed;

    return FullVariantOfScene(p_Scene);
}

std::string FullSentinelFileName(const FullVariant& p_Variant) {
    return "freelancerrandomizer-" + std::string(p_Variant.key) + "-loading.txt";
}

std::vector<std::string_view> NeverTargetRepos(const FullVariant& p_Variant) {
    std::vector<std::string_view> repos;

    for (const LandslideNeverTarget& target : p_Variant.neverTargets)
        repos.push_back(target.repo);

    return repos;
}

} // namespace fr
