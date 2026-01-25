#pragma once

#include "edgenetswitch/control/ControlProtocol.hpp"

#include <string>

namespace edgenetswitch::control
{

    inline std::string encodeResponse(const ControlResponse &resp)
    {
        std::string out;

        if (resp.success)
        {
            out += "OK\n";
            out += resp.payload;
            if (!resp.payload.empty() && resp.payload.back() != '\n')
                out += "\n";
        }
        else
        {
            out += "ERR\n";
            out += "error_code=" + resp.error_code + "\n";

            if (!resp.message.empty())
            {
                out += "message=" + resp.message + "\n";
            }
        }

        out += "END\n";
        return out;
    }

} // namespace edgenetswitch::control