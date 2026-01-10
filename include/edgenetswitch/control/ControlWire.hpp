#pragma once

#include "edgenetswitch/control/ControlProtocol.hpp"

#include <string>

namespace edgenetswitch::control{

    inline std::string encodeResponse(const ControlResponse& resp){
        std::string out;
        out += (resp.success ? "OK\n" : "ERR\n");

                if (resp.success)
        {
            out += resp.payload;
            if (!out.empty() && out.back() != '\n') out += '\n';
        }
        else
        {
            out += "message=" + resp.error + "\n";
        }

        out += "END\n";
        return out;
    }

} // namespace edgenetswitch::control