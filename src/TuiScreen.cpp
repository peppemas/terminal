#include "TuiScreen.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <functional>
#include <iostream>

TuiScreen::TuiScreen(HANDLE hConsole, HANDLE hInput, DWORD shellInputMode)
    : m_hConsole{hConsole}
    , m_hInput{hInput}
    , m_shellInputMode{shellInputMode}
    , m_originalCP{GetConsoleCP()}
    , m_originalOutputCP{GetConsoleOutputCP()}
{
}

void TuiScreen::enterScreen()
{
    // Give the standard streams a clean state before FTXUI takes over.
    std::cout.flush();
    std::cerr.flush();

    // Restore cooked input (line + echo + processed) so FTXUI can read key
    // events through its own input path using regular C++ streams.
    if (m_hInput != INVALID_HANDLE_VALUE) {
        DWORD cookedMode = m_shellInputMode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;
        SetConsoleMode(m_hInput, cookedMode);
    }

    // FTXUI's ScreenInteractive will enter the alternate screen buffer itself.
    // We only need to make sure the output codepage is UTF-8 so box drawing
    // characters and symbols are interpreted correctly.
    SetConsoleOutputCP(CP_UTF8);
}

void TuiScreen::exitScreen()
{
    // Restore the shell's preferred codepages.
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // Re-enable the shell's raw input mode.
    if (m_hInput != INVALID_HANDLE_VALUE) {
        SetConsoleMode(m_hInput, m_shellInputMode);
    }

    // Clear any keystrokes that FTXUI may have left unread and force a full
    // repaint of the shell's current line.
    FlushConsoleInputBuffer(m_hInput);
}

void TuiScreen::run(ftxui::Component component,
                    std::function<bool(const ftxui::Event&)> shouldExit)
{
    enterScreen();

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    if (shouldExit) {
        component |= ftxui::CatchEvent(
            [&screen, shouldExit](const ftxui::Event& event) {
                if (shouldExit(event)) {
                    screen.ExitLoopClosure()();
                    return true;
                }
                return false;
            });
    }
    screen.Loop(component);
    // When Loop() returns, FTXUI has already left the alternate screen. We just
    // need to restore the console state the shell REPL expects.

    exitScreen();
}
