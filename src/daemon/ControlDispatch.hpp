#pragma once

#include "edgenetswitch/control/ControlProtocol.hpp"
#include "ControlContext.hpp"

namespace edgenetswitch::control
{

    ControlResponse dispatchControlRequest(
        const ControlRequest &req,
        const ControlContext &ctx);

} // namespace edgenetswitch::control