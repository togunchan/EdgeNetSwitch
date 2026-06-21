#include "edgenetswitch/runtime/ShutdownRequest.hpp"
#include "edgenetswitch/runtime/ShutdownReason.hpp"
#include <atomic>

namespace edgenetswitch
{
    void ShutdownRequest::request(ShutdownReason reason)
    {
        bool expected = false;

        if (requested_.compare_exchange_strong(expected, true))
        {
            reason_.store(reason, std::memory_order_relaxed);
        }
    }

    bool ShutdownRequest::isRequested() const
    {
        return requested_.load(std::memory_order_relaxed);
    }

    ShutdownReason ShutdownRequest::reason() const
    {
        return reason_.load(std::memory_order_relaxed);
    }

} // namespace edgenetswitch