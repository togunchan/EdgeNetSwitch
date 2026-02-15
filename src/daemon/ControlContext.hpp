#pragma once

#include "edgenetswitch/RuntimeStatus.hpp"

#include <memory>

namespace edgenetswitch::control
{

    struct ControlContext
    {
        const std::shared_ptr<const edgenetswitch::RuntimeStatus> *snapshot_ptr{nullptr};
    };

} // namespace edgenetswitch::control
