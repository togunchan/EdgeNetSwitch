#include "lifecycle_test_support.hpp"

#include "edgenetswitch/failure/FailureInjector.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/packet/PacketStats.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"

#include <memory>

using namespace edgenetswitch;
using lifecycle_test::requireCleanExit;

namespace
{
    int drainPacket()
    {
        MessagingBus bus;
        MacTable mac_table{1};
        InterfaceRegistry interfaces;
        SwitchForwardingEngine forwarding(mac_table, interfaces);
        lifecycle_test::Gate gate;
        unsigned terminal_delivered = 0;
        // Gate a separate, earlier event so PacketProcessed cannot be published before release.
        // Empty forwarding dependencies suffice: ingress_port selects this path even without
        // a transport backend or MACs.
        bus.subscribe(MessageType::ForwardingDecisionMade,
                      [&](const Message &) { gate.arriveAndWait(); });

        // Keep stats alive during drain, including exceptional cleanup.
        auto stats = std::make_unique<PacketStats>(bus);
        auto processor = std::make_unique<PacketProcessor>(bus, &forwarding);
        lifecycle_test::ReleaseGateOnExit release{gate};
        bus.subscribe(MessageType::PacketProcessed, [&](const Message &) { ++terminal_delivered; });

        Packet packet{};
        packet.id = 42;
        packet.lifecycle_id = 7001;
        packet.timestamp_ms = 1;
        packet.payload = "lifetime regression";
        packet.ingress_port = 1;
        bus.publish(Message{.type = MessageType::PacketRx, .timestamp_ms = 1, .payload = packet});
        const bool arrived = gate.waitForArrival();
        // PacketRx publication is synchronous: all ingress observers have finished here.
        const bool ingress_recorded = stats->snapshotAt(1).ingress_packets == 1;
        if (!arrived || !ingress_recorded)
        {
            gate.release();
            processor->stop();
            std::fprintf(stderr, "packet gate/ingress setup failed\n");
            return 2;
        }

        // Hold the worker on the first packet while accepting a second into the queue.
        packet.id = 43;
        packet.lifecycle_id = 7002;
        bus.publish(Message{.type = MessageType::PacketRx, .timestamp_ms = 1, .payload = packet});
        const bool queued = processor->queueDepth() == 1;
        const auto blocked_metrics = stats->snapshotAt(1);
        const bool terminal_pending =
            terminal_delivered == 0 && blocked_metrics.ingress_packets == 2 &&
            blocked_metrics.processed_packets == 0 && blocked_metrics.terminal_events == 0;
        // Both synchronous PacketRx publications have returned; ingress is quiescent.
        gate.release();
        processor->stop(); // Must drain/join before stats is inspected or destroyed.
        const auto metrics = stats->snapshotAt(1);
        const bool stats_updated = metrics.processed_packets == 2 && metrics.terminal_events == 2;
        const bool drained =
            terminal_delivered == 2 && stats_updated && processor->queueDepth() == 0;
        processor->stop(); // Repeated stop is safe after the worker has joined.
        stats.reset();
        processor.reset(); // Destructor fallback is safe after explicit stop and stats destruction.
        return queued && terminal_pending && drained ? 0 : 1;
    }
} // namespace

TEST_CASE("Packet processor stop drains terminal event before observer destruction",
          "[PacketRuntimeLifecycle]")
{
    requireCleanExit([] { return drainPacket(); });
}

TEST_CASE("Packet processor rejects PacketRx after stop returns", "[PacketRuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            MessagingBus bus;
            PacketStats stats(bus);
            unsigned processed_events = 0;
            unsigned dropped_events = 0;
            PacketProcessor processor(bus);
            bus.subscribe(MessageType::PacketProcessed,
                          [&](const Message &) { ++processed_events; });
            bus.subscribe(MessageType::PacketDropped, [&](const Message &) { ++dropped_events; });

            processor.stop();
            const auto before = stats.snapshotAt(1);

            Packet packet{};
            packet.id = 43;
            packet.lifecycle_id = 7002;
            packet.timestamp_ms = 1;
            packet.payload = "post-stop admission regression";
            bus.publish(
                Message{.type = MessageType::PacketRx, .timestamp_ms = 1, .payload = packet});

            // Publication is synchronous and stop() joined the worker: no Gate is needed.
            const auto published = stats.snapshotAt(1);
            const bool no_immediate_terminal =
                processed_events == 0 && dropped_events == 0 &&
                published.processed_packets == before.processed_packets &&
                published.terminal_events == before.terminal_events;

            // A joined worker alone would hide an incorrectly admitted, stranded packet.
            // The public loop drains any retained work synchronously with running_ false,
            // then returns. A valid queued packet would emit PacketProcessed here.
            processor.processLoop();
            const auto after = stats.snapshotAt(1);
            const bool rejected = processed_events == 0 && dropped_events == 0 &&
                                  after.processed_packets == before.processed_packets &&
                                  after.terminal_events == before.terminal_events;
            // PacketStats observes bus ingress even when PacketProcessor rejects admission.
            const bool ingress_observed = published.ingress_packets == before.ingress_packets + 1;
            if (!ingress_observed || !no_immediate_terminal || !rejected)
            {
                std::fprintf(stderr,
                             "post-stop PacketRx was not rejected without terminal events\n");
                return 1;
            }
            return 0;
        });
}

TEST_CASE("Packet processor rejects injected terminal failures after stop returns",
          "[PacketRuntimeLifecycle][FailureInjection]")
{
    using failure::FailureType;
    FailureType type = FailureType::None;
    SECTION("MalformedPacket")
    {
        type = FailureType::MalformedPacket;
    }
    SECTION("ValidationError")
    {
        type = FailureType::ValidationError;
    }
    SECTION("SimulatedLoss")
    {
        type = FailureType::SimulatedLoss;
    }
    SECTION("ProcessingRejection")
    {
        type = FailureType::ProcessingRejection;
    }

    requireCleanExit(
        [type]
        {
            MessagingBus bus;
            PacketStats stats(bus);
            unsigned processed_events = 0;
            unsigned dropped_events = 0;
            const failure::FailureConfig config{
                .type = type, .enabled = true, .every_n_packets = 1};
            PacketProcessor processor(bus, nullptr, nullptr, failure::FailureInjector{config});
            bus.subscribe(MessageType::PacketProcessed,
                          [&](const Message &) { ++processed_events; });
            bus.subscribe(MessageType::PacketDropped, [&](const Message &) { ++dropped_events; });

            processor.stop();
            processor.stop(); // Admission stays closed after repeated stop.
            const auto before = stats.snapshotAt(1);
            const auto drops_before = stats.drops();

            Packet packet{};
            packet.id = 44;
            packet.lifecycle_id = 7003;
            packet.timestamp_ms = 1;
            packet.payload = "post-stop failure injection regression";
            bus.publish(
                Message{.type = MessageType::PacketRx, .timestamp_ms = 1, .payload = packet});

            // Publication is synchronous and the worker is joined. Inspect the queue before
            // draining it synchronously to detect both stranded work and terminal events.
            const bool no_queued_work = processor.queueDepth() == 0;
            processor.processLoop();
            processor.stop();
            const auto after = stats.snapshotAt(1);
            if (!no_queued_work || processed_events != 0 || dropped_events != 0 ||
                stats.drops() != drops_before || after.drops_by_reason != before.drops_by_reason ||
                after.processed_packets != before.processed_packets ||
                after.terminal_events != before.terminal_events ||
                after.ingress_packets != before.ingress_packets + 1)
            {
                std::fprintf(
                    stderr,
                    "post-stop PacketRx admitted work or emitted an injected terminal event\n");
                return 1;
            }
            return 0;
        });
}

TEST_CASE("Idle packet worker stop must complete", "[PacketRuntimeLifecycle]")
{
    requireCleanExit(
        []
        {
            MessagingBus bus;
            {
                PacketProcessor processor(bus);
                processor.stop();
                processor.stop();
            }
            // No public gate exists between the predicate evaluation and wait.
            // SIGALRM bounds stop/destructor completion; a pass cannot disprove lost wakeup.
            return 0;
        });
}
