#pragma once

#include "edgenetswitch/runtime/RuntimeStatus.hpp"

#include <memory>

namespace edgenetswitch::daemon
{
    class SnapshotPublisher;
}

namespace edgenetswitch::control
{

    struct ControlContext
    {
        // Non-owning access to runtime snapshot publisher (read-only boundary).
        const edgenetswitch::daemon::SnapshotPublisher *publisher{nullptr};
    };

} // namespace edgenetswitch::control
