#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/network/UdpReceiver.hpp"
#include "edgenetswitch/packet/LifecycleIdGenerator.hpp"
#include "edgenetswitch/packet/PacketProcessor.hpp"
#include "edgenetswitch/switching/ForwardingEvent.hpp"
#include "edgenetswitch/switching/InterfaceRegistry.hpp"
#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/switching/SwitchPort.hpp"
#include "edgenetswitch/transport/PortBackend.hpp"
#include "edgenetswitch/transport/TransportManager.hpp"

#include <arpa/inet.h>
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

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

    Packet makePacket(std::uint64_t lifecycle_id, const MacAddress &source,
                      const MacAddress &destination, std::optional<std::uint32_t> ingress_port)
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

    Packet makeRuntimePacket(std::uint64_t id, std::uint64_t lifecycle_id,
                             const MacAddress &source, const MacAddress &destination,
                             std::uint32_t ingress_port, std::string payload,
                             std::uint64_t timestamp_ms)
    {
        Packet packet = makePacket(lifecycle_id, source, destination, ingress_port);
        packet.id = id;
        packet.timestamp_ms = timestamp_ms;
        packet.payload = std::move(payload);
        packet.payload_size = static_cast<std::uint32_t>(packet.payload.size());
        packet.wire_size = packet.payload_size + 14;
        return packet;
    }

    void requirePacketIntegrity(const Packet &actual, const Packet &expected)
    {
        REQUIRE(actual.id == expected.id);
        REQUIRE(actual.lifecycle_id == expected.lifecycle_id);
        REQUIRE(actual.payload == expected.payload);
        REQUIRE(actual.source_mac == expected.source_mac);
        REQUIRE(actual.destination_mac == expected.destination_mac);
        REQUIRE(actual.ingress_port == expected.ingress_port);
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
                              const auto *event = std::get_if<ForwardingEvent>(&message.payload);
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
                               [&] { return processed_packets.size() >= expected_count; });
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
            : interfaces(std::move(registry)), forwarding_engine(mac_table, interfaces),
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
        explicit FakePortBackend(std::uint32_t port_id, transport::TransmitStatus status =
                                                            transport::TransmitStatus::Success)
            : port_id_(port_id), status_(status)
        {
        }

        transport::TransmitResult transmit(const Packet &packet) override
        {
            ++transmit_count;
            last_packet_id = packet.id;
            last_lifecycle_id = packet.lifecycle_id;
            transmitted_packets.push_back(packet);

            const std::size_t bytes_transmitted =
                status_ == transport::TransmitStatus::Success ? packet.payload.size() : 0;

            return transport::TransmitResult{
                .status = status_, .port_id = port_id_, .bytes_transmitted = bytes_transmitted};
        }

        std::size_t transmit_count{0};
        std::uint64_t last_packet_id{0};
        std::uint64_t last_lifecycle_id{0};
        std::vector<Packet> transmitted_packets;

    private:
        std::uint32_t port_id_{0};
        transport::TransmitStatus status_{transport::TransmitStatus::Success};
    };

    void registerBackend(transport::TransportManager &transport_manager, std::uint32_t port_id,
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
            : interfaces(std::move(registry)), forwarding_engine(mac_table, interfaces),
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

    std::uint16_t boundPort(const UdpReceiver &receiver)
    {
        sockaddr_in address{};
        socklen_t address_size = sizeof(address);
        REQUIRE(::getsockname(receiver.fd(), reinterpret_cast<sockaddr *>(&address),
                              &address_size) == 0);
        return ntohs(address.sin_port);
    }

    void sendDatagram(std::uint16_t port, const std::string &payload)
    {
        const int sender = ::socket(AF_INET, SOCK_DGRAM, 0);
        REQUIRE(sender >= 0);

        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(port);
        REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &destination.sin_addr) == 1);

        const auto sent = ::sendto(sender, payload.data(), payload.size(), 0,
                                   reinterpret_cast<const sockaddr *>(&destination),
                                   sizeof(destination));
        ::close(sender);

        REQUIRE(sent == static_cast<ssize_t>(payload.size()));
    }

    std::string makeDatagram(std::uint64_t id, std::string_view source,
                             std::string_view destination, std::string_view payload)
    {
        return "id=" + std::to_string(id) + ";src=" + std::string(source) +
               ";dst=" + std::string(destination) + ";payload=" + std::string(payload) + ";";
    }

    struct UdpIngressRuntimeFixture
    {
        UdpIngressRuntimeFixture()
            : endpoint1(runtime.bus, 1, 0, lifecycle_generator, nullptr,
                        IngressMode::NonBlocking),
              endpoint2(runtime.bus, 2, 0, lifecycle_generator, nullptr,
                        IngressMode::NonBlocking)
        {
            endpoint1.initializeSocket();
            endpoint2.initializeSocket();
            REQUIRE(endpoint1.fd() >= 0);
            REQUIRE(endpoint2.fd() >= 0);
        }

        void receive(UdpReceiver &endpoint, const std::string &datagram)
        {
            sendDatagram(boundPort(endpoint), datagram);
            endpoint.processReadableEvent();
        }

        TransportRuntimeFixture runtime;
        LifecycleIdGenerator lifecycle_generator;
        UdpReceiver endpoint1;
        UdpReceiver endpoint2;
    };
} // namespace

TEST_CASE("PacketProcessor emits forwarding decision before processed event",
          "[PacketForwardingRuntime]")
{
    ForwardingRuntimeFixture fixture;
    const Packet packet = makePacket(1, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

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
    const Packet packet =
        makePacket(2, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), std::nullopt);

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
    const Packet packet = makePacket(3, mac("00:11:22:33:44:01"), mac("ff:ff:ff:ff:ff:ff"), 2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events.front().egress_ports == std::vector<std::uint32_t>{1, 3, 4});
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
    const Packet packet = makePacket(4, mac("00:11:22:33:44:01"), destination, 2);

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
    const Packet packet = makePacket(5, mac("00:11:22:33:44:01"), destination, 2);

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
    const Packet packet = makePacket(6, mac("00:11:22:33:44:01"), destination, 2);

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
    const Packet packet = makePacket(7, mac("00:11:22:33:44:01"), mac("ff:ff:ff:ff:ff:ff"), 2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events.front().egress_ports == std::vector<std::uint32_t>{1, 3, 4});
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

TEST_CASE("PacketProcessor floods unknown unicast to every up port except ingress",
          "[PacketForwardingRuntime][Transport][UnknownUnicast]")
{
    TransportRuntimeFixture fixture;
    FakePortBackend &port1 = fixture.registerBackend(1);
    FakePortBackend &ingress_port = fixture.registerBackend(2);
    FakePortBackend &port3 = fixture.registerBackend(3);
    FakePortBackend &port4 = fixture.registerBackend(4);
    const Packet packet = makePacket(16, mac("00:11:22:33:44:10"), mac("00:11:22:33:44:99"), 2);

    publishPacketRx(fixture.bus, packet);

    REQUIRE(fixture.events.waitForProcessedPackets(1));
    const EventSnapshot events = fixture.events.snapshot();

    REQUIRE(events.forwarding_events.size() == 1);
    REQUIRE(events.forwarding_events.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.forwarding_events.front().action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events.front().egress_ports == std::vector<std::uint32_t>{1, 3, 4});
    REQUIRE(events.processed_packets.size() == 1);
    REQUIRE(port1.transmit_count == 1);
    REQUIRE(ingress_port.transmit_count == 0);
    REQUIRE(port3.transmit_count == 1);
    REQUIRE(port4.transmit_count == 1);
    REQUIRE(port1.last_packet_id == packet.id);
    REQUIRE(port3.last_packet_id == packet.id);
    REQUIRE(port4.last_packet_id == packet.id);
    REQUIRE(port1.last_lifecycle_id == packet.lifecycle_id);
    REQUIRE(port3.last_lifecycle_id == packet.lifecycle_id);
    REQUIRE(port4.last_lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.processed_packets.front().id == packet.id);
    REQUIRE(events.processed_packets.front().lifecycle_id == packet.lifecycle_id);
    REQUIRE(events.processed_packets.front().ingress_port == packet.ingress_port);
    REQUIRE(events.processed_packets.front().payload == packet.payload);
    REQUIRE(events.processed_packets.front().source_mac == packet.source_mac);
    REQUIRE(events.processed_packets.front().destination_mac == packet.destination_mac);
    requireCounters(fixture.transport_manager.counters(), {.tx_packets = 3,
                                                           .tx_bytes = packet.payload.size() * 3,
                                                           .tx_failed = 0,
                                                           .backend_unavailable = 0,
                                                           .port_down = 0,
                                                           .invalid_packet = 0});
}

TEST_CASE("PacketProcessor learns source ports and forwards the return packet without flooding",
          "[PacketForwardingRuntime][Transport][LearningSwitch]")
{
    TransportRuntimeFixture fixture;
    FakePortBackend &port1 = fixture.registerBackend(1);
    FakePortBackend &port2 = fixture.registerBackend(2);
    FakePortBackend &port3 = fixture.registerBackend(3);
    FakePortBackend &port4 = fixture.registerBackend(4);
    const MacAddress host_a = mac("00:11:22:33:44:a1");
    const MacAddress host_b = mac("00:11:22:33:44:b2");
    const Packet packet1 =
        makeRuntimePacket(201, 21, host_a, host_b, 1, "a-to-b", 2001);
    const Packet packet2 =
        makeRuntimePacket(202, 22, host_b, host_a, 2, "b-to-a", 2002);

    publishPacketRx(fixture.bus, packet1);
    REQUIRE(fixture.events.waitForProcessedPackets(1));
    publishPacketRx(fixture.bus, packet2);
    REQUIRE(fixture.events.waitForProcessedPackets(2));

    const EventSnapshot events = fixture.events.snapshot();
    REQUIRE(events.forwarding_events.size() == 2);
    REQUIRE(events.processed_packets.size() == 2);
    REQUIRE(events.forwarding_events[0].lifecycle_id == packet1.lifecycle_id);
    REQUIRE(events.forwarding_events[0].action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events[0].egress_ports == std::vector<std::uint32_t>{2, 3, 4});
    REQUIRE(events.forwarding_events[1].lifecycle_id == packet2.lifecycle_id);
    REQUIRE(events.forwarding_events[1].action == ForwardingAction::ForwardToPorts);
    REQUIRE(events.forwarding_events[1].egress_ports == std::vector<std::uint32_t>{1});
    REQUIRE(events.order == std::vector<MessageType>{MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed,
                                                     MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed});

    requirePacketIntegrity(events.processed_packets[0], packet1);
    requirePacketIntegrity(events.processed_packets[1], packet2);
    REQUIRE(fixture.mac_table.lookup(host_a) == 1);
    REQUIRE(fixture.mac_table.lookup(host_b) == 2);

    REQUIRE(port1.transmit_count == 1);
    REQUIRE(port2.transmit_count == 1);
    REQUIRE(port3.transmit_count == 1);
    REQUIRE(port4.transmit_count == 1);
    REQUIRE(port1.transmitted_packets.size() == 1);
    REQUIRE(port2.transmitted_packets.size() == 1);
    REQUIRE(port3.transmitted_packets.size() == 1);
    REQUIRE(port4.transmitted_packets.size() == 1);
    requirePacketIntegrity(port1.transmitted_packets[0], packet2);
    requirePacketIntegrity(port2.transmitted_packets[0], packet1);
    requirePacketIntegrity(port3.transmitted_packets[0], packet1);
    requirePacketIntegrity(port4.transmitted_packets[0], packet1);
    requireCounters(fixture.transport_manager.counters(),
                    {.tx_packets = 4,
                     .tx_bytes = (packet1.payload.size() * 3) + packet2.payload.size()});
}

TEST_CASE("PacketProcessor floods traffic after the learned destination ages out",
          "[PacketForwardingRuntime][Transport][MacTableAging]")
{
    constexpr std::uint64_t max_age = 20;

    TransportRuntimeFixture fixture;
    FakePortBackend &port1 = fixture.registerBackend(1);
    FakePortBackend &port2 = fixture.registerBackend(2);
    FakePortBackend &port3 = fixture.registerBackend(3);
    FakePortBackend &port4 = fixture.registerBackend(4);
    const MacAddress learned_host = mac("00:11:22:33:44:a1");
    const Packet learning_packet = makeRuntimePacket(
        301, 31, learned_host, mac("00:11:22:33:44:b2"), 1, "learn-host", 3001);

    publishPacketRx(fixture.bus, learning_packet);
    REQUIRE(fixture.events.waitForProcessedPackets(1));
    REQUIRE(fixture.mac_table.lookup(learned_host) == 1);

    fixture.mac_table.ageOut(learning_packet.timestamp_ms + max_age + 1, max_age);
    REQUIRE_FALSE(fixture.mac_table.lookup(learned_host).has_value());

    const Packet packet_after_aging =
        makeRuntimePacket(302, 32, mac("00:11:22:33:44:c3"), learned_host, 2,
                          "after-aging", learning_packet.timestamp_ms + max_age + 1);
    publishPacketRx(fixture.bus, packet_after_aging);
    REQUIRE(fixture.events.waitForProcessedPackets(2));

    const EventSnapshot events = fixture.events.snapshot();
    REQUIRE(events.forwarding_events.size() == 2);
    REQUIRE(events.processed_packets.size() == 2);
    REQUIRE(events.forwarding_events[1].lifecycle_id == packet_after_aging.lifecycle_id);
    REQUIRE(events.forwarding_events[1].action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events[1].egress_ports == std::vector<std::uint32_t>{1, 3, 4});
    requirePacketIntegrity(events.processed_packets[1], packet_after_aging);
    REQUIRE_FALSE(fixture.mac_table.lookup(learned_host).has_value());

    REQUIRE(port1.transmit_count == 1);
    REQUIRE(port2.transmit_count == 1);
    REQUIRE(port3.transmit_count == 2);
    REQUIRE(port4.transmit_count == 2);
    REQUIRE(port1.transmitted_packets.size() == 1);
    requirePacketIntegrity(port1.transmitted_packets[0], packet_after_aging);
    requireCounters(fixture.transport_manager.counters(),
                    {.tx_packets = 6,
                     .tx_bytes = (learning_packet.payload.size() * 3) +
                                 (packet_after_aging.payload.size() * 3)});
}

TEST_CASE("UDP ingress endpoints share one strictly increasing lifecycle ID sequence",
          "[PacketForwardingRuntime][Transport][GlobalLifecycleId]")
{
    UdpIngressRuntimeFixture fixture;
    FakePortBackend &port1 = fixture.runtime.registerBackend(1);
    FakePortBackend &port2 = fixture.runtime.registerBackend(2);
    FakePortBackend &port3 = fixture.runtime.registerBackend(3);
    FakePortBackend &port4 = fixture.runtime.registerBackend(4);
    const std::vector<std::uint64_t> packet_ids{401, 402, 403, 404};
    const std::vector<std::string> payloads{"endpoint-1a", "endpoint-2a", "endpoint-1b",
                                            "endpoint-2b"};

    fixture.receive(fixture.endpoint1,
                    makeDatagram(packet_ids[0], "00:11:22:33:44:01", "ff:ff:ff:ff:ff:ff",
                                 payloads[0]));
    REQUIRE(fixture.runtime.events.waitForProcessedPackets(1));
    fixture.receive(fixture.endpoint2,
                    makeDatagram(packet_ids[1], "00:11:22:33:44:02", "ff:ff:ff:ff:ff:ff",
                                 payloads[1]));
    REQUIRE(fixture.runtime.events.waitForProcessedPackets(2));
    fixture.receive(fixture.endpoint1,
                    makeDatagram(packet_ids[2], "00:11:22:33:44:03", "ff:ff:ff:ff:ff:ff",
                                 payloads[2]));
    REQUIRE(fixture.runtime.events.waitForProcessedPackets(3));
    fixture.receive(fixture.endpoint2,
                    makeDatagram(packet_ids[3], "00:11:22:33:44:04", "ff:ff:ff:ff:ff:ff",
                                 payloads[3]));
    REQUIRE(fixture.runtime.events.waitForProcessedPackets(4));

    const EventSnapshot events = fixture.runtime.events.snapshot();
    REQUIRE(events.forwarding_events.size() == 4);
    REQUIRE(events.processed_packets.size() == 4);

    std::vector<std::uint64_t> lifecycle_ids;
    for (std::size_t i = 0; i < events.processed_packets.size(); ++i)
    {
        const Packet &processed = events.processed_packets[i];
        lifecycle_ids.push_back(processed.lifecycle_id);
        REQUIRE(processed.id == packet_ids[i]);
        REQUIRE(processed.payload == payloads[i]);
        REQUIRE(processed.ingress_port == (i % 2 == 0 ? 1 : 2));
        REQUIRE(events.forwarding_events[i].lifecycle_id == processed.lifecycle_id);
        REQUIRE(events.forwarding_events[i].action == ForwardingAction::Flood);
        REQUIRE(events.forwarding_events[i].egress_ports ==
                (i % 2 == 0 ? std::vector<std::uint32_t>{2, 3, 4}
                            : std::vector<std::uint32_t>{1, 3, 4}));
    }

    REQUIRE(lifecycle_ids == std::vector<std::uint64_t>{1, 2, 3, 4});
    REQUIRE(port1.transmit_count == 2);
    REQUIRE(port2.transmit_count == 2);
    REQUIRE(port3.transmit_count == 4);
    REQUIRE(port4.transmit_count == 4);
    const std::size_t total_payload_bytes =
        payloads[0].size() + payloads[1].size() + payloads[2].size() + payloads[3].size();
    requireCounters(fixture.runtime.transport_manager.counters(),
                    {.tx_packets = 12, .tx_bytes = total_payload_bytes * 3});
}

TEST_CASE("Multiple UDP endpoints preserve packets through the complete forwarding pipeline",
          "[PacketForwardingRuntime][Transport][MultiEndpointRuntime]")
{
    UdpIngressRuntimeFixture fixture;
    FakePortBackend &port1 = fixture.runtime.registerBackend(1);
    FakePortBackend &port2 = fixture.runtime.registerBackend(2);
    FakePortBackend &port3 = fixture.runtime.registerBackend(3);
    FakePortBackend &port4 = fixture.runtime.registerBackend(4);
    const MacAddress host_a = mac("00:11:22:33:44:a1");
    const MacAddress host_b = mac("00:11:22:33:44:b2");

    fixture.receive(fixture.endpoint1,
                    makeDatagram(501, host_a.toString(), host_b.toString(), "udp-a-to-b"));
    REQUIRE(fixture.runtime.events.waitForProcessedPackets(1));
    fixture.receive(fixture.endpoint2,
                    makeDatagram(502, host_b.toString(), host_a.toString(), "udp-b-to-a"));
    REQUIRE(fixture.runtime.events.waitForProcessedPackets(2));

    const EventSnapshot events = fixture.runtime.events.snapshot();
    REQUIRE(events.forwarding_events.size() == 2);
    REQUIRE(events.processed_packets.size() == 2);
    REQUIRE(events.order == std::vector<MessageType>{MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed,
                                                     MessageType::ForwardingDecisionMade,
                                                     MessageType::PacketProcessed});
    REQUIRE(events.forwarding_events[0].lifecycle_id == 1);
    REQUIRE(events.forwarding_events[0].action == ForwardingAction::Flood);
    REQUIRE(events.forwarding_events[0].egress_ports == std::vector<std::uint32_t>{2, 3, 4});
    REQUIRE(events.forwarding_events[1].lifecycle_id == 2);
    REQUIRE(events.forwarding_events[1].action == ForwardingAction::ForwardToPorts);
    REQUIRE(events.forwarding_events[1].egress_ports == std::vector<std::uint32_t>{1});

    const Packet &packet1 = events.processed_packets[0];
    const Packet &packet2 = events.processed_packets[1];
    REQUIRE(packet1.id == 501);
    REQUIRE(packet1.lifecycle_id == 1);
    REQUIRE(packet1.payload == "udp-a-to-b");
    REQUIRE(packet1.source_mac == host_a);
    REQUIRE(packet1.destination_mac == host_b);
    REQUIRE(packet1.ingress_port == 1);
    REQUIRE(packet2.id == 502);
    REQUIRE(packet2.lifecycle_id == 2);
    REQUIRE(packet2.payload == "udp-b-to-a");
    REQUIRE(packet2.source_mac == host_b);
    REQUIRE(packet2.destination_mac == host_a);
    REQUIRE(packet2.ingress_port == 2);

    REQUIRE(port1.transmit_count == 1);
    REQUIRE(port2.transmit_count == 1);
    REQUIRE(port3.transmit_count == 1);
    REQUIRE(port4.transmit_count == 1);
    requirePacketIntegrity(port1.transmitted_packets[0], packet2);
    requirePacketIntegrity(port2.transmitted_packets[0], packet1);
    requirePacketIntegrity(port3.transmitted_packets[0], packet1);
    requirePacketIntegrity(port4.transmitted_packets[0], packet1);
    requireCounters(fixture.runtime.transport_manager.counters(),
                    {.tx_packets = 4,
                     .tx_bytes = (packet1.payload.size() * 3) + packet2.payload.size()});
}

TEST_CASE("PacketProcessor does not dispatch drop decisions to TransportManager",
          "[PacketForwardingRuntime][Transport]")
{
    TransportRuntimeFixture fixture(makeInterfacesWithDownPort());
    FakePortBackend &backend = fixture.registerBackend(4);
    const MacAddress destination = mac("00:11:22:33:44:02");
    fixture.mac_table.learn(destination, 4, 5);
    const Packet packet = makePacket(8, mac("00:11:22:33:44:01"), destination, 2);

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
    const Packet packet = makePacket(9, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

    const auto result = transport_manager.transmit(99, packet);

    REQUIRE(result.status == transport::TransmitStatus::BackendUnavailable);
    REQUIRE(result.port_id == 99);
    requireCounters(transport_manager.counters(), {.tx_failed = 1, .backend_unavailable = 1});
}

TEST_CASE("TransportManager updates counters for successful transmit",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::Success);
    const Packet packet = makePacket(10, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::Success);
    requireCounters(transport_manager.counters(),
                    {.tx_packets = 1, .tx_bytes = packet.payload.size()});
}

TEST_CASE("TransportManager updates counters for port down", "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::PortDown);
    const Packet packet = makePacket(12, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::PortDown);
    requireCounters(transport_manager.counters(), {.tx_failed = 1, .port_down = 1});
}

TEST_CASE("TransportManager updates counters for invalid packet",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::InvalidPacket);
    const Packet packet = makePacket(13, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::InvalidPacket);
    requireCounters(transport_manager.counters(), {.tx_failed = 1, .invalid_packet = 1});
}

TEST_CASE("TransportManager updates counters for send failed",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::SendFailed);
    const Packet packet = makePacket(14, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

    const auto result = transport_manager.transmit(4, packet);

    REQUIRE(result.status == transport::TransmitStatus::SendFailed);
    requireCounters(transport_manager.counters(), {.tx_failed = 1});
}

TEST_CASE("TransportManager resetCounters clears accumulated counters",
          "[PacketForwardingRuntime][Transport]")
{
    transport::TransportManager transport_manager;
    registerBackend(transport_manager, 4, transport::TransmitStatus::Success);
    const Packet packet = makePacket(15, mac("00:11:22:33:44:01"), mac("00:11:22:33:44:02"), 2);

    const auto result = transport_manager.transmit(4, packet);
    REQUIRE(result.status == transport::TransmitStatus::Success);
    requireCounters(transport_manager.counters(),
                    {.tx_packets = 1, .tx_bytes = packet.payload.size()});

    transport_manager.resetCounters();

    requireCountersZero(transport_manager.counters());
}
