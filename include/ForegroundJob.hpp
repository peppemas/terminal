#pragma once

#ifndef TERMINAL_FOREGROUNDJOB_HPP
#define TERMINAL_FOREGROUNDJOB_HPP

#include <windows.h>
#include <string>

struct ForegroundJob {
    HANDLE hProcess{INVALID_HANDLE_VALUE};
    DWORD  groupId{0};   // always 0: signal the whole console; the shell's ctrl handler swallows it
    bool   active{false};

    bool start(const std::wstring& commandLine);
    bool interrupt() const;          // sends CTRL_C_EVENT to the console
    bool interruptBreak() const;     // sends CTRL_BREAK_EVENT to the console
    bool terminate(DWORD exitCode = 1) const;  // calls TerminateProcess
    void reset();
    bool isActive() const { return active; }
    bool wait(DWORD timeoutMs = INFINITE, DWORD* outExitCode = nullptr) const;
};

#endif // TERMINAL_FOREGROUNDJOB_HPP
