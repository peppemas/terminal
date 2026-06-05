#pragma once

#ifndef TERMINAL_GLOBEXPANDER_HPP
#define TERMINAL_GLOBEXPANDER_HPP

#include <string>
#include <vector>
#include "ShellTokenizer.hpp"

class GlobExpander {
public:
    static std::vector<std::string> expand(const std::vector<shell::Token>& tokens);

private:
    static bool matchPattern(const std::string& pattern, const std::string& name);
    static std::vector<std::string> matchInCurrentDirectory(const std::string& pattern);
};

#endif // TERMINAL_GLOBEXPANDER_HPP
