#pragma once

#include "edgenetswitch/RuntimeMetrics.hpp"
#include <string>

namespace edgenetswitch
{
    struct RuntimeStatus
    {
        RuntimeMetrics metrics;
        std::string state; // booting, running, stopping etc...
    };
}