#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/switching/InterfaceRegistry.hpp"
#include "edgenetswitch/switching/SwitchPort.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace edgenetswitch;

namespace
{
    SwitchPort makePort(std::uint32_t id, const std::string &name, PortState state)
    {
        SwitchPort port(id, name);
        port.setState(state);
        return port;
    }
} // namespace

TEST_CASE("InterfaceRegistry addPort stores ports correctly", "[InterfaceRegistry]")
{
    InterfaceRegistry registry;

    registry.addPort(makePort(2, "eth2", PortState::Up));
    registry.addPort(makePort(1, "eth1", PortState::Down));

    const auto first = registry.findPort(1);
    const auto second = registry.findPort(2);

    REQUIRE(first.has_value());
    REQUIRE(first->id() == 1);
    REQUIRE(first->name() == "eth1");
    REQUIRE(first->state() == PortState::Down);

    REQUIRE(second.has_value());
    REQUIRE(second->id() == 2);
    REQUIRE(second->name() == "eth2");
    REQUIRE(second->state() == PortState::Up);
}

TEST_CASE("InterfaceRegistry findPort returns correct and unknown ports",
          "[InterfaceRegistry]")
{
    InterfaceRegistry registry;
    registry.addPort(makePort(7, "uplink", PortState::Up));

    const auto known = registry.findPort(7);
    const auto unknown = registry.findPort(99);

    REQUIRE(known.has_value());
    REQUIRE(known->id() == 7);
    REQUIRE(known->name() == "uplink");
    REQUIRE(known->state() == PortState::Up);
    REQUIRE(unknown == std::nullopt);
}

TEST_CASE("InterfaceRegistry isUp reflects Up, Down, and unknown ports",
          "[InterfaceRegistry]")
{
    InterfaceRegistry registry;
    registry.addPort(makePort(1, "eth1", PortState::Up));
    registry.addPort(makePort(2, "eth2", PortState::Down));

    REQUIRE(registry.isUp(1));
    REQUIRE_FALSE(registry.isUp(2));
    REQUIRE_FALSE(registry.isUp(99));
}

TEST_CASE("InterfaceRegistry setState updates existing port state",
          "[InterfaceRegistry]")
{
    InterfaceRegistry registry;
    registry.addPort(makePort(3, "eth3", PortState::Down));

    registry.setState(3, PortState::Up);

    const auto port = registry.findPort(3);
    REQUIRE(port.has_value());
    REQUIRE(port->state() == PortState::Up);
    REQUIRE(registry.isUp(3));
}

TEST_CASE("InterfaceRegistry setState ignores unknown ports", "[InterfaceRegistry]")
{
    InterfaceRegistry registry;
    registry.addPort(makePort(1, "eth1", PortState::Up));
    registry.addPort(makePort(2, "eth2", PortState::Down));

    registry.setState(99, PortState::Up);

    const auto snapshot = registry.snapshot();
    REQUIRE(snapshot.size() == 2);
    REQUIRE(registry.findPort(99) == std::nullopt);
    REQUIRE(registry.isUp(1));
    REQUIRE_FALSE(registry.isUp(2));
}

TEST_CASE("InterfaceRegistry activePortIds returns only Up ports in deterministic order",
          "[InterfaceRegistry]")
{
    InterfaceRegistry registry;
    registry.addPort(makePort(30, "eth30", PortState::Up));
    registry.addPort(makePort(10, "eth10", PortState::Up));
    registry.addPort(makePort(20, "eth20", PortState::Down));
    registry.addPort(makePort(40, "eth40", PortState::Up));

    const auto active_ports = registry.activePortIds();

    REQUIRE(active_ports == std::vector<std::uint32_t>{10, 30, 40});
}

TEST_CASE("InterfaceRegistry snapshot returns all ports in deterministic order",
          "[InterfaceRegistry]")
{
    InterfaceRegistry registry;
    registry.addPort(makePort(30, "eth30", PortState::Up));
    registry.addPort(makePort(10, "eth10", PortState::Down));
    registry.addPort(makePort(20, "eth20", PortState::Up));

    const auto snapshot = registry.snapshot();

    REQUIRE(snapshot.size() == 3);
    REQUIRE(snapshot[0].id() == 10);
    REQUIRE(snapshot[0].name() == "eth10");
    REQUIRE(snapshot[0].state() == PortState::Down);
    REQUIRE(snapshot[1].id() == 20);
    REQUIRE(snapshot[1].name() == "eth20");
    REQUIRE(snapshot[1].state() == PortState::Up);
    REQUIRE(snapshot[2].id() == 30);
    REQUIRE(snapshot[2].name() == "eth30");
    REQUIRE(snapshot[2].state() == PortState::Up);
}
