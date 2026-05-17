#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/packet/Packet.hpp"
#include "edgenetswitch/switching/ForwardingDecision.hpp"
#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

using namespace edgenetswitch;

namespace
{
    MacAddress mac(std::string_view text)
    {
        auto parsed = MacAddress::fromString(text);
        REQUIRE(parsed.has_value());
        return *parsed;
    }

    Packet makePacket(const MacAddress &source, const MacAddress &destination)
    {
        Packet packet{};
        packet.id = 42;
        packet.lifecycle_id = 1001;
        packet.timestamp_ms = 1234;
        packet.payload = "payload";
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.wire_size = packet.payload_size + 14;
        packet.valid = true;
        packet.source_mac = source;
        packet.destination_mac = destination;
        return packet;
    }

    Packet makePacketWithoutMacs()
    {
        Packet packet{};
        packet.id = 43;
        packet.lifecycle_id = 1002;
        packet.timestamp_ms = 1235;
        packet.payload = "payload";
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.wire_size = packet.payload_size;
        packet.valid = true;
        return packet;
    }

    const std::vector<std::uint32_t> kPorts{1, 2, 3, 4};
} // namespace

TEST_CASE("SwitchForwardingEngine drops packets without MAC addresses",
          "[SwitchForwardingEngine]")
{
    MacTable mac_table(16);
    SwitchForwardingEngine engine(mac_table);

    SECTION("missing source and destination")
    {
        const Packet packet = makePacketWithoutMacs();

        const ForwardingDecision decision = engine.processPacket(packet, 1, 10, kPorts);

        REQUIRE(decision.action == ForwardingAction::Drop);
        REQUIRE(decision.egress_ports.empty());
        REQUIRE(mac_table.size() == 0);
    }

    SECTION("missing source")
    {
        Packet packet = makePacketWithoutMacs();
        packet.destination_mac = mac("00:11:22:33:44:55");

        const ForwardingDecision decision = engine.processPacket(packet, 1, 10, kPorts);

        REQUIRE(decision.action == ForwardingAction::Drop);
        REQUIRE(decision.egress_ports.empty());
        REQUIRE(mac_table.size() == 0);
    }

    SECTION("missing destination")
    {
        Packet packet = makePacketWithoutMacs();
        packet.source_mac = mac("00:11:22:33:44:66");

        const ForwardingDecision decision = engine.processPacket(packet, 1, 10, kPorts);

        REQUIRE(decision.action == ForwardingAction::Drop);
        REQUIRE(decision.egress_ports.empty());
        REQUIRE(mac_table.size() == 0);
    }
}

TEST_CASE("SwitchForwardingEngine floods broadcast packets except ingress",
          "[SwitchForwardingEngine]")
{
    MacTable mac_table(16);
    SwitchForwardingEngine engine(mac_table);
    const Packet packet = makePacket(mac("00:11:22:33:44:01"),
                                     mac("ff:ff:ff:ff:ff:ff"));

    const ForwardingDecision decision = engine.processPacket(packet, 2, 10, kPorts);

    REQUIRE(decision.action == ForwardingAction::Flood);
    REQUIRE(decision.egress_ports == std::vector<std::uint32_t>{1, 3, 4});
}

TEST_CASE("SwitchForwardingEngine floods unknown unicast packets except ingress",
          "[SwitchForwardingEngine]")
{
    MacTable mac_table(16);
    SwitchForwardingEngine engine(mac_table);
    const Packet packet = makePacket(mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"));

    const ForwardingDecision decision = engine.processPacket(packet, 3, 10, kPorts);

    REQUIRE(decision.action == ForwardingAction::Flood);
    REQUIRE(decision.egress_ports == std::vector<std::uint32_t>{1, 2, 4});
}

TEST_CASE("SwitchForwardingEngine forwards known unicast packets to learned port only",
          "[SwitchForwardingEngine]")
{
    MacTable mac_table(16);
    SwitchForwardingEngine engine(mac_table);
    const MacAddress source = mac("00:11:22:33:44:01");
    const MacAddress destination = mac("00:11:22:33:44:02");
    mac_table.learn(destination, 4, 5);

    const Packet packet = makePacket(source, destination);
    const ForwardingDecision decision = engine.processPacket(packet, 2, 10, kPorts);

    REQUIRE(decision.action == ForwardingAction::ForwardToPorts);
    REQUIRE(decision.egress_ports == std::vector<std::uint32_t>{4});
}

TEST_CASE("SwitchForwardingEngine learns source MAC ingress port",
          "[SwitchForwardingEngine]")
{
    MacTable mac_table(16);
    SwitchForwardingEngine engine(mac_table);
    const MacAddress source = mac("00:11:22:33:44:01");
    const Packet packet = makePacket(source, mac("00:11:22:33:44:02"));

    const ForwardingDecision decision = engine.processPacket(packet, 2, 10, kPorts);

    REQUIRE(decision.action == ForwardingAction::Flood);
    REQUIRE(mac_table.lookup(source) == 2);
}

TEST_CASE("SwitchForwardingEngine relearns source MAC on new ingress port",
          "[SwitchForwardingEngine]")
{
    MacTable mac_table(16);
    SwitchForwardingEngine engine(mac_table);
    const MacAddress source = mac("00:11:22:33:44:01");
    const Packet first_packet = makePacket(source, mac("00:11:22:33:44:02"));
    const Packet second_packet = makePacket(source, mac("00:11:22:33:44:03"));

    const ForwardingDecision first_decision =
        engine.processPacket(first_packet, 1, 10, kPorts);
    const ForwardingDecision second_decision =
        engine.processPacket(second_packet, 3, 11, kPorts);

    REQUIRE(first_decision.action == ForwardingAction::Flood);
    REQUIRE(second_decision.action == ForwardingAction::Flood);
    REQUIRE(mac_table.lookup(source) == 3);
}
