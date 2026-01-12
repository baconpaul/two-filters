/*
 * Two Filters
 *
 * Two Filters, and some controls thereof
 *
 * Copyright 2024-2026, Paul Walker and Various authors, as described in the github
 * transaction log.
 *
 * This source repo is released under the MIT license, but has
 * GPL3 dependencies, as such the combined work will be
 * released under GPL3.
 *
 * The source code and license are at https://github.com/baconpaul/two-filters
 */

#define CATCH_CONFIG_RUNNER
#include "catch2/catch2.hpp"

int main(int argc, char *argv[])
{
    int result = Catch::Session().run(argc, argv);

    return result;
}

// void *hInstance = 0;

TEST_CASE("Tests Exist", "[basics]")
{
    SECTION("Asset True") { REQUIRE(1); }
}
