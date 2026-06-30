#include "edgenetswitch/control/ControlServer.hpp"
#include "control/ControlDispatch.hpp"
#include "control/JsonResponse.hpp"
#include "edgenetswitch/control/ControlContext.hpp"
#include "edgenetswitch/control/ControlProtocol.hpp"
#include "edgenetswitch/control/ControlWire.hpp"

#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/transport/TransportManager.hpp"
#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace edgenetswitch::control

{

    ControlServer::ControlServer(FileDescriptor &listen_fd, daemon::SnapshotPublisher &publisher,
                                 const core::Config &config, MessagingBus &bus,
                                 SwitchForwardingEngine &forwarding_engine, FdRegistry &fd_registry,
                                 edgenetswitch::transport::TransportManager &transport_manager)
        : listen_fd_(listen_fd), publisher_(publisher), config_(config), bus_(bus),
          forwarding_engine_(forwarding_engine), fd_registry_(fd_registry), transport_manager_(transport_manager)

    {
    }

    int ControlServer::fd() const noexcept
    {
        return listen_fd_.get();
    }

    void ControlServer::processReadableEvent()
    {
        while (true)
        {
            int client_fd =
                ::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);

            if (client_fd < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }

                Logger::error("Control socket accept failed");
                break;
            }

            handleClient(client_fd);
        }
    }

    void ControlServer::handleClient(int client_fd)
    {
        char buffer[128]{};
        ssize_t n = ::read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0)
        {
            std::string cmd(buffer, n);

            auto sep = cmd.find('|');
            if (sep == std::string::npos)
            {
                const auto resp =
                    control::makeJsonError(control::error::InvalidRequest, "malformed_request");
                writeControlResponse(client_fd, resp);
                ::close(client_fd);
                return;
            }

            control::ControlRequest req{.version = cmd.substr(0, sep),
                                        .command = cmd.substr(sep + 1)};

            // trim newline
            req.command.erase(req.command.find_last_not_of(" \n\r\t") + 1);

            Logger::info("Control command received: " + req.command);

            control::ControlContext ctx{
                .publisher = &publisher_, .config = &config_, .bus = &bus_,
                .forwarding_engine = &forwarding_engine_, .fd_registry = &fd_registry_,
                .transport_manager = &transport_manager_};

            const control::ControlResponse resp = control::dispatchControlRequest(req, ctx);
            writeControlResponse(client_fd, resp);
        }
        ::close(client_fd);
    }

    void ControlServer::writeControlResponse(int client_fd, const control::ControlResponse &resp)
    {
        const std::string wire =
            resp.payload.empty() ? control::encodeResponse(resp) : resp.payload;

        ::write(client_fd, wire.c_str(), wire.size());
    }
} // namespace edgenetswitch::control