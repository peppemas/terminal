#include "Terminal.hpp"
#include "Commands.hpp"

#include <iostream>
#include <filesystem>

Terminal::Terminal()
    : m_hConsole{INVALID_HANDLE_VALUE}, m_originalMode{0}, m_vtEnabled{false}
{
    m_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (m_hConsole == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!GetConsoleMode(m_hConsole, &m_originalMode)) {
        return;
    }

    DWORD newMode = m_originalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(m_hConsole, newMode)) {
        m_vtEnabled = true;
    }

    // Register all command handlers
    m_parser.registerCommand("ls",   commands::ls);
    m_parser.registerCommand("rm",   commands::rm);
    m_parser.registerCommand("cp",   commands::cp);
    m_parser.registerCommand("mv",   commands::mv);
    m_parser.registerCommand("cat",  commands::cat);
    m_parser.registerCommand("tail", commands::tail);
    m_parser.registerCommand("grep", commands::grep);
    m_parser.registerCommand("cd",   commands::cd);
    m_parser.registerCommand("clear", commands::clear);
    m_parser.registerCommand("cls",   commands::clear);
}

Terminal::~Terminal()
{
    restoreRawInput();
    if (m_hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleMode(m_hConsole, m_originalMode);
    }
}

void Terminal::printPrompt() const
{
    const std::string path = std::filesystem::current_path().string();

    if (m_vtEnabled) {
        std::cout << commands::BOLD_BLUE << path << commands::RESET << "> ";
    } else {
        std::cout << path << "> ";
    }
}

bool Terminal::setupRawInput()
{
    m_hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (m_hInput == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!GetConsoleMode(m_hInput, &m_originalInputMode)) {
        return false;
    }

    DWORD newMode = m_originalInputMode;
    newMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    newMode |= ENABLE_PROCESSED_INPUT;

    if (!SetConsoleMode(m_hInput, newMode)) {
        return false;
    }

    return true;
}

void Terminal::restoreRawInput()
{
    if (m_hInput != INVALID_HANDLE_VALUE) {
        SetConsoleMode(m_hInput, m_originalInputMode);
    }
}

void Terminal::refreshLine() const
{
    std::cout << "\r\x1b[K";
    printPrompt();
    std::cout << m_inputBuffer;
    std::cout.flush();
}

std::string Terminal::readLineRaw()
{
    m_inputBuffer.clear();
    m_cursorPos = 0;
    m_lastWasTab = false;

    INPUT_RECORD record;
    DWORD count = 0;

    while (true) {
        if (!ReadConsoleInput(m_hInput, &record, 1, &count)) {
            std::cout << '\n';
            return m_inputBuffer;
        }

        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue;
        }

        WORD vk = record.Event.KeyEvent.wVirtualKeyCode;
        CHAR ch = record.Event.KeyEvent.uChar.AsciiChar;

        if (vk == VK_RETURN) {
            std::cout << '\n';
            return m_inputBuffer;
        }

        if (vk == VK_BACK) {
            if (!m_inputBuffer.empty()) {
                m_inputBuffer.pop_back();
                --m_cursorPos;
                m_lastWasTab = false;
                refreshLine();
            }
            continue;
        }

        if (vk == VK_TAB) {
            handleTab();
            continue;
        }

        if (ch >= 32 && ch <= 126) {
            m_inputBuffer.push_back(ch);
            ++m_cursorPos;
            m_lastWasTab = false;
            refreshLine();
        }
    }
}

void Terminal::handleTab()
{
    size_t tokenStart = 0;
    if (m_cursorPos > 0) {
        size_t delim = m_inputBuffer.find_last_of(" \t", m_cursorPos - 1);
        if (delim != std::string::npos) {
            tokenStart = delim + 1;
        }
    }

    std::string token = m_inputBuffer.substr(tokenStart, m_cursorPos - tokenStart);
    bool isFirstToken = (tokenStart == 0);

    std::vector<std::string> candidates;
    if (isFirstToken) {
        candidates = getCommandCandidates(token);
    } else {
        candidates = getPathCandidates(token);
    }

    if (candidates.empty()) {
        return;
    }

    if (candidates.size() == 1) {
        replaceToken(tokenStart, token, candidates[0]);
        m_lastWasTab = false;
        return;
    }

    std::string lcp = longestCommonPrefix(candidates);
    if (lcp.length() > token.length()) {
        replaceToken(tokenStart, token, lcp);
        m_lastWasTab = true;
        m_lastTabToken = token;
    } else {
        if (m_lastWasTab && m_lastTabToken == token) {
            displayCandidates(candidates);
        }
        m_lastWasTab = true;
        m_lastTabToken = token;
    }
}

void Terminal::replaceToken(size_t start, const std::string& oldToken, const std::string& replacement)
{
    m_inputBuffer.erase(start, oldToken.length());
    m_inputBuffer.insert(start, replacement);
    m_cursorPos = start + replacement.length();
    refreshLine();
}

std::vector<std::string> Terminal::getCommandCandidates(const std::string& prefix) const
{
    std::vector<std::string> all = m_parser.getRegisteredCommands();
    std::vector<std::string> filtered;
    for (const auto& name : all) {
        if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
            filtered.push_back(name);
        }
    }
    return filtered;
}

std::vector<std::string> Terminal::getPathCandidates(const std::string& token) const
{
    namespace fs = std::filesystem;
    fs::path p(token);
    fs::path dir = p.parent_path();
    std::string prefix = p.filename().string();
    if (dir.empty()) {
        dir = ".";
    }

    std::vector<std::string> candidates;
    try {
        for (const auto& entry : fs::directory_iterator(dir)) {
            std::string fname = entry.path().filename().string();
            if (fname.size() >= prefix.size() && fname.compare(0, prefix.size(), prefix) == 0) {
                if (entry.is_directory()) {
                    fname.push_back(static_cast<char>(fs::path::preferred_separator));
                }
                if (!p.parent_path().empty()) {
                    fname = (p.parent_path() / fname).string();
                }
                candidates.push_back(fname);
            }
        }
    } catch (const fs::filesystem_error&) {
        // return empty list
    }
    return candidates;
}

std::string Terminal::longestCommonPrefix(const std::vector<std::string>& items) const
{
    if (items.empty()) {
        return "";
    }
    const std::string& first = items[0];
    for (size_t i = 0; i < first.size(); ++i) {
        for (const auto& s : items) {
            if (i >= s.size() || s[i] != first[i]) {
                return first.substr(0, i);
            }
        }
    }
    return first;
}

void Terminal::displayCandidates(const std::vector<std::string>& candidates) const
{
    std::cout << '\n';
    for (const auto& c : candidates) {
        std::cout << c << '\n';
    }
    std::cout << '\n';
    printPrompt();
    std::cout << m_inputBuffer;
    std::cout.flush();
}

void Terminal::run()
{
    bool raw = setupRawInput();
    if (raw) {
        while (true) {
            printPrompt();
            std::string line = readLineRaw();

            if (line == "exit" || line == "quit") {
                break;
            }

            try {
                m_parser.execute(line);
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << '\n';
            }
        }
        restoreRawInput();
    } else {
        while (true) {
            printPrompt();
            std::string line;
            if (!std::getline(std::cin, line)) {
                // EOF reached
                std::cout << '\n';
                break;
            }

            if (line == "exit" || line == "quit") {
                break;
            }

            try {
                m_parser.execute(line);
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << '\n';
            }
        }
    }
}
