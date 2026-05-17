#include "edgenetswitch/switching/MacAddress.hpp"
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>

namespace edgenetswitch
{
    MacAddress::MacAddress(Bytes bytes) : bytes_(bytes) {}

    std::string MacAddress::toString() const
    {
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');

        for (std::size_t index = 0; index < bytes_.size(); ++index)
        {
            if (index != 0)
                stream << ":";

            stream << std::setw(2) << static_cast<int>(bytes_[index]);
        }
        return stream.str();
    }

    const MacAddress::Bytes &MacAddress::bytes() const noexcept
    {
        return bytes_;
    }

    bool MacAddress::isBroadcast() const noexcept
    {
        for (std::uint8_t byte : bytes_)
        {
            if (byte != 0xFF)
                return false;
        }
        return true;
    }

    bool MacAddress::isZero() const noexcept
    {
        for (std::uint8_t byte : bytes_)
        {
            if (byte != 0x00)
                return false;
        }
        return true;
    }

    static std::optional<std::uint8_t> hexValue(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';

        if (c >= 'a' && c <= 'f')
            return 10 + (c - 'a');

        if (c >= 'A' && c <= 'F')
            return 10 + (c - 'A');

        return std::nullopt;
    }

    std::optional<MacAddress> MacAddress::fromString(std::string_view text)
    {
        if (text.size() != 17)
            return std::nullopt;

        Bytes bytes{};

        for (std::size_t index = 0; index < 6; ++index)
        {
            std::size_t offset = index * 3;

            if (index != 5 && text[offset + 2] != ':')
            {
                return std::nullopt;
            }

            auto high = hexValue(text[offset]);
            auto low = hexValue(text[(offset + 1)]);

            if (!high || !low)
            {
                return std::nullopt;
            }
            bytes[index] = static_cast<std::uint8_t>((*high << 4) | *low);
        }
        return MacAddress(bytes);
    }
} // namespace edgenetswitch
