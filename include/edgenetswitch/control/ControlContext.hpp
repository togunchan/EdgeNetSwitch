#pragma once

#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"

namespace edgenetswitch::daemon
{
    class SnapshotPublisher;
}

namespace edgenetswitch::core
{
    struct Config;
}

namespace edgenetswitch::control
{

    struct ControlContext
    {
        // Non-owning access to runtime snapshot publisher (read-only boundary).
        const edgenetswitch::daemon::SnapshotPublisher *publisher{nullptr};
        const edgenetswitch::core::Config *config{nullptr};
        MessagingBus *bus{nullptr};
        SwitchForwardingEngine *forwarding_engine{nullptr};
        FdRegistry *fd_registry{nullptr};
    };

} // namespace edgenetswitch::control
