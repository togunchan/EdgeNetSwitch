#include "edgenetswitch/system/FdRegistry.hpp"
#include "edgenetswitch/system/FdRecord.hpp"
#include "edgenetswitch/system/FdState.hpp"
#include <mutex>
#include <vector>

namespace edgenetswitch
{
    void FdRegistry::registerFd(int fd, FdState state)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        records_[fd] = FdRecord{.fd = fd, .state = state};
    }

    void FdRegistry::updateState(int fd, FdState state)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = records_.find(fd);

        if (it == records_.end())
            return;

        it->second.state = state;
    }

    void FdRegistry::unregisterFd(int fd)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        records_.erase(fd);
    }

    std::vector<FdRecord> FdRegistry::snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<FdRecord> snapshot;
        snapshot.reserve(records_.size());

        for (const auto &[fd, record] : records_)
        {
            snapshot.push_back(record);
        }

        return snapshot;
    }
} // namespace edgenetswitch
