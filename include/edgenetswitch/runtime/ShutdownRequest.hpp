#pragma once

#include "edgenetswitch/runtime/ShutdownReason.hpp"

#include <atomic>

namespace edgenetswitch
{
    class ShutdownRequest
    {
    public:
        void request(ShutdownReason reason);

        bool isRequested() const;

        ShutdownReason reason() const;

    private:
        std::atomic<bool> requested_{false};
        std::atomic<ShutdownReason> reason_{ShutdownReason::Unknown};
    };
} // namespace edgenetswitch
