#include "edgenetswitch/switching/MacTable.hpp"
#include "edgenetswitch/switching/MacAddress.hpp"
#include "edgenetswitch/switching/MacTableEntry.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace edgenetswitch
{
    MacTable::MacTable(std::size_t capacity) : capacity_(capacity) {}

    void MacTable::learn(const MacAddress &mac, std::uint32_t port_id, std::uint64_t tick)
    {

        if (capacity_ == 0)
            return;

        auto it = entries_.find(mac);

        if (it != entries_.end())
        {
            it->second.port_id = port_id;
            it->second.last_seen_tick = tick;
            return;
        }

        if (entries_.size() >= capacity_)
        {
            auto oldest = entries_.begin();

            for (auto candidate = entries_.begin(); candidate != entries_.end(); ++candidate)
            {
                if (candidate->second.last_seen_tick < oldest->second.last_seen_tick)
                {
                    oldest = candidate;
                }
            }
            entries_.erase(oldest);
        }
        entries_.emplace(mac, MacTableEntry{
                                  .mac = mac,
                                  .port_id = port_id,
                                  .last_seen_tick = tick,
                              });
    }

    std::optional<std::uint32_t> MacTable::lookup(const MacAddress &mac) const
    {
        auto it = entries_.find(mac);

        if (it == entries_.end())
            return std::nullopt;

        return it->second.port_id;
    }

    void MacTable::ageOut(std::uint64_t current_tick, std::uint64_t max_age)
    {
        auto it = entries_.begin();

        while (it != entries_.end())
        {
            if ((current_tick - it->second.last_seen_tick) > max_age)
            {
                it = entries_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::vector<MacTableEntry> MacTable::snapshot() const
    {
        std::vector<MacTableEntry> snapshot;

        for (const auto &entry_pair : entries_)
        {
            snapshot.push_back(entry_pair.second);
        }

        return snapshot;
    }

    std::size_t MacTable::size() const noexcept
    {
        return entries_.size();
    }

    std::size_t MacTable::capacity() const noexcept
    {
        return capacity_;
    }

} // namespace edgenetswitch