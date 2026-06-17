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

    void ls  (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void rm  (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void cp  (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void mv  (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void cat (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void tail(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void grep(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void cd  (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void clear(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void pwd(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void open(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void echo(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void push(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void pop (const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void slots(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void more(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);
    void mkdir(const Args& args, std::ostream& out, std::istream& in, std::ostream& err);

} // namespace commands

#endif // TERMINAL_COMMANDS_HPP
