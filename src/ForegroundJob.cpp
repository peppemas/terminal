#include "ForegroundJob.hpp"

#include <vector>

bool ForegroundJob::start(const std::wstring& commandLine)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};

    std::vector<wchar_t> buf(commandLine.begin(), commandLine.end());
    buf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, buf.data(), nullptr, nullptr, FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr, nullptr, &si, &pi);

    if (!ok) {
        return false;
    }

    hProcess = pi.hProcess;
    groupId  = pi.dwProcessId;
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
