#include "doctest.h"

#include "Guid.h"
#include "ParamNames.h"
#include "PatchBook.h"

#include <cmath>
#include <optional>
#include <string>

TEST_CASE("param_label") {
    CHECK(fr::ParamLabel(1) == "EDP_AIAttentionGainMultiplier");
    CHECK(fr::ParamLabel(std::nullopt) == "<unreadable m_eParameter>");
    CHECK(fr::ParamLabel(-5) == "<unknown parameter -5>");
}

TEST_CASE("guid_format_and_parse") {
    // A GUID from Realistic AI's repository file.
    const auto guid = fr::ParseGuid("fc471a2e-198d-4c35-97d9-54b8bbce927e");
    REQUIRE(guid.has_value());
    CHECK(guid->data1 == 0xfc471a2eu);
    CHECK(guid->data2 == 0x198d);
    CHECK(guid->data3 == 0x4c35);
    CHECK(guid->data4[0] == 0x97);
    CHECK(guid->data4[1] == 0xd9);
    CHECK(guid->data4[2] == 0x54);
    CHECK(guid->data4[7] == 0x7e);
    CHECK(fr::FormatGuid(*guid) == "fc471a2e-198d-4c35-97d9-54b8bbce927e");

    // Upper case parses; output is lower case with leading zeros kept.
    const auto upper = fr::ParseGuid("047CB532-1791-4A28-A9DA-6AA7CFCFC178");
    REQUIRE(upper.has_value());
    CHECK(fr::FormatGuid(*upper) == "047cb532-1791-4a28-a9da-6aa7cfcfc178");

    fr::Guid small;
    small.data1 = 1;
    small.data4[7] = 2;
    CHECK(fr::FormatGuid(small) == "00000001-0000-0000-0000-000000000002");
    CHECK(fr::FormatGuid(fr::Guid {}) == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("guid_parse_rejects_malformed") {
    CHECK_FALSE(fr::ParseGuid("").has_value());
    CHECK_FALSE(fr::ParseGuid("fc471a2e198d4c3597d954b8bbce927e").has_value());
    CHECK_FALSE(fr::ParseGuid("fc471a2e-198d-4c35-97d9-54b8bbce927").has_value());
    CHECK_FALSE(fr::ParseGuid("fc471a2e-198d-4c35-97d9-54b8bbce927g").has_value());
    CHECK_FALSE(fr::ParseGuid("fc471a2e-198d-4c35-97d954-b8bbce927e").has_value());
}

TEST_CASE("guid_compare_and_empty") {
    const auto a = *fr::ParseGuid("00000001-0000-0000-0000-000000000000");
    const auto b = *fr::ParseGuid("00000001-0000-0000-0000-000000000001");
    CHECK(a == a);
    CHECK(a != b);
    CHECK(((a < b) != (b < a)));
    CHECK_FALSE(a < a);
    CHECK(fr::Guid {}.IsEmpty());
    CHECK_FALSE(a.IsEmpty());

    // High/Low split the 16 bytes; distinct GUIDs give distinct keys.
    CHECK((a.High() != b.High() || a.Low() != b.Low()));

    // Round trip through the halves (repository patch keys).
    const auto c = *fr::ParseGuid("fc471a2e-198d-4c35-97d9-54b8bbce927e");
    CHECK(fr::Guid::FromHalves(c.High(), c.Low()) == c);
    CHECK(fr::FormatGuid(fr::Guid::FromHalves(c.High(), c.Low())) == "fc471a2e-198d-4c35-97d9-54b8bbce927e");
}

TEST_CASE("restore_decision") {
    const fr::Patch patch {7, 2.0, 5.0};
    CHECK(fr::DecideRestore(5.0, patch) == fr::RestoreDecision::Restore);
    CHECK(fr::DecideRestore(2.0, patch) == fr::RestoreDecision::AlreadyOriginal);
    CHECK(fr::DecideRestore(3.0, patch) == fr::RestoreDecision::ChangedElsewhere);

    // Written equal to the original (x1, e.g. a kept default): nothing to restore.
    const fr::Patch same {7, 2.0, 2.0};
    CHECK(fr::DecideRestore(2.0, same) == fr::RestoreDecision::AlreadyOriginal);
}

TEST_CASE("patch_book_no_stacking") {
    fr::PatchBook book;
    const fr::PatchKey key {0x1234, 0};
    CHECK(book.Empty());

    // Unknown key: roll from the current value.
    CHECK(book.BaseFor(key, 7, 0.6) == 0.6);
    CHECK_FALSE(book.WouldRestoreBase(key, 7, 0.6));

    book.Record(key, 7, 0.6, 0.012);
    CHECK(book.Size() == 1);

    // Still holds what was written (restore missed): roll from the original.
    CHECK(book.WouldRestoreBase(key, 7, 0.012));
    CHECK(book.BaseFor(key, 7, 0.012) == 0.6);

    // Changed since, or another parameter on that key: roll from current.
    CHECK(book.BaseFor(key, 7, 0.5) == 0.5);
    CHECK(book.BaseFor(key, 8, 0.012) == 0.012);

    // A second write keeps the first original (two entities sharing one GUID,
    // or a re-apply): never stacks.
    book.Record(key, 7, 0.012, 0.024);
    REQUIRE(book.Find(key) != nullptr);
    CHECK(book.Find(key)->original == 0.6);
    CHECK(book.Find(key)->written == 0.024);
    CHECK(book.BaseFor(key, 7, 0.024) == 0.6);

    // A different tag replaces the record.
    book.Record(key, 8, 1.0, 2.0);
    CHECK(book.Find(key)->tag == 8);
    CHECK(book.Find(key)->original == 1.0);

    book.Forget(key);
    CHECK(book.Find(key) == nullptr);
    CHECK(book.Empty());
}

TEST_CASE("patch_book_entries_and_clear") {
    fr::PatchBook book;
    book.Record({2, 0}, 1, 1.0, 2.0);
    book.Record({1, 5}, 1, 3.0, 4.0);
    const auto entries = book.Entries();
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].first == fr::PatchKey {1, 5});
    CHECK(entries[1].first == fr::PatchKey {2, 0});
    book.Clear();
    CHECK(book.Empty());
}

TEST_CASE("scale_repo_value") {
    CHECK(fr::ScaleRepoValue(0.15, 4.0, false) == 0.15 * 4.0);
    CHECK(fr::ScaleRepoValue(0.15, 4.0, true) == static_cast<double>(static_cast<float>(0.15 * 4.0)));
    CHECK(fr::ScaleRepoValue(40.0, 0.02, false) == 40.0 * 0.02);
    CHECK(fr::ScaleRepoValue(0.0, 3.0, false) == 0.0);
}

TEST_CASE("entity_matches_repo_value") {
    // A float32 entity built from a float64 entry holds the entry rounded to float.
    CHECK(fr::EntityMatchesRepo(static_cast<float>(60.0 * 0.05), 60.0 * 0.05));
    CHECK(fr::EntityMatchesRepo(3.0f, 3.0));
    CHECK(fr::EntityMatchesRepo(0.0f, 0.0));
    CHECK(fr::EntityMatchesRepo(-2.5f, -2.5));
    // One float step off still matches (rounding of an intermediate).
    CHECK(fr::EntityMatchesRepo(std::nextafter(3.0f, 4.0f), 3.0));
    CHECK_FALSE(fr::EntityMatchesRepo(60.0f, 3.0));
    CHECK_FALSE(fr::EntityMatchesRepo(3.01f, 3.0));
    CHECK_FALSE(fr::EntityMatchesRepo(std::nanf(""), 3.0));
}
