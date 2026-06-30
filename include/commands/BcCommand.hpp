#pragma once

#include "Commands.hpp"

namespace commands {

    // Calculator: open an FTXUI modal calculator that lets the user evaluate
    // basic arithmetic expressions. When the user exits, the final result is
    // written to `out`.
    void bc(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);

} // namespace commands
