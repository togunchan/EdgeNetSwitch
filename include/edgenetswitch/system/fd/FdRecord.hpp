#pragma once

#include "edgenetswitch/system/fd/FdState.hpp"
#include "edgenetswitch/system/fd/FdType.hpp"

namespace edgenetswitch
{
    struct FdRecord
    {
        int fd{-1};
        FdState state{FdState::Invalid};
        FdType fd_type{FdType::Unknown};
    };
} // namespace edgenetswitch