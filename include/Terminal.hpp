#pragma once

#ifndef TERMINAL_TERMINAL_HPP
#define TERMINAL_TERMINAL_HPP

#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#include "CommandParser.hpp"

class Terminal {
public:
    Terminal();
    ~Terminal();

    void run();

private:
    void printPrompt() const;

    bool setupRawInput();
    void restoreRawInput();
    std::string readLineRaw();
    void refreshLine() const;
    void handleTab();
    void replaceToken(size_t start, const std::string& oldToken, const std::string& replacement);
    std::vector<std::string> getCommandCandidates(const std::string& prefix) const;
    std::vector<std::string> getPathCandidates(const std::string& token) const;
    std::string longestCommonPrefix(const std::vector<std::string>& items) const;
    void displayCandidates(const std::vector<std::string>& candidates) const;

    bool m_vtEnabled{false};
    HANDLE m_hConsole{INVALID_HANDLE_VALUE};
    DWORD m_originalMode{0};

    CommandParser m_parser;

    std::string m_inputBuffer;
    size_t m_cursorPos{0};
    std::string m_lastTabToken;
    bool m_lastWasTab{false};
    HANDLE m_hInput{INVALID_HANDLE_VALUE};
    DWORD m_originalInputMode{0};
};

#endif // TERMINAL_TERMINAL_HPP
