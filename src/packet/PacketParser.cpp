#include "edgenetswitch/packet/PacketParser.hpp"

namespace edgenetswitch
{
    Packet parsePacket(const std::string &data)
    {
        Packet p{};
        p.valid = false;
        p.payload = data;

        auto idPos = data.find("id=");
        if (idPos != std::string::npos)
        {
            auto end = data.find(';', idPos);

            std::string idStr;
            if (end != std::string::npos)
                idStr = data.substr(idPos + 3, end - (idPos + 3));
            else
                idStr = data.substr(idPos + 3);

            try
            {
                p.id = std::stoull(idStr);
            }
            catch (...)
            {
            }

            auto payloadPos = data.find("payload=");
            if (payloadPos != std::string::npos)
            {
                auto payloadEnd = data.find(';', payloadPos);

                if (payloadEnd != std::string::npos)
                    p.payload = data.substr(payloadPos + 8,
                                            payloadEnd - (payloadPos + 8));
                else
                    p.payload = data.substr(payloadPos + 8);
            }
        }

        p.size_bytes = static_cast<std::uint32_t>(data.size());
        p.valid = true;
        return p;
    }
} // namespace edgenetswitch