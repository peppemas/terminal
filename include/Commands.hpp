#pragma once

#ifndef TERMINAL_COMMANDS_HPP
#define TERMINAL_COMMANDS_HPP

#include <string>
#include <vector>

namespace commands {

    using Args = std::vector<std::string>;

    inline constexpr const char* RESET     = "\x1b[0m";
    inline constexpr const char* BOLD_BLUE = "\x1b[1;34m";
    inline constexpr const char* RED       = "\x1b[31m";
    inline constexpr const char* BOLD_GREEN = "\x1b[1;32m";
    inline constexpr const char* CYAN       = "\x1b[36m";
    inline constexpr const char* MAGENTA    = "\x1b[35m";

    void ls  (const Args& args);
    void rm  (const Args& args);
    void cp  (const Args& args);
    void mv  (const Args& args);
    void cat (const Args& args);
    void tail(const Args& args);
    void grep(const Args& args);
    void cd  (const Args& args);
    void clear(const Args& args);
    void pwd(const Args& args);
    void open(const Args& args);

} // namespace commands

#endif // TERMINAL_COMMANDS_HPP
