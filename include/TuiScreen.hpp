#pragma once

#include <windows.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>

#include <functional>

// Helper class that saves/restores the Windows console state around an FTXUI
// fullscreen session. The shell runs the raw-input REPL; FTXUI needs cooked
// input and the alternate screen buffer. TuiScreen handles both transitions.
class TuiScreen {
public:
    TuiScreen(HANDLE hConsole, HANDLE hInput, DWORD shellInputMode);

    // Restore the shell's console state. Safe to call multiple times.
    void enterScreen();
    void exitScreen();

    // Full helper that creates its own fullscreen screen. If `shouldExit` is
    // provided and returns true for an event, the loop is terminated.
    void run(ftxui::Component component,
             std::function<bool(const ftxui::Event&)> shouldExit = {});

private:
    HANDLE m_hConsole;
    HANDLE m_hInput;
    DWORD  m_shellInputMode;
    UINT   m_originalCP;
    UINT   m_originalOutputCP;
};
