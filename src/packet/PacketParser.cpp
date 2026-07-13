#include "edgenetswitch/packet/PacketParser.hpp"

namespace edgenetswitch
{
    Packet parsePacket(const std::string &data)
    {
        Packet p{};
        p.valid = false;

        if (data.empty())
            return p;

        auto idPos = data.find("id=");
        if (idPos == std::string::npos)
            return p;

        auto end = data.find(';', idPos);

        std::string idStr;
        if (end != std::string::npos)
            idStr = data.substr(idPos + 3, end - (idPos + 3));
        else
            idStr = data.substr(idPos + 3);

        if (idStr.empty())
            return p;

        try
        {
            p.id = std::stoull(idStr);
        }
        catch (...)
        {
            return p;
        }
        auto srcPos = data.find("src=");

        if (srcPos != std::string::npos)
        {
            auto srcEnd = data.find(';', srcPos);

            std::string srcStr;

            if (srcEnd != std::string::npos)
                srcStr = data.substr(srcPos + 4, srcEnd - (srcPos + 4));
            else
                srcStr = data.substr(srcPos + 4);

            p.source_mac = MacAddress::fromString(srcStr);
        }

        auto dstPos = data.find("dst=");

        if (dstPos != std::string::npos)
        {
            auto dstEnd = data.find(';', dstPos);

            std::string dstStr;

            if (dstEnd != std::string::npos)
                dstStr = data.substr(dstPos + 4, dstEnd - (dstPos + 4));
            else
                dstStr = data.substr(dstPos + 4);

            p.destination_mac = MacAddress::fromString(dstStr);
        }

        auto payloadPos = data.find("payload=");
        if (payloadPos != std::string::npos)
        {
            auto payloadEnd = data.find(';', payloadPos);

            if (payloadEnd != std::string::npos)
                p.payload = data.substr(payloadPos + 8, payloadEnd - (payloadPos + 8));
            else
                p.payload = data.substr(payloadPos + 8);
        }

        p.payload_size = static_cast<std::uint32_t>(p.payload.size());
        p.valid = p.source_mac.has_value() && p.destination_mac.has_value();

        return p;
    }
} // namespace edgenetswitch