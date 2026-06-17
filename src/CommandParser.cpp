#include "CommandParser.hpp"

#include "CommandResolver.hpp"
#include "ExternalExecutor.hpp"
#include "GlobExpander.hpp"
#include "ShellTokenizer.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <windows.h>

void CommandParser::registerCommand(const std::string& name, Handler handler)
{
    m_registry[name] = std::move(handler);
}

bool CommandParser::executePipeline(const std::string& line, std::istream& in, std::ostream& out) const
{
    if (line.empty()) {
        return true;
    }

    auto tokenResult = shell::tokenizePipeline(line);
    if (tokenResult.error) {
        std::cerr << "syntax error: " << *tokenResult.error << '\n';
        return false;
    }
    std::vector<std::vector<std::string>> stages;
    stages.reserve(tokenResult.stages.size());
    for (const auto& ts : tokenResult.stages) {
        stages.push_back(GlobExpander::expand(ts));
    }
    if (stages.empty()) {
        return true;
    }

    // Classify each stage: is it a builtin or an external command?
    enum class StageType { Builtin, External };
    struct StageInfo {
        StageType type;
        std::optional<CommandResolver::ResolutionResult> resolved;
    };
    std::vector<StageInfo> stageInfos;
    stageInfos.reserve(stages.size());

    for (std::size_t s = 0; s < stages.size(); ++s) {
        const auto& argv = stages[s];
        if (argv.empty()) {
            std::cerr << "syntax error: empty pipeline stage\n";
            return false;
        }
        const std::string& cmd = argv[0];

        if (m_registry.find(cmd) != m_registry.end()) {
            stageInfos.push_back({StageType::Builtin, std::nullopt});
        } else {
            auto resolved = CommandResolver::resolve(cmd, argv);
            if (!resolved) {
                std::cerr << "command not found: " << cmd << '\n';
                return false;
            }
            stageInfos.push_back({StageType::External, std::move(resolved)});
        }
    }

    // Check if all stages are external - if so, use concurrent piped execution
    bool allExternal = true;
    for (const auto& info : stageInfos) {
        if (info.type != StageType::External) {
            allExternal = false;
            break;
        }
    }

    // If there are only external stages, launch them all concurrently with pipes
    if (allExternal && stages.size() > 1) {
        // Create pipes between stages and launch all processes concurrently
        struct PipeHandles {
            HANDLE hRead = NULL;
            HANDLE hWrite = NULL;
        };

        // We need (stages.size() - 1) pipes connecting consecutive stages
        std::vector<PipeHandles> pipes(stages.size() - 1);
        std::vector<HANDLE> processHandles;
        processHandles.reserve(stages.size());

        // Create all pipes
        for (std::size_t i = 0; i < pipes.size(); ++i) {
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = FALSE; // We'll selectively mark inheritable below

            if (!CreatePipe(&pipes[i].hRead, &pipes[i].hWrite, &sa, 0)) {
                DWORD err = GetLastError();
                std::cerr << "failed to create pipe: Windows error " << err << '\n';
                // Clean up any already created pipes
                for (std::size_t j = 0; j < i; ++j) {
                    CloseHandle(pipes[j].hRead);
                    CloseHandle(pipes[j].hWrite);
                }
                return false;
            }

            // Mark the handles as inheritable so child processes can use them
            SetHandleInformation(pipes[i].hRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            SetHandleInformation(pipes[i].hWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        }

        // Launch all processes concurrently
        bool launchFailed = false;
        for (std::size_t s = 0; s < stages.size(); ++s) {
            // stdin for this stage: read end of previous pipe (NULL for first stage)
            HANDLE hIn = (s > 0) ? pipes[s - 1].hRead : NULL;
            // stdout for this stage: write end of current pipe (NULL for last stage)
            HANDLE hOut = (s < stages.size() - 1) ? pipes[s].hWrite : NULL;

            HANDLE hProcess = INVALID_HANDLE_VALUE;
            auto pipedResult = ExternalExecutor::runPiped(
                *stageInfos[s].resolved, hIn, hOut, &hProcess);

            if (!pipedResult.started) {
                std::cerr << pipedResult.errorMessage << '\n';
                launchFailed = true;
                break;
            }

            processHandles.push_back(hProcess);

            // Close the parent-side handles that this stage uses to prevent deadlocks.
            // After CreateProcess, the child has inherited these handles, so we close ours.
            if (hIn != NULL) {
                CloseHandle(hIn);
                pipes[s - 1].hRead = NULL;  // Mark as closed
            }
            if (hOut != NULL) {
                CloseHandle(hOut);
                pipes[s].hWrite = NULL;  // Mark as closed
            }
        }

        // If launch failed, clean up remaining pipe handles and processes
        if (launchFailed) {
            for (auto& p : pipes) {
                if (p.hRead) CloseHandle(p.hRead);
                if (p.hWrite) CloseHandle(p.hWrite);
            }
            for (auto h : processHandles) {
                if (h != INVALID_HANDLE_VALUE) {
                    TerminateProcess(h, 1);
                    CloseHandle(h);
                }
            }
            return false;
        }

        // Wait for all processes to complete
        if (!processHandles.empty()) {
            WaitForMultipleObjects(
                static_cast<DWORD>(processHandles.size()),
                processHandles.data(),
                TRUE,   // Wait for all
                INFINITE);
        }

        // Clean up process handles
        for (auto h : processHandles) {
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
            }
        }

        // Clean up any remaining pipe handles (shouldn't be any if all went well)
        for (auto& p : pipes) {
            if (p.hRead) CloseHandle(p.hRead);
            if (p.hWrite) CloseHandle(p.hWrite);
        }

        return true;
    }

    // If we have a single external command, or a mix of builtins and externals,
    // use the original sequential approach (builtins use stringstreams,
    // single external commands use piped execution)
    std::string previousOutput;
    bool hasPrevious = false;

    for (std::size_t s = 0; s < stages.size(); ++s) {
        const auto& argv = stages[s];
        const std::string& cmd = argv[0];

        std::istringstream prevStream(previousOutput);
        std::istream* stageIn = hasPrevious ? static_cast<std::istream*>(&prevStream) : &in;

        std::ostringstream nextStream;
        std::ostream* stageOut = (s + 1 == stages.size()) ? &out : static_cast<std::ostream*>(&nextStream);

        if (stageInfos[s].type == StageType::Builtin) {
            auto it = m_registry.find(cmd);
            it->second(argv, *stageOut, *stageIn, std::cerr);
        } else {
            // External command in a mixed or single-stage pipeline
            // For a single external command, just run it normally
            if (stages.size() == 1) {
                int exitCode = 0;
                if (!ExternalExecutor::run(*stageInfos[s].resolved, &exitCode)) {
                    std::cerr << "failed to start: " << cmd << '\n';
                    return false;
                }
            } else {
                // Mixed pipeline: external command in a pipeline with builtins
                // Use piped execution for stdin/stdout redirection
                HANDLE hIn = NULL;
                HANDLE hOut = NULL;
                HANDLE hInRead = NULL;
                HANDLE hOutRead = NULL;

                SECURITY_ATTRIBUTES sa = {};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = FALSE;

                // If this stage has previous output, write it to a pipe for the process's stdin
                if (hasPrevious && !previousOutput.empty()) {
                    HANDLE hWrite = NULL;
                    if (!CreatePipe(&hInRead, &hWrite, &sa, 0)) {
                        std::cerr << "failed to create input pipe for: " << cmd << '\n';
                        return false;
                    }
                    SetHandleInformation(hInRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

                    // Write previous output to the pipe
                    DWORD written = 0;
                    WriteFile(hWrite, previousOutput.data(),
                              static_cast<DWORD>(previousOutput.size()), &written, nullptr);
                    CloseHandle(hWrite);  // Close write end so child sees EOF
                    hIn = hInRead;
                }

                // If this is not the last stage, capture stdout via a pipe
                HANDLE hOutWrite = NULL;
                if (s + 1 != stages.size()) {
                    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
                        std::cerr << "failed to create output pipe for: " << cmd << '\n';
                        if (hInRead) CloseHandle(hInRead);
                        return false;
                    }
                    SetHandleInformation(hOutWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                    hOut = hOutWrite;
                }

                HANDLE hProcess = INVALID_HANDLE_VALUE;
                auto pipedResult = ExternalExecutor::runPiped(
                    *stageInfos[s].resolved, hIn, hOut, &hProcess);

                // Close parent-side handles after CreateProcess
                if (hInRead) CloseHandle(hInRead);
                if (hOutWrite) CloseHandle(hOutWrite);

                if (!pipedResult.started) {
                    std::cerr << pipedResult.errorMessage << '\n';
                    if (hOutRead) CloseHandle(hOutRead);
                    return false;
                }

                // Wait for the process
                WaitForSingleObject(hProcess, INFINITE);
                DWORD exitCode = 0;
                GetExitCodeProcess(hProcess, &exitCode);
                CloseHandle(hProcess);

                // Read captured output if not the last stage
                if (hOutRead) {
                    std::string captured;
                    char buf[4096];
                    DWORD bytesRead = 0;
                    while (ReadFile(hOutRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
                        captured.append(buf, bytesRead);
                    }
                    CloseHandle(hOutRead);
                    previousOutput = std::move(captured);
                    hasPrevious = true;
                    continue;
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

bool CommandParser::executeParsed(const std::vector<std::vector<std::string>>& stages) const
{
    return executeParsed(stages, std::cin, std::cout);
}

bool CommandParser::executeParsed(const std::vector<std::vector<std::string>>& stages, std::istream& in, std::ostream& out) const
{
    if (stages.empty()) {
        return true;
    }

    // Classify each stage: is it a builtin or an external command?
    enum class StageType { Builtin, External };
    struct StageInfo {
        StageType type;
        std::optional<CommandResolver::ResolutionResult> resolved;
    };
    std::vector<StageInfo> stageInfos;
    stageInfos.reserve(stages.size());

    for (std::size_t s = 0; s < stages.size(); ++s) {
        const auto& argv = stages[s];
        if (argv.empty()) {
            std::cerr << "syntax error: empty pipeline stage\n";
            return false;
        }
        const std::string& cmd = argv[0];

        if (m_registry.find(cmd) != m_registry.end()) {
            stageInfos.push_back({StageType::Builtin, std::nullopt});
        } else {
            auto resolved = CommandResolver::resolve(cmd, argv);
            if (!resolved) {
                std::cerr << "command not found: " << cmd << '\n';
                return false;
            }
            stageInfos.push_back({StageType::External, std::move(resolved)});
        }
    }

    // Check if all stages are external - if so, use concurrent piped execution
    bool allExternal = true;
    for (const auto& info : stageInfos) {
        if (info.type != StageType::External) {
            allExternal = false;
            break;
        }
    }

    // If there are only external stages, launch them all concurrently with pipes
    if (allExternal && stages.size() > 1) {
        struct PipeHandles {
            HANDLE hRead = NULL;
            HANDLE hWrite = NULL;
        };

        std::vector<PipeHandles> pipes(stages.size() - 1);
        std::vector<HANDLE> processHandles;
        processHandles.reserve(stages.size());

        for (std::size_t i = 0; i < pipes.size(); ++i) {
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = FALSE;

            if (!CreatePipe(&pipes[i].hRead, &pipes[i].hWrite, &sa, 0)) {
                DWORD err = GetLastError();
                std::cerr << "failed to create pipe: Windows error " << err << '\n';
                for (std::size_t j = 0; j < i; ++j) {
                    CloseHandle(pipes[j].hRead);
                    CloseHandle(pipes[j].hWrite);
                }
                return false;
            }

            SetHandleInformation(pipes[i].hRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
            SetHandleInformation(pipes[i].hWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        }

        bool launchFailed = false;
        for (std::size_t s = 0; s < stages.size(); ++s) {
            HANDLE hIn = (s > 0) ? pipes[s - 1].hRead : NULL;
            HANDLE hOut = (s < stages.size() - 1) ? pipes[s].hWrite : NULL;

            HANDLE hProcess = INVALID_HANDLE_VALUE;
            auto pipedResult = ExternalExecutor::runPiped(
                *stageInfos[s].resolved, hIn, hOut, &hProcess);

            if (!pipedResult.started) {
                std::cerr << pipedResult.errorMessage << '\n';
                launchFailed = true;
                break;
            }

            processHandles.push_back(hProcess);

            if (hIn != NULL) {
                CloseHandle(hIn);
                pipes[s - 1].hRead = NULL;
            }
            if (hOut != NULL) {
                CloseHandle(hOut);
                pipes[s].hWrite = NULL;
            }
        }

        if (launchFailed) {
            for (auto& p : pipes) {
                if (p.hRead) CloseHandle(p.hRead);
                if (p.hWrite) CloseHandle(p.hWrite);
            }
            for (auto h : processHandles) {
                if (h != INVALID_HANDLE_VALUE) {
                    TerminateProcess(h, 1);
                    CloseHandle(h);
                }
            }
            return false;
        }

        if (!processHandles.empty()) {
            WaitForMultipleObjects(
                static_cast<DWORD>(processHandles.size()),
                processHandles.data(),
                TRUE,
                INFINITE);
        }

        for (auto h : processHandles) {
            if (h != INVALID_HANDLE_VALUE) {
                CloseHandle(h);
            }
        }

        for (auto& p : pipes) {
            if (p.hRead) CloseHandle(p.hRead);
            if (p.hWrite) CloseHandle(p.hWrite);
        }

        return true;
    }

    // Sequential execution for mixed builtin/external pipelines or single commands
    std::string previousOutput;
    bool hasPrevious = false;

    for (std::size_t s = 0; s < stages.size(); ++s) {
        const auto& argv = stages[s];
        const std::string& cmd = argv[0];

        std::istringstream prevStream(previousOutput);
        std::istream* stageIn = hasPrevious ? static_cast<std::istream*>(&prevStream) : &in;

        std::ostringstream nextStream;
        std::ostream* stageOut = (s + 1 == stages.size()) ? &out : static_cast<std::ostream*>(&nextStream);

        if (stageInfos[s].type == StageType::Builtin) {
            auto it = m_registry.find(cmd);
            it->second(argv, *stageOut, *stageIn, std::cerr);
        } else {
            // External command in a mixed or single-stage pipeline
            if (stages.size() == 1) {
                int exitCode = 0;
                if (!ExternalExecutor::run(*stageInfos[s].resolved, &exitCode)) {
                    std::cerr << "failed to start: " << cmd << '\n';
                    return false;
                }
            } else {
                HANDLE hIn = NULL;
                HANDLE hOut = NULL;
                HANDLE hInRead = NULL;
                HANDLE hOutRead = NULL;

                SECURITY_ATTRIBUTES sa = {};
                sa.nLength = sizeof(sa);
                sa.bInheritHandle = FALSE;

                if (hasPrevious && !previousOutput.empty()) {
                    HANDLE hWrite = NULL;
                    if (!CreatePipe(&hInRead, &hWrite, &sa, 0)) {
                        std::cerr << "failed to create input pipe for: " << cmd << '\n';
                        return false;
                    }
                    SetHandleInformation(hInRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

                    DWORD written = 0;
                    WriteFile(hWrite, previousOutput.data(),
                              static_cast<DWORD>(previousOutput.size()), &written, nullptr);
                    CloseHandle(hWrite);
                    hIn = hInRead;
                }

                HANDLE hOutWrite = NULL;
                if (s + 1 != stages.size()) {
                    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
                        std::cerr << "failed to create output pipe for: " << cmd << '\n';
                        if (hInRead) CloseHandle(hInRead);
                        return false;
                    }
                    SetHandleInformation(hOutWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                    hOut = hOutWrite;
                }

                HANDLE hProcess = INVALID_HANDLE_VALUE;
                auto pipedResult = ExternalExecutor::runPiped(
                    *stageInfos[s].resolved, hIn, hOut, &hProcess);

                if (hInRead) CloseHandle(hInRead);
                if (hOutWrite) CloseHandle(hOutWrite);

                if (!pipedResult.started) {
                    std::cerr << pipedResult.errorMessage << '\n';
                    if (hOutRead) CloseHandle(hOutRead);
                    return false;
                }

                WaitForSingleObject(hProcess, INFINITE);
                DWORD exitCode = 0;
                GetExitCodeProcess(hProcess, &exitCode);
                CloseHandle(hProcess);

                if (hOutRead) {
                    std::string captured;
                    char buf[4096];
                    DWORD bytesRead = 0;
                    while (ReadFile(hOutRead, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
                        captured.append(buf, bytesRead);
                    }
                    CloseHandle(hOutRead);
                    previousOutput = std::move(captured);
                    hasPrevious = true;
                    continue;
                }
            }
        }

        previousOutput = nextStream.str();
        hasPrevious = true;
    }

    return true;
}

CommandParser::CommandDispatch CommandParser::dispatch(const std::string& line) const
{
    CommandDispatch d;
    if (line.empty()) {
        d.action = CommandDispatch::Action::None;
        return d;
    }

    auto tokenResult = shell::tokenizePipeline(line);
    if (tokenResult.error) {
        d.action = CommandDispatch::Action::Failed;
        d.message = *tokenResult.error;
        return d;
    }
    std::vector<std::vector<std::string>> stages;
    stages.reserve(tokenResult.stages.size());
    for (const auto& ts : tokenResult.stages) {
        stages.push_back(GlobExpander::expand(ts));
    }
    if (stages.empty()) {
        d.action = CommandDispatch::Action::None;
        return d;
    }

    // Store parsed stages for reuse in executeParsed()
    d.parsedStages = stages;

    // Only handle single-stage commands here. Multi-stage pipelines
    // must still go through executeParsed() (which handles them directly).
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
    if (m_registry.count(cmd)) {
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
    return m_registry.find(name) != m_registry.end();
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
