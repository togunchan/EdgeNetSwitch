#include "edgenetswitch/MessagingBus.hpp"

namespace edgenetswitch
{

    void MessagingBus::subscribe(MessageType type, Callback callback)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_[type].push_back(std::move(callback));
    }

    void MessagingBus::publish(const Message &message)
    {
        std::vector<Callback> callbacks;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = subscribers_.find(message.type);
            if (it != subscribers_.end())
            {
                callbacks = it->second; // copy callbacks for safe iteration
            }
        }

        for (const auto &cb : callbacks)
        {
            cb(message);
        }
    }

} // namespace edgenetswitch