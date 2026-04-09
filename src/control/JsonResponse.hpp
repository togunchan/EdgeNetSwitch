#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "ControlDispatch.hpp"

namespace edgenetswitch::control
{

    inline ControlResponse makeJsonSuccess(const nlohmann::json &data)
    {
        nlohmann::json j;
        j["status"] = "ok";
        j["data"] = data;

        return ControlResponse{
            .success = true,
            .payload = j.dump(2)};
    }

    inline ControlResponse makeJsonError(const std::string &code, const std::string &msg)
    {
        nlohmann::json j;
        j["status"] = "error";
        j["error"]["code"] = code;
        j["error"]["message"] = msg;

        return ControlResponse{
            .success = false,
            .payload = j.dump(2),
            .error_code = code,
            .message = msg};
    }

} // namespace edgenetswitch::control