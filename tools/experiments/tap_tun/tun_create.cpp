#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/if_tun.h>
#include <net/if.h>
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

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    std::strncpy(ifr.ifr_name, "tun0", IFNAMSIZ);

    if (::ioctl(fd, TUNSETIFF, &ifr) < 0)
    {
        std::perror("ioctl");
        ::close(fd);
        return 1;
    }

    std::cout << "Created interface: " << ifr.ifr_name << "\n";

    std::array<std::byte, 2048> buffer{};

    std::cout << "Waiting for IP packet on tun0...\n";

    while (true)
    {
        const ssize_t received = ::read(fd, buffer.data(), buffer.size());

        if (received < 0)
        {
            std::perror("read");
            ::close(fd);
            return 1;
        }

        if (received == 0)
        {
            continue;
        }

        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(buffer.data());

        const unsigned char version = bytes[0] >> 4;

        std::cout << std::dec << "Received " << received << " bytes"
                  << " IP version: " << static_cast<int>(version) << "\n";

        if (version == 4)
        {
            std::cout << "IPv4 packet received\n";
            break;
        }
    }

    ::close(fd);

    return 0;
}