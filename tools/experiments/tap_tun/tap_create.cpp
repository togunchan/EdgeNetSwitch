#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <linux/if_tun.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main()
{
    const int fd = ::open("/dev/net/tun", O_RDWR);

    if (fd < 0)
    {
        std::perror("open");
        return 1;
    }

    struct ifreq ifr
    {
    };

    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    std::strncpy(ifr.ifr_name, "tap0", IFNAMSIZ);

    if (::ioctl(fd, TUNSETIFF, &ifr) < 0)
    {
        std::perror("ioctl");
        ::close(fd);
        return 1;
    }

    std::cout << "Created interface: " << ifr.ifr_name << "\n";

    std::array<std::byte, 2048> buffer{};

    std::cout << "Waiting for Ethernet frame on tap0...\n";

    while (true)
    {
        const ssize_t received = ::read(fd, buffer.data(), buffer.size());

        if (received < 0)
        {
            std::perror("read");
            ::close(fd);
            return 1;
        }

        if (received < 14)
        {
            continue;
        }

        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(buffer.data());

        std::uint16_t ether_type{};

        std::memcpy(&ether_type, bytes + 12, sizeof(ether_type));

        ether_type = ntohs(ether_type);

        std::cout << std::dec << "Received " << received << " bytes" << " EtherType: 0x" << std::hex
                  << std::setw(4) << std::setfill('0') << ether_type << "\n";

        if (ether_type == ETH_P_ARP)
        {
            std::cout << "ARP frame received\n";
            break;
        }
    }

    close(fd);

    return 0;
}