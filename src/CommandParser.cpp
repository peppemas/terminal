#include "CommandParser.hpp"

#include "CommandResolver.hpp"
#include "ExternalExecutor.hpp"
#include "GlobExpander.hpp"
#include "ShellTokenizer.hpp"
#include <iostream>
#include <sstream>

void CommandParser::registerCommand(const std::string& name, Handler handler)
{
    m_registry[name] = std::move(handler);
}

void CommandParser::registerPipelineCommand(const std::string& name, PipelineHandler handler)
{
    m_pipelineRegistry[name] = std::move(handler);
}

bool CommandParser::executePipeline(const std::string& line, std::istream& in, std::ostream& out) const
{
    if (line.empty()) {
        return true;
    }

    auto tokenStages = shell::tokenizePipeline(line);
    std::vector<std::vector<std::string>> stages;
    stages.reserve(tokenStages.size());
    for (const auto& ts : tokenStages) {
        stages.push_back(GlobExpander::expand(ts));
    }
    if (stages.empty()) {
        return true;
    }

    std::string previousOutput;
    bool hasPrevious = false;

    for (std::size_t s = 0; s < stages.size(); ++s) {
        const auto& argv = stages[s];
        if (argv.empty()) {
            std::cerr << "syntax error: empty pipeline stage\n";
            return false;
        }

        const std::string& cmd = argv[0];

        std::istringstream prevStream(previousOutput);
        std::istream* stageIn = hasPrevious ? static_cast<std::istream*>(&prevStream) : &in;

        std::ostringstream nextStream;
        std::ostream* stageOut = (s + 1 == stages.size()) ? &out : static_cast<std::ostream*>(&nextStream);

        auto pIt = m_pipelineRegistry.find(cmd);
        if (pIt != m_pipelineRegistry.end()) {
            pIt->second(argv, *stageOut, *stageIn);
        } else {
            auto lIt = m_registry.find(cmd);
            if (lIt != m_registry.end()) {
                lIt->second(argv);
            } else {
                auto resolved = CommandResolver::resolve(cmd, argv);
                if (resolved) {
                    int exitCode = 0;
                    if (!ExternalExecutor::run(*resolved, &exitCode)) {
                        std::cerr << "failed to start: " << cmd << '\n';
                        return false;
                    }
                } else {
                    std::cerr << "command not found: " << cmd << '\n';
                    return false;
                }
            }
        }

        previousOutput = nextStream.str();
        hasPrevious = true;
    }

    return true;
}

bool CommandParser::execute(const std::string& line, std::istream& in, std::ostream& out) const
{
    if (line.empty()) {
        return true;
    }
    return executePipeline(line, in, out);
}

bool CommandParser::execute(const std::string& line, std::ostream& out) const
{
    return execute(line, std::cin, out);
}

void CommandParser::execute(const std::string& line) const
{
    // The inner execute() -> executePipeline() already prints the
    // "command not found" error to std::cerr when no handler matches
    // and CommandResolver::resolve() fails. We just forward the call.
    execute(line, std::cin, std::cout);
}

CommandParser::CommandDispatch CommandParser::dispatch(const std::string& line) const
{
    CommandDispatch d;
    if (line.empty()) {
        d.action = CommandDispatch::Action::None;
        return d;
    }

    auto tokenStages = shell::tokenizePipeline(line);
    std::vector<std::vector<std::string>> stages;
    stages.reserve(tokenStages.size());
    for (const auto& ts : tokenStages) {
        stages.push_back(GlobExpander::expand(ts));
    }
    if (stages.empty()) {
        d.action = CommandDispatch::Action::None;
        return d;
    }

    // Only handle single-stage commands here. Multi-stage pipelines
    // must still go through executePipeline() (which auto-executes).
    if (stages.size() != 1) {
        d.action = CommandDispatch::Action::RunBuiltin;
        return d;
    }

    const auto& argv = stages[0];
    if (argv.empty()) {
        d.action = CommandDispatch::Action::Failed;
        d.message = "syntax error: empty command";
        return d;
    }
    const std::string& cmd = argv[0];

    // Check builtin first
    if (m_registry.count(cmd) || m_pipelineRegistry.count(cmd)) {
        d.action = CommandDispatch::Action::RunBuiltin;
        return d;
    }

    // Try to resolve as external
    auto resolved = CommandResolver::resolve(cmd, argv);
    if (resolved) {
        d.action = CommandDispatch::Action::RunExternal;
        d.external = std::move(*resolved);
        return d;
    }

    d.action = CommandDispatch::Action::NotFound;
    d.message = "command not found: " + cmd;
    return d;
}

bool CommandParser::hasCommand(const std::string& name) const
{
    return m_registry.find(name) != m_registry.end() ||
           m_pipelineRegistry.find(name) != m_pipelineRegistry.end();
}

std::vector<std::string> CommandParser::getRegisteredCommands() const
{
    std::vector<std::string> commands;
    commands.reserve(m_registry.size() + m_pipelineRegistry.size());
    for (const auto& pair : m_registry) {
        commands.push_back(pair.first);
    }
    for (const auto& pair : m_pipelineRegistry) {
        const std::string& name = pair.first;
        if (m_registry.find(name) == m_registry.end()) {
            commands.push_back(name);
        }
    }
    return commands;
}
