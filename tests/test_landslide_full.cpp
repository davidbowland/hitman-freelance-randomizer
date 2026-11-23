#include "doctest.h"

#include "support/FullVariantTestUtil.h"

#include "Guid.h"
#include "LandslideFull.h"
#include "Variants.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr std::string_view kOwnFile = "chunk26patch290.rpkg";

// A resource container with these newest copies (and an optional fault).
struct FakeContainer {
    std::map<uint64_t, fr::ResourceSeen> seen;
    uint64_t faultOn = 0;

    void Add(uint64_t p_Rid, uint32_t p_Size, std::string p_Package) {
        seen[p_Rid] = {0, true, p_Size, "../runtime//" + p_Package};
    }

    fr::ResourceLookup Lookup() const {
        return [this](uint64_t p_Rid) {
            if (p_Rid == faultOn)
                return fr::ResourceSeen {0xC0000005u, false, 0, {}};

            const auto s_It = seen.find(p_Rid);
            return s_It != seen.end() ? s_It->second : fr::ResourceSeen {};
        };
    }
};

constexpr std::array<fr::LandslideFingerprint, 2> kPrints = {{
    {0x006026A87FB981F5ull, 3437671, "chunk26patch4.rpkg", "mission_mamba.brick"},
    {0x00FD4FC867F311FEull, 328, "chunk26.rpkg", "_scene_mamba.entity"},
}};

FakeContainer AsBuilt() {
    FakeContainer s_Container;
    s_Container.Add(0x006026A87FB981F5ull, 3437671, "chunk26patch4.rpkg");
    s_Container.Add(0x00FD4FC867F311FEull, 328, "chunk26.rpkg");
    return s_Container;
}

fr::LandslideGate OpenGate() {
    fr::LandslideGate s_Gate;
    s_Gate.builtIn = true;
    s_Gate.buildable = true;
    s_Gate.installed = true;
    s_Gate.partitionSeen = true;
    s_Gate.raisedTo = 290;
    s_Gate.target = 290;
    s_Gate.freelancerLayer = true;
    return s_Gate;
}

} // namespace

TEST_CASE("landslide full: package names") {
    CHECK(fr::PackageFileName("../runtime//chunk26patch4.rpkg") == "chunk26patch4.rpkg");
    CHECK(fr::PackageFileName("S:\\HITMAN 3\\Runtime\\chunk26.rpkg") == "chunk26.rpkg");
    CHECK(fr::PackageFileName("chunk0.rpkg") == "chunk0.rpkg");
    CHECK(fr::PackagePatchNumber("../runtime//chunk26patch4.rpkg") == 4);
    CHECK(fr::PackagePatchNumber("../runtime//CHUNK26PATCH300.RPKG") == 300);
    CHECK(fr::PackagePatchNumber("../runtime//chunk26.rpkg") == 0);
    CHECK(fr::PackagePatchNumber("../runtime//chunk0patch290.rpkg") == 290);
    CHECK_FALSE(fr::PackagePatchNumber("../runtime//dlc0.rpkg"));
    CHECK_FALSE(fr::PackagePatchNumber("../runtime//chunk26patch.rpkg"));
    CHECK_FALSE(fr::PackagePatchNumber("../runtime//chunkpatch4.rpkg"));
    CHECK_FALSE(fr::PackagePatchNumber("../runtime//chunk26patch4.rpkg.bak"));
    CHECK_FALSE(fr::PackagePatchNumber(""));
}

TEST_CASE("landslide full: the gate, in order") {
    CHECK(fr::CheckFullGate(fr::LandslideFull(), OpenGate()).block == fr::LandslideBlock::None);
    CHECK(fr::CheckFullGate(fr::LandslideFull(), OpenGate()).why.empty());

    // No recipe comes first, then Full map variants off, then a recipe the game's files can't build.
    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.builtIn = false;
    s_Gate.switchedOff = true;
    s_Gate.buildable = false;
    s_Gate.installed = false;
    s_Gate.turnedOff = true;
    const fr::LandslideVerdict s_NoRecipe = fr::CheckFullGate(fr::LandslideFull(), s_Gate);
    CHECK(s_NoRecipe.block == fr::LandslideBlock::NotBuiltIn);
    CHECK(s_NoRecipe.why.find("recipe") != std::string::npos);

    s_Gate.builtIn = true;
    const fr::LandslideVerdict s_Switched = fr::CheckFullGate(fr::LandslideFull(), s_Gate);
    CHECK(s_Switched.block == fr::LandslideBlock::SwitchedOff);
    CHECK(s_Switched.why.find("full_map_variants") != std::string::npos);

    s_Gate.switchedOff = false;
    const fr::LandslideVerdict s_Unbuildable = fr::CheckFullGate(fr::LandslideFull(), s_Gate);
    CHECK(s_Unbuildable.block == fr::LandslideBlock::NotBuildable);
    CHECK(s_Unbuildable.why.find("game files") != std::string::npos);

    // The crash sentinel's turned-off is its own block, never Full map variants off.
    s_Gate.buildable = true;
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::TurnedOff);

    s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    s_Gate.installed = false;
    const fr::LandslideVerdict s_Off = fr::CheckFullGate(fr::LandslideFull(), s_Gate);
    CHECK(s_Off.block == fr::LandslideBlock::TurnedOff);
    CHECK(s_Off.why.find("did not finish") != std::string::npos);

    s_Gate = OpenGate();
    s_Gate.installed = false;
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::NotInstalled);

    s_Gate = OpenGate();
    s_Gate.partitionSeen = false;
    const fr::LandslideVerdict s_Unseen = fr::CheckFullGate(fr::LandslideFull(), s_Gate);
    CHECK(s_Unseen.block == fr::LandslideBlock::NotRaised);
    CHECK(s_Unseen.why.find("never saw") != std::string::npos);

    s_Gate = OpenGate();
    s_Gate.raisedTo = 4;
    const fr::LandslideVerdict s_Low = fr::CheckFullGate(fr::LandslideFull(), s_Gate);
    CHECK(s_Low.block == fr::LandslideBlock::NotRaised);
    CHECK(s_Low.why.find("patch level 4") != std::string::npos);

    // MaxPatchLevel raises the partition itself (after our hook, or before it).
    s_Gate.maxPatchLevel = true;
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::None);
    s_Gate.partitionSeen = false;
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::None);

    s_Gate = OpenGate();
    s_Gate.raisedTo = 10000; // a higher level mounts the package too
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::None);

    // Without the Freelancer layer (the test package) only a forced Landslide swaps.
    s_Gate = OpenGate();
    s_Gate.freelancerLayer = false;
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::TestOnly);
    s_Gate.forced = true;
    CHECK(fr::CheckFullGate(fr::LandslideFull(), s_Gate).block == fr::LandslideBlock::None);
}

TEST_CASE("landslide full: why a full variant can't be picked, in order") {
    const fr::FullAvailabilityInputs s_Open {true, false, true, false, true};
    CHECK(fr::JudgeFullAvailability(s_Open) == fr::FullAvailability::Available);

    // Full map variants off comes first, then no recipe or a game file that changed, then not installed.
    CHECK(fr::JudgeFullAvailability({false, true, false, false, false}) == fr::FullAvailability::SwitchedOff);
    CHECK(fr::JudgeFullAvailability({false, false, true, false, false}) == fr::FullAvailability::NotBuildable);
    CHECK(fr::JudgeFullAvailability({true, false, false, false, false}) == fr::FullAvailability::NotBuildable);
    CHECK(fr::JudgeFullAvailability({true, false, true, false, false}) == fr::FullAvailability::NotInstalled);

    // A game file that could not be read is no game version problem.
    CHECK(fr::JudgeFullAvailability({true, false, false, true, true}) == fr::FullAvailability::NotInstalled);
    CHECK(fr::JudgeFullAvailability({true, false, false, true, false}) == fr::FullAvailability::NotInstalled);
    CHECK(fr::JudgeFullAvailability({false, false, false, true, false}) == fr::FullAvailability::NotBuildable);
}

TEST_CASE("landslide full: an option's availability is its first unavailable full version's") {
    const fr::FullAvailabilityOf s_All = [](const fr::FullVariant&) { return fr::FullAvailability::Available; };
    CHECK(fr::FullOptionAvailability("landslide", s_All) == fr::FullAvailability::Available);

    const fr::FullAvailabilityOf s_Mixed = [](const fr::FullVariant& p_Full) {
        if (p_Full.key == "icon")
            return fr::FullAvailability::NotBuildable;
        return p_Full.partition == 26 ? fr::FullAvailability::NotInstalled : fr::FullAvailability::Available;
    };
    CHECK(fr::FullOptionAvailability("landslide", s_Mixed) == fr::FullAvailability::NotInstalled);
    CHECK(fr::FullOptionAvailability(fr::FullVariantOfKey("icon")->option, s_Mixed) ==
          fr::FullAvailability::NotBuildable);
    CHECK(fr::FullOptionAvailability("no_such_option", s_Mixed) == fr::FullAvailability::Available);
    CHECK(fr::FullOptionAvailability("", s_Mixed) == fr::FullAvailability::Available);
}

TEST_CASE("landslide full: the contract's brick list") {
    const std::vector<std::string> s_Vanilla = {"assembly:/_pro/scenes/missions/coastaltown/mission_mild_bingo.brick"};
    CHECK(fr::CheckFullContract(fr::LandslideFull(), s_Vanilla).verdict.block == fr::LandslideBlock::None);

    const std::vector<std::string> s_Case = {"assembly:/_PRO/Scenes/Missions/CoastalTown/Mission_Mild_Bingo.brick"};
    CHECK(fr::CheckFullContract(fr::LandslideFull(), s_Case).verdict.block == fr::LandslideBlock::None);

    const std::vector<std::string> s_Extra = {s_Vanilla[0], "assembly:/_pro/scenes/bricks/new.brick"};
    const fr::LandslideVerdict s_More = fr::CheckFullContract(fr::LandslideFull(), s_Extra).verdict;
    CHECK(s_More.block == fr::LandslideBlock::Contract);
    CHECK(s_More.why.find("new.brick") != std::string::npos);

    CHECK(fr::CheckFullContract(fr::LandslideFull(), std::vector<std::string> {}).verdict.block ==
          fr::LandslideBlock::Contract);
    const std::vector<std::string> s_Other = {"assembly:/_pro/scenes/missions/coastaltown/mission_hot_bingo.brick"};
    CHECK(fr::CheckFullContract(fr::LandslideFull(), s_Other).verdict.block == fr::LandslideBlock::Contract);
}

TEST_CASE("landslide full: the package's own resources") {
    FakeContainer s_Container;
    const std::array<uint64_t, 2> s_Own = {0x00D1C948471CA87Full, 0x0011111111111111ull};
    s_Container.Add(s_Own[0], 1989, "chunk26patch290.rpkg");
    s_Container.Add(s_Own[1], 10, "CHUNK26PATCH290.RPKG");
    CHECK(fr::CheckOwnResources(s_Own, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::None);

    // Another package of the same chunk holds a newer copy.
    s_Container.Add(s_Own[1], 10, "chunk26patch300.rpkg");
    const fr::LandslideVerdict s_Shadowed = fr::CheckOwnResources(s_Own, s_Container.Lookup(), kOwnFile);
    CHECK(s_Shadowed.block == fr::LandslideBlock::OwnResource);
    CHECK(s_Shadowed.why.find("chunk26patch300.rpkg") != std::string::npos);

    s_Container.seen.erase(s_Own[1]);
    const fr::LandslideVerdict s_Missing = fr::CheckOwnResources(s_Own, s_Container.Lookup(), kOwnFile);
    CHECK(s_Missing.block == fr::LandslideBlock::OwnResource);
    CHECK(s_Missing.why.find("0011111111111111") != std::string::npos);

    s_Container.faultOn = s_Own[0];
    CHECK(fr::CheckOwnResources(s_Own, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::ReadFault);
}

TEST_CASE("landslide full: dependencies") {
    FakeContainer s_Container = AsBuilt();
    const std::array<uint64_t, 2> s_Deps = {0x006026A87FB981F5ull, 0x00FD4FC867F311FEull};
    CHECK(fr::CheckDependencies(s_Deps, s_Container.Lookup()).block == fr::LandslideBlock::None);

    const std::array<uint64_t, 4> s_More = {0x006026A87FB981F5ull, 0x0000000000000001ull, 0x00FD4FC867F311FEull,
                                            0x0000000000000002ull};
    const fr::LandslideVerdict s_Missing = fr::CheckDependencies(s_More, s_Container.Lookup());
    CHECK(s_Missing.block == fr::LandslideBlock::Dependency);
    // Every missing hash is counted, and named.
    CHECK(s_Missing.why.find("2 of the 4") != std::string::npos);
    CHECK(s_Missing.why.find("0000000000000001") != std::string::npos);
    CHECK(s_Missing.why.find("0000000000000002") != std::string::npos);

    s_Container.faultOn = 0x00FD4FC867F311FEull;
    CHECK(fr::CheckDependencies(s_Deps, s_Container.Lookup()).block == fr::LandslideBlock::ReadFault);
}

TEST_CASE("landslide full: the fingerprint decision table") {
    std::string s_Summary;

    SUBCASE("as built: passes, and the summary lists each") {
        const FakeContainer s_Container = AsBuilt();
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile, &s_Summary).block ==
              fr::LandslideBlock::None);
        CHECK(s_Summary == "mission_mamba.brick 3437671 chunk26patch4.rpkg, _scene_mamba.entity 328 chunk26.rpkg");
    }

    SUBCASE("a newer IOI patch with the same size: passes, the move is logged") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x006026A87FB981F5ull, 3437671, "chunk26patch5.rpkg");
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile, &s_Summary).block ==
              fr::LandslideBlock::None);
        CHECK(s_Summary.find("mission_mamba.brick 3437671 chunk26patch5.rpkg (built against chunk26patch4.rpkg)") !=
              std::string::npos);
    }

    SUBCASE("a game update changed the size") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x006026A87FB981F5ull, 3440012, "chunk26patch5.rpkg");
        const fr::LandslideVerdict s_Verdict = fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile);
        CHECK(s_Verdict.block == fr::LandslideBlock::GameChanged);
        CHECK(s_Verdict.why ==
              "a game update changed mission_mamba.brick (3437671 bytes in chunk26patch4.rpkg when this mod was "
              "made, now 3440012 in chunk26patch5.rpkg)");
    }

    SUBCASE("the same package, another size") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x00FD4FC867F311FEull, 320, "chunk26.rpkg");
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::GameChanged);
    }

    SUBCASE("removed") {
        FakeContainer s_Container = AsBuilt();
        s_Container.seen.erase(0x00FD4FC867F311FEull);
        const fr::LandslideVerdict s_Verdict = fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile);
        CHECK(s_Verdict.block == fr::LandslideBlock::GameChanged);
        CHECK(s_Verdict.why.find("_scene_mamba.entity") != std::string::npos);
        CHECK(s_Verdict.why.find("no longer") != std::string::npos);
    }

    SUBCASE("another mod's package replaces it, even at the same size") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x00FD4FC867F311FEull, 328, "chunk26patch300.rpkg");
        const fr::LandslideVerdict s_Verdict = fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile);
        CHECK(s_Verdict.block == fr::LandslideBlock::OtherMod);
        CHECK(s_Verdict.why.find("chunk26patch300.rpkg") != std::string::npos);
        CHECK(s_Verdict.why.find("another mod") != std::string::npos);
    }

    SUBCASE("a mod's copy of another size names the mod") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x00FD4FC867F311FEull, 320, "chunk26patch300.rpkg");
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::OtherMod);
    }

    SUBCASE("a package name the rules do not know passes on its size") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x00FD4FC867F311FEull, 328, "dlc5.rpkg");
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::None);
    }

    SUBCASE("the mod's own package may hold one") {
        FakeContainer s_Container = AsBuilt();
        s_Container.Add(0x00FD4FC867F311FEull, 328, "chunk26patch290.rpkg");
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::None);
    }

    SUBCASE("a read fault") {
        FakeContainer s_Container = AsBuilt();
        s_Container.faultOn = 0x006026A87FB981F5ull;
        CHECK(fr::CheckFingerprints(kPrints, s_Container.Lookup(), kOwnFile).block == fr::LandslideBlock::ReadFault);
    }
}

TEST_CASE("landslide full: overlay text (no simple version: the normal map)") {
    const fr::FullVariant& s_Landslide = fr::LandslideFull();
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::None) == "Landslide");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::GameChanged) ==
          "normal map (Landslide could not be loaded: the game changed since this mod was made)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::Dependency) ==
          fr::FullOverlay(s_Landslide, fr::LandslideBlock::GameChanged));
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::Contract) ==
          fr::FullOverlay(s_Landslide, fr::LandslideBlock::GameChanged));
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::TurnedOff) ==
          "normal map (Landslide is off after a load that did not finish; try it again in Settings)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::OtherMod) ==
          "normal map (Landslide could not be loaded: another mod changes Sapienza)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::NotRaised) == "normal map (Landslide could not be loaded)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::NotBuiltIn) ==
          "normal map (Landslide is not in this build of the mod)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::TestOnly) ==
          fr::FullOverlay(s_Landslide, fr::LandslideBlock::NotBuiltIn));
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::NotBuildable) ==
          "normal map (Landslide doesn't work with your version of the game yet)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::SwitchedOff) ==
          "normal map (full map variants are off in Settings)");
    CHECK(fr::FullOverlay(s_Landslide, fr::LandslideBlock::SwitchedOff) !=
          fr::FullOverlay(s_Landslide, fr::LandslideBlock::TurnedOff));

    // Every full variant, every block: its name when used, the normal map otherwise; never a simple version.
    for (const fr::FullVariant* s_Variant : fr::FullVariants()) {
        CAPTURE(s_Variant->key);
        CHECK(fr::FullOverlay(*s_Variant, fr::LandslideBlock::None) == s_Variant->display);

        for (const fr::LandslideBlock s_Block :
             {fr::LandslideBlock::NotBuiltIn, fr::LandslideBlock::NotBuildable, fr::LandslideBlock::TurnedOff,
              fr::LandslideBlock::NotInstalled, fr::LandslideBlock::NotRaised, fr::LandslideBlock::TestOnly,
              fr::LandslideBlock::Contract, fr::LandslideBlock::NotMounted, fr::LandslideBlock::ReadFault,
              fr::LandslideBlock::OwnResource, fr::LandslideBlock::Dependency, fr::LandslideBlock::GameChanged,
              fr::LandslideBlock::OtherMod}) {
            const std::string s_Text = fr::FullOverlay(*s_Variant, s_Block);
            CHECK(s_Text.starts_with("normal map (" + std::string(s_Variant->display)));
            CHECK(s_Text.find("simple") == std::string::npos);
            CHECK(s_Text.find("only") == std::string::npos);
        }
    }
}

TEST_CASE("landslide full: the crash sentinel") {
    const std::vector<uint8_t> s_Package = {1, 2, 3, 4};
    const std::string s_Build = fr::LandslideBuildId("0.1.0", s_Package);
    CHECK(s_Build.rfind("0.1.0+", 0) == 0);
    CHECK(s_Build.size() == 6 + 8);
    CHECK(s_Build == fr::LandslideBuildId("0.1.0", s_Package)); // stable
    CHECK(s_Build != fr::LandslideBuildId("0.1.1", s_Package));
    const std::vector<uint8_t> s_Other = {1, 2, 3, 5};
    CHECK(s_Build != fr::LandslideBuildId("0.1.0", s_Other));

    const std::string s_Text = fr::LandslideSentinelText(s_Build, "2026-11-08 12:46:47", fr::kLandslideScene);
    CHECK(s_Text.find("build " + s_Build + "\n") != std::string::npos);
    CHECK(s_Text.find("written 2026-11-08 12:46:47\n") != std::string::npos);
    CHECK(fr::LandslideSentinelBuild(s_Text) == s_Build);
    CHECK(fr::LandslideSentinelBuild("written x\r\nbuild  abc \r\n") == "abc");
    CHECK(fr::LandslideSentinelBuild("").empty());
    CHECK(fr::LandslideSentinelBuild("builder x\n").empty());

    CHECK(fr::JudgeSentinel(nullptr, true, s_Build) == fr::SentinelAction::None);
    CHECK(fr::JudgeSentinel(&s_Text, true, s_Build) == fr::SentinelAction::TurnOff);
    CHECK(fr::JudgeSentinel(&s_Text, true, "0.2.0+00000000") == fr::SentinelAction::Discard);
    // Unreadable, empty or damaged: assume the worst.
    const std::string s_Empty;
    CHECK(fr::JudgeSentinel(&s_Empty, true, s_Build) == fr::SentinelAction::TurnOff);
    CHECK(fr::JudgeSentinel(&s_Empty, false, s_Build) == fr::SentinelAction::TurnOff);
}

TEST_CASE("landslide_never_targets") {
    const auto never = fr::LandslideFull().neverTargets;
    REQUIRE(never.size() == 1);
    REQUIRE(fr::NeverTargetRepos(fr::LandslideFull()).size() == never.size());

    std::set<uint64_t> ids;

    for (const fr::LandslideNeverTarget& target : never) {
        CAPTURE(target.name);
        CHECK(fr::ParseGuid(target.repo).has_value());
        ids.insert(target.entityId);
    }

    CHECK(ids.size() == never.size());

    // Marco Abiatti, Landslide's own target (entity and repository id measured in game).
    REQUIRE(!never.empty());
    CHECK(never[0].name == "Marco Abiatti");
    CHECK(never[0].entityId == 0x33160A4D0FD92760ull);
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[0].repo)) == "853b4070-99b1-45da-938b-6fa7947eb7cd");
}

// --- The full map variants (the full Landslide, the full Author and the full Icon) ---

TEST_CASE("full variants: the registry") {
    REQUIRE(fr::FullVariants().size() == 18); // LandslidePackage keeps flags for up to 24
    const fr::FullVariant& s_Landslide = fr::LandslideFull();
    const fr::FullVariant& s_Author = fr::test::Full("author");
    const fr::FullVariant& s_Icon = fr::test::Full("icon");
    const fr::FullVariant& s_Marrakesh = fr::test::Full("marrakesh");
    // The packages' order: Sapienza's three (the Icon's resources last), then Marrakesh's.
    CHECK(fr::FullVariants()[0] == &s_Landslide);
    CHECK(fr::FullVariants()[1] == &s_Author);
    CHECK(fr::FullVariants()[2] == &s_Icon);
    CHECK(fr::FullVariants()[3] == &s_Marrakesh);
    CHECK(fr::FullVariants()[4] == &fr::test::Full("badboy"));      // after the others: their sentinel indexes stay
    CHECK(fr::FullVariants()[5] == &fr::test::Full("patientzero")); // likewise
    CHECK(fr::FullVariants()[6] == &fr::test::Full("entertainer")); // likewise
    CHECK(fr::FullVariants()[7] == &fr::test::Full("gluttony"));    // likewise
    // The spot fixes after them, likewise.
    CHECK(fr::FullVariants()[8] == &fr::test::Full("revolutionary"));
    CHECK(fr::FullVariants()[9] == &fr::test::Full("serpent"));
    CHECK(fr::FullVariants()[10] == &fr::test::Full("reflection"));
    CHECK(fr::FullVariants()[11] == &fr::test::Full("gardenshow"));
    CHECK(fr::FullVariants()[12] == &fr::test::Full("sloth"));
    CHECK(fr::FullVariants()[13] == &fr::test::Full("illusions"));
    CHECK(fr::FullVariants()[14] == &fr::test::Full("undying"));
    CHECK(fr::FullVariants()[15] == &fr::test::Full("undyingreturns"));
    CHECK(fr::FullVariants()[16] == &fr::test::Full("banker"));
    CHECK(fr::FullVariants()[17] == &fr::test::Full("bitterpill"));

    CHECK(s_Landslide.key == "landslide");
    CHECK(s_Landslide.display == "Landslide");
    CHECK(s_Landslide.option == "landslide");
    CHECK(s_Landslide.scene == fr::kLandslideScene);
    CHECK_FALSE(s_Landslide.sharesCrowd);
    CHECK(s_Landslide.fullName == "the full Landslide");

    CHECK(s_Author.key == "author");
    CHECK(s_Author.display == "The Author");
    CHECK(s_Author.option == "tod_ebola_night");
    CHECK(s_Author.scene == fr::kAuthorScene);
    CHECK(fr::kAuthorScene == "assembly:/_pro/scenes/missions/coastaltown/scene_ebola.entity");
    // The Author keeps mild Sapienza's day crowd bricks: their mover rows apply until it has its own.
    CHECK(s_Author.sharesCrowd);
    CHECK(s_Author.fullName == "the full Author");

    CHECK(s_Icon.key == "icon");
    CHECK(s_Icon.display == "The Icon");
    CHECK(s_Icon.option == "lighting_copperhead");
    CHECK(s_Icon.scene == fr::kIconScene);
    CHECK(fr::kIconScene == "assembly:/_pro/scenes/missions/coastaltown/_scene_copperhead.entity");
    // The Icon's set has its own crowd (none of mild Sapienza's day crowd): its own mover rows only.
    CHECK_FALSE(s_Icon.sharesCrowd);
    CHECK(s_Icon.fullName == "the full Icon");

    // Sapienza's three share partition 26's package; the Marrakesh night has partition 25's.
    for (const fr::FullVariant* s_Sapienza : {&s_Landslide, &s_Author, &s_Icon}) {
        CHECK(s_Sapienza->partition == 26);
        CHECK(s_Sapienza->partitionId == "coastaltown");
        CHECK(s_Sapienza->location == "Sapienza");
        CHECK(s_Sapienza->contractBrick == fr::kLandslideContractBrick);
        REQUIRE(s_Sapienza->droppable.size() == 2);
        CHECK(fr::FullPackageFileName(s_Sapienza->partition, 290) == "chunk26patch290.rpkg");
    }

    CHECK(s_Marrakesh.key == "marrakesh");
    CHECK(s_Marrakesh.display == "Marrakesh at night");
    // No option of its own (the plain marrakesh_night option was removed): only the full Entertainer's base.
    CHECK(s_Marrakesh.option.empty());
    CHECK(s_Marrakesh.map == "Marrakesh mild");
    CHECK(s_Marrakesh.scene == fr::kMarrakeshScene);
    CHECK(fr::kMarrakeshScene == "assembly:/_pro/scenes/missions/marrakesh/_scene_python.entity");
    // The night level has its own crowds: its own mover rows only.
    CHECK_FALSE(s_Marrakesh.sharesCrowd);
    CHECK(s_Marrakesh.fullName == "the full Marrakesh night");
    CHECK(s_Marrakesh.partition == 25);
    CHECK(s_Marrakesh.partitionId == "marrakesh");
    CHECK(s_Marrakesh.location == "Marrakesh");
    CHECK(s_Marrakesh.contractBrick == "assembly:/_pro/scenes/missions/marrakesh/mission_mild_mancala.brick");
    REQUIRE(s_Marrakesh.droppable.size() == 2);
    CHECK(fr::FullPackageFileName(s_Marrakesh.partition, 290) == "chunk25patch290.rpkg");
    CHECK(fr::PackageLogName(26, "coastaltown") == "landslide package");
    CHECK(fr::PackageLogName(25, "marrakesh") == "marrakesh package");

    // Mild Sapienza only; the option by its name.
    CHECK(fr::FullVariantOf("landslide", "Sapienza mild") == &s_Landslide);
    CHECK(fr::FullVariantOf("tod_ebola_night", "Sapienza mild") == &s_Author);
    CHECK_FALSE(fr::FullVariantOf("landslide", "Sapienza hot"));
    CHECK_FALSE(fr::FullVariantOf("tod_ebola_night", "Sapienza hot"));
    CHECK(fr::FullVariantOf("lighting_copperhead", "Sapienza mild") == &s_Icon);
    CHECK_FALSE(fr::FullVariantOf("lighting_copperhead", "Sapienza hot"));
    CHECK_FALSE(fr::FullVariantOf("tod_summer_night_clear", "Sapienza mild"));
    CHECK_FALSE(fr::FullVariantOf("", "Sapienza mild"));
    // The full Marrakesh night is never picked: the removed option's name (or none) names nothing.
    CHECK_FALSE(fr::FullVariantOf("marrakesh_night", "Marrakesh mild"));
    CHECK_FALSE(fr::FullVariantOf("", "Marrakesh mild"));

    CHECK(fr::FullVariantOfKey("landslide") == &s_Landslide);
    CHECK(fr::FullVariantOfKey("author") == &s_Author);
    CHECK(fr::FullVariantOfKey("icon") == &s_Icon);
    CHECK(fr::FullVariantOfKey("marrakesh") == &s_Marrakesh);
    CHECK_FALSE(fr::FullVariantOfKey("Author"));
    CHECK_FALSE(fr::FullVariantOfKey(""));

    // By the scene it loads, any case.
    CHECK(fr::FullVariantOfScene("ASSEMBLY:/_PRO/Scenes/Missions/CoastalTown/scene_ebola.entity") == &s_Author);
    CHECK(fr::FullVariantOfScene(fr::kLandslideScene) == &s_Landslide);
    CHECK(fr::FullVariantOfScene("assembly:/_pro/scenes/missions/coastaltown/_SCENE_COPPERHEAD.entity") == &s_Icon);
    CHECK_FALSE(
        fr::FullVariantOfScene("assembly:/_pro/scenes/missions/coastaltown/scene_costaltown_mild_bingo.entity"));
    CHECK(fr::FullVariantOfScene("assembly:/_pro/scenes/missions/Marrakesh/_scene_python.entity") == &s_Marrakesh);
    CHECK_FALSE(fr::FullVariantOfScene("assembly:/_pro/scenes/missions/Marrakesh/scene_spider_mild_mancala.entity"));

    // Each its own crash sentinel; Landslide's keeps its file name.
    CHECK(fr::FullSentinelFileName(s_Landslide) == "freelancerrandomizer-landslide-loading.txt");
    CHECK(fr::FullSentinelFileName(s_Author) == "freelancerrandomizer-author-loading.txt");
    CHECK(fr::FullSentinelFileName(s_Icon) == "freelancerrandomizer-icon-loading.txt");
    CHECK(fr::FullSentinelFileName(s_Marrakesh) == "freelancerrandomizer-marrakesh-loading.txt");
}

TEST_CASE("full variants: one per option, on its mild map") {
    // Each full variant is the full version of one option, on its mild map (test_variants checks the hot map does
    // not offer the option at all: there is no simple version to play there). The full Marrakesh night has none:
    // it is only the full Entertainer's base.
    std::set<std::string_view> s_Options;

    for (const fr::FullVariant* s_Variant : fr::FullVariants()) {
        CAPTURE(s_Variant->key);

        if (s_Variant == &fr::test::Full("marrakesh")) {
            CHECK(s_Variant->option.empty());
            continue;
        }

        CHECK(fr::FindVariantOption(s_Variant->option) != nullptr);
        CHECK(s_Variant->map.ends_with(" mild"));
        CHECK(fr::FullVariantOf(s_Variant->option, s_Variant->map) == s_Variant);
        s_Options.insert(s_Variant->option);
    }

    CHECK(s_Options.size() == fr::FullVariants().size() - 1);
}

TEST_CASE("full variants: the Bad Boy, a layer on the Icon") {
    const fr::FullVariant& s_BadBoy = fr::test::Full("badboy");
    const fr::FullVariant& s_Icon = fr::test::Full("icon");
    CHECK(s_BadBoy.key == "badboy");
    CHECK(s_BadBoy.display == "The Bad Boy");
    CHECK(s_BadBoy.option == "the_bad_boy");
    CHECK(s_BadBoy.map == "Sapienza mild");
    CHECK(s_BadBoy.fullName == "the full Bad Boy");
    // The full Icon's scene, partition and contract; a layer on it.
    CHECK(s_BadBoy.scene == s_Icon.scene);
    CHECK(s_BadBoy.partition == 26);
    CHECK(s_BadBoy.partitionId == "coastaltown");
    CHECK(s_BadBoy.contractBrick == fr::kLandslideContractBrick);
    CHECK(s_BadBoy.droppable.size() == 2);
    CHECK(s_BadBoy.layerOf == "icon");
    CHECK_FALSE(s_BadBoy.sharesCrowd);
    for (const fr::FullVariant* s_Other :
         {&fr::LandslideFull(), &fr::test::Full("author"), &s_Icon, &fr::test::Full("marrakesh")})
        CHECK(s_Other->layerOf.empty());

    CHECK(fr::FullVariantOf("the_bad_boy", "Sapienza mild") == &s_BadBoy);
    CHECK_FALSE(fr::FullVariantOf("the_bad_boy", "Sapienza hot"));
    CHECK(fr::FullVariantOfKey("badboy") == &s_BadBoy);
    CHECK(fr::FullSentinelFileName(s_BadBoy) == "freelancerrandomizer-badboy-loading.txt");
    CHECK(fr::FullPackageFileName(s_BadBoy.partition, 290) == "chunk26patch290.rpkg");

    // The two share a scene: the scene alone names the Icon; the alias's key tells them apart.
    CHECK(fr::FullVariantOfScene(fr::kIconScene) == &s_Icon);
    CHECK(fr::FullVariantOfLoad("badboy", fr::kIconScene) == &s_BadBoy);
    CHECK(fr::FullVariantOfLoad("badboy", "assembly:/_pro/scenes/missions/CoastalTown/_Scene_Copperhead.entity") ==
          &s_BadBoy);
    CHECK(fr::FullVariantOfLoad("icon", fr::kIconScene) == &s_Icon);
    CHECK(fr::FullVariantOfLoad("", fr::kIconScene) == &s_Icon);
    CHECK(fr::FullVariantOfLoad("badboy", fr::kLandslideScene) == &fr::LandslideFull()); // a key of another scene
    CHECK(fr::FullVariantOfLoad("nope", fr::kMarrakeshScene) == &fr::test::Full("marrakesh"));
    CHECK_FALSE(fr::FullVariantOfLoad("badboy", "assembly:/_pro/scenes/missions/coastaltown/scene_x.entity"));

    CHECK(fr::FullTurnedOffNote(s_BadBoy) ==
          "The Bad Boy is off: a mission of The Bad Boy did not finish loading last time. Until you try again, "
          "Sapienza missions that pick The Bad Boy play the normal map.");
}

TEST_CASE("full variants: Gluttony appends to the contract's own scene") {
    const fr::FullVariant& s_Gluttony = fr::test::Full("gluttony");
    CHECK(s_Gluttony.key == "gluttony");
    CHECK(s_Gluttony.display == "Gluttony");
    CHECK(s_Gluttony.option == "gluttony");
    CHECK(s_Gluttony.fullName == "the full Gluttony");
    // No scene swap: the contract's scene and bricks stay; its package is Chongqing's.
    CHECK(s_Gluttony.appends);
    CHECK(s_Gluttony.layerOf.empty());
    CHECK(s_Gluttony.scene == fr::kChongqingMildScene);
    CHECK(fr::kChongqingMildScene == "assembly:/_pro/scenes/missions/wet/scene_wet_mild_mahjong.entity");
    CHECK(s_Gluttony.contractBrick == fr::kChongqingContractBrick);
    CHECK(s_Gluttony.droppable.empty());
    CHECK(s_Gluttony.sharesCrowd); // the contract's crowd stays, so its mover rows apply
    CHECK(s_Gluttony.neverTargets.empty());
    CHECK(s_Gluttony.partition == 6);
    CHECK(s_Gluttony.partitionId == "wet");
    CHECK(s_Gluttony.location == "Chongqing");
    CHECK(fr::FullPackageFileName(s_Gluttony.partition, 290) == "chunk6patch290.rpkg");
    CHECK(fr::FullSentinelFileName(s_Gluttony) == "freelancerrandomizer-gluttony-loading.txt");
    // Gluttony and the spot fixes (after it in the list) append; the scene swaps before it do not.
    for (size_t i = 0; i < fr::FullVariants().size(); ++i)
        CHECK((fr::FullVariants()[i]->appends == (i >= 7)));

    // Mild Chongqing and its showdown both offer Gluttony: the full version is used on both.
    CHECK(s_Gluttony.map == "Chongqing mild");
    CHECK(s_Gluttony.alsoMap == "Chongqing hot");
    CHECK(fr::FullVariantOf("gluttony", "Chongqing mild") == &s_Gluttony);
    CHECK(fr::FullVariantOf("gluttony", "Chongqing hot") == &s_Gluttony);
    CHECK_FALSE(fr::FullVariantOf("gluttony", "Marrakesh mild"));
    CHECK_FALSE(fr::FullVariantOf("the_rage", "Chongqing mild"));
    CHECK_FALSE(fr::FullVariantOf("", "Chongqing hot"));
    CHECK(fr::FullVariantOfKey("gluttony") == &s_Gluttony);
    // It swaps in no scene: the contract's own scene is never taken for a full variant's.
    CHECK_FALSE(fr::FullVariantOfScene(fr::kChongqingMildScene));
    CHECK_FALSE(fr::FullVariantOfLoad("gluttony", fr::kChongqingMildScene));
    CHECK_FALSE(fr::FullVariantOfLoad("", "assembly:/_pro/scenes/missions/wet/scene_wet_hot_mahjong.entity"));

    CHECK(fr::CheckFullGate(s_Gluttony, OpenGate()).block == fr::LandslideBlock::None);
    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    CHECK(fr::CheckFullGate(s_Gluttony, s_Gate).why.find("a Gluttony load did not finish") != std::string::npos);
    s_Gate = OpenGate();
    s_Gate.partitionSeen = false;
    CHECK(fr::CheckFullGate(s_Gluttony, s_Gate).why.find("Chongqing's partition 6") != std::string::npos);

    CHECK(fr::FullOverlay(s_Gluttony, fr::LandslideBlock::None) == "Gluttony");
    CHECK(fr::FullOverlay(s_Gluttony, fr::LandslideBlock::NotBuiltIn) ==
          "normal map (Gluttony is not in this build of the mod)");
    CHECK(fr::FullOverlay(s_Gluttony, fr::LandslideBlock::TurnedOff) ==
          "normal map (Gluttony is off after a load that did not finish; try it again in Settings)");
    CHECK(fr::FullTurnedOffNote(s_Gluttony) ==
          "Gluttony is off: a Gluttony mission did not finish loading last time. Until you try again, Chongqing "
          "missions that pick Gluttony play the normal map.");
}

TEST_CASE("full variants: the Bad Boy's gate and overlay") {
    const fr::FullVariant& s_BadBoy = fr::test::Full("badboy");
    CHECK(fr::CheckFullGate(s_BadBoy, OpenGate()).block == fr::LandslideBlock::None);

    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    CHECK(fr::CheckFullGate(s_BadBoy, s_Gate).why.find("a load of The Bad Boy did not finish") != std::string::npos);
    s_Gate = OpenGate();
    s_Gate.freelancerLayer = false;
    CHECK(fr::CheckFullGate(s_BadBoy, s_Gate).why.find("variant_force = the_bad_boy") != std::string::npos);

    CHECK(fr::FullOverlay(s_BadBoy, fr::LandslideBlock::None) == "The Bad Boy");
    CHECK(fr::FullOverlay(s_BadBoy, fr::LandslideBlock::NotBuiltIn) ==
          "normal map (The Bad Boy is not in this build of the mod)");
    CHECK(fr::FullOverlay(s_BadBoy, fr::LandslideBlock::GameChanged) ==
          "normal map (The Bad Boy could not be loaded: the game changed since this mod was made)");
    CHECK(fr::FullOverlay(s_BadBoy, fr::LandslideBlock::TurnedOff) ==
          "normal map (The Bad Boy is off after a load that did not finish; try it again in Settings)");
}

TEST_CASE("badboy_never_targets") {
    const auto never = fr::test::Full("badboy").neverTargets;
    REQUIRE(never.size() == 3);
    // The Icon's two, still loaded under him, then Bartholomew Argus (the contract's Kill objective).
    CHECK(never[0].repo == fr::test::Full("icon").neverTargets[0].repo);
    CHECK(never[1].repo == fr::test::Full("icon").neverTargets[1].repo);
    CHECK(never[2].name == "Bartholomew Argus");
    CHECK(never[2].entityId == 0x30685A26D9C1F190ull); // mission_caipirinha's CHAR_ElusiveTarget_Unique_Caipirinha_M
    REQUIRE(fr::ParseGuid(never[2].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[2].repo)) == "ab3fe438-acd0-49a4-a0f4-690c86a10324");
    CHECK(fr::NeverTargetRepos(fr::test::Full("badboy")).size() == 3);
}

TEST_CASE("full variants: the spot fixes append to the contract's own scene, as Gluttony") {
    struct Expected {
        const fr::FullVariant* variant;
        std::string_view key, option, map, alsoMap, scene, location, partitionId, package;
        int partition;
    };
    const Expected s_All[] = {
        {&fr::test::Full("revolutionary"), "revolutionary", "revolutionary", "Santa Fortuna mild", "Santa Fortuna hot",
         fr::kSantaFortunaMildScene, "Santa Fortuna", "colombia", "chunk18patch290.rpkg", 18},
        {&fr::test::Full("serpent"), "serpent", "embrace_of_the_serpent", "Santa Fortuna mild", "Santa Fortuna hot",
         fr::kSantaFortunaMildScene, "Santa Fortuna", "colombia", "chunk18patch290.rpkg", 18},
        {&fr::test::Full("reflection"), "reflection", "reflection", "Hokkaido mild", "Hokkaido hot",
         fr::kHokkaidoMildScene, "Hokkaido", "hokkaido", "chunk22patch290.rpkg", 22},
        {&fr::test::Full("gardenshow"), "gardenshow", "garden_show", "Dartmoor mild", "", fr::kDartmoorMildScene,
         "Dartmoor", "ancestral", "chunk3patch290.rpkg", 3},
        {&fr::test::Full("sloth"), "sloth", "sloth", "Dartmoor mild", "", fr::kDartmoorMildScene, "Dartmoor",
         "ancestral", "chunk3patch290.rpkg", 3},
        {&fr::test::Full("illusions"), "illusions", "illusions_of_grandeur", "Mumbai mild", "Mumbai hot",
         fr::kMumbaiMildScene, "Mumbai", "mongoose", "chunk17patch290.rpkg", 17},
        {&fr::test::Full("undying"), "undying", "the_undying", "Miami mild", "Miami hot", fr::kMiamiMildScene, "Miami",
         "miami", "chunk19patch290.rpkg", 19},
        {&fr::test::Full("undyingreturns"), "undyingreturns", "the_undying_returns", "Miami mild", "Miami hot",
         fr::kMiamiMildScene, "Miami", "miami", "chunk19patch290.rpkg", 19},
        {&fr::test::Full("banker"), "banker", "the_banker", "Paris mild", "Paris hot", fr::kParisMildScene, "Paris",
         "paris", "chunk27patch290.rpkg", 27},
        {&fr::test::Full("bitterpill"), "bitterpill", "a_bitter_pill", "Whittleton Creek mild", "Whittleton Creek hot",
         fr::kWhittletonMildScene, "Whittleton Creek", "skunk", "chunk16patch290.rpkg", 16},
    };

    for (const Expected& e : s_All) {
        const fr::FullVariant& v = *e.variant;
        CAPTURE(e.key);
        CHECK(v.key == e.key);
        CHECK(v.option == e.option);
        CHECK(v.map == e.map);
        CHECK(v.alsoMap == e.alsoMap);
        CHECK(v.scene == e.scene);
        CHECK(v.location == e.location);
        CHECK(v.partition == e.partition);
        CHECK(v.partitionId == e.partitionId);
        CHECK(fr::FullPackageFileName(v.partition, 290) == e.package);
        CHECK(fr::FullSentinelFileName(v) == "freelancerrandomizer-" + std::string(e.key) + "-loading.txt");
        // No scene swap: the contract's scene, bricks and crowd stay; no never-target of its own.
        CHECK(v.appends);
        CHECK(v.layerOf.empty());
        CHECK(v.droppable.empty());
        CHECK(v.sharesCrowd);
        CHECK(v.neverTargets.empty());
        CHECK(v.fullName == "the full " + std::string(v.display.starts_with("The ") ? v.display.substr(4) : v.display));
        // Used on its map (and the showdown, where the option is offered), never by scene.
        CHECK(fr::FullVariantOf(e.option, e.map) == &v);
        if (!e.alsoMap.empty())
            CHECK(fr::FullVariantOf(e.option, e.alsoMap) == &v);
        CHECK(fr::FullVariantOfKey(e.key) == &v);
        CHECK_FALSE(fr::FullVariantOfScene(e.scene));
        CHECK_FALSE(fr::FullVariantOfLoad(e.key, e.scene));
        CHECK(fr::CheckFullGate(v, OpenGate()).block == fr::LandslideBlock::None);
        fr::LandslideGate s_Gate = OpenGate();
        s_Gate.partitionSeen = false;
        CHECK(fr::CheckFullGate(v, s_Gate).why.find(std::string(e.location) + "'s partition " +
                                                    std::to_string(e.partition)) != std::string::npos);
        CHECK(fr::FullOverlay(v, fr::LandslideBlock::None) == v.display);
        CHECK(fr::FullTurnedOffNote(v).starts_with(std::string(v.display) + " is off: "));
    }

    // Garden Show and Sloth are mild Dartmoor's only; the others' showdowns offer them.
    CHECK_FALSE(fr::FullVariantOf("garden_show", "Dartmoor hot"));
    CHECK_FALSE(fr::FullVariantOf("sloth", "Dartmoor hot"));
    CHECK_FALSE(fr::FullVariantOf("the_banker", "Miami mild"));
    // The options left plain on these maps have no full version.
    CHECK_FALSE(fr::FullVariantOf("holiday_hoarders", "Paris mild"));
    CHECK_FALSE(fr::FullVariantOf("a_silver_tongue", "Miami mild"));
    CHECK_FALSE(fr::FullVariantOf("snow_festival", "Hokkaido hot"));
    CHECK_FALSE(fr::FullVariantOf("serial_killer", "Whittleton Creek mild"));
    CHECK_FALSE(fr::FullVariantOf("a_bitter_pill", "Paris mild"));
    CHECK(fr::FullTurnedOffNote(fr::test::Full("revolutionary")) ==
          "The Revolutionary is off: a mission of The Revolutionary did not finish loading last time. Until you try "
          "again, Santa Fortuna missions that pick The Revolutionary play the normal map.");
}

// Several full variants on one Freelancer scene (tools/landslide/gen_package_inc.py scene_problem): a pick names
// exactly one by its option and map, a swapped load exactly one by its scene (a layer by its alias key), and the
// appending ones that share a contract scene are never taken for each other.
TEST_CASE("full variants: several on one scene, one per pick") {
    const auto s_Lower = [](std::string_view p_Text) {
        std::string s_Out(p_Text);
        for (char& c : s_Out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s_Out;
    };
    std::set<std::string_view> s_Keys;
    std::map<std::string, const fr::FullVariant*> s_Swapped; // the scenes swaps load (lower case)
    std::map<std::string, std::vector<const fr::FullVariant*>> s_Appended;

    for (const fr::FullVariant* v : fr::FullVariants()) {
        CAPTURE(v->key);
        CHECK(s_Keys.insert(v->key).second);
        CHECK_FALSE((v->appends && !v->layerOf.empty()));
        if (v->appends)
            s_Appended[s_Lower(v->scene)].push_back(v);
        else if (v->layerOf.empty())
            CHECK(s_Swapped.emplace(s_Lower(v->scene), v).second); // a scene names one swap
        // A pick of its option on its map (or alsoMap) names it, and only it (the full Marrakesh night, a base
        // only, has none).
        CHECK(fr::FullVariantOf(v->option, v->map) == (v->option.empty() ? nullptr : v));
        if (!v->alsoMap.empty())
            CHECK(fr::FullVariantOf(v->option, v->alsoMap) == v);
        CHECK(fr::FullVariantOfKey(v->key) == v);
    }

    for (const fr::FullVariant* v : fr::FullVariants()) {
        CAPTURE(v->key);
        if (!v->layerOf.empty()) {
            // A layer shares its base's scene; the scene alone names the base, its key the layer.
            const fr::FullVariant* s_Base = fr::FullVariantOfKey(v->layerOf);
            REQUIRE(s_Base != nullptr);
            CHECK(s_Base->scene == v->scene);
            CHECK(fr::FullVariantOfScene(v->scene) == s_Base);
            CHECK(fr::FullVariantOfLoad(v->key, v->scene) == v);
        }
        else if (v->appends) {
            // No scene swap claims an appending variant's contract scene.
            CHECK_FALSE(s_Swapped.contains(s_Lower(v->scene)));
            CHECK_FALSE(fr::FullVariantOfScene(v->scene));
            CHECK_FALSE(fr::FullVariantOfLoad(v->key, v->scene));
        }
        else {
            CHECK(fr::FullVariantOfScene(v->scene) == v);
        }
    }

    // The shared contract scenes of today's spot fixes: each pick still names its own.
    CHECK(s_Appended[s_Lower(fr::kSantaFortunaMildScene)].size() == 2);
    CHECK(s_Appended[s_Lower(fr::kDartmoorMildScene)].size() == 2);
    CHECK(s_Appended[s_Lower(fr::kMiamiMildScene)].size() == 2);
    CHECK(fr::FullVariantOf("revolutionary", "Santa Fortuna mild") == &fr::test::Full("revolutionary"));
    CHECK(fr::FullVariantOf("embrace_of_the_serpent", "Santa Fortuna mild") == &fr::test::Full("serpent"));
    CHECK(fr::FullVariantOf("garden_show", "Dartmoor mild") == &fr::test::Full("gardenshow"));
    CHECK(fr::FullVariantOf("sloth", "Dartmoor mild") == &fr::test::Full("sloth"));
    CHECK(fr::FullVariantOf("the_undying", "Miami hot") == &fr::test::Full("undying"));
    CHECK(fr::FullVariantOf("the_undying_returns", "Miami hot") == &fr::test::Full("undyingreturns"));
    // Hokkaido: Patient Zero swaps in the outbreak's scene, The Reflection appends to the mild one.
    CHECK(fr::FullVariantOfScene(fr::kPatientZeroScene) == &fr::test::Full("patientzero"));
    CHECK_FALSE(fr::FullVariantOfScene(fr::kHokkaidoMildScene));
    CHECK(fr::FullVariantOf("reflection", "Hokkaido mild") == &fr::test::Full("reflection"));
    CHECK(fr::FullVariantOf("patient_zero", "Hokkaido mild") == &fr::test::Full("patientzero"));
    // Their sentinels are their own (by key, never by scene).
    CHECK(fr::FullSentinelFileName(fr::test::Full("revolutionary")) !=
          fr::FullSentinelFileName(fr::test::Full("serpent")));
}

TEST_CASE("full variants: the Settings note of a turned-off one") {
    CHECK(fr::FullTurnedOffNote(fr::LandslideFull()) ==
          "Landslide is off: a Landslide mission did not finish loading last time. Until you try again, "
          "Sapienza missions that pick Landslide play the normal map.");
    CHECK(fr::FullTurnedOffNote(fr::test::Full("author")) ==
          "The Author is off: a mission of The Author did not finish loading last time. Until you try again, "
          "Sapienza missions that pick The Author play the normal map.");
    CHECK(fr::FullTurnedOffNote(fr::test::Full("icon")) ==
          "The Icon is off: a mission of The Icon did not finish loading last time. Until you try again, "
          "Sapienza missions that pick The Icon play the normal map.");
    CHECK(fr::FullTurnedOffNote(fr::test::Full("marrakesh")) ==
          "Marrakesh at night is off: a Marrakesh night mission did not finish loading last time. Until you try "
          "again, Marrakesh missions that pick the night play the normal map.");

    // None of them offers a simple version.
    for (const fr::FullVariant* s_Variant : fr::FullVariants()) {
        CAPTURE(s_Variant->key);
        CHECK(fr::FullTurnedOffNote(*s_Variant).find("the normal map") != std::string_view::npos);
        CHECK(fr::FullTurnedOffNote(*s_Variant).find("simple") == std::string_view::npos);
        CHECK(fr::FullTurnedOffNote(*s_Variant).find("only") == std::string_view::npos);
    }
}

TEST_CASE("full variants: the Author's gate and overlay") {
    const fr::FullVariant& s_Author = fr::test::Full("author");
    CHECK(fr::CheckFullGate(s_Author, OpenGate()).block == fr::LandslideBlock::None);

    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    const fr::LandslideVerdict s_TurnedOff = fr::CheckFullGate(s_Author, s_Gate);
    CHECK(s_TurnedOff.block == fr::LandslideBlock::TurnedOff);
    CHECK(s_TurnedOff.why.find("The Author") != std::string::npos);
    CHECK(s_TurnedOff.why.find("Landslide") == std::string::npos);

    s_Gate = OpenGate();
    s_Gate.freelancerLayer = false;
    const fr::LandslideVerdict s_Test = fr::CheckFullGate(s_Author, s_Gate);
    CHECK(s_Test.block == fr::LandslideBlock::TestOnly);
    CHECK(s_Test.why.find("variant_force = tod_ebola_night") != std::string::npos);

    // The package's checks name no variant: the same for both.
    s_Gate = OpenGate();
    s_Gate.installed = false;
    CHECK(fr::CheckFullGate(s_Author, s_Gate).block == fr::LandslideBlock::NotInstalled);

    // What plays instead is the normal map (no night on it).
    CHECK(fr::FullOverlay(s_Author, fr::LandslideBlock::None) == "The Author");
    CHECK(fr::FullOverlay(s_Author, fr::LandslideBlock::GameChanged) ==
          "normal map (The Author could not be loaded: the game changed since this mod was made)");
    CHECK(fr::FullOverlay(s_Author, fr::LandslideBlock::OtherMod) ==
          "normal map (The Author could not be loaded: another mod changes Sapienza)");
    CHECK(fr::FullOverlay(s_Author, fr::LandslideBlock::ReadFault) == "normal map (The Author could not be loaded)");
}

TEST_CASE("full variants: the Icon's gate and overlay") {
    const fr::FullVariant& s_Icon = fr::test::Full("icon");
    CHECK(fr::CheckFullGate(s_Icon, OpenGate()).block == fr::LandslideBlock::None);

    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    const fr::LandslideVerdict s_TurnedOff = fr::CheckFullGate(s_Icon, s_Gate);
    CHECK(s_TurnedOff.block == fr::LandslideBlock::TurnedOff);
    CHECK(s_TurnedOff.why.find("a load of The Icon did not finish") != std::string::npos);

    s_Gate = OpenGate();
    s_Gate.freelancerLayer = false;
    CHECK(fr::CheckFullGate(s_Icon, s_Gate).why.find("variant_force = lighting_copperhead") != std::string::npos);

    // What plays instead is the normal map (no lighting-only Icon).
    CHECK(fr::FullOverlay(s_Icon, fr::LandslideBlock::None) == "The Icon");
    CHECK(fr::FullOverlay(s_Icon, fr::LandslideBlock::TurnedOff) ==
          "normal map (The Icon is off after a load that did not finish; try it again in Settings)");
    CHECK(fr::FullOverlay(s_Icon, fr::LandslideBlock::OtherMod) ==
          "normal map (The Icon could not be loaded: another mod changes Sapienza)");
}

TEST_CASE("icon_never_targets") {
    const auto never = fr::test::Full("icon").neverTargets;
    REQUIRE(never.size() == 2);
    // Dino Bosco, The Icon's own target, and Sophia Wilde, his agent (scripted into his Q&A).
    CHECK(never[0].name == "Dino Bosco");
    CHECK(never[0].entityId == 0x0C285FADF7D81D2Dull);
    REQUIRE(fr::ParseGuid(never[0].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[0].repo)) == "9c81b545-a234-46c5-9296-54f9f2a0bb6d");
    CHECK(never[1].name == "Sophia Wilde");
    CHECK(never[1].entityId == 0x9C3AA306C142E51Bull);
    REQUIRE(fr::ParseGuid(never[1].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[1].repo)) == "005e3ab8-508c-471b-8426-53aca5c6ef4b");
    CHECK(fr::NeverTargetRepos(fr::test::Full("icon")).size() == 2);
}

TEST_CASE("author_never_targets") {
    const auto never = fr::test::Full("author").neverTargets;
    REQUIRE(never.size() == 1);
    // Craig Black, The Author's own target (his repository id is the Author contract's target).
    CHECK(never[0].name == "Craig Black");
    CHECK(never[0].entityId == 0xC7CB1D860F5AB7BEull);
    REQUIRE(fr::ParseGuid(never[0].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[0].repo)) == "ab48a89d-e8a7-4df4-ae72-f0fccaa65e7f");

    const std::vector<std::string_view> s_Repos = fr::NeverTargetRepos(fr::test::Full("author"));
    REQUIRE(s_Repos.size() == 1);
    CHECK(s_Repos[0] == never[0].repo);
}

TEST_CASE("full variants: the Marrakesh night's gate and overlay") {
    const fr::FullVariant& s_Marrakesh = fr::test::Full("marrakesh");
    CHECK(fr::CheckFullGate(s_Marrakesh, OpenGate()).block == fr::LandslideBlock::None);

    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    CHECK(fr::CheckFullGate(s_Marrakesh, s_Gate).why.find("a Marrakesh night load did not finish") !=
          std::string::npos);

    // Its own partition in the reasons.
    s_Gate = OpenGate();
    s_Gate.partitionSeen = false;
    const fr::LandslideVerdict s_Unseen = fr::CheckFullGate(s_Marrakesh, s_Gate);
    CHECK(s_Unseen.block == fr::LandslideBlock::NotRaised);
    CHECK(s_Unseen.why.find("never saw Marrakesh's partition 25") != std::string::npos);
    s_Gate = OpenGate();
    s_Gate.raisedTo = 4;
    const fr::LandslideVerdict s_Low = fr::CheckFullGate(s_Marrakesh, s_Gate);
    CHECK(s_Low.why.find("Marrakesh's partition was created with patch level 4") != std::string::npos);
    CHECK(s_Low.why.find("\"marrakesh package: partition 25\"") != std::string::npos);
    // ... and Sapienza's as before.
    CHECK(fr::CheckFullGate(fr::test::Full("icon"), s_Gate).why.find("\"landslide package: partition 26\"") !=
          std::string::npos);

    CHECK(fr::FullOverlay(s_Marrakesh, fr::LandslideBlock::None) == "Marrakesh at night");
    CHECK(fr::FullOverlay(s_Marrakesh, fr::LandslideBlock::GameChanged) ==
          "normal map (Marrakesh at night could not be loaded: the game changed since this mod was made)");
    CHECK(fr::FullOverlay(s_Marrakesh, fr::LandslideBlock::OtherMod) ==
          "normal map (Marrakesh at night could not be loaded: another mod changes Marrakesh)");
    CHECK(fr::FullOverlay(s_Marrakesh, fr::LandslideBlock::NotRaised) ==
          "normal map (Marrakesh at night could not be loaded)");
}

TEST_CASE("full variants: the contract rule (the difficulty brick is left out)") {
    const fr::FullVariant& s_Marrakesh = fr::test::Full("marrakesh");
    const std::string s_Mission = "assembly:/_pro/scenes/missions/marrakesh/mission_mild_mancala.brick";
    const std::string s_Hard = "assembly:/_pro/scenes/missions/marrakesh/difficulty_pro1_marrakesh_spider.brick";
    const std::string s_Easy = "assembly:/_pro/scenes/missions/marrakesh/difficulty_easy_marrakesh_spider.brick";

    // Normal: the mission brick alone.
    fr::ContractVerdict s_Normal = fr::CheckFullContract(s_Marrakesh, std::vector<std::string> {s_Mission});
    CHECK(s_Normal.verdict.block == fr::LandslideBlock::None);
    CHECK(s_Normal.dropped.empty());

    // Hard (every Marrakesh log so far) and easy: accepted, the difficulty brick left out.
    fr::ContractVerdict s_HardOk = fr::CheckFullContract(s_Marrakesh, std::vector<std::string> {s_Mission, s_Hard});
    CHECK(s_HardOk.verdict.block == fr::LandslideBlock::None);
    CHECK(s_HardOk.dropped == std::vector<std::string> {s_Hard});
    CHECK(fr::DroppedBrickNote(s_HardOk.dropped[0]) ==
          "the contract's hard-difficulty brick (difficulty_pro1_marrakesh_spider) is left out: it belongs to the "
          "day level");
    const std::vector<std::string> s_EasyCase = {
        "assembly:/_PRO/Scenes/Missions/Marrakesh/Mission_Mild_Mancala.brick",
        "ASSEMBLY:/_pro/scenes/missions/marrakesh/DIFFICULTY_EASY_MARRAKESH_SPIDER.brick"};
    fr::ContractVerdict s_EasyOk = fr::CheckFullContract(s_Marrakesh, s_EasyCase);
    CHECK(s_EasyOk.verdict.block == fr::LandslideBlock::None);
    CHECK(s_EasyOk.dropped == std::vector<std::string> {s_EasyCase[1]});
    CHECK(fr::DroppedBrickNote(s_Easy).starts_with(
        "the contract's easy-difficulty brick (difficulty_easy_marrakesh_spider)"));

    // Refused: both difficulty bricks, no mission brick, it twice, another brick, nothing.
    for (const std::vector<std::string>& s_Bad :
         {std::vector<std::string> {s_Mission, s_Hard, s_Easy}, std::vector<std::string> {s_Hard},
          std::vector<std::string> {s_Mission, s_Mission},
          std::vector<std::string> {s_Mission, "assembly:/x/new.brick"}, std::vector<std::string> {}}) {
        const fr::ContractVerdict s_Verdict = fr::CheckFullContract(s_Marrakesh, s_Bad);
        CHECK(s_Verdict.verdict.block == fr::LandslideBlock::Contract);
        CHECK(s_Verdict.dropped.empty());
        CHECK(s_Verdict.verdict.why.find("with at most one of") != std::string::npos);
    }

    // Sapienza's variants drop its own difficulty brick, not Marrakesh's.
    const std::string s_Pro1 = "assembly:/_pro/scenes/missions/coastaltown/octopus_pro1.brick";
    const std::string s_SapEasy = "assembly:/_pro/scenes/missions/coastaltown/difficulty_easy_sapienza_octopus.brick";
    for (const fr::FullVariant* s_Sapienza :
         {&fr::LandslideFull(), &fr::test::Full("author"), &fr::test::Full("icon")}) {
        const fr::ContractVerdict s_Hard2 = fr::CheckFullContract(
            *s_Sapienza, std::vector<std::string> {std::string(fr::kLandslideContractBrick), s_Pro1});
        CHECK(s_Hard2.verdict.block == fr::LandslideBlock::None);
        CHECK(s_Hard2.dropped == std::vector<std::string> {s_Pro1});
        CHECK(fr::CheckFullContract(*s_Sapienza,
                                    std::vector<std::string> {std::string(fr::kLandslideContractBrick), s_SapEasy})
                  .verdict.block == fr::LandslideBlock::None);
        CHECK(fr::CheckFullContract(*s_Sapienza,
                                    std::vector<std::string> {std::string(fr::kLandslideContractBrick), s_Hard})
                  .verdict.block == fr::LandslideBlock::Contract);
    }
    CHECK(fr::DroppedBrickNote(s_Pro1) ==
          "the contract's hard-difficulty brick (octopus_pro1) is left out: it belongs to the day level");
    CHECK(fr::CheckFullContract(fr::test::Full("icon"), std::vector<std::string> {s_Mission}).verdict.block ==
          fr::LandslideBlock::Contract);
}

TEST_CASE("marrakesh_never_targets") {
    const auto never = fr::test::Full("marrakesh").neverTargets;
    REQUIRE(never.size() == 2);
    // A House Built on Sand's two targets, in the night level's base.
    CHECK(never[0].name == "Kong Tuo-Kwang");
    CHECK(never[0].entityId == 0x57E229B4E7543CAAull);
    REQUIRE(fr::ParseGuid(never[0].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[0].repo)) == "23415c1d-6ee8-41a8-9975-523f3eede581");
    CHECK(never[1].name == "Matthieu Mendola");
    CHECK(never[1].entityId == 0x67F6C22407232163ull);
    REQUIRE(fr::ParseGuid(never[1].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[1].repo)) == "4d0953f0-7f19-4130-abae-610a8f5dddca");
    CHECK(fr::NeverTargetRepos(fr::test::Full("marrakesh")).size() == 2);
}

// --- The full Patient Zero (Hokkaido's outbreak level, partition 22) ---

TEST_CASE("full variants: Patient Zero") {
    const fr::FullVariant& s_Zero = fr::test::Full("patientzero");
    CHECK(s_Zero.key == "patientzero");
    CHECK(s_Zero.display == "Patient Zero");
    CHECK(s_Zero.option == "patient_zero");
    CHECK(s_Zero.map == "Hokkaido mild");
    CHECK(s_Zero.scene == fr::kPatientZeroScene);
    CHECK(fr::kPatientZeroScene == "assembly:/_pro/scenes/missions/hokkaido/_scene_flu.entity");
    CHECK(s_Zero.fullName == "the full Patient Zero");
    // The outbreak keeps the hotel's people (npc_mission_snowcrane): mild Hokkaido's mover rows apply.
    CHECK(s_Zero.sharesCrowd);
    CHECK(s_Zero.layerOf.empty());
    // Hokkaido's own package.
    CHECK(s_Zero.partition == 22);
    CHECK(s_Zero.partitionId == "hokkaido");
    CHECK(s_Zero.location == "Hokkaido");
    CHECK(fr::FullPackageFileName(s_Zero.partition, 290) == "chunk22patch290.rpkg");
    CHECK(fr::PackageLogName(22, "hokkaido") == "hokkaido package");
    CHECK(fr::FullSentinelFileName(s_Zero) == "freelancerrandomizer-patientzero-loading.txt");
    CHECK(s_Zero.contractBrick == "assembly:/_pro/scenes/missions/hokkaido/mission_mild_shogi.brick");
    REQUIRE(s_Zero.droppable.size() == 2);

    // Mild Hokkaido only, by the option's name; by key; by its scene, any case.
    CHECK(fr::FullVariantOf("patient_zero", "Hokkaido mild") == &s_Zero);
    CHECK_FALSE(fr::FullVariantOf("patient_zero", "Hokkaido hot"));
    CHECK_FALSE(fr::FullVariantOf("patient_zero", "Sapienza mild"));
    CHECK_FALSE(fr::FullVariantOf("snow_festival", "Hokkaido mild"));
    CHECK(fr::FullVariantOfKey("patientzero") == &s_Zero);
    CHECK(fr::FullVariantOfScene("assembly:/_PRO/Scenes/Missions/Hokkaido/_scene_flu.entity") == &s_Zero);
    CHECK(fr::FullVariantOfLoad("", fr::kPatientZeroScene) == &s_Zero);
    CHECK_FALSE(fr::FullVariantOfScene("assembly:/_PRO/Scenes/Missions/hokkaido/scene_snowcrane_mild_shogi.entity"));

    CHECK(fr::FullTurnedOffNote(s_Zero) ==
          "Patient Zero is off: a Patient Zero mission did not finish loading last time. Until you try again, "
          "Hokkaido missions that pick Patient Zero play the normal map.");

    // Its never-targets: Owen Cage and Klaus Liebleid, with their entity ids in mission_flu (as
    // tools/landslide/patientzero_spec.py has them).
    const auto never = s_Zero.neverTargets;
    REQUIRE(never.size() == 2);
    CHECK(never[0].name == "Owen Cage");
    CHECK(never[0].repo == "3798fcf3-4e33-40db-a8a2-c160a3ec55bf");
    CHECK(never[0].entityId == 0x1C3B8BC862DB9082ull);
    CHECK(never[1].name == "Klaus Liebleid");
    CHECK(never[1].repo == "01822ba2-7b3b-4bbd-a9f3-cf8006ba945a");
    CHECK(never[1].entityId == 0x01432E3710A37DA0ull);
}

TEST_CASE("full variants: Patient Zero's gate and overlay") {
    const fr::FullVariant& s_Zero = fr::test::Full("patientzero");
    CHECK(fr::CheckFullGate(s_Zero, OpenGate()).block == fr::LandslideBlock::None);

    // A DLL with no recipe for it: the normal map plays, and the overlay says why.
    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.builtIn = false;
    CHECK(fr::CheckFullGate(s_Zero, s_Gate).block == fr::LandslideBlock::NotBuiltIn);
    CHECK(fr::FullOverlay(s_Zero, fr::LandslideBlock::NotBuiltIn) ==
          "normal map (Patient Zero is not in this build of the mod)");

    s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    CHECK(fr::CheckFullGate(s_Zero, s_Gate).why.find("a Patient Zero load did not finish") != std::string::npos);

    // Its own partition in the reasons.
    s_Gate = OpenGate();
    s_Gate.partitionSeen = false;
    CHECK(fr::CheckFullGate(s_Zero, s_Gate).why.find("never saw Hokkaido's partition 22") != std::string::npos);
    s_Gate = OpenGate();
    s_Gate.raisedTo = 4;
    const fr::LandslideVerdict s_Low = fr::CheckFullGate(s_Zero, s_Gate);
    CHECK(s_Low.why.find("Hokkaido's partition was created with patch level 4") != std::string::npos);
    CHECK(s_Low.why.find("\"hokkaido package: partition 22\"") != std::string::npos);

    s_Gate = OpenGate();
    s_Gate.freelancerLayer = false;
    CHECK(fr::CheckFullGate(s_Zero, s_Gate).why.find("variant_force = patient_zero") != std::string::npos);

    CHECK(fr::FullOverlay(s_Zero, fr::LandslideBlock::None) == "Patient Zero");
    CHECK(fr::FullOverlay(s_Zero, fr::LandslideBlock::OtherMod) ==
          "normal map (Patient Zero could not be loaded: another mod changes Hokkaido)");
}

TEST_CASE("full variants: Patient Zero's contract rule") {
    const fr::FullVariant& s_Zero = fr::test::Full("patientzero");
    const std::string s_Mission = "assembly:/_pro/scenes/missions/hokkaido/mission_mild_shogi.brick";
    const std::string s_Hard = "assembly:/_pro/scenes/missions/hokkaido/difficulty_pro1_hokkaido_snowcrane.brick";
    const std::string s_Easy = "assembly:/_pro/scenes/missions/hokkaido/difficulty_easy_hokkaido_snowcrane.brick";

    const fr::ContractVerdict s_Normal = fr::CheckFullContract(s_Zero, std::vector<std::string> {s_Mission});
    CHECK(s_Normal.verdict.block == fr::LandslideBlock::None);
    CHECK(s_Normal.dropped.empty());

    // The logs' case (scene_snowcrane_mild_shogi names the mission brick in any case).
    const std::vector<std::string> s_HardCase = {"assembly:/_PRO/Scenes/Missions/hokkaido/mission_mild_shogi.brick",
                                                 s_Hard};
    const fr::ContractVerdict s_HardOk = fr::CheckFullContract(s_Zero, s_HardCase);
    CHECK(s_HardOk.verdict.block == fr::LandslideBlock::None);
    CHECK(s_HardOk.dropped == std::vector<std::string> {s_Hard});
    CHECK(fr::DroppedBrickNote(s_HardOk.dropped[0], s_Zero.droppedWhy) ==
          "the contract's hard-difficulty brick (difficulty_pro1_hokkaido_snowcrane) is left out: it was made for "
          "the normal hospital, not the outbreak");
    const fr::ContractVerdict s_EasyOk = fr::CheckFullContract(s_Zero, std::vector<std::string> {s_Mission, s_Easy});
    CHECK(s_EasyOk.verdict.block == fr::LandslideBlock::None);
    CHECK(s_EasyOk.dropped == std::vector<std::string> {s_Easy});

    // Refused: both, Marrakesh's, another map's mission brick.
    CHECK(fr::CheckFullContract(s_Zero, std::vector<std::string> {s_Mission, s_Hard, s_Easy}).verdict.block ==
          fr::LandslideBlock::Contract);
    CHECK(fr::CheckFullContract(
              s_Zero,
              std::vector<std::string> {
                  s_Mission, "assembly:/_pro/scenes/missions/marrakesh/difficulty_pro1_marrakesh_spider.brick"})
              .verdict.block == fr::LandslideBlock::Contract);
    CHECK(fr::CheckFullContract(s_Zero, std::vector<std::string> {std::string(fr::kLandslideContractBrick)})
              .verdict.block == fr::LandslideBlock::Contract);

    // The other variants keep their reason (the default).
    CHECK(fr::test::Full("marrakesh").droppedWhy == "it belongs to the day level");
    CHECK(fr::LandslideFull().droppedWhy == "it belongs to the day level");
}

TEST_CASE("patientzero_never_targets") {
    const auto never = fr::test::Full("patientzero").neverTargets;
    REQUIRE(never.size() == 2);
    // The campaign contract's two Kill objectives (HOKKAIDO/PATIENTZERO.json); their entity ids are not read yet.
    CHECK(never[0].name == "Owen Cage");
    REQUIRE(fr::ParseGuid(never[0].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[0].repo)) == "3798fcf3-4e33-40db-a8a2-c160a3ec55bf");
    CHECK(never[1].name == "Klaus Liebleid");
    REQUIRE(fr::ParseGuid(never[1].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[1].repo)) == "01822ba2-7b3b-4bbd-a9f3-cf8006ba945a");
    CHECK(fr::NeverTargetRepos(fr::test::Full("patientzero")).size() == 2);
}

// --- The full Entertainer (a layer on the full Marrakesh night, partition 25) ---

TEST_CASE("full variants: the Entertainer, a layer on the Marrakesh night") {
    const fr::FullVariant& s_Entertainer = fr::test::Full("entertainer");
    const fr::FullVariant& s_Night = fr::test::Full("marrakesh");
    CHECK(s_Entertainer.key == "entertainer");
    CHECK(s_Entertainer.display == "The Entertainer");
    CHECK(s_Entertainer.option == "the_entertainer");
    CHECK(s_Entertainer.map == "Marrakesh mild");
    CHECK(s_Entertainer.fullName == "the full Entertainer");
    // The night's scene, partition and contract (its difficulty bricks left out); a layer on it.
    CHECK(s_Entertainer.scene == s_Night.scene);
    CHECK(s_Entertainer.partition == 25);
    CHECK(s_Entertainer.partitionId == "marrakesh");
    CHECK(s_Entertainer.location == "Marrakesh");
    CHECK(s_Entertainer.contractBrick == s_Night.contractBrick);
    CHECK(s_Entertainer.droppable.size() == 2);
    CHECK(s_Entertainer.droppedWhy == s_Night.droppedWhy);
    CHECK(s_Entertainer.layerOf == "marrakesh");
    CHECK_FALSE(s_Entertainer.sharesCrowd);
    CHECK(s_Night.layerOf.empty());

    CHECK(fr::FullVariantOf("the_entertainer", "Marrakesh mild") == &s_Entertainer);
    CHECK_FALSE(fr::FullVariantOf("the_entertainer", "Marrakesh hot"));
    CHECK_FALSE(fr::FullVariantOf("marrakesh_night", "Marrakesh mild")); // removed: the night only as a base
    CHECK(fr::FullVariantOfKey("entertainer") == &s_Entertainer);
    CHECK(fr::FullSentinelFileName(s_Entertainer) == "freelancerrandomizer-entertainer-loading.txt");
    CHECK(fr::FullPackageFileName(s_Entertainer.partition, 290) == "chunk25patch290.rpkg");

    // The two share a scene: the scene alone names the night; the alias's key tells them apart.
    CHECK(fr::FullVariantOfScene(fr::kMarrakeshScene) == &s_Night);
    CHECK(fr::FullVariantOfLoad("entertainer", fr::kMarrakeshScene) == &s_Entertainer);
    CHECK(fr::FullVariantOfLoad("entertainer", "assembly:/_pro/scenes/missions/Marrakesh/_scene_python.entity") ==
          &s_Entertainer);
    CHECK(fr::FullVariantOfLoad("marrakesh", fr::kMarrakeshScene) == &s_Night);
    CHECK(fr::FullVariantOfLoad("", fr::kMarrakeshScene) == &s_Night);
    CHECK(fr::FullVariantOfLoad("entertainer", fr::kIconScene) == &fr::test::Full("icon")); // a key of another scene

    CHECK(fr::FullTurnedOffNote(s_Entertainer) ==
          "The Entertainer is off: a mission of The Entertainer did not finish loading last time. Until you try "
          "again, Marrakesh missions that pick The Entertainer play the normal map.");

    // The contract rule is the night's: the hard brick is left out.
    const std::string s_Mission = "assembly:/_pro/scenes/missions/marrakesh/mission_mild_mancala.brick";
    const std::string s_Hard = "assembly:/_pro/scenes/missions/marrakesh/difficulty_pro1_marrakesh_spider.brick";
    const fr::ContractVerdict s_HardOk =
        fr::CheckFullContract(s_Entertainer, std::vector<std::string> {s_Mission, s_Hard});
    CHECK(s_HardOk.verdict.block == fr::LandslideBlock::None);
    CHECK(s_HardOk.dropped == std::vector<std::string> {s_Hard});
}

TEST_CASE("full variants: the Entertainer's gate and overlay") {
    const fr::FullVariant& s_Entertainer = fr::test::Full("entertainer");
    CHECK(fr::CheckFullGate(s_Entertainer, OpenGate()).block == fr::LandslideBlock::None);

    fr::LandslideGate s_Gate = OpenGate();
    s_Gate.turnedOff = true;
    CHECK(fr::CheckFullGate(s_Entertainer, s_Gate).why.find("a load of The Entertainer did not finish") !=
          std::string::npos);
    s_Gate = OpenGate();
    s_Gate.partitionSeen = false;
    CHECK(fr::CheckFullGate(s_Entertainer, s_Gate).why.find("never saw Marrakesh's partition 25") != std::string::npos);
    s_Gate = OpenGate();
    s_Gate.freelancerLayer = false;
    CHECK(fr::CheckFullGate(s_Entertainer, s_Gate).why.find("variant_force = the_entertainer") != std::string::npos);

    CHECK(fr::FullOverlay(s_Entertainer, fr::LandslideBlock::None) == "The Entertainer");
    CHECK(fr::FullOverlay(s_Entertainer, fr::LandslideBlock::NotBuiltIn) ==
          "normal map (The Entertainer is not in this build of the mod)");
    CHECK(fr::FullOverlay(s_Entertainer, fr::LandslideBlock::OtherMod) ==
          "normal map (The Entertainer could not be loaded: another mod changes Marrakesh)");
}

TEST_CASE("entertainer_never_targets") {
    const auto never = fr::test::Full("entertainer").neverTargets;
    REQUIRE(never.size() == 3);
    // The night's two, still loaded under him, then The Entertainer's target (the contract's one Kill objective,
    // MARRAKESH/ELUSIVE/THEENTERTAINER.json).
    CHECK(never[0].repo == fr::test::Full("marrakesh").neverTargets[0].repo);
    CHECK(never[1].repo == fr::test::Full("marrakesh").neverTargets[1].repo);
    CHECK(never[2].entityId == 0x28433E998F0F6321ull); // mission_marrakesh_maitai's CHAR_ElusiveTarget_Unique_MaiTai
    REQUIRE(fr::ParseGuid(never[2].repo).has_value());
    CHECK(fr::FormatGuid(*fr::ParseGuid(never[2].repo)) == "e63259c4-1a82-4299-9633-6ac54991e1e9");
    CHECK(fr::NeverTargetRepos(fr::test::Full("entertainer")).size() == 3);
}
