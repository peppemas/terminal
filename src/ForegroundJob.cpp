#include "ForegroundJob.hpp"

#include <vector>

bool ForegroundJob::start(const std::wstring& commandLine)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
    buf.push_back(L'\0');

    // No CREATE_NEW_PROCESS_GROUP: a process created in its own group starts
    // with Ctrl+C handling disabled (SetConsoleCtrlHandler(NULL, TRUE)
    // semantics), so commands like ping would ignore CTRL_C_EVENT entirely.
    // The child stays in our group; signals target the whole console
    // (group 0) and the shell protects itself with its own ctrl handler.
    BOOL ok = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, FALSE,
        0,
        nullptr, nullptr, &si, &pi);

    if (!ok) {
        return false;
    }

    hProcess = pi.hProcess;
    groupId  = 0;
    active   = true;
    CloseHandle(pi.hThread);
    return true;
}

bool ForegroundJob::interrupt() const
{
    if (!active) {
        return false;
    }
    return GenerateConsoleCtrlEvent(CTRL_C_EVENT, groupId) != 0;
}

bool ForegroundJob::interruptBreak() const
{
    if (!active) {
        return false;
    }
    return GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, groupId) != 0;
}

bool ForegroundJob::terminate(DWORD exitCode) const
{
    if (!active || hProcess == INVALID_HANDLE_VALUE) {
        return false;
    }
    return ::TerminateProcess(hProcess, exitCode) != 0;
}

bool ForegroundJob::wait(DWORD timeoutMs, DWORD* outExitCode) const
{
    if (!active || hProcess == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD waitResult = WaitForSingleObject(hProcess, timeoutMs);
    if (waitResult != WAIT_OBJECT_0) {
        return false;
    }

    if (outExitCode) {
        DWORD code = 0;
        if (!GetExitCodeProcess(hProcess, &code)) {
            return false;
        }
        *outExitCode = code;
    }

    return true;
}

void ForegroundJob::reset()
{
    if (hProcess != INVALID_HANDLE_VALUE) {
        CloseHandle(hProcess);
    }
    hProcess = INVALID_HANDLE_VALUE;
    groupId  = 0;
    active   = false;
}
