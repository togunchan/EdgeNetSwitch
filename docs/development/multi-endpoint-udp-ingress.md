# Multi-Endpoint UDP Ingress

We already have a `UdpReceiver.cpp`. It creates a socket, binds it, configures it as non-blocking, uses `recvfrom()`, creates a `Packet`, runs `PacketValidator`, and finally publishes a `PacketRx`.

However, back in v1.8 we originally wrote this class as an echo implementation. At that time we simply wanted to verify whether an incoming packet could be received and immediately sent back to the sender. The egress side of the runtime had not been implemented yet.

Today our packet pipeline looks like this.

```text
Packet
    ↓
PacketProcessor
    ↓
SwitchForwardingEngine
    ↓
TransportManager
    ↓
UdpPortBackend
    ↓
sendto()
```

At this point `UdpReceiver` should no longer contain any `sendto()` call.

Packet transmission has become entirely the responsibility of the `TransportManager -> UdpPortBackend` pipeline.

For that reason I removed the following code.

```cpp
sendto(socket_fd_.get(), buffer, len, 0,
       (struct sockaddr *)&client_addr, addr_len);
```

---

`UdpReceiver.hpp` currently contains an `int port_`.

That means every `UdpReceiver` binds to exactly one UDP port.

As a result it can only receive packets arriving on that specific port.

For example, suppose `UdpReceiver` is bound to

```text
127.0.0.1:5000
```

If a packet arrives on

```text
127.0.0.1:5001
```

this receiver will never see it.

The Linux kernel delivers every UDP packet only to the socket that is bound to the destination endpoint.

However, a real switch should behave differently.

Let's imagine a virtual switch with four ports.

- Port1 -> UDP :5001
- Port2 -> UDP :5002
- Port3 -> UDP :5003
- Port4 -> UDP :5004

Then another question appears.

Should every switch port own its own `UdpReceiver`?

Or should a single `UdpReceiver` manage multiple sockets?

The first solution immediately seems more natural because today's `UdpReceiver` already owns exactly one socket and exactly one UDP endpoint.

The architecture would then look like this.

- UdpReceiver (Port1 → UDP:5001)
- UdpReceiver (Port2 → UDP:5002)
- UdpReceiver (Port3 → UDP:5003)
- UdpReceiver (Port4 → UDP:5004)

The Linux networking model actually fits this design very well.

One socket naturally represents one endpoint.

---

Then another problem appeared.

Today `main.cpp` contains the following initialization sequence.

```cpp
if (cfg.udp.enabled)
{
    udpReceiver = ...
    udpReceiver->initializeSocket();

    udpHandler = ...

    epollManager.add(...);
    epollLoop.registerHandler(...);
}
```

What are we doing here?

- Creating the `UdpReceiver`
- Initializing the socket
- Registering the socket with epoll
- Creating a `UdpReadyHandler`
- Registering the handler with `EpollEventLoop`

All of this is written for only one receiver.

Now imagine a six-port switch.

We would have to duplicate exactly the same initialization sequence six times.

`main.cpp` would continue to grow because it is directly managing the entire ingress lifecycle.

On the egress side this responsibility already belongs to `TransportManager`.

I thought the ingress side deserved a similar lifecycle manager.

Its responsibilities should be:

- Create `UdpReceiver`
- Initialize sockets
- Register sockets with epoll
- Create `UdpReadyHandler`
- Register handlers with `EpollEventLoop`
- Stop receivers cleanly during shutdown

Initially I considered placing this class under the `network` directory.

After thinking about it for a while, I realized something.

This class never performs any actual networking operations.

It never calls

- `socket()`
- `bind()`
- `recvfrom()`
- `sendto()`

Its responsibility is not networking.

Its responsibility is managing the lifetime of ingress runtime objects.

For that reason I decided to place `IngressManager` under the `runtime` directory.

The first implementation looked like this.

```cpp
IngressManager::initialize(...)
{
    udpReceiver_ = ...
    udpReceiver_->initializeSocket();

    udpHandler_ = ...

    epollManager_.add(...);
    epollLoop_.registerHandler(...);
}

IngressManager::shutdown()
{
    if (!udpReceiver_)
        return;

    udpReceiver_->stop();
}
```

As a result, `main.cpp` became much simpler.

```cpp
IngressManager ingressManager(...);

if (cfg.udp.enabled)
{
    ingressManager.initialize(cfg.udp);
}

...

ingressManager.shutdown();
```

At this point `main.cpp` no longer knows how `UdpReceiver` objects are created, how sockets are initialized, or how they are registered with epoll.

All of those responsibilities have been moved into `IngressManager`.

Up to this point I had only separated the ingress lifecycle from `main.cpp`.

I still had not implemented multi-endpoint UDP support.

---

The next problem was obvious.

How should multiple UDP endpoints be managed?

The first solution that came to mind was using a `std::vector`.

However, another detail immediately caught my attention.

`UdpReadyHandler` derives from `IEpollHandler`.

Inside `EpollEventLoop::run()`, epoll events are dispatched to the corresponding handler.

```cpp
while (running_)
{
    auto events = epoll_.wait(...);

    for (const auto& event : events)
    {
        handlers_[event.fd]->onEvent(event);
    }
}
```

This event loop itself runs on a dedicated `epollThread` inside `main.cpp`.

Another important observation is that every `UdpReadyHandler` owns exactly one `UdpReceiver`.

These two objects always exist together.

One has no meaning without the other.

Instead of storing them in two separate vectors, I thought it would be cleaner to group them into a single structure.

```cpp
struct UdpIngressEndpoint
{
    std::unique_ptr<UdpReceiver> receiver;
    std::unique_ptr<UdpReadyHandler> handler;
};
```

Instead of storing one `UdpReceiver` and one `UdpReadyHandler`, `IngressManager` now owns

```cpp
std::vector<UdpIngressEndpoint> ingressEndpoints_;
```

At this point the runtime behavior had not changed.

Only the internal data model changed.

Instead of thinking in terms of a single receiver, the runtime now thinks in terms of ingress endpoints.

Inside `initialize()`, I now create a complete `UdpIngressEndpoint`, populate it, and finally move it into `ingressEndpoints_`.

Likewise, `shutdown()` no longer stops a single receiver.

Instead, it iterates over every endpoint and stops every receiver.

That means no further changes will be necessary once true multi-port support is added.

---

The next problem appeared in the configuration layer.

Up until now we had only supported a single UDP port.

```json
"udp": {
    "enabled": true,
    "port": 9000
}
```

However, that was no longer sufficient because the runtime was now moving toward multiple ingress endpoints.

The first step was changing the configuration into the following form.

```json
"udp": {
    "enabled": true,
    "endpoints": [
        {
            "switch_port": 1,
            "listen_port": 9001
        },
        {
            "switch_port": 2,
            "listen_port": 9002
        }
    ]
}
```

After taking another step forward, I realized that this would not be enough in the long run either.

Every endpoint actually owns two different addresses.

The first one is the address it listens on.

The second one is the address it transmits packets to.

Because of that, I changed the configuration once again.

```json
"udp": {
    "enabled": true,
    "endpoints": [
        {
            "switch_port": 1,
            "listen": {
                "ip": "0.0.0.0",
                "port": 9001
            },
            "peer": {
                "ip": "127.0.0.1",
                "port": 9101
            }
        }
    ]
}
```

With this change, ingress and egress addresses became completely independent from each other.

After that I updated the configuration structures accordingly.

```cpp
struct UdpEndpointConfig
{
    std::string ip;
    std::uint16_t port;
};

struct UdpIngressConfig
{
    std::uint32_t switch_port;

    UdpEndpointConfig listen;
    UdpEndpointConfig peer;
};

struct UdpConfig
{
    bool enabled;
    std::vector<UdpIngressConfig> endpoints;
};
```

`ConfigLoader` now parses both `listen` and `peer` objects and stores them inside `UdpIngressConfig`.

After that, `IngressManager::initialize()` also changed.

Instead of creating a single receiver, it now iterates over every configured endpoint.

```cpp
for (const auto& endpointConfig : udpConfig.endpoints)
{
    UdpIngressEndpoint endpoint;

    ...

    ingressEndpoints_.push_back(std::move(endpoint));
}
```

That means `IngressManager` is now capable of creating as many `UdpReceiver` and `UdpReadyHandler` instances as there are ingress endpoints defined in the configuration.

---

The next important problem appeared inside `UdpReceiver`.

Until now `UdpReceiver` only knew which UDP port it was listening on.

However, from the switching layer's point of view, the UDP port itself is not important.

The important information is which logical switch port the packet entered from.

Because of that I changed the constructor into the following form.

```cpp
UdpReceiver(
    bus,
    switch_port,
    listen.port,
    ...
);
```

Now, inside `handleReadable()`, the receiver assigns

```cpp
packet.ingress_port = switchPort_;
```

As a result, neither `PacketProcessor` nor `SwitchForwardingEngine` ever need to know Linux UDP port numbers.

They only see

> "This packet entered through Switch Port 2."

Linux-specific networking details remain inside `UdpReceiver`, while the switching layer continues working entirely with logical switch ports.

---

Then another problem appeared.

Until this point `UdpPortBackend` had only been transmitting

```cpp
packet.payload
```

As soon as the packet reached another endpoint, `PacketParser` could only reconstruct the payload.

The packet ID and both MAC addresses were completely lost.

Once I started testing forwarding behavior, I realized that this was no longer sufficient.

To solve this, I introduced a very small wire format inside `UdpPortBackend`.

```text
id=1;
src=00:11:22:33:44:55;
dst=ff:ff:ff:ff:ff:ff;
payload=test
```

Before transmitting a packet, `UdpPortBackend` now serializes it into this format.

On the receiving side, `PacketParser` parses the same fields again.

As a result, packet identity and MAC addresses are now preserved while the packet travels through the virtual switch.

I also extended `PacketParser`.

It no longer parses only

- `id`
- `payload`

It now also parses

- `src`
- `dst`

and converts both values into `MacAddress` objects.

A packet is now considered valid only if both the source and destination MAC addresses are successfully parsed.

Because of that, I also updated the parser test suite.

Every valid packet now includes MAC addresses.

In addition, I added failure tests for

- missing source MAC
- missing destination MAC
- malformed MAC addresses

---

Finally, I updated the runtime to use the new endpoint model.

Previously `TransportManager` registered only a single backend.

```cpp
registerBackend(1, ...);
```

Now it iterates directly over the configured endpoints.

```cpp
for (const auto& endpoint : cfg.udp.endpoints)
{
    transportManager.registerBackend(
        endpoint.switch_port,
        std::make_unique<UdpPortBackend>(
            endpoint.switch_port,
            transport::UdpEndpoint{
                endpoint.peer.ip,
                endpoint.peer.port
            },
            &fd_registry));
}
```

Transport backends are now created entirely from the runtime configuration.

Adding another switch port no longer requires changing the source code.

Adding another endpoint to the configuration is enough.

At this point I successfully verified real packet forwarding between two ports.

A broadcast packet entering Port 1 is forwarded only to Port 2.

Forwarding decisions are now driven entirely by the endpoint model defined in the configuration.

The next goal is completing the remaining switch behavior on top of this endpoint model.

In particular:

- lifecycle management
- forwarding validation
- learning switch behavior

---

Then I noticed another architectural detail.

Every `UdpReceiver` owned its own `LifecycleIdGenerator`.

That means every receiver was generating its own sequence.

```text
1
2
3
...
```

After I introduced multiple UDP ingress endpoints, this turned into a real problem.

Since every receiver maintained its own counter, two completely different packets could end up receiving the same lifecycle ID.

For example, the logs looked like this.

**Port1**

```text
lifecycle_id = 1
```

**Port2**

```text
lifecycle_id = 1
```

In other words, it became possible for two different packets to exist in the runtime with exactly the same lifecycle ID.

However, a lifecycle ID should not belong to an ingress port.

It should belong to the runtime itself.

For that reason I decided to move `LifecycleIdGenerator` out of `UdpReceiver`.

The generator is now created only once.

```cpp
LifecycleIdGenerator lifecycleGenerator;
```

This object is created inside `main.cpp`.

It is then passed to `IngressManager`.

Finally, `IngressManager` forwards the same generator reference to every `UdpReceiver` it creates.

```cpp
UdpReceiver(
    ...,
    lifecycleGenerator,
    ...
);
```

As a result, `UdpReceiver` is no longer the owner of the generator.

It simply becomes a consumer of a shared runtime-wide generator.

After this change, lifecycle IDs became globally unique across the entire runtime.

The logs now look like this.

**Port1**

```text
lifecycle_id = 1
```

**Port2**

```text
lifecycle_id = 2
```

**Port1**

```text
lifecycle_id = 3
```

**Port2**

```text
lifecycle_id = 4
```

instead of maintaining an independent sequence per receiver.

---

At this point I also wanted to verify that the runtime behaved correctly under real execution scenarios.

Until now most of the forwarding tests were unit-level tests.

They verified forwarding decisions and transport behavior independently.

Now that multiple ingress endpoints were working, I wanted to validate the complete runtime pipeline.

The first runtime test verifies unknown unicast flooding.

It confirms that

- an unknown unicast produces a `Flood` decision,
- the ingress port never receives its own packet,
- every eligible egress port receives exactly one packet,
- packet IDs remain unchanged,
- lifecycle IDs remain unchanged,
- transport counters report the correct packet and byte counts.

After that I added a learning switch runtime test.

The test verifies the expected learning behavior.

The first packet is flooded because the destination MAC address is unknown.

Once the source MAC address has been learned, the return packet is forwarded only to the learned destination instead of being flooded again.

I also verify that

- packet contents remain unchanged,
- lifecycle IDs remain unchanged,
- MAC table learning succeeds,
- transport dispatch occurs only on the expected ports,
- transport counters match the expected values.

The next runtime test validates MAC table aging.

After learning a MAC address, the corresponding entry is intentionally aged out.

Once the entry expires, another packet targeting that destination is injected.

The runtime correctly returns to flooding behavior because the destination is no longer present in the MAC table.

Finally, I wanted to verify the new lifecycle architecture.

I created two real `UdpReceiver` instances sharing the same production `LifecycleIdGenerator`.

Packets are then injected alternately into both receivers.

The expected lifecycle sequence becomes

```text
1
2
3
4
```

instead of

```text
1
1
2
2
```

This verifies that lifecycle IDs are globally unique regardless of which ingress endpoint receives the packet.

The last runtime test validates the complete multi-endpoint pipeline.

Two real UDP ingress endpoints receive packets through actual UDP sockets.

The packets then pass through the complete runtime.

```text
UDP Socket
    ↓
UdpReceiver
    ↓
PacketParser
    ↓
PacketValidator
    ↓
MessagingBus
    ↓
PacketProcessor
    ↓
SwitchForwardingEngine
    ↓
TransportManager
    ↓
PortBackend
```

The test verifies the entire forwarding pipeline.

It confirms

- correct ingress port assignment,
- correct forwarding decisions,
- correct learning behavior,
- packet integrity,
- lifecycle integrity,
- MAC address preservation,
- transport dispatch,
- transport counters.

At this point I had verified that the complete runtime behaves correctly with multiple UDP ingress endpoints.

The ingress architecture, forwarding pipeline, transport layer, lifecycle management, and runtime validation are now all driven by the endpoint model instead of hard-coded runtime configuration.