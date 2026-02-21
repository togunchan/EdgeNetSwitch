#pragma once

#include "edgenetswitch/RuntimeStatus.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace edgenetswitch::daemon
{
    class SnapshotPublisher final
    {
    public:
        SnapshotPublisher() = default;
        std::uint64_t publish(RuntimeStatus &status);
        std::shared_ptr<const RuntimeStatus> load() const;
        std::uint64_t version() const noexcept;

    private:
        std::atomic<std::uint64_t> version_{0};
        std::shared_ptr<const RuntimeStatus> snapshot_;
    };
} // namespace edgenetswitch::daemon
