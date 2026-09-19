#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <linux/if_ether.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
    const int fd = ::socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));

    if (fd < 0)
    {
        std::perror("socket");
        return 1;
    }

    const char *interface_name = "veth-af-host";

    const unsigned int interface_index = ::if_nametoindex(interface_name);

    if (interface_index == 0)
    {
        std::cerr << "Failed to resolve interface index\n";
        ::close(fd);
        return 1;
    }

    struct sockaddr_ll address
    {
    };
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(ETH_P_IP);
    address.sll_ifindex = static_cast<int>(interface_index);

    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        std::cerr << "Failed to bind AF_PACKET socket\n";
        ::close(fd);
        return 1;
    }

    std::cout << "Listening on " << interface_name << " (ifindex=" << interface_index << ")\n";

    std::array<std::byte, 2048> buffer{};

    const ssize_t received = ::recvfrom(fd, buffer.data(), buffer.size(), 0, nullptr, nullptr);

    if (received < 0)
    {
        std::cerr << "Failed to receive packet\n";
        ::close(fd);
        return 1;
    }

    std::cout << "Received " << received << " bytes\n";

    // if (received < ETH_HLEN)
    // {
    //     std::cerr << "Frame is smaller than Ethernet header\n";
    //     ::close(fd);
    //     return 1;
    // }

    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(buffer.data());

    std::cout << "First byte: 0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(bytes[0]) << "\n";

    // std::cout << "Destination MAC: ";

    // for (int i = 0; i < ETH_ALEN; ++i)
    // {
    //     if (i != 0)
    //     {
    //         std::cout << ":";
    //     }

    //     std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    // }

    // std::cout << "\nSource MAC: ";

    // for (int i = 0; i < ETH_ALEN; ++i)
    // {
    //     if (i != 0)
    //     {
    //         std::cout << ":";
    //     }

    //     std::cout << std::hex << std::setw(2) << std::setfill('0')
    //               << static_cast<int>(bytes[ETH_ALEN + i]);
    // }

    // std::uint16_t ether_type{};

    // std::memcpy(&ether_type, bytes + 12, sizeof(ether_type));

    // ether_type = ntohs(ether_type);

    // std::cout << "\nEtherType: 0x" << std::hex << std::setw(4) << std::setfill('0') << ether_type
    //           << "\n";

    // const unsigned char ip_protocol = bytes[ETH_HLEN + 9];

    // std::cout << std::dec << "IPv4 protocol: " << static_cast<int>(ip_protocol) << "\n";

    // const std::size_t ip_offset = ETH_HLEN;

    // const unsigned char ip_header_length = (bytes[ip_offset] & 0x0F) * 4;

    // const std::size_t udp_offset = ETH_HLEN + ip_header_length;

    // std::uint16_t source_port{};
    // std::uint16_t destination_port{};

    // std::memcpy(&source_port, bytes + udp_offset, sizeof(source_port));

    // std::memcpy(&destination_port, bytes + udp_offset + 2, sizeof(destination_port));

    // source_port = ntohs(source_port);
    // destination_port = ntohs(destination_port);

    // std::cout << "UDP source port: " << std::dec << source_port << "\n";

    // std::cout << "UDP destination port: " << destination_port << "\n";

    // const std::size_t payload_offset = udp_offset + 8;

    // std::cout << "Payload: ";

    // for (std::size_t i = payload_offset; i < static_cast<std::size_t>(received); ++i)
    // {
    //     std::cout << bytes[i];
    // }

    // std::cout << "\n";

    ::close(fd);
    return 0;
}