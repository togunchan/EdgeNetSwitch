#pragma once

#include "edgenetswitch/system/FdState.hpp"

namespace edgenetswitch
{
    struct FdRecord
    {
        int fd{-1};
        FdState state{FdState::Invalid};
    };
} // namespace edgenetswitch