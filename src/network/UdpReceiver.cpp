#include "edgenetswitch/network/UdpReceiver.hpp"

#include <iostream>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

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
            ssize_t len = recvfrom(sockfd_, buffer, sizeof(buffer), 0, nullptr, nullptr);

            if (len <= 0)
                continue;

            Packet packet{};
            packet.timestamp_ms = 0; // dummy for now
            packet.size_bytes = static_cast<std::uint32_t>(len);

            Message msg{};
            msg.type = MessageType::PacketRx;
            msg.timestamp_ms = packet.timestamp_ms;
            msg.payload = packet;

            bus_.publish(msg);
        }
    }
} // namespace edgenetswitch