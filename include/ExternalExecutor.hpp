#pragma once

#ifndef TERMINAL_EXTERNALEXECUTOR_HPP
#define TERMINAL_EXTERNALEXECUTOR_HPP

#include "CommandResolver.hpp"
#include <windows.h>
#include <string>

class ExternalExecutor {
public:
    // Run a resolved external command as a foreground process.
    // Blocks until the process exits, then retrieves its exit code.
    // Returns true on success; false if the process could not be started.
    // If outExitCode is non-null, writes the process exit code to it.
    static bool run(const CommandResolver::ResolutionResult& result,
                    int* outExitCode = nullptr);

    // Result of a piped external command execution.
    struct PipedResult {
        int exitCode;
        bool started;
        std::string errorMessage;
    };

    // Run a resolved external command with redirected stdin/stdout for pipeline use.
    // hStdinRead:   read handle for the process's stdin (NULL for first stage - inherits console).
    // hStdoutWrite: write handle for the process's stdout (NULL for last stage - inherits console).
    // hProcess:     [out] receives the process handle for the caller to wait on.
    // Returns a PipedResult indicating success/failure.
    static PipedResult runPiped(const CommandResolver::ResolutionResult& resolved,
                                HANDLE hStdinRead,
                                HANDLE hStdoutWrite,
                                HANDLE* hProcess);
};

#endif // TERMINAL_EXTERNALEXECUTOR_HPP
