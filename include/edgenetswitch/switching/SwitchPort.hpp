#pragma once

#include <cstdint>
#include <string>

namespace edgenetswitch
{
    enum class PortState
    {
        Down,
        Up
    };

    class SwitchPort
    {
    public:
        SwitchPort(std::uint32_t id, std::string name);

        [[nodiscard]]
        std::uint32_t id() const noexcept;

        [[nodiscard]]
        const std::string &name() const noexcept;

        [[nodiscard]]
        PortState state() const noexcept;

        void setState(PortState state);

    private:
        std::uint32_t id_{0};
        std::string name_;
        PortState state_{PortState::Down};
    };
} // namespace edgenetswitch
