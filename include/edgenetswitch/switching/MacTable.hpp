#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/MacTableEntry.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace edgenetswitch
{
    class MacTable
    {
    public:
        explicit MacTable(std::size_t capacity);

        void learn(const MacAddress &mac, std::uint32_t port_id, std::uint64_t tick);

        [[nodiscard]]
        std::optional<std::uint32_t> lookup(const MacAddress &mac) const;

        [[nodiscard]]
        std::vector<MacTableEntry> snapshot() const;

        [[nodiscard]]
        std::size_t size() const noexcept;

        [[nodiscard]]
        std::size_t capacity() const noexcept;

    private:
        std::size_t capacity_{0};
        std::map<MacAddress, MacTableEntry> entries_;
    };
} // namespace edgenetswitch