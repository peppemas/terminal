#pragma once

#ifndef TERMINAL_EXTERNALEXECUTOR_HPP
#define TERMINAL_EXTERNALEXECUTOR_HPP

#include "CommandResolver.hpp"

class ExternalExecutor {
public:
    // Run a resolved external command as a foreground process.
    // Blocks until the process exits, then retrieves its exit code.
    // Returns true on success; false if the process could not be started.
    // If outExitCode is non-null, writes the process exit code to it.
    static bool run(const CommandResolver::ResolutionResult& result,
                    int* outExitCode = nullptr);
};

#endif // TERMINAL_EXTERNALEXECUTOR_HPP
