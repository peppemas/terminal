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
    using Handler = std::function<void(const Args&)>;
    using PipelineHandler = std::function<void(const Args&, std::ostream&, std::istream&)>;

    struct CommandDispatch {
        enum class Action { None, RunBuiltin, RunExternal, NotFound, Failed };
        Action action{Action::None};
        CommandResolver::ResolutionResult external; // valid when action == RunExternal
        std::string message;                       // error messages if any
    };

    void registerCommand(const std::string& name, Handler handler);
    void registerPipelineCommand(const std::string& name, PipelineHandler handler);
    void execute(const std::string& line) const;
    bool execute(const std::string& line, std::ostream& out) const;
    bool execute(const std::string& line, std::istream& in, std::ostream& out) const;
    bool executePipeline(const std::string& line, std::istream& in, std::ostream& out) const;
    bool hasCommand(const std::string& name) const;
    std::vector<std::string> getRegisteredCommands() const;
    CommandDispatch dispatch(const std::string& line) const;

private:
    std::unordered_map<std::string, Handler> m_registry;
    std::unordered_map<std::string, PipelineHandler> m_pipelineRegistry;
};

#endif // TERMINAL_COMMANDPARSER_HPP
