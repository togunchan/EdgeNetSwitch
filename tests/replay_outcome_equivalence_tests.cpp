#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/failure/FailureInjector.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/replay/ReplayOutcomeCollector.hpp"
#include "edgenetswitch/replay/ReplayPlayer.hpp"
#include "edgenetswitch/replay/ReplayRecorder.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace edgenetswitch;

namespace
{
    constexpr std::chrono::milliseconds kOutcomeWaitTimeout{2000};
    constexpr std::chrono::milliseconds kOutcomeWaitStep{1};

    struct ReplayRunCapture
    {
        std::vector<ReplayRecord> records;
        std::vector<ReplayOutcome> outcomes;
    };

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
        packet.source_ip = "203.0.113." + std::to_string(id);
        packet.source_port = static_cast<std::uint16_t>(15000 + id);
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

    bool waitForOutcomes(const ReplayOutcomeCollector &collector, std::size_t expected_count)
    {
        const auto deadline = std::chrono::steady_clock::now() + kOutcomeWaitTimeout;

        while (std::chrono::steady_clock::now() < deadline)
        {
            if (collector.snapshot().size() == expected_count)
                return true;

            std::this_thread::sleep_for(kOutcomeWaitStep);
        }

        return collector.snapshot().size() == expected_count;
    }

    ReplayRunCapture runOriginalRuntime(const std::vector<Packet> &packets)
    {
        MessagingBus bus;
        PacketStats stats(bus);
        ReplayRecorder recorder(bus);
        ReplayOutcomeCollector outcome_collector(bus);
        PacketProcessor processor(bus);

        for (const Packet &packet : packets)
        {
            bus.publish(packetRxMessage(packet));
        }

        REQUIRE(waitForOutcomes(outcome_collector, packets.size()));

        return ReplayRunCapture{
            .records = recorder.snapshot(),
            .outcomes = outcome_collector.snapshot(),
        };
    }

    std::vector<ReplayOutcome> runReplayRuntime(const std::vector<ReplayRecord> &records)
    {
        MessagingBus bus;
        PacketStats stats(bus);
        ReplayOutcomeCollector outcome_collector(bus);
        PacketProcessor processor(bus);
        ReplayPlayer player(bus);

        player.replay(records);

        REQUIRE(waitForOutcomes(outcome_collector, records.size()));

        return outcome_collector.snapshot();
    }

    ReplayRunCapture runOriginalRuntimeWithFailureConfig(
        const std::vector<Packet> &packets,
        const failure::FailureConfig &failure_config)
    {
        MessagingBus bus;
        PacketStats stats(bus);
        ReplayRecorder recorder(bus);
        ReplayOutcomeCollector outcome_collector(bus);
        PacketProcessor processor(bus, nullptr, nullptr, failure::FailureInjector{failure_config});

        for (std::size_t index = 0; index < packets.size(); ++index)
        {
            bus.publish(packetRxMessage(packets[index]));
            REQUIRE(waitForOutcomes(outcome_collector, index + 1));
        }

        return ReplayRunCapture{
            .records = recorder.snapshot(),
            .outcomes = outcome_collector.snapshot(),
        };
    }

    std::vector<ReplayOutcome> runReplayRuntimeWithFailureConfig(
        const std::vector<ReplayRecord> &records,
        const failure::FailureConfig &failure_config)
    {
        MessagingBus bus;
        PacketStats stats(bus);
        ReplayOutcomeCollector outcome_collector(bus);
        PacketProcessor processor(bus, nullptr, nullptr, failure::FailureInjector{failure_config});
        ReplayPlayer player(bus);

        for (std::size_t index = 0; index < records.size(); ++index)
        {
            const std::vector<ReplayRecord> next_record{records[index]};
            player.replay(next_record);
            REQUIRE(waitForOutcomes(outcome_collector, index + 1));
        }

        return outcome_collector.snapshot();
    }

    void requireSameTerminalOrdering(const std::vector<ReplayOutcome> &original,
                                     const std::vector<ReplayOutcome> &replayed)
    {
        REQUIRE(original.size() == replayed.size());

        for (std::size_t index = 0; index < original.size(); ++index)
        {
            REQUIRE(original[index].sequence == index);
            REQUIRE(replayed[index].sequence == index);
            REQUIRE(replayed[index].type == original[index].type);
            REQUIRE(replayed[index].lifecycle_id == original[index].lifecycle_id);
            REQUIRE(replayed[index].drop_reason == original[index].drop_reason);
        }
    }
} // namespace

TEST_CASE("Replay reproduces terminal event outcomes exactly", "[Replay][Outcome][Equivalence]")
{
    constexpr std::uint64_t base_time_ms = 1700003000;

    const std::vector<Packet> packets{
        makePacket(100, 9001, base_time_ms + 1, "deterministic-valid-first"),
        makePacket(101, 9002, base_time_ms + 2, std::string(600, 'x')),
        makePacket(102, 9003, base_time_ms + 3, "deterministic-valid-second"),
        makePacket(103, 9004, 0, "missing-timestamp-validation-drop"),
        makePacket(104, 9005, base_time_ms + 5, "deterministic-valid-third"),
    };

    const std::vector<ReplayOutcome> expected_outcomes{
        {.sequence = 0,
         .type = ReplayOutcomeType::Processed,
         .lifecycle_id = 9001,
         .drop_reason = PacketDropReason::Unknown},
        {.sequence = 1,
         .type = ReplayOutcomeType::Dropped,
         .lifecycle_id = 9002,
         .drop_reason = PacketDropReason::ValidationError},
        {.sequence = 2,
         .type = ReplayOutcomeType::Processed,
         .lifecycle_id = 9003,
         .drop_reason = PacketDropReason::Unknown},
        {.sequence = 3,
         .type = ReplayOutcomeType::Dropped,
         .lifecycle_id = 9004,
         .drop_reason = PacketDropReason::ValidationError},
        {.sequence = 4,
         .type = ReplayOutcomeType::Processed,
         .lifecycle_id = 9005,
         .drop_reason = PacketDropReason::Unknown},
    };

    const auto original = runOriginalRuntime(packets);

    REQUIRE(original.records.size() == packets.size());
    REQUIRE(original.outcomes.size() == expected_outcomes.size());
    REQUIRE(original.outcomes == expected_outcomes);

    const auto replay_outcomes = runReplayRuntime(original.records);

    REQUIRE(replay_outcomes.size() == original.outcomes.size());
    requireSameTerminalOrdering(original.outcomes, replay_outcomes);
    REQUIRE(original.outcomes == replay_outcomes);
}

TEST_CASE("Replay reproduces deterministic failure outcomes",
          "[Replay][Outcome][Failure][Equivalence]")
{
    constexpr std::uint64_t base_time_ms = 1700004000;

    const failure::FailureConfig failure_config{
        .enabled = true,
        .lifecycle_rules = {
            {.lifecycle_id = 9102, .type = failure::FailureType::SimulatedLoss},
            {.lifecycle_id = 9104, .type = failure::FailureType::SimulatedLoss},
        },
    };

    const std::vector<Packet> packets{
        makePacket(200, 9101, base_time_ms + 1, "failure-valid-first"),
        makePacket(201, 9102, base_time_ms + 2, "failure-injected-drop-first"),
        makePacket(202, 9103, base_time_ms + 3, "failure-valid-second"),
        makePacket(203, 9104, base_time_ms + 4, "failure-injected-drop-second"),
        makePacket(204, 9105, base_time_ms + 5, "failure-valid-third"),
    };

    const std::vector<ReplayOutcome> expected_outcomes{
        {.sequence = 0,
         .type = ReplayOutcomeType::Processed,
         .lifecycle_id = 9101,
         .drop_reason = PacketDropReason::Unknown},
        {.sequence = 1,
         .type = ReplayOutcomeType::Dropped,
         .lifecycle_id = 9102,
         .drop_reason = PacketDropReason::SimulatedLoss},
        {.sequence = 2,
         .type = ReplayOutcomeType::Processed,
         .lifecycle_id = 9103,
         .drop_reason = PacketDropReason::Unknown},
        {.sequence = 3,
         .type = ReplayOutcomeType::Dropped,
         .lifecycle_id = 9104,
         .drop_reason = PacketDropReason::SimulatedLoss},
        {.sequence = 4,
         .type = ReplayOutcomeType::Processed,
         .lifecycle_id = 9105,
         .drop_reason = PacketDropReason::Unknown},
    };

    const auto original = runOriginalRuntimeWithFailureConfig(packets, failure_config);

    REQUIRE(original.records.size() == packets.size());
    REQUIRE(original.outcomes.size() == expected_outcomes.size());
    REQUIRE(original.outcomes == expected_outcomes);

    const auto replay_outcomes = runReplayRuntimeWithFailureConfig(original.records, failure_config);

    REQUIRE(replay_outcomes.size() == original.outcomes.size());
    requireSameTerminalOrdering(original.outcomes, replay_outcomes);
    REQUIRE(original.outcomes == replay_outcomes);
}
