#include "edgenetswitch/switching/SwitchPort.hpp"

namespace edgenetswitch
{
    SwitchPort::SwitchPort(std::uint32_t id, std::string name) : id_(id), name_(std::move(name)) {}

    std::uint32_t SwitchPort::id() const noexcept
    {
        return id_;
    }

    const std::string &SwitchPort::name() const noexcept
    {
        return name_;
    }

    PortState SwitchPort::state() const noexcept
    {
        return state_;
    }

    void SwitchPort::setState(PortState state)
    {
        state_ = state;
    }
} // namespace edgenetswitch