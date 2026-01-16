#pragma once

#include <functional>
#include <string>
#include <vector>

#include "edgenetswitch/control/ControlProtocol.hpp"

namespace edgenetswitch::control
{
    // ControlDispatch.hpp
    struct ControlContext;

    using Handler = std::function<ControlResponse(const ControlContext &)>;

    struct CommandDescriptor
    {
        std::string name;
        std::string description;
        std::vector<std::string> fields;
        Handler handler;
    };

    ControlResponse dispatchControlRequest(
        const ControlRequest &req,
        const ControlContext &ctx);

} // namespace edgenetswitch::control