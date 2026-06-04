#pragma once

#ifndef TERMINAL_COMMANDPARSER_HPP
#define TERMINAL_COMMANDPARSER_HPP

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <iostream>

class CommandParser {
public:
    using Args = std::vector<std::string>;
    using Handler = std::function<void(const Args&)>;
    using PipelineHandler = std::function<void(const Args&, std::ostream&, std::istream&)>;

    void registerCommand(const std::string& name, Handler handler);
    void registerPipelineCommand(const std::string& name, PipelineHandler handler);
    void execute(const std::string& line) const;
    bool execute(const std::string& line, std::ostream& out) const;
    bool execute(const std::string& line, std::istream& in, std::ostream& out) const;
    bool executePipeline(const std::string& line, std::istream& in, std::ostream& out) const;
    bool hasCommand(const std::string& name) const;
    std::vector<std::string> getRegisteredCommands() const;

private:
    std::unordered_map<std::string, Handler> m_registry;
    std::unordered_map<std::string, PipelineHandler> m_pipelineRegistry;
};

#endif // TERMINAL_COMMANDPARSER_HPP
