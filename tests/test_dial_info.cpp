#include "doctest.h"

#include "support/DialTestUtil.h"

#include "DialInfo.h"
#include "Dials.h"
#include "ParamNames.h"

#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace {

int32_t P(std::string_view name) {
    return *fr::ParamFromName(name);
}

} // namespace

TEST_CASE("dial_info_covers_dial_table") {
    const auto info = fr::DialInfoTable();
    REQUIRE(info.size() == fr::test::AllDials().size());

    std::set<std::string_view> names;
    std::set<int32_t> params;

    for (size_t i = 0; i < info.size(); ++i) {
        CAPTURE(fr::ParamLabel(info[i].param));
        // One entry per dial.
        CHECK(fr::FindDial(info[i].param) != nullptr);
        CHECK(params.insert(info[i].param).second);
        CHECK(fr::FindDialInfo(info[i].param) == &info[i]);
        CHECK_FALSE(info[i].name.empty());
        CHECK_FALSE(info[i].description.empty());
        CHECK(names.insert(info[i].name).second);
        // A description is documented or inferred; only a unit can be read off the name.
        CHECK(info[i].basis != fr::Basis::Named);
        // A documented unit or description names its source; any other entry has none.
        const bool anyDocumented = info[i].basis == fr::Basis::Documented || info[i].unitBasis == fr::Basis::Documented;
        CHECK(anyDocumented == !info[i].source.empty());
        // Plain ASCII, so the overlay font can draw it.
        for (const char c : info[i].description)
            CHECK(static_cast<unsigned char>(c) < 0x80);
        for (const char c : info[i].name)
            CHECK(static_cast<unsigned char>(c) < 0x80);
    }

    CHECK(fr::FindDialInfo(P("EDP_SecurityCameras")) == nullptr);
    CHECK(fr::FindDialInfo(-1) == nullptr);
}

TEST_CASE("dial_info_units") {
    CHECK(fr::UnitLabel(fr::DialUnit::Meters) == "m");
    CHECK(fr::UnitLabel(fr::DialUnit::Seconds) == "s");
    CHECK(fr::UnitLabel(fr::DialUnit::Multiplier) == "x");
    CHECK(fr::UnitLabel(fr::DialUnit::Attention) == "attention");
    CHECK(fr::UnitLabel(fr::DialUnit::Count) == "");
    CHECK(fr::UnitLabel(fr::DialUnit::PerSecond) == "/s");
    CHECK(fr::UnitLabel(fr::DialUnit::Percent) == "%");
    CHECK(fr::UnitLabel(fr::DialUnit::Degrees) == "deg");
    CHECK(fr::UnitLabel(fr::DialUnit::HitPoints) == "HP");

    // Every hearing range is in meters.
    for (const auto& info : fr::DialInfoTable()) {
        if (fr::ParamName(info.param).starts_with("EDP_HearRange"))
            CHECK(info.unit == fr::DialUnit::Meters);
    }
}

TEST_CASE("dial_display_name") {
    CHECK(fr::DialDisplayName(P("EDP_HearRangeGunShot")) == "Gunshot hearing range");
    CHECK(fr::DialDisplayName(P("EDP_SecurityCameras")) == "EDP_SecurityCameras");
    CHECK(fr::DialDisplayName(99999) == "<unknown parameter 99999>");
}

TEST_CASE("dial_number_format") {
    CHECK(fr::FormatDialNumber(60) == "60");
    CHECK(fr::FormatDialNumber(120.108444) == "120.1");
    CHECK(fr::FormatDialNumber(0.726357311) == "0.7264");
    CHECK(fr::FormatDialNumber(13.9225171) == "13.92");
    CHECK(fr::FormatDialNumber(0) == "0");
    CHECK(fr::FormatDialNumber(-2.5) == "-2.5");
    CHECK(fr::FormatDialNumber(15000.4) == "15000");
    CHECK(fr::FormatDialNumber(2e6) == "2e+06");
    CHECK(fr::FormatDialNumber(std::numeric_limits<double>::quiet_NaN()) == "nan");
    CHECK(fr::FormatDialNumber(std::numeric_limits<double>::infinity()) == "inf");
}

TEST_CASE("dial_change_format") {
    using fr::Basis;
    using fr::DialUnit;
    CHECK(fr::FormatDialValues(60, 120.108444, DialUnit::Meters) == "60 -> 120.1");
    CHECK(fr::FormatDialValues(1.1, 1.43, DialUnit::Multiplier) == "x1.1 -> x1.43");
    CHECK(fr::FormatDialValues(0.3, 0.45, DialUnit::Attention) == "0.3 -> 0.45");
    CHECK(fr::FormatDialChange(60, 120.108444, DialUnit::Meters, Basis::Documented) == "60 -> 120.1 m");
    CHECK(fr::FormatDialChange(60, 120.108444, DialUnit::Meters, Basis::Inferred) == "60 -> 120.1 m (?)");
    CHECK(fr::FormatDialChange(2, 3, DialUnit::Seconds, Basis::Documented) == "2 -> 3 s");
    CHECK(fr::FormatDialChange(2, 3, DialUnit::Seconds, Basis::Named) == "2 -> 3 s");
    CHECK(fr::FormatDialChange(1.1, 1.43, DialUnit::Multiplier, Basis::Documented) == "x1.1 -> x1.43");
    CHECK(fr::FormatDialChange(1.1, 1.43, DialUnit::Multiplier, Basis::Inferred) == "x1.1 -> x1.43 (?)");
    CHECK(fr::FormatDialChange(0.3, 0.45, DialUnit::Attention, Basis::Inferred) == "0.3 -> 0.45 attention (?)");
    CHECK(fr::FormatDialChange(2, 3, DialUnit::Count, Basis::Documented) == "2 -> 3");
    CHECK(fr::FormatDialChange(2, 3, DialUnit::Count, Basis::Inferred) == "2 -> 3 (?)");
    CHECK(fr::FormatDialChange(5, 2.5, DialUnit::PerSecond, Basis::Inferred) == "5 -> 2.5/s (?)");
    CHECK(fr::FormatDialChange(50, 75, DialUnit::Percent, Basis::Documented) == "50 -> 75%");
    CHECK(fr::FormatDialChange(90, 45, DialUnit::Degrees, Basis::Documented) == "90 -> 45 deg");
    CHECK(fr::FormatDialChange(100, 80, DialUnit::HitPoints, Basis::Documented) == "100 -> 80 HP");
}

TEST_CASE("dial_harder_when") {
    CHECK(fr::HarderWhen(1) == "larger");
    CHECK(fr::HarderWhen(-1) == "smaller");
    CHECK(fr::HarderWhen(0) == "neither");
}

TEST_CASE("dial_basis_labels") {
    CHECK(fr::BasisLabel(fr::Basis::Documented) == "documented");
    CHECK(fr::BasisLabel(fr::Basis::Named) == "from its name");
    CHECK(fr::BasisLabel(fr::Basis::Inferred) == "inferred");
}
