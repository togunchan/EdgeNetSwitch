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
            if (!resp.payload.empty())
            {
                out += resp.payload;
                if (resp.payload.back() != '\n')
                    out += "\n";
            }
            out += "END\n";
            return out;
        }
        out += "ERR\n";
        const std::string &code = resp.error_code.empty() ? error::InternalError : resp.error_code;
        const std::string &msg = resp.message.empty() ? std::string("internal error") : resp.message;
        out += "error_code=" + code + "\n";
        out += "message=" + msg + "\n";
        out += "END\n";
        return out;
    }

} // namespace edgenetswitch::control
