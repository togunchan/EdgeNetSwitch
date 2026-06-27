#pragma once

#include <cstdint>

namespace edgenetswitch::transport
{
    enum class TransmitStatus
    {
        Success,
        PortDown,
        BackendUnavailable,
        InvalidPacket,
        SendFailed
    };

    struct TransmitResult
    {
        TransmitStatus status{TransmitStatus::Success};
        std::uint32_t port_id{0};
        std::size_t bytes_transmitted{0};
        int native_error{0}; // errno
    };
} // namespace edgenetswitch::transport
