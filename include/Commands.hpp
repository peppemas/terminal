#pragma once

#ifndef TERMINAL_COMMANDS_HPP
#define TERMINAL_COMMANDS_HPP

#include <string>
#include <vector>
#include <iostream>

namespace commands {

    using Args = std::vector<std::string>;

    inline constexpr const char* RESET     = "\x1b[0m";
    inline constexpr const char* BOLD_BLUE = "\x1b[1;34m";
    inline constexpr const char* RED       = "\x1b[31m";
    inline constexpr const char* BOLD_GREEN = "\x1b[1;32m";
    inline constexpr const char* CYAN       = "\x1b[36m";
    inline constexpr const char* MAGENTA    = "\x1b[35m";

    void ls  (const Args& args, std::ostream& out, std::istream& in);
    void rm  (const Args& args, std::ostream& out, std::istream& in);
    void cp  (const Args& args, std::ostream& out, std::istream& in);
    void mv  (const Args& args, std::ostream& out, std::istream& in);
    void cat (const Args& args, std::ostream& out, std::istream& in);
    void tail(const Args& args, std::ostream& out, std::istream& in);
    void grep(const Args& args, std::ostream& out, std::istream& in);
    void cd  (const Args& args, std::ostream& out, std::istream& in);
    void clear(const Args& args, std::ostream& out, std::istream& in);
    void pwd(const Args& args, std::ostream& out, std::istream& in);
    void open(const Args& args, std::ostream& out, std::istream& in);
    void echo(const Args& args, std::ostream& out, std::istream& in);
    void push(const Args& args, std::ostream& out, std::istream& in);
    void pop (const Args& args, std::ostream& out, std::istream& in);
    void slots(const Args& args, std::ostream& out, std::istream& in);
    void more(const Args& args, std::ostream& out, std::istream& in);

    // Legacy backward-compatible wrappers
    inline void ls  (const Args& args) { ls(args, std::cout, std::cin); }
    inline void rm  (const Args& args) { rm(args, std::cout, std::cin); }
    inline void cp  (const Args& args) { cp(args, std::cout, std::cin); }
    inline void mv  (const Args& args) { mv(args, std::cout, std::cin); }
    inline void cat (const Args& args) { cat(args, std::cout, std::cin); }
    inline void tail(const Args& args) { tail(args, std::cout, std::cin); }
    inline void grep(const Args& args) { grep(args, std::cout, std::cin); }
    inline void cd  (const Args& args) { cd(args, std::cout, std::cin); }
    inline void clear(const Args& args) { clear(args, std::cout, std::cin); }
    inline void pwd(const Args& args) { pwd(args, std::cout, std::cin); }
    inline void open(const Args& args) { open(args, std::cout, std::cin); }
    inline void echo(const Args& args) { echo(args, std::cout, std::cin); }
    inline void push(const Args& args) { push(args, std::cout, std::cin); }
    inline void pop (const Args& args) { pop (args, std::cout, std::cin); }
    inline void slots(const Args& args) { slots(args, std::cout, std::cin); }
    inline void more(const Args& args) { more(args, std::cout, std::cin); }

} // namespace commands

#endif // TERMINAL_COMMANDS_HPP
