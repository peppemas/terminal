#pragma once

#ifndef TERMINAL_TERMINAL_HPP
#define TERMINAL_TERMINAL_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "CommandParser.hpp"
#include "Config.hpp"
#include "ForegroundJob.hpp"

class Terminal {
public:
    Terminal();
    ~Terminal();

    void run();

private:
    std::string getPromptString() const;
    void printPrompt() const;

    bool setupRawInput();
    void restoreRawInput();
    std::string readLineRaw();
    void refreshLine();
    void handleTab();
    void replaceToken(size_t start, const std::string& oldToken, const std::string& replacement);
    std::vector<std::string> getCommandCandidates(const std::string& prefix) const;
    std::vector<std::string> getPathCandidates(const std::string& token) const;
    std::string longestCommonPrefix(const std::vector<std::string>& items) const;
    void displayCandidates(const std::vector<std::string>& candidates);

    // --- History & word navigation helpers ---
    void addToHistory(const std::string& line);
    void recallHistory(int direction);
    void loadHistory();
    void saveHistory();
    void moveCursorToPrevSpace();
    void moveCursorToNextSpace();

    // --- Line processing (shared between raw/cooked loops) ---
    void processLine(const std::string& line);

    // --- External command execution ---
    int runExternalCommand(const CommandResolver::ResolutionResult& resolved);

    bool m_vtEnabled{false};
    HANDLE m_hConsole{INVALID_HANDLE_VALUE};
    DWORD m_originalMode{0};
    UINT m_originalCP{0};
    UINT m_originalOutputCP{0};

    CommandParser m_parser;

    std::string m_inputBuffer;
    size_t m_cursorPos{0};
    std::string m_lastTabToken;
    bool m_lastWasTab{false};

    // --- History & word navigation ---
    static constexpr char kCtrlCSentinel = '\x03';

    ForegroundJob m_fgJob;

    std::vector<std::string> m_history;
    size_t m_historyIndex{0};
    std::string m_scratchBuffer;
    bool m_inHistoryRecall{false};

    HANDLE m_hInput{INVALID_HANDLE_VALUE};
    DWORD m_originalInputMode{0};

    // --- Prompt caching ---
    mutable std::string m_cachedPrompt;
    mutable bool m_promptDirty{true};
};

#endif // TERMINAL_TERMINAL_HPP
