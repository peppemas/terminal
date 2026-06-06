#include "ExternalExecutor.hpp"
#include "ForegroundJob.hpp"

bool ExternalExecutor::run(const CommandResolver::ResolutionResult& result,
                           int* outExitCode)
{
    ForegroundJob job;

    if (!job.start(result.commandLine)) {
        return false;
    }

    DWORD exitCode = 0;
    if (!job.wait(INFINITE, &exitCode)) {
        job.reset();
        return false;
    }

    job.reset();

    if (outExitCode) {
        *outExitCode = static_cast<int>(exitCode);
    }

    return true;
}
