#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main()
{
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0)
    {
        std::perror("socket");
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(5000);

    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0)
    {
        std::perror("bind");
        ::close(fd);
        return 1;
    }

    std::cout << "UDP receiver listening on port 5000\n";

    std::array<char, 2048> buffer{};

    const ssize_t received = ::recvfrom(fd, buffer.data(), buffer.size(), 0, nullptr, nullptr);

    if (received < 0)
    {
        std::perror("recvfrom");
        ::close(fd);
        return 1;
    }

    std::cout << "Received bytes: " << received << "\n";

    std::cout << "Payload: " << std::string(buffer.data(), received) << "\n";

    ::close(fd);

    return 0;
}