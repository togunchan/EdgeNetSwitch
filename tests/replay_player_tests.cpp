#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/replay/ReplayPlayer.hpp"
#include "edgenetswitch/replay/ReplayRecord.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace edgenetswitch;

namespace
{
    Packet makePacket(std::uint64_t id,
                      std::uint64_t lifecycle_id,
                      std::uint64_t timestamp_ms,
                      std::string payload)
    {
        Packet packet{};
        packet.id = id;
        packet.lifecycle_id = lifecycle_id;
        packet.timestamp_ms = timestamp_ms;
        packet.payload = std::move(payload);
        packet.wire_size = static_cast<std::uint32_t>(packet.payload.size() + 16);
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.valid = true;
        packet.source_ip = "198.51.100." + std::to_string(id);
        packet.source_port = static_cast<std::uint16_t>(12000 + id);
        return packet;
    }

    ReplayRecord makeRecord(std::uint64_t sequence,
                            std::uint64_t id,
                            std::uint64_t lifecycle_id,
                            std::uint64_t timestamp_ms,
                            std::string payload)
    {
        return ReplayRecord{
            .sequence = sequence,
            .packet = makePacket(id, lifecycle_id, timestamp_ms, std::move(payload)),
        };
    }

    void requireSamePacket(const Packet &actual, const Packet &expected)
    {
        REQUIRE(actual.id == expected.id);
        REQUIRE(actual.lifecycle_id == expected.lifecycle_id);
        REQUIRE(actual.timestamp_ms == expected.timestamp_ms);
        REQUIRE(actual.payload == expected.payload);
        REQUIRE(actual.wire_size == expected.wire_size);
        REQUIRE(actual.payload_size == expected.payload_size);
        REQUIRE(actual.valid == expected.valid);
        REQUIRE(actual.source_ip == expected.source_ip);
        REQUIRE(actual.source_port == expected.source_port);
    }

    void requireSameRecord(const ReplayRecord &actual, const ReplayRecord &expected)
    {
        REQUIRE(actual.sequence == expected.sequence);
        requireSamePacket(actual.packet, expected.packet);
    }

    std::vector<Packet> replayAndCapturePackets(const std::vector<ReplayRecord> &records)
    {
        MessagingBus bus;
        ReplayPlayer player(bus);
        std::vector<Packet> observed;

        bus.subscribe(MessageType::PacketRx,
                      [&](const Message &message)
                      {
                          const auto *packet = std::get_if<Packet>(&message.payload);
                          REQUIRE(packet != nullptr);
                          REQUIRE(message.timestamp_ms == packet->timestamp_ms);
                          observed.push_back(*packet);
                      });

        player.replay(records);

        return observed;
    }
} // namespace

TEST_CASE("ReplayPlayer publishes PacketRx events during replay", "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records{
        makeRecord(0, 10, 1001, 1700001000, "alpha"),
    };

    const auto observed = replayAndCapturePackets(records);

    REQUIRE(observed.size() == 1);
    requireSamePacket(observed[0], records[0].packet);
}

TEST_CASE("ReplayPlayer preserves recorded ingress ordering", "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records{
        makeRecord(20, 20, 2001, 1700001100, "first"),
        makeRecord(10, 21, 2002, 1700001101, "second"),
        makeRecord(30, 22, 2003, 1700001102, "third"),
    };

    const auto observed = replayAndCapturePackets(records);

    REQUIRE(observed.size() == records.size());
    REQUIRE(observed[0].id == records[0].packet.id);
    REQUIRE(observed[1].id == records[1].packet.id);
    REQUIRE(observed[2].id == records[2].packet.id);
}

TEST_CASE("ReplayPlayer preserves lifecycle ids", "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records{
        makeRecord(0, 30, 3001, 1700001200, "first"),
        makeRecord(1, 31, 3002, 1700001201, "second"),
        makeRecord(2, 32, 3003, 1700001202, "third"),
    };

    const auto observed = replayAndCapturePackets(records);

    REQUIRE(observed.size() == records.size());
    REQUIRE(observed[0].lifecycle_id == 3001);
    REQUIRE(observed[1].lifecycle_id == 3002);
    REQUIRE(observed[2].lifecycle_id == 3003);
}

TEST_CASE("ReplayPlayer preserves packet payload data", "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records{
        makeRecord(0, 40, 4001, 1700001300, "payload:alpha"),
        makeRecord(1, 41, 4002, 1700001301, "payload:beta"),
    };

    const auto observed = replayAndCapturePackets(records);

    REQUIRE(observed.size() == records.size());
    REQUIRE(observed[0].payload == "payload:alpha");
    REQUIRE(observed[0].payload_size == records[0].packet.payload_size);
    REQUIRE(observed[1].payload == "payload:beta");
    REQUIRE(observed[1].payload_size == records[1].packet.payload_size);
}

TEST_CASE("ReplayPlayer empty input produces no PacketRx events", "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records;

    const auto observed = replayAndCapturePackets(records);

    REQUIRE(observed.empty());
}

TEST_CASE("ReplayPlayer does not mutate original ReplayRecord input", "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records{
        makeRecord(0, 50, 5001, 1700001400, "immutable-first"),
        makeRecord(1, 51, 5002, 1700001401, "immutable-second"),
    };
    const std::vector<ReplayRecord> original = records;

    const auto observed = replayAndCapturePackets(records);

    REQUIRE(observed.size() == records.size());
    REQUIRE(records.size() == original.size());
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        requireSameRecord(records[index], original[index]);
    }
}

TEST_CASE("ReplayPlayer produces identical PacketRx ordering across repeated runs",
          "[ReplayPlayer]")
{
    const std::vector<ReplayRecord> records{
        makeRecord(0, 60, 6001, 1700001500, "deterministic-first"),
        makeRecord(1, 61, 6002, 1700001501, "deterministic-second"),
        makeRecord(2, 62, 6003, 1700001502, "deterministic-third"),
    };

    const auto firstRun = replayAndCapturePackets(records);
    const auto secondRun = replayAndCapturePackets(records);

    REQUIRE(firstRun.size() == records.size());
    REQUIRE(secondRun.size() == records.size());
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        requireSamePacket(firstRun[index], records[index].packet);
        requireSamePacket(secondRun[index], records[index].packet);
        requireSamePacket(secondRun[index], firstRun[index]);
    }
}
