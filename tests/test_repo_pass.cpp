#include "doctest.h"

#include "RepoPass.h"

TEST_CASE("repo_pass_overrun") {
    // A pass may visit every entry (and one more, for slack); beyond that the
    // map's links are broken and the pass stops.
    CHECK_FALSE(fr::RepoPassOverran(0, 0));
    CHECK_FALSE(fr::RepoPassOverran(1, 0));
    CHECK(fr::RepoPassOverran(2, 0));
    CHECK_FALSE(fr::RepoPassOverran(17'243, 17'243));
    CHECK_FALSE(fr::RepoPassOverran(17'244, 17'243));
    CHECK(fr::RepoPassOverran(17'245, 17'243));
}
