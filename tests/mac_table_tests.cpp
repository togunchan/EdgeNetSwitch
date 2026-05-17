#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/MacTable.hpp"

#include <optional>
#include <string_view>

using namespace edgenetswitch;

namespace
{
    MacAddress mac(std::string_view text)
    {
        auto parsed = MacAddress::fromString(text);
        REQUIRE(parsed.has_value());
        return *parsed;
    }
} // namespace

TEST_CASE("MacTable ageOut removes stale entries", "[MacTable]")
{
    MacTable table(4);
    const MacAddress stale = mac("00:11:22:33:44:01");

    table.learn(stale, 2, 79);
    table.ageOut(100, 20);

    REQUIRE(table.lookup(stale) == std::nullopt);
    REQUIRE(table.size() == 0);
}

TEST_CASE("MacTable ageOut preserves fresh entries", "[MacTable]")
{
    MacTable table(4);
    const MacAddress fresh = mac("00:11:22:33:44:02");

    table.learn(fresh, 3, 80);
    table.ageOut(100, 20);

    REQUIRE(table.lookup(fresh) == 3);
    REQUIRE(table.size() == 1);
}

TEST_CASE("MacTable ageOut handles mixed stale and fresh entries", "[MacTable]")
{
    MacTable table(8);
    const MacAddress stale_first = mac("00:11:22:33:44:01");
    const MacAddress fresh_first = mac("00:11:22:33:44:02");
    const MacAddress stale_second = mac("00:11:22:33:44:03");
    const MacAddress fresh_second = mac("00:11:22:33:44:04");

    table.learn(stale_first, 1, 70);
    table.learn(fresh_first, 2, 90);
    table.learn(stale_second, 3, 79);
    table.learn(fresh_second, 4, 100);

    table.ageOut(100, 20);

    REQUIRE(table.lookup(stale_first) == std::nullopt);
    REQUIRE(table.lookup(stale_second) == std::nullopt);
    REQUIRE(table.lookup(fresh_first) == 2);
    REQUIRE(table.lookup(fresh_second) == 4);
    REQUIRE(table.size() == 2);
}

TEST_CASE("MacTable ageOut preserves entries newer than max age", "[MacTable]")
{
    MacTable table(4);
    const MacAddress fresh = mac("00:11:22:33:44:05");

    table.learn(fresh, 5, 81);
    table.ageOut(100, 20);

    REQUIRE(table.lookup(fresh) == 5);
    REQUIRE(table.size() == 1);
}
