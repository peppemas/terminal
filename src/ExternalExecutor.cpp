#include "ExternalExecutor.hpp"
#include "ForegroundJob.hpp"

#include <vector>
#include <sstream>

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

ExternalExecutor::PipedResult ExternalExecutor::runPiped(
    const CommandResolver::ResolutionResult& resolved,
    HANDLE hStdinRead,
    HANDLE hStdoutWrite,
    HANDLE* hProcess)
{
    PipedResult result{0, false, ""};

    if (hProcess) {
        *hProcess = INVALID_HANDLE_VALUE;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;

    // For stdin: use provided handle or inherit the console stdin
    si.hStdInput = hStdinRead ? hStdinRead : GetStdHandle(STD_INPUT_HANDLE);
    // For stdout: use provided handle or inherit the console stdout
    si.hStdOutput = hStdoutWrite ? hStdoutWrite : GetStdHandle(STD_OUTPUT_HANDLE);
    // Always inherit stderr to the console
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    PROCESS_INFORMATION pi = {};

    // Mutable copy of command line for CreateProcessW
    std::vector<wchar_t> cmdBuf(resolved.commandLine.begin(), resolved.commandLine.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,   // bInheritHandles must be TRUE for pipe redirection
        0,
        nullptr,
        nullptr,
        &si,
        &pi);

    if (!ok) {
        DWORD err = GetLastError();
        // Build a descriptive error message with the command name and error code
        std::string cmdName(resolved.executable.filename().string());
        std::ostringstream oss;
        oss << "failed to start '" << cmdName << "': Windows error " << err;
        result.errorMessage = oss.str();
        result.started = false;
        return result;
    }

    result.started = true;

    // Give process handle back to the caller for waiting
    if (hProcess) {
        *hProcess = pi.hProcess;
    } else {
        CloseHandle(pi.hProcess);
    }
    CloseHandle(pi.hThread);

    return result;
}
