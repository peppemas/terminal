#pragma once

#ifndef TERMINAL_FOREGROUNDJOB_HPP
#define TERMINAL_FOREGROUNDJOB_HPP

#include <windows.h>
#include <string>

struct ForegroundJob {
    HANDLE hProcess{INVALID_HANDLE_VALUE};
    DWORD  groupId{0};
    bool   active{false};

    bool start(const std::wstring& commandLine);
    bool interrupt() const;
    void reset();
    bool isActive() const { return active; }
    bool wait(DWORD timeoutMs = INFINITE, DWORD* outExitCode = nullptr) const;
};

#endif // TERMINAL_FOREGROUNDJOB_HPP
