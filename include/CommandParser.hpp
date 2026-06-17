#pragma once

#ifndef TERMINAL_COMMANDPARSER_HPP
#define TERMINAL_COMMANDPARSER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <iostream>

#include <optional>
#include "CommandResolver.hpp"

class CommandParser {
public:
    using Args = std::vector<std::string>;
    using Handler = std::function<void(const Args&, std::ostream&, std::istream&, std::ostream& err)>;

    struct CommandDispatch {
        enum class Action { None, RunBuiltin, RunExternal, NotFound, Failed };
        Action action{Action::None};
        CommandResolver::ResolutionResult external; // valid when action == RunExternal
        std::string message;                       // error messages if any
        std::vector<std::vector<std::string>> parsedStages; // parsed & glob-expanded stages for reuse
    };

    void registerCommand(const std::string& name, Handler handler);
    void execute(const std::string& line) const;
    bool execute(const std::string& line, std::ostream& out) const;
    bool execute(const std::string& line, std::istream& in, std::ostream& out) const;
    bool executePipeline(const std::string& line, std::istream& in, std::ostream& out) const;
    bool executeParsed(const std::vector<std::vector<std::string>>& stages, std::istream& in, std::ostream& out) const;
    bool executeParsed(const std::vector<std::vector<std::string>>& stages) const;
    bool hasCommand(const std::string& name) const;
    std::vector<std::string> getRegisteredCommands() const;
    CommandDispatch dispatch(const std::string& line) const;

private:
    std::unordered_map<std::string, Handler> m_registry;
};

#endif // TERMINAL_COMMANDPARSER_HPP
