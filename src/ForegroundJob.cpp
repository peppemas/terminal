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

void ForegroundJob::reset()
{
    if (hProcess != INVALID_HANDLE_VALUE) {
        CloseHandle(hProcess);
    }
    hProcess = INVALID_HANDLE_VALUE;
    groupId  = 0;
    active   = false;
}
