#include "CommandParser.hpp"

#include <iostream>
#include <sstream>

void CommandParser::registerCommand(const std::string& name, Handler handler)
{
    m_registry[name] = std::move(handler);
}

void CommandParser::execute(const std::string& line) const
{
    if (line.empty()) {
        return;
    }

    std::istringstream iss(line);
    Args args;
    std::string token;
    while (iss >> token) {
        args.push_back(token);
    }

    if (args.empty()) {
        return;
    }

    const std::string& cmd = args[0];
    auto it = m_registry.find(cmd);
    if (it != m_registry.end()) {
        it->second(args);
    } else {
        std::cerr << "command not found: " << cmd << '\n';
    }
}

std::vector<std::string> CommandParser::getRegisteredCommands() const
{
    std::vector<std::string> commands;
    commands.reserve(m_registry.size());
    for (const auto& pair : m_registry) {
        commands.push_back(pair.first);
    }
    return commands;
}
