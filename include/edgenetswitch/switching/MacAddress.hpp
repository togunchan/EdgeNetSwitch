#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace edgenetswitch
{
    class MacAddress
    {
    public:
        using Bytes = std::array<std::uint8_t, 6>;

        MacAddress() = delete;
        explicit MacAddress(Bytes bytes);

        [[nodiscard]] static std::optional<MacAddress> fromString(std::string_view text);

        [[nodiscard]] std::string toString() const;
        [[nodiscard]] const Bytes &bytes() const noexcept;

        [[nodiscard]] bool isBroadcast() const noexcept;
        [[nodiscard]] bool isZero() const noexcept;

        // Allow MAC addresses to be compared using ==, <, >, and related operators.
        auto operator<=>(const MacAddress &) const = default;

    private:
        Bytes bytes_{};
    };
} // namespace edgenetswitch
