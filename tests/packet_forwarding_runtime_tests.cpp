#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/switching/ForwardingEvent.hpp"
#include "edgenetswitch/switching/InterfaceRegistry.hpp"
#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/switching/SwitchPort.hpp"
#include "edgenetswitch/transport/PortBackend.hpp"
#include "edgenetswitch/transport/TransportManager.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
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
              processor(bus, &forwarding_engine)
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

    class FakePortBackend final : public transport::PortBackend
    {
    public:
        explicit FakePortBackend(
            std::uint32_t port_id,
            transport::TransmitStatus status = transport::TransmitStatus::Success)
            : port_id_(port_id), status_(status)
        {
        }

        transport::TransmitResult transmit(const Packet &packet) override
        {
            ++transmit_count;
            last_packet_id = packet.id;
            last_lifecycle_id = packet.lifecycle_id;

            const std::size_t bytes_transmitted =
                status_ == transport::TransmitStatus::Success ? packet.payload.size() : 0;

            return transport::TransmitResult{.status = status_,
                                             .port_id = port_id_,
                                             .bytes_transmitted = bytes_transmitted};
        }

        std::size_t transmit_count{0};
        std::uint64_t last_packet_id{0};
        std::uint64_t last_lifecycle_id{0};

    private:
        std::uint32_t port_id_{0};
        transport::TransmitStatus status_{transport::TransmitStatus::Success};
    };

    void registerBackend(transport::TransportManager &transport_manager,
                         std::uint32_t port_id,
                         transport::TransmitStatus status = transport::TransmitStatus::Success)
    {
        transport_manager.registerBackend(port_id,
                                          std::make_unique<FakePortBackend>(port_id, status));
    }

    void requireCounters(const transport::TransportCounters &counters,
                         const transport::TransportCounters &expected)
    {
        REQUIRE(counters.tx_packets == expected.tx_packets);
        REQUIRE(counters.tx_bytes == expected.tx_bytes);
        REQUIRE(counters.tx_failed == expected.tx_failed);
        REQUIRE(counters.backend_unavailable == expected.backend_unavailable);
        REQUIRE(counters.port_down == expected.port_down);
        REQUIRE(counters.invalid_packet == expected.invalid_packet);
    }

    void requireCountersZero(const transport::TransportCounters &counters)
    {
        requireCounters(counters, {});
    }

    struct TransportRuntimeFixture
    {
        explicit TransportRuntimeFixture(InterfaceRegistry registry = makeInterfaces())
            : interfaces(std::move(registry)),
              forwarding_engine(mac_table, interfaces),
              processor(bus, &forwarding_engine, &transport_manager)
        {
            events.subscribe(bus);
        }

        FakePortBackend &registerBackend(std::uint32_t port_id)
        {
            auto backend = std::make_unique<FakePortBackend>(port_id);
            FakePortBackend *raw_backend = backend.get();
            transport_manager.registerBackend(port_id, std::move(backend));
            return *raw_backend;
        }

        MessagingBus bus;
        EventRecorder events;
        MacTable mac_table{16};
        InterfaceRegistry interfaces;
        SwitchForwardingEngine forwarding_engine;
        transport::TransportManager transport_manager;
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

TEST_CASE("PacketProcessor dispatches known unicast to TransportManager backend",
          "[PacketForwardingRuntime][Transport]")
{
    TransportRuntimeFixture fixture;
    FakePortBackend &backend = fixture.registerBackend(4);
    const MacAddress destination = mac("00:11:22:33:44:02");
    fixture.mac_table.learn(destination, 4, 5);
    const Packet packet = makePacket(6,
                                     mac("00:11:22:33:44:01"),
                                     destination,
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::ForwardToPorts);
    REQUIRE(events.forwarding_events.front().egress_ports == std::vector<std::uint32_t>{4});
    REQUIRE(backend.transmit_count == 1);
    REQUIRE(backend.last_packet_id == packet.id);
    REQUIRE(backend.last_lifecycle_id == packet.lifecycle_id);
}

TEST_CASE("PacketProcessor dispatches flood decision once per egress port",
          "[PacketForwardingRuntime][Transport]")
{
    TransportRuntimeFixture fixture;
    FakePortBackend &port1 = fixture.registerBackend(1);
    FakePortBackend &port3 = fixture.registerBackend(3);
    FakePortBackend &port4 = fixture.registerBackend(4);
    const Packet packet = makePacket(7,
                                     mac("00:11:22:33:44:01"),
                                     mac("ff:ff:ff:ff:ff:ff"),
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events.front().egress_ports ==
            std::vector<std::uint32_t>{1, 3, 4});
    REQUIRE(port1.transmit_count == 1);
    REQUIRE(port3.transmit_count == 1);
    REQUIRE(port4.transmit_count == 1);
    REQUIRE(port1.last_packet_id == packet.id);
    REQUIRE(port3.last_packet_id == packet.id);
    REQUIRE(port4.last_packet_id == packet.id);
    REQUIRE(port1.last_lifecycle_id == packet.lifecycle_id);
    REQUIRE(port3.last_lifecycle_id == packet.lifecycle_id);
    REQUIRE(port4.last_lifecycle_id == packet.lifecycle_id);
}

TEST_CASE("PacketProcessor does not dispatch drop decisions to TransportManager",
          "[PacketForwardingRuntime][Transport]")
{
    TransportRuntimeFixture fixture(makeInterfacesWithDownPort());
    FakePortBackend &backend = fixture.registerBackend(4);
    const MacAddress destination = mac("00:11:22:33:44:02");
    fixture.mac_table.learn(destination, 4, 5);
    const Packet packet = makePacket(8,
                                     mac("00:11:22:33:44:01"),
                                     destination,
                                     2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Drop);
    REQUIRE(events.forwarding_events.front().egress_ports.empty());
    REQUIRE(backend.transmit_count == 0);
    REQUIRE(backend.last_packet_id == 0);
    REQUIRE(backend.last_lifecycle_id == 0);
}

TEST_CASE("TransportManager reports backend unavailable and updates counters",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    const Packet packet = makePacket(9,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    const auto result = transport_manager.transmit(99, packet);

    REQUIRE(result.status == transport::TransmitStatus::BackendUnavailable);
    REQUIRE(result.port_id == 99);
    requireCounters(transport_manager.counters(),
                    {.tx_failed = 1, .backend_unavailable = 1});
}

TEST_CASE("TransportManager updates counters for successful transmit",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::Success);
    const Packet packet = makePacket(10,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::Success);
    requireCounters(transport_manager.counters(),
                    {.tx_packets = 1, .tx_bytes = packet.payload.size()});
}

TEST_CASE("TransportManager updates counters for port down",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::PortDown);
    const Packet packet = makePacket(12,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::PortDown);
    requireCounters(transport_manager.counters(), {.tx_failed = 1, .port_down = 1});
}

TEST_CASE("TransportManager updates counters for invalid packet",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::InvalidPacket);
    const Packet packet = makePacket(13,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::InvalidPacket);
    requireCounters(transport_manager.counters(), {.tx_failed = 1, .invalid_packet = 1});
}

TEST_CASE("TransportManager updates counters for send failed",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::SendFailed);
    const Packet packet = makePacket(14,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::SendFailed);
    requireCounters(transport_manager.counters(), {.tx_failed = 1});
}

TEST_CASE("TransportManager resetCounters clears accumulated counters",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::Success);
    const Packet packet = makePacket(15,
                                     mac("00:11:22:33:44:01"),
                                     mac("00:11:22:33:44:02"),
                                     2);

    const auto result = transport_manager.transmit(4, packet);
    REQUIRE(result.status == transport::TransmitStatus::Success);
    requireCounters(transport_manager.counters(),
                    {.tx_packets = 1, .tx_bytes = packet.payload.size()});

    transport_manager.resetCounters();

    requireCountersZero(transport_manager.counters());
}
