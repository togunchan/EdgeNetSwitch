#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/switching/ForwardingEvent.hpp"
#include "edgenetswitch/switching/InterfaceRegistry.hpp"
#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/switching/SwitchPort.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
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

    void addPort(InterfaceRegistry &interfaces, std::uint32_t id, PortState state)
    {
        SwitchPort port(id, "eth" + std::to_string(id));
        port.setState(state);
        interfaces.addPort(std::move(port));
    }

    InterfaceRegistry makeInterfaces()
    {
        InterfaceRegistry interfaces;
        addPort(interfaces, 4, PortState::Up);
        addPort(interfaces, 2, PortState::Up);
        addPort(interfaces, 1, PortState::Up);
        addPort(interfaces, 3, PortState::Up);
        return interfaces;
    }

    InterfaceRegistry makeInterfacesWithDownPort()
    {
        InterfaceRegistry interfaces;
        addPort(interfaces, 4, PortState::Down);
        addPort(interfaces, 2, PortState::Up);
        addPort(interfaces, 1, PortState::Up);
        addPort(interfaces, 3, PortState::Up);
        return interfaces;
    }

    Packet makePacket(std::uint64_t lifecycle_id,
                      const MacAddress &source,
                      const MacAddress &destination,
                      std::optional<std::uint32_t> ingress_port)
    {
        Packet packet{};
        packet.id = lifecycle_id + 100;
        packet.lifecycle_id = lifecycle_id;
        packet.timestamp_ms = 1000 + lifecycle_id;
        packet.payload = "payload";
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.wire_size = packet.payload_size + 14;
        packet.valid = true;
        packet.source_mac = source;
        packet.destination_mac = destination;
        packet.ingress_port = ingress_port;
        return packet;
    }

    void publishPacketRx(MessagingBus &bus, const Packet &packet)
    {
        Message message{};
        message.type = MessageType::PacketRx;
        message.timestamp_ms = packet.timestamp_ms;
        message.payload = packet;

        bus.publish(message);
    }

    struct EventSnapshot
    {
        std::vector<ForwardingEvent> forwarding_events;
        std::vector<Packet> processed_packets;
        std::vector<MessageType> order;
    };

    struct EventRecorder
    {
        void subscribe(MessagingBus &bus)
        {
            bus.subscribe(MessageType::ForwardingDecisionMade,
                          [this](const Message &message)
                          {
                              const auto *event =
                                  std::get_if<ForwardingEvent>(&message.payload);
                              if (!event)
                                  return;

                              {
                                  std::lock_guard<std::mutex> lock(mutex);
                                  forwarding_events.push_back(*event);
                                  order.push_back(message.type);
                              }
                              cv.notify_all();
                          });

            bus.subscribe(MessageType::PacketProcessed,
                          [this](const Message &message)
                          {
                              const auto *packet = std::get_if<Packet>(&message.payload);
                              if (!packet)
                                  return;

                              {
                                  std::lock_guard<std::mutex> lock(mutex);
                                  processed_packets.push_back(*packet);
                                  order.push_back(message.type);
                              }
                              cv.notify_all();
                          });
        }

        [[nodiscard]]
        bool waitForProcessedPackets(std::size_t expected_count)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return cv.wait_for(lock, std::chrono::seconds(1),
                               [&]
                               { return processed_packets.size() >= expected_count; });
        }

        [[nodiscard]]
        EventSnapshot snapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return EventSnapshot{.forwarding_events = forwarding_events,
                                 .processed_packets = processed_packets,
                                 .order = order};
        }

        mutable std::mutex mutex;
        std::condition_variable cv;
        std::vector<ForwardingEvent> forwarding_events;
        std::vector<Packet> processed_packets;
        std::vector<MessageType> order;
    };

    struct ForwardingRuntimeFixture
    {
        explicit ForwardingRuntimeFixture(InterfaceRegistry registry = makeInterfaces())
            : interfaces(std::move(registry)),
              forwarding_engine(mac_table, interfaces),
              processor(bus, forwarding_engine)
        {
            events.subscribe(bus);
        }

        MessagingBus bus;
        EventRecorder events;
        MacTable mac_table{16};
        InterfaceRegistry interfaces;
        SwitchForwardingEngine forwarding_engine;
        PacketProcessor processor;
    };
} // namespace

TEST_CASE("PacketProcessor emits forwarding decision before processed event",
          "[PacketForwardingRuntime]")
{
    ForwardingRuntimeFixture fixture;
    const Packet packet = makePacket(1,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.processed_packets.size() == 1);
    REQUIRE(events.forwarding_events.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.processed_packets.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.order == std::vector<MessageType>{MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed});
}

TEST_CASE("PacketProcessor skips forwarding decision when ingress port is missing",
          "[PacketForwardingRuntime]")
{
    ForwardingRuntimeFixture fixture;
    const Packet packet = makePacket(2,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     std::nullopt);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.empty());
    REQUIRE(events.processed_packets.size() == 1);
    REQUIRE(events.processed_packets.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.order == std::vector<MessageType>{MessageType::PacketProcessed});
}

TEST_CASE("PacketProcessor publishes flood decision for broadcast destination",
          "[PacketForwardingRuntime]")
{
    ForwardingRuntimeFixture fixture;
    const Packet packet = makePacket(3,
                                     mac("00:11:22:33:44:01"),
                                     mac("ff:ff:ff:ff:ff:ff"),
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events.front().egress_ports ==
            std::vector<std::uint32_t>{1, 3, 4});
    REQUIRE(events.processed_packets.size() == 1);
    REQUIRE(events.order == std::vector<MessageType>{MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed});
}

TEST_CASE("PacketProcessor publishes forward decision for known unicast destination",
          "[PacketForwardingRuntime]")
{
    ForwardingRuntimeFixture fixture;
    const MacAddress destination = mac("00:11:22:33:44:02");
    fixture.mac_table.learn(destination, 4, 5);
    const Packet packet = makePacket(4,
                                     mac("00:11:22:33:44:01"),
                                     destination,
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::ForwardToPorts);
    REQUIRE(events.forwarding_events.front().egress_ports == std::vector<std::uint32_t>{4});
    REQUIRE(events.processed_packets.size() == 1);
    REQUIRE(events.order == std::vector<MessageType>{MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed});
}

TEST_CASE("PacketProcessor publishes drop decision for known unicast on down port",
          "[PacketForwardingRuntime]")
{
    ForwardingRuntimeFixture fixture(makeInterfacesWithDownPort());
    const MacAddress destination = mac("00:11:22:33:44:02");
    fixture.mac_table.learn(destination, 4, 5);
    const Packet packet = makePacket(5,
                                     mac("00:11:22:33:44:01"),
                                     destination,
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Drop);
    REQUIRE(events.forwarding_events.front().egress_ports.empty());
    REQUIRE(events.processed_packets.size() == 1);
    REQUIRE(events.order == std::vector<MessageType>{MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed});
}
