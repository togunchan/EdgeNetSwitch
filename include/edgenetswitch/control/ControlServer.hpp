#pragma once

#include "edgenetswitch/control/ControlProtocol.hpp"
#include "edgenetswitch/core/Config.hpp"
#include "edgenetswitch/messaging/MessagingBus.hpp"
#include "edgenetswitch/switching/SwitchForwardingEngine.hpp"
#include "edgenetswitch/system/fd/FdRegistry.hpp"
#include "edgenetswitch/system/fd/FileDescriptor.hpp"
#include "runtime/SnapshotPublisher.hpp"

namespace edgenetswitch::control
{
    class ControlServer
    {
    public:
        ControlServer(FileDescriptor &listen_fd, daemon::SnapshotPublisher &publisher,
                      const core::Config &config, MessagingBus &bus,
                      SwitchForwardingEngine &forwarding_engine, FdRegistry &fd_registry);

        [[nodiscard]]
        int fd() const noexcept;

        void processReadableEvent();

    private:
        void handleClient(int client_fd);
        static void writeControlResponse(int client_fd, const control::ControlResponse &resp);

        FileDescriptor &listen_fd_;
        daemon::SnapshotPublisher &publisher_;
        const core::Config &config_;
        MessagingBus &bus_;
        SwitchForwardingEngine &forwarding_engine_;
        FdRegistry &fd_registry_;
    };
} // namespace edgenetswitch::control