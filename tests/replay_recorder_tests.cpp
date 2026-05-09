#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/replay/ReplayRecorder.hpp"

#include <cstdint>
#include <string>

using namespace edgenetswitch;

namespace
{
    Packet makePacket(std::uint64_t id, std::uint64_t lifecycle_id, std::uint64_t timestamp_ms)
    {
        Packet packet{};
        packet.id = id;
        packet.lifecycle_id = lifecycle_id;
        packet.timestamp_ms = timestamp_ms;
        packet.payload = "packet-" + std::to_string(id);
        packet.wire_size = static_cast<std::uint32_t>(packet.payload.size() + 16);
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.valid = true;
        packet.source_ip = "192.0.2." + std::to_string(id);
        packet.source_port = static_cast<std::uint16_t>(10000 + id);
        return packet;
    }

    Message packetRxMessage(const Packet &packet)
    {
        Message message{};
        message.type = MessageType::PacketRx;
        message.timestamp_ms = packet.timestamp_ms;
        message.payload = packet;
        return message;
    }
} // namespace

TEST_CASE("ReplayRecorder snapshot is empty before PacketRx events", "[ReplayRecorder]")
{
    MessagingBus bus;
    ReplayRecorder recorder(bus);

    REQUIRE(recorder.snapshot().empty());
}

TEST_CASE("ReplayRecorder records PacketRx events", "[ReplayRecorder]")
{
    MessagingBus bus;
    ReplayRecorder recorder(bus);

    const Packet packet = makePacket(42, 1001, 1700000000);

    bus.publish(packetRxMessage(packet));

    const auto records = recorder.snapshot();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].sequence == 0);
    REQUIRE(records[0].packet.id == packet.id);
    REQUIRE(records[0].packet.lifecycle_id == packet.lifecycle_id);
    REQUIRE(records[0].packet.timestamp_ms == packet.timestamp_ms);
    REQUIRE(records[0].packet.payload == packet.payload);
    REQUIRE(records[0].packet.source_ip == packet.source_ip);
    REQUIRE(records[0].packet.source_port == packet.source_port);
}

TEST_CASE("ReplayRecorder sequence ordering increments monotonically", "[ReplayRecorder]")
{
    MessagingBus bus;
    ReplayRecorder recorder(bus);

    for (std::uint64_t index = 0; index < 4; ++index)
    {
        const Packet packet = makePacket(index + 1, 2000 + index, 1700000100 + index);
        bus.publish(packetRxMessage(packet));
    }

    const auto records = recorder.snapshot();
    REQUIRE(records.size() == 4);

    for (std::uint64_t index = 0; index < records.size(); ++index)
    {
        REQUIRE(records[index].sequence == index);
    }
}

TEST_CASE("ReplayRecorder preserves PacketRx ingress order", "[ReplayRecorder]")
{
    MessagingBus bus;
    ReplayRecorder recorder(bus);

    const Packet first = makePacket(10, 3001, 1700000200);
    const Packet second = makePacket(11, 3002, 1700000201);
    const Packet third = makePacket(12, 3003, 1700000202);

    bus.publish(packetRxMessage(first));
    bus.publish(packetRxMessage(second));
    bus.publish(packetRxMessage(third));

    const auto records = recorder.snapshot();
    REQUIRE(records.size() == 3);
    REQUIRE(records[0].packet.lifecycle_id == first.lifecycle_id);
    REQUIRE(records[1].packet.lifecycle_id == second.lifecycle_id);
    REQUIRE(records[2].packet.lifecycle_id == third.lifecycle_id);
    REQUIRE(records[0].packet.id == first.id);
    REQUIRE(records[1].packet.id == second.id);
    REQUIRE(records[2].packet.id == third.id);
}

TEST_CASE("ReplayRecorder snapshot returns an immutable copy", "[ReplayRecorder]")
{
    MessagingBus bus;
    ReplayRecorder recorder(bus);

    const Packet packet = makePacket(21, 4001, 1700000300);
    bus.publish(packetRxMessage(packet));

    auto snapshot = recorder.snapshot();
    REQUIRE(snapshot.size() == 1);

    snapshot.clear();
    snapshot.push_back({.sequence = 99, .packet = makePacket(99, 4999, 1700000399)});

    const auto records = recorder.snapshot();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].sequence == 0);
    REQUIRE(records[0].packet.id == packet.id);
    REQUIRE(records[0].packet.lifecycle_id == packet.lifecycle_id);
}

TEST_CASE("ReplayRecorder ignores non-Packet payloads", "[ReplayRecorder]")
{
    MessagingBus bus;
    ReplayRecorder recorder(bus);

    Message telemetryMessage{};
    telemetryMessage.type = MessageType::PacketRx;
    telemetryMessage.timestamp_ms = 1700000400;
    telemetryMessage.payload = TelemetryData{
        .uptime_ms = 10,
        .tick_count = 1,
        .timestamp_ms = telemetryMessage.timestamp_ms,
    };

    bus.publish(telemetryMessage);

    REQUIRE(recorder.snapshot().empty());
}
