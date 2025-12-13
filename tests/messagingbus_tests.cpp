#include <catch2/catch_test_macros.hpp>

#include "edgenetswitch/MessagingBus.hpp"

using namespace edgenetswitch;

TEST_CASE("Single subscriber receives published message", "[MessagingBus]")
{
    MessagingBus bus;

    bool received = false;

    bus.subscribe(MessageType::SystemStart,
                  [&](const Message &msg)
                  {
                      received = true;
                  });

    Message message{MessageType::SystemStart};
    bus.publish(message);

    REQUIRE(received == true);
}

TEST_CASE("Multiple subscribers receive the same message", "[MessagingBus]")
{
    MessagingBus bus;

    int counter = 0;

    bus.subscribe(MessageType::SystemStart,
                  [&](const Message &)
                  {
                      counter++;
                  });

    bus.subscribe(MessageType::SystemStart,
                  [&](const Message &)
                  {
                      counter++;
                  });

    bus.publish({MessageType::SystemStart});

    REQUIRE(counter == 2);
}

TEST_CASE("Subscribers only receive messages of the subscribed type", "[MessagingBus]")
{
    MessagingBus bus;

    bool systemStartReceived = false;
    bool healthStatusReceived = false;

    bus.subscribe(MessageType::SystemStart,
                  [&](const Message &)
                  {
                      systemStartReceived = true;
                  });

    bus.subscribe(MessageType::HealthStatus,
                  [&](const Message &)
                  {
                      healthStatusReceived = true;
                  });

    bus.publish({MessageType::SystemStart});

    REQUIRE(systemStartReceived == true);
    REQUIRE(healthStatusReceived == false);
}