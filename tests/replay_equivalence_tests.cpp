#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/replay/ReplayPlayer.hpp"
#include "edgenetswitch/replay/ReplayRecorder.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <unistd.h>

using namespace edgenetswitch;

namespace
{
    constexpr int kMaxWaitIterations = 1000;
    constexpr useconds_t kWaitStepUs = 1000;

    template <typename Predicate> bool waitUntil(const Predicate &predicate)
    {
        for (int i = 0; i < kMaxWaitIterations; ++i)
        {
            if (predicate())
                return true;

            ::usleep(kWaitStepUs);
        }

        return false;
    }

    Packet makePacket(std::uint64_t id, std::uint64_t lifecycle_id, std::uint64_t timestamp_ms)
    {
        Packet packet{};
        packet.id = id;
        packet.lifecycle_id = lifecycle_id;
        packet.timestamp_ms = timestamp_ms;
        packet.payload = "replay-equivalence-payload-" + std::to_string(id);
        packet.wire_size = static_cast<std::uint32_t>(packet.payload.size() + 16);
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.valid = true;
        packet.source_ip = "203.0.113." + std::to_string(id);
        packet.source_port = static_cast<std::uint16_t>(14000 + id);
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

    bool hasConverged(const PacketStats &stats,
                      std::uint64_t snapshot_time_ms,
                      std::uint64_t packet_count,
                      PacketMetrics &metrics)
    {
        metrics = stats.snapshotAt(snapshot_time_ms);
        return metrics.ingress_packets == packet_count &&
               metrics.pending_terminal_events == 0 &&
               metrics.terminal_events == packet_count;
    }
} // namespace

TEST_CASE("Replay produces equivalent PacketStats snapshot", "[Replay][Equivalence]")
{
    constexpr std::uint64_t now_ms = 1700002000;
    constexpr std::uint64_t packet_count = 5;
    constexpr std::uint64_t snapshot_time_ms = now_ms + packet_count;

    std::vector<Packet> packets;
    packets.reserve(packet_count);
    for (std::uint64_t index = 0; index < packet_count; ++index)
    {
        packets.push_back(makePacket(100 + index, 5000 + index, now_ms + index));
    }

    PacketMetrics original_metrics{};
    std::vector<ReplayRecord> records;

    {
        MessagingBus original_bus;
        PacketStats original_stats(original_bus);
        PacketProcessor original_processor(original_bus);
        ReplayRecorder recorder(original_bus);

        for (const Packet &packet : packets)
        {
            original_bus.publish(packetRxMessage(packet));
        }

        REQUIRE(waitUntil(
            [&]
            { return hasConverged(original_stats, snapshot_time_ms, packet_count, original_metrics); }));

        original_metrics = original_stats.snapshotAt(snapshot_time_ms);
        records = recorder.snapshot();
    }

    REQUIRE(records.size() == packet_count);

    PacketMetrics replay_metrics{};

    {
        MessagingBus replay_bus;
        PacketStats replay_stats(replay_bus);
        PacketProcessor replay_processor(replay_bus);
        ReplayPlayer player(replay_bus);

        player.replay(records);

        REQUIRE(waitUntil(
            [&]
            { return hasConverged(replay_stats, snapshot_time_ms, packet_count, replay_metrics); }));

        replay_metrics = replay_stats.snapshotAt(snapshot_time_ms);
    }

    REQUIRE(replay_metrics.ingress_packets == original_metrics.ingress_packets);
    REQUIRE(replay_metrics.processed_packets == original_metrics.processed_packets);
    REQUIRE(replay_metrics.terminal_events == original_metrics.terminal_events);
    REQUIRE(replay_metrics.pending_terminal_events == original_metrics.pending_terminal_events);
    REQUIRE(replay_metrics.duplicate_events == original_metrics.duplicate_events);
    REQUIRE(replay_metrics.drops_by_reason == original_metrics.drops_by_reason);
}
