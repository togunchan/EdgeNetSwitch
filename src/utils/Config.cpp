#include "edgenetswitch/Config.hpp"

#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <vector>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace edgenetswitch
{
    Config ConfigLoader::loadFromFile(const std::string &path)
    {
        namespace fs = std::filesystem;

        // Resolve config path by searching upward from the current working directory.
        // This allows running the daemon from build/ or other subdirectories.
        const fs::path requested(path);
        std::vector<fs::path> candidates;

        if (requested.is_absolute())
        {
            candidates.push_back(requested);
        }
        else
        {
            std::error_code ec;
            fs::path dir = fs::current_path(ec);

            if (ec)
            {
                throw std::runtime_error("Failed to determine current working directory: " + ec.message());
            }

            for (;;)
            {
                candidates.push_back(dir / requested);
                if (dir == dir.parent_path())
                {
                    break;
                }
                dir = dir.parent_path();
            }
        }

        fs::path resolvedPath;
        std::ifstream file;

        for (const auto &candidate : candidates)
        {
            std::ifstream candidateStream(candidate);
            if (candidateStream.is_open())
            {
                resolvedPath = candidate;
                file = std::move(candidateStream);
                break;
            }
        }

        if (!file.is_open())
        {
            std::string message = "Failed to open config file: " + path;
            if (!candidates.empty())
            {
                message += " (searched:";
                for (const auto &candidate : candidates)
                {
                    message += " " + candidate.string();
                }
                message += ")";
            }
            throw std::runtime_error(message);
        }

        json j;
        try
        {
            file >> j;
        }
        catch (const json::parse_error &err)
        {
            throw std::runtime_error("Failed to parse config file: " + resolvedPath.string() + " (" + std::string(err.what()) + ")");
        }

        Config cfg;

        auto objectOrEmpty = [](const json &parent, const std::string &key) -> json
        {
            auto it = parent.find(key);
            if (it != parent.end() && it->is_object())
            {
                return *it;
            }
            return json::object();
        };

        json logJson = objectOrEmpty(j, "log");
        json daemonJson = objectOrEmpty(j, "daemon");

        cfg.log.level = logJson.value("level", "info");
        cfg.log.file = logJson.value("file", "edgenetswitch.log");

        cfg.daemon.tick_ms = daemonJson.value("tick_ms", 100);

        return cfg;
    }
} // namespace edgenetswitch
