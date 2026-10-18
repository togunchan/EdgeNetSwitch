#include "lifecycle_test_support.hpp"

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
        bool terminal_delivered = false;
        // Gate a separate, earlier event so PacketProcessed cannot be published before release.
        // Empty forwarding dependencies suffice: ingress_port selects this path even without
        // a transport backend or MACs.
        bus.subscribe(MessageType::ForwardingDecisionMade,
                      [&](const Message &) { gate.arriveAndWait(); });

        // Keep stats alive during drain, including exceptional cleanup.
        auto stats = std::make_unique<PacketStats>(bus);
        auto processor = std::make_unique<PacketProcessor>(bus, &forwarding);
        lifecycle_test::ReleaseGateOnExit release{gate};
        bus.subscribe(MessageType::PacketProcessed,
                      [&](const Message &) { terminal_delivered = true; });

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

        const auto blocked_metrics = stats->snapshotAt(1);
        const bool terminal_pending = !terminal_delivered &&
                                      blocked_metrics.processed_packets == 0 &&
                                      blocked_metrics.terminal_events == 0;
        // The single synchronous PacketRx publication has returned; ingress is quiescent.
        gate.release();
        processor->stop(); // Must drain/join before stats is inspected or destroyed.
        const auto metrics = stats->snapshotAt(1);
        const bool stats_updated = metrics.processed_packets == 1 && metrics.terminal_events == 1;
        const bool drained = terminal_delivered && stats_updated;
        processor->stop(); // Repeated stop is safe after the worker has joined.
        stats.reset();
        processor.reset(); // Destructor fallback is safe after explicit stop and stats destruction.
        return terminal_pending && drained ? 0 : 1;
    }
} // namespace

TEST_CASE("Packet processor stop drains terminal event before observer destruction",
          "[PacketRuntimeLifecycle]")
{
    requireCleanExit([] { return drainPacket(); });
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
