#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

#include "edgenetswitch/packet/PacketParser.hpp"

using namespace edgenetswitch;

TEST_CASE("parsePacket handles valid input with payload", "[PacketParser]")
{
    const std::string input = "id=42;payload=hello";
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == 42);
    REQUIRE(packet.payload == "hello");
}

TEST_CASE("parsePacket handles valid input without payload", "[PacketParser]")
{
    const std::string input = "id=1";
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == 1);
    REQUIRE(packet.payload.empty());
}

TEST_CASE("parsePacket rejects input missing id", "[PacketParser]")
{
    const std::string input = "payload=hello";
    const Packet packet = parsePacket(input);
    REQUIRE_FALSE(packet.valid);
}

TEST_CASE("parsePacket rejects non-numeric id", "[PacketParser]")
{
    const std::string input = "id=abc;payload=x";
    const Packet packet = parsePacket(input);
    REQUIRE_FALSE(packet.valid);
}

TEST_CASE("parsePacket rejects empty input", "[PacketParser]")
{
    const std::string input;
    const Packet packet = parsePacket(input);
    REQUIRE_FALSE(packet.valid);
}

TEST_CASE("parsePacket rejects malformed input", "[PacketParser]")
{
    const std::string input = "id=;payload=";
    const Packet packet = parsePacket(input);
    REQUIRE_FALSE(packet.valid);
}

TEST_CASE("parsePacket handles input with extra fields without crashing", "[PacketParser]")
{
    const std::string input = "id=5;payload=test;foo=bar";
    REQUIRE_NOTHROW(parsePacket(input));
}

TEST_CASE("parsePacket handles uint64_t max id", "[PacketParser]")
{
    const std::uint64_t max_id = std::numeric_limits<std::uint64_t>::max();
    const std::string input = "id=" + std::to_string(max_id) + ";payload=max";
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == max_id);
    REQUIRE(packet.payload == "max");
}

TEST_CASE("parsePacket handles leading and trailing spaces without crashing", "[PacketParser]")
{
    const std::string input = " id=42 ; payload=hello ";
    REQUIRE_NOTHROW(parsePacket(input));
}

TEST_CASE("parsePacket accepts different field order", "[PacketParser]")
{
    const std::string input = "payload=hello;id=42";
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == 42);
    REQUIRE(packet.payload == "hello");
}

TEST_CASE("parsePacket handles multiple semicolons between fields", "[PacketParser]")
{
    const std::string input = "id=1;;payload=x";
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == 1);
    REQUIRE(packet.payload == "x");
}

TEST_CASE("parsePacket handles empty payload value", "[PacketParser]")
{
    const std::string input = "id=5;payload=";
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == 5);
    REQUIRE(packet.payload.empty());
}

TEST_CASE("parsePacket rejects id key without value", "[PacketParser]")
{
    const std::string input = "id=";
    const Packet packet = parsePacket(input);
    REQUIRE_FALSE(packet.valid);
}

TEST_CASE("parsePacket rejects random garbage input", "[PacketParser]")
{
    const std::string input = "!@#$%^&*()";
    const Packet packet = parsePacket(input);
    REQUIRE_FALSE(packet.valid);
}

TEST_CASE("parsePacket handles long payload strings", "[PacketParser]")
{
    const std::string long_payload(1500, 'x');
    const std::string input = "id=77;payload=" + long_payload;
    const Packet packet = parsePacket(input);
    REQUIRE(packet.valid);
    REQUIRE(packet.id == 77);
    REQUIRE(packet.payload == long_payload);
}

TEST_CASE("parsePacket handles duplicate id fields without crashing", "[PacketParser]")
{
    const std::string input = "id=1;id=2;payload=x";
    REQUIRE_NOTHROW(parsePacket(input));
}
