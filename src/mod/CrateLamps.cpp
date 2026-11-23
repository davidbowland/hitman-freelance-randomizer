// Night crate lamps (see CrateLamps.h). Engine reads and writes only through
// the guarded helpers of EntityWalk.h and EntityReads.h.

#include "CrateLamps.h"

#include "../core/LootMerces.h" // fr::FindLootTemplate, fr::LootKind::Stash
#include "EngineText.h"
#include "EntityWalk.h"
#include "LogTopics.h"
#include "ModLog.h"
#include "EntityReads.h"
#include "Scene.h"
#include "SceneOf.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <span>
#include <string_view>

namespace {

// More sub-entities than this in one brick is a bad read: the brick is skipped.
constexpr int64_t kMaxBrickSubEntities = 2'000'000;
// The open line waits this long after a crate's side lights first change, so
// the lamp core has sent its power (and any fade has settled).
constexpr double kOpenReportSeconds = 1.0;
// A side light differing from its scene-playing value by more: its crate opened.
constexpr float kChangeTolerance = 0.01f;
// Failures listed per part in the summary line.
constexpr size_t kMaxListed = 4;

constexpr size_t kLampCoreIndex = 0;
constexpr size_t kFirstSideIndex = 3;

constexpr std::string_view kTag = "crate lamps";

// A float32 property: its raw storage when it has one (address set), else
// through its getter (the type's float listing).
struct FloatRead {
    std::optional<float> value;
    std::string why;       // when not read
    uintptr_t address = 0; // raw storage, when read from it
};

FloatRead ReadFloat(ZEntityType** p_Entity, std::string_view p_Name) {
    const PropertyLocation s_Location = LocateProperty(p_Entity, p_Name);

    switch (s_Location.status) {
        case PropertyLocation::Status::Missing: return {std::nullopt, "no such property"};
        case PropertyLocation::Status::Fault: return {std::nullopt, "locating it: " + FaultText(s_Location.faultCode)};
        case PropertyLocation::Status::HasGetter: {
            const FloatProperties s_Floats = ReadFloatProperties(p_Entity);

            for (const NamedFloat& s_Float : s_Floats.floats) {
                if (s_Float.name == p_Name)
                    return s_Float.value ? FloatRead {*s_Float.value, {}}
                                         : FloatRead {std::nullopt, "getter read failed"};
            }

            return {std::nullopt, s_Floats.faultCode ? "getter: listing faulted" : "getter: not a float32"};
        }
        case PropertyLocation::Status::Ok: break;
    }

    if (std::string_view(s_Location.typeName) != "float32" || s_Location.typeSize != sizeof(float))
        return {std::nullopt,
                fmt::format("engine type {} (size {})", s_Location.typeName[0] ? s_Location.typeName : "<unknown>",
                            s_Location.typeSize)};

    float s_Value = 0.0f;

    if (const uint32_t s_Code = ReadEngineBytes(s_Location.address, &s_Value, sizeof(s_Value)))
        return {std::nullopt, FaultText(s_Code)};

    return {s_Value, {}, s_Location.address};
}

std::string FloatText(const FloatRead& p_Read) {
    return p_Read.value ? fmt::format("{:g}", *p_Read.value) : fmt::format("<{}>", p_Read.why);
}

// One part lowered to its night cap: the old value, the setter's result and
// the value read back.
struct Lowered {
    std::optional<float> before;
    bool written = false; // above the cap: written
    bool took = false;    // written and read back as written (or at or below the cap already)
    std::string why;      // when it did not take
};

Lowered LowerToCap(ZEntityType** p_Entity, const fr::CrateLampEntity& p_Part) {
    const FloatRead s_Before = ReadFloat(p_Entity, p_Part.property);

    if (!s_Before.value)
        return {std::nullopt, false, false, s_Before.why};

    const std::optional<float> s_Target = fr::CrateLampNightValue(p_Part.part, *s_Before.value);

    if (!s_Target)
        return {s_Before.value, false, true, {}};

    bool s_Ok = false;

    if (const uint32_t s_Code = WriteFloatProperty(p_Entity, p_Part.property, *s_Target, &s_Ok))
        return {s_Before.value, true, false, "SetProperty " + FaultText(s_Code)};

    const FloatRead s_Back = ReadFloat(p_Entity, p_Part.property);
    const bool s_Took = s_Ok && s_Back.value && *s_Back.value == *s_Target;
    std::string s_Why;

    if (!s_Ok)
        s_Why = "SetProperty returned false";
    else if (!s_Took)
        s_Why = "read back " + FloatText(s_Back);

    return {s_Before.value, true, s_Took, s_Why};
}

// What one part did over all crates, for the summary line.
struct PartTally {
    std::vector<float> before;
    size_t written = 0;
    size_t took = 0;
    size_t atCap = 0;   // already at or below the cap
    size_t missing = 0; // the entity was not found in the crate
    size_t noProperty = 0;
    std::vector<std::string> failures; // "crate 3: <why>", at most kMaxListed
    size_t moreFailures = 0;
};

std::string TallyText(const fr::CrateLampEntity& p_Part, const PartTally& p_Tally) {
    const float s_Cap = fr::CrateLampNightCap(p_Part.part);
    std::string s_Text = fmt::format(
        "{} {} {} -> {:g}: {} written, {} read back as written", fr::CrateLampPartName(p_Part.part), p_Part.property,
        fr::CrateLampValueSpan(p_Tally.before), s_Cap, p_Tally.written, p_Tally.took - p_Tally.atCap);

    if (p_Tally.atCap)
        s_Text += fmt::format(", {} already at or below {:g}", p_Tally.atCap, s_Cap);

    if (p_Tally.missing)
        s_Text += fmt::format(", {} crates without it", p_Tally.missing);

    if (!p_Tally.failures.empty()) {
        s_Text += "; not changed: ";

        for (size_t i = 0; i < p_Tally.failures.size(); ++i)
            s_Text += fmt::format("{}{}", i ? ", " : "", p_Tally.failures[i]);

        if (p_Tally.moreFailures)
            s_Text += fmt::format(" and {} more", p_Tally.moreFailures);
    }

    return s_Text;
}

} // namespace

void CrateLamps::OnLoadingStage(ESceneLoadingStage p_Stage, ZEntitySceneContext* p_Context,
                                const AppliedVariant& p_Variant) noexcept {
    if (!p_Context || p_Stage != ESceneLoadingStage::eLoading_ScenePlaying)
        return;

    Contained(kTag, "scene playing", [&] {
        OnSceneClear();
        const std::string s_Scene = SceneOf(p_Context); // a full variant counts as its contract's scene

        if (!fr::IsFreelancerMission(p_Context->m_SceneInitParameters.m_Type.ToStringView(), s_Scene))
            return;

        m_Scene = SceneLabelOf(p_Context->m_SceneInitParameters.m_SceneResource.ToStringView());
        const bool s_ThisLoad = !p_Variant.scene.empty() && p_Variant.scene == s_Scene;
        const fr::NightVerdict s_Night =
            s_ThisLoad ? fr::JudgeNightLoad(p_Variant.option, p_Variant.full) : fr::JudgeNightLoad({}, false);

        if (!s_Night.night) {
            LogTo(fr::LogTopic::MapVariants,
                  "crate lamps: {}: a day load ({}): the supply crates' lamps are left as "
                  "they are",
                  m_Scene, s_Night.why);
            return;
        }

        Find(p_Context);
        Dim(s_Night);
    });
}

void CrateLamps::Tick(double p_GameSeconds) noexcept {
    if (!m_Watching)
        return;

    m_Play += p_GameSeconds;
    Contained(kTag, "play", [&] { Hold(); });
}

void CrateLamps::OnSceneClear() noexcept {
    m_Scene.clear();
    m_Crates.clear();
    m_Watching = false;
    m_Play = 0.0;
}

// One pass over each loaded brick's own sub-entities: the crate template's instances.
void CrateLamps::Find(ZEntitySceneContext* p_Context) {
    const auto& s_Bricks = p_Context->m_aLoadedBricks;
    size_t s_Faults = 0;
    std::string s_Skipped;

    for (size_t b = 0; b < s_Bricks.size(); ++b) {
        const SBrickAllocationInfo& s_Brick = s_Bricks[b];
        ZEntityType** s_Root = s_Brick.m_EntityRef.m_pObj;
        ZEntityBlueprintFactoryBase* s_Factory = nullptr;
        int64_t s_Count = 0;
        const char* s_Why = nullptr;

        if (!s_Root)
            s_Why = "no root entity";
        else if (ReadOwnBlueprintFactory(s_Root, &s_Factory))
            s_Why = "blueprint factory faulted";
        else if (!s_Factory)
            s_Why = "no blueprint factory";
        else if (ReadSubEntityCount(s_Factory, &s_Count))
            s_Why = "sub-entity count faulted";
        else if (s_Count < 0 || s_Count > kMaxBrickSubEntities)
            s_Why = "implausible sub-entity count";

        if (s_Why) {
            s_Skipped += fmt::format("{}{:016X} ({})", s_Skipped.empty() ? "" : ", ",
                                     s_Brick.m_RuntimeResourceID.GetID(), s_Why);
            continue;
        }

        for (int i = 0; i < s_Count; ++i) {
            ZEntityType** s_Sub = nullptr;
            ZEntityBlueprintFactoryBase* s_SubFactory = nullptr;
            uint64_t s_Rid = 0;

            if (ReadSubEntityAt(s_Factory, s_Root, i, &s_Sub, &s_SubFactory) || ReadFactoryRid(s_SubFactory, &s_Rid)) {
                ++s_Faults;
                continue;
            }

            const fr::LootTemplate* s_Template = fr::FindLootTemplate(s_Rid);

            if (!s_Sub || !s_Template || s_Template->kind != fr::LootKind::Stash)
                continue;

            Crate s_Crate;
            s_Crate.entity = s_Sub;
            s_Crate.factory = s_SubFactory;
            s_Crate.brickRid = s_Brick.m_RuntimeResourceID.GetID();
            std::string s_Class;

            if (ReadEntityClassName(s_Sub, s_Class, &s_Crate.id))
                ++s_Faults;

            m_Crates.push_back(s_Crate);
        }
    }

    if (s_Faults || !s_Skipped.empty())
        LogTo(fr::LogTopic::MapVariants, "crate lamps: {}: crate search: {} read faults{}", m_Scene, s_Faults,
              s_Skipped.empty() ? "" : fmt::format("; bricks skipped: {}", s_Skipped));

    // Each crate's lamp entities, by their sub-entity ids in the template.
    const auto s_Entities = fr::CrateLampEntities();

    for (size_t c = 0; c < m_Crates.size(); ++c) {
        Crate& s_Crate = m_Crates[c];
        std::string s_Missing;

        for (size_t i = 0; i < s_Entities.size(); ++i) {
            s_Crate.parts[i] = LookUpSubEntity(s_Crate.entity, s_Crate.factory, s_Entities[i].id).entity;

            if (!s_Crate.parts[i])
                s_Missing +=
                    fmt::format("{}{} ({:016x})", s_Missing.empty() ? "" : ", ", s_Entities[i].name, s_Entities[i].id);
        }

        if (!s_Missing.empty())
            LogTo(fr::LogTopic::MapVariants,
                  "crate lamps: {}: crate {}/{} (entity {:016x} in brick {:016X}): not found "
                  "in the crate: {}; those lights keep their day power (the crate template may have changed with a "
                  "game update)",
                  m_Scene, c + 1, m_Crates.size(), s_Crate.id, s_Crate.brickRid, s_Missing);
    }
}

// Lowers each crate's center light, environment light and lamp core to their
// night caps, and sets up the side lights' watch.
void CrateLamps::Dim(const fr::NightVerdict& p_Night) {
    if (m_Crates.empty()) {
        LogTo(fr::LogTopic::MapVariants,
              "crate lamps: {}: a night load ({}), but no supply crate "
              "(setpiece_evergreen_stash_unique) was found among the loaded bricks' own entities: the crates' lamps "
              "keep their day power",
              m_Scene, p_Night.why);
        return;
    }

    const auto s_Entities = fr::CrateLampEntities();
    std::array<PartTally, kFirstSideIndex> s_Tallies {};
    size_t s_Watched = 0;
    std::vector<float> s_SideLoaded;
    std::string s_Unwatched;

    for (size_t c = 0; c < m_Crates.size(); ++c) {
        Crate& s_Crate = m_Crates[c];

        for (size_t i = 0; i < kFirstSideIndex; ++i) {
            PartTally& s_Tally = s_Tallies[i];

            if (!s_Crate.parts[i]) {
                ++s_Tally.missing;
                continue;
            }

            const Lowered s_Lowered = LowerToCap(s_Crate.parts[i], s_Entities[i]);

            if (s_Lowered.before)
                s_Tally.before.push_back(*s_Lowered.before);

            s_Tally.written += s_Lowered.written ? 1 : 0;
            s_Tally.took += s_Lowered.took ? 1 : 0;
            s_Tally.atCap += s_Lowered.took && !s_Lowered.written ? 1 : 0;

            if (s_Lowered.took)
                continue;

            if (!s_Lowered.before && s_Lowered.why == "no such property")
                ++s_Tally.noProperty;
            else if (s_Tally.failures.size() < kMaxListed)
                s_Tally.failures.push_back(fmt::format("crate {}: {}", c + 1, s_Lowered.why));
            else
                ++s_Tally.moreFailures;
        }

        for (size_t s = 0; s < fr::kCrateSideLightCount; ++s) {
            const size_t i = kFirstSideIndex + s;
            Side& s_Side = s_Crate.sides[s];
            s_Side.entity = s_Crate.parts[i];

            if (!s_Side.entity)
                continue;

            const FloatRead s_Read = ReadFloat(s_Side.entity, s_Entities[i].property);

            if (!s_Read.value || !s_Read.address) {
                if (s_Unwatched.size() < 300)
                    s_Unwatched += fmt::format("{}crate {} {}: {}", s_Unwatched.empty() ? "" : ", ", c + 1,
                                               s_Entities[i].name, s_Read.value ? "no raw storage" : s_Read.why);
                continue;
            }

            s_Side.address = s_Read.address;
            s_Side.loaded = *s_Read.value;
            s_SideLoaded.push_back(*s_Read.value);
            ++s_Watched;
        }
    }

    m_Watching = s_Watched > 0;

    // The lamp core's DiffusePower may be a template alias, not a property of its entity at run time: then
    // the side lights are held at the cap in play instead.
    const PartTally& s_Core = s_Tallies[kLampCoreIndex];
    const float s_SideCap = fr::CrateLampNightCap(fr::CrateLampPart::Side);
    std::string s_CoreText;

    if (s_Core.noProperty && s_Core.noProperty + s_Core.missing == m_Crates.size())
        s_CoreText = fmt::format("lamp core DiffusePower: not a property of its entity at run time (a template alias), "
                                 "so the side lights are held at {:g} when a crate opens instead",
                                 s_SideCap);
    else {
        s_CoreText = TallyText(s_Entities[kLampCoreIndex], s_Core);

        if (s_Core.noProperty)
            s_CoreText += fmt::format(", not a property at run time on {} crates", s_Core.noProperty);
    }

    LogTo(fr::LogTopic::MapVariants,
          "crate lamps: {}: a night load ({}): {} supply crates dimmed so an open does not "
          "blind; {}; {}; {}; side lights watched in play: {} of {} (at {} now; anything above {:g} is set back to "
          "it){}",
          m_Scene, p_Night.why, m_Crates.size(), TallyText(s_Entities[1], s_Tallies[1]),
          TallyText(s_Entities[2], s_Tallies[2]), s_CoreText, s_Watched, m_Crates.size() * fr::kCrateSideLightCount,
          fr::CrateLampValueSpan(s_SideLoaded), s_SideCap,
          s_Unwatched.empty() ? std::string() : fmt::format("; not watched: {}", s_Unwatched));
}

// Every play-mode frame: a side light above the cap (the lamp core sent its
// day power) is set back to it; a crate whose side lights changed has opened.
void CrateLamps::Hold() {
    const float s_Cap = fr::CrateLampNightCap(fr::CrateLampPart::Side);
    const auto s_Entities = fr::CrateLampEntities();

    for (size_t c = 0; c < m_Crates.size(); ++c) {
        Crate& s_Crate = m_Crates[c];

        for (size_t s = 0; s < fr::kCrateSideLightCount; ++s) {
            Side& s_Side = s_Crate.sides[s];

            if (!s_Side.address)
                continue;

            float s_Value = 0.0f;

            if (const uint32_t s_Code = ReadEngineBytes(s_Side.address, &s_Value, sizeof(s_Value))) {
                LogTo(fr::LogTopic::MapVariants,
                      "crate lamps: {}: crate {}/{}: reading a side light faulted ({}); "
                      "it is no longer watched",
                      m_Scene, c + 1, m_Crates.size(), FaultText(s_Code));
                s_Side.address = 0;
                continue;
            }

            if (!s_Crate.openedAt && std::fabs(s_Value - s_Side.loaded) > kChangeTolerance)
                s_Crate.openedAt = m_Play;

            if (!fr::CrateLampNightValue(fr::CrateLampPart::Side, s_Value))
                continue;

            const std::string_view s_Property = s_Entities[kFirstSideIndex + s].property;
            bool s_Ok = false;
            const uint32_t s_Code = WriteFloatProperty(s_Side.entity, s_Property, s_Cap, &s_Ok);

            if (s_Code || !s_Ok) {
                LogTo(fr::LogTopic::MapVariants,
                      "crate lamps: {}: crate {}/{}: setting a side light's {} back failed "
                      "({}); it is no longer watched",
                      m_Scene, c + 1, m_Crates.size(), s_Property,
                      s_Code ? FaultText(s_Code) : std::string("SetProperty returned false"));
                s_Side.address = 0;
                continue;
            }

            ++s_Crate.held;
        }

        if (s_Crate.openedAt && !s_Crate.openLogged && m_Play - *s_Crate.openedAt >= kOpenReportSeconds) {
            s_Crate.openLogged = true;
            LogOpen(c);
        }
    }
}

// About a second after a crate opened: what its lamp shows.
void CrateLamps::LogOpen(size_t p_Index) {
    const Crate& s_Crate = m_Crates[p_Index];
    const auto s_Entities = fr::CrateLampEntities();
    const auto s_Value = [&](size_t i) {
        return s_Crate.parts[i] ? FloatText(ReadFloat(s_Crate.parts[i], s_Entities[i].property))
                                : std::string("<not found>");
    };

    std::string s_Sides;

    for (size_t s = 0; s < fr::kCrateSideLightCount; ++s)
        s_Sides += fmt::format("{}{}", s ? ", " : "", s_Value(kFirstSideIndex + s));

    const PositionRead s_Position = ReadPosition(s_Crate.entity, true);
    const std::string s_At = s_Position.at ? fmt::format(" at ({:.2f}, {:.2f}, {:.2f})", s_Position.at->x,
                                                         s_Position.at->y, s_Position.at->z)
                                           : std::string();
    const float s_Cap = fr::CrateLampNightCap(fr::CrateLampPart::Side);

    LogTo(fr::LogTopic::MapVariants,
          "crate lamps: {}: crate {}/{} (entity {:016x}){} opened at {:.0f} s of play: side "
          "lights {} ({}), center light {}, environment light {}, lamp core DiffusePower {}",
          m_Scene, p_Index + 1, m_Crates.size(), s_Crate.id, s_At, *s_Crate.openedAt, s_Sides,
          s_Crate.held ? fmt::format("the lamp core sent more than {:g}: set back {} times", s_Cap, s_Crate.held)
                       : fmt::format("the lamp core's own power, at most {:g}", s_Cap),
          s_Value(1), s_Value(2), s_Value(kLampCoreIndex));
}
