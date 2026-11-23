#include "doctest.h"

#include "VariantDamage.h"

#include <optional>

// Sloth's ancestral_harebell: "Damage" (10 every 1 s once vitality runs out)
// and "Damage effect" (one hit at 0.01 s, its damage left at the class's own).
TEST_CASE("variant damage: the damage over time class, in any case") {
    CHECK(fr::IsDamageOverTimeClass("ZHitmanDamageOverTime"));
    CHECK(fr::IsDamageOverTimeClass("zhitmandamageovertime"));
    CHECK_FALSE(fr::IsDamageOverTimeClass("ZHM5HealthSoundController"));
    CHECK_FALSE(fr::IsDamageOverTimeClass("ZHitmanDamageOverTimeX"));
    CHECK_FALSE(fr::IsDamageOverTimeClass(""));
}

TEST_CASE("variant damage: what it is set to never hurts and never ticks") {
    CHECK(fr::kDamageOff == 0.0f);
    CHECK(fr::kIntervalOff >= 1.0e6f); // longer than any mission
    CHECK(fr::kDamageProperty == "m_fDamage");
    CHECK(fr::kIntervalProperty == "m_fInterval");
}

TEST_CASE("variant damage: values before") {
    CHECK(fr::DamageBeforeText({10.0f, 1.0f}) == "10 every 1 s");
    CHECK(fr::DamageBeforeText({std::nullopt, 0.01f}) == "? every 0.01 s");
    CHECK(fr::DamageBeforeText({}) == "? every ? s");
}

TEST_CASE("variant damage: read back") {
    CHECK(fr::DamageReadBackOk(0.0f, fr::kDamageOff));
    CHECK_FALSE(fr::DamageReadBackOk(10.0f, fr::kDamageOff));
    CHECK(fr::DamageReadBackOk(fr::kIntervalOff, fr::kIntervalOff));
    CHECK_FALSE(fr::DamageReadBackOk(1.0f, fr::kIntervalOff));
    CHECK(fr::DamageReadBackOk(std::nullopt, fr::kDamageOff)); // not readable: SetProperty's result stands
}

TEST_CASE("variant damage: the log line's tally") {
    CHECK(fr::DamageTallyText({}) == "timed damage: none in this variant's bricks");

    fr::DamageTally tally;
    fr::AddDamage(tally, {10.0f, 1.0f}, true);
    fr::AddDamage(tally, {std::nullopt, 0.01f}, true);
    CHECK(tally.switchedOff == 2);
    CHECK(fr::DamageTallyText(tally) ==
          "timed damage switched off (2 ZHitmanDamageOverTime; they dealt 10 every 1 s, ? "
          "every 0.01 s; set to 0 damage, never ticking)");

    fr::AddDamage(tally, {5.0f, 2.0f}, false);
    CHECK(fr::DamageTallyText(tally) ==
          "timed damage switched off (3 ZHitmanDamageOverTime, 1 could not be switched off: "
          "47 may still take damage; they dealt 10 every 1 s, ? every 0.01 s, 5 every 2 s; "
          "set to 0 damage, never ticking)");

    fr::DamageTally failed;
    fr::AddDamage(failed, {10.0f, 1.0f}, false);
    CHECK(failed.failed == 1);
    CHECK(fr::DamageTallyText(failed) == "timed damage not switched off (1 ZHitmanDamageOverTime, 1 could not be "
                                         "switched off: 47 may still take damage; they dealt 10 every 1 s; set to 0 "
                                         "damage, never ticking)");
}
