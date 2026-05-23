#pragma once

#include "edgenetswitch/system/FdRecord.hpp"
#include "edgenetswitch/system/FdState.hpp"

#include <mutex>
#include <unordered_map>
#include <vector>

namespace edgenetswitch
{
    class FdRegistry
    {
    public:
        void registerFd(int fd, FdState state);

        void updateState(int fd, FdState state);

        void unregisterFd(int fd);

        [[nodiscard]]
        std::vector<FdRecord> snapshot() const;

    private:
        mutable std::mutex mutex_;
        std::unordered_map<int, FdRecord> records_;
        
    };
} // namespace edgenetswitch
