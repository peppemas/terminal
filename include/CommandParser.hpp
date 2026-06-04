#pragma once

#ifndef TERMINAL_COMMANDPARSER_HPP
#define TERMINAL_COMMANDPARSER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

class CommandParser {
public:
    using Args = std::vector<std::string>;
    using Handler = std::function<void(const Args&)>;

    void registerCommand(const std::string& name, Handler handler);
    void execute(const std::string& line) const;
    std::vector<std::string> getRegisteredCommands() const;

private:
    std::unordered_map<std::string, Handler> m_registry;
};

#endif // TERMINAL_COMMANDPARSER_HPP
