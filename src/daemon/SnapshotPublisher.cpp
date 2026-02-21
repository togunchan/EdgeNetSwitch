#include "SnapshotPublisher.hpp"

namespace edgenetswitch::daemon
{
    std::uint64_t SnapshotPublisher::publish(RuntimeStatus &status)
    {
        const auto v = version_.fetch_add(1, std::memory_order_relaxed) + 1;

        status.snapshot_version = v;

        auto snap = std::make_shared<const RuntimeStatus>(status);

        std::atomic_store(&snapshot_, std::move(snap));

        return v;
    }

    std::shared_ptr<const RuntimeStatus> SnapshotPublisher::load() const
    {
        return std::atomic_load(&snapshot_);
    }

    std::uint64_t SnapshotPublisher::version() const noexcept
    {
        return version_.load(std::memory_order_relaxed);
    }
} // namespace edgenetswitch::daemon
