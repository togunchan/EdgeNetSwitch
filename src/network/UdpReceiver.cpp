#include "edgenetswitch/network/UdpReceiver.hpp"

#include <iostream>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "edgenetswitch/core/Logger.hpp"
#include "edgenetswitch/core/TimeUtils.hpp"
#include "edgenetswitch/packet/PacketParser.hpp"

namespace edgenetswitch
{
    UdpReceiver::UdpReceiver(MessagingBus &bus, int port) : bus_(bus),
                                                            port_(port) {}

    UdpReceiver::~UdpReceiver()
    {
        stop();
    }

    void UdpReceiver::start()
    {
        if (running_)
            return;

        // Create UDP socket
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0)
        {
            std::cerr << "Failed to create socket\n";
            return;
        }

        // Bind to port
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(sockfd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            std::cerr << "Failed to bind socket\n";
            close(sockfd_);
            sockfd_ = -1;
            return;
        }

        running_ = true;

        // Start worker thread
        worker_ = std::thread(&UdpReceiver::run, this);

        std::cout << "[UdpReceiver] Listening on port " << port_ << "\n";
    }

    void UdpReceiver::stop()
    {
        if (!running_)
            return;

        running_ = false;

        if (sockfd_ >= 0)
        {
            close(sockfd_);
            sockfd_ = -1;
        }

        if (worker_.joinable())
        {
            worker_.join();
        }

        std::cout << "[UdpReceiver] Stopped\n";
    }

    void UdpReceiver::run()
    {
        char buffer[1024];

        while (running_)
        {
            sockaddr_in client_addr{};
            socklen_t addr_len = sizeof(client_addr);

            ssize_t len = recvfrom(sockfd_, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &addr_len);

            if (len < 0)
            {
                if (errno == EBADF)
                    break; // socket closed, exit thread cleanly

                if (errno == EINTR)
                    continue;

                Logger::error("recvfrom failed: " + std::string(strerror(errno)));
                continue;
            }

            std::string data(buffer, static_cast<size_t>(len));
            Logger::info("UDP packet received: " + data);
            auto packet = parsePacket(data);
            if (!packet.valid)
            {
                Logger::warn("Invalid packet dropped: " + data);
                continue;
            }
            packet.timestamp_ms = nowMs();

            sendto(sockfd_, buffer, len, 0, (struct sockaddr *)&client_addr, addr_len);

            Message msg{};
            msg.type = MessageType::PacketRx;
            msg.timestamp_ms = packet.timestamp_ms;
            msg.payload = std::move(packet);

            bus_.publish(std::move(msg));
        }
    }
} // namespace edgenetswitch