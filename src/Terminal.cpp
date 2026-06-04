#include "Terminal.hpp"
#include "Commands.hpp"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT) {
        return TRUE; // swallow the signal so the shell never terminates
    }
    return FALSE;
}

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

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

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
    m_parser.registerCommand("pwd",   commands::pwd);
    m_parser.registerCommand("open",  commands::open);
}

Terminal::~Terminal()
{
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    restoreRawInput();
    if (m_hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleMode(m_hConsole, m_originalMode);
    }
}

static std::string formatPromptPath(const std::string& raw)
{
    constexpr std::size_t maxLen = 30;
    if (raw.length() <= maxLen) {
        return raw;
    }

    std::size_t sepPos = raw.find_last_of("\\/");
    if (sepPos == std::string::npos || sepPos == 0) {
        return raw;
    }

    std::string leaf = raw.substr(sepPos); // includes separator
    std::size_t budget = maxLen - 3 - leaf.length();
    if (budget <= 0) {
        return raw.substr(0, maxLen - 3) + "...";
    }

    return raw.substr(0, budget) + "..." + leaf;
}

std::string Terminal::printPrompt() const
{
    const std::string raw = std::filesystem::current_path().string();
    const std::string display = formatPromptPath(raw);

    if (m_vtEnabled) {
        std::cout << commands::BOLD_BLUE << display << commands::RESET << "> ";
    } else {
        std::cout << display << "> ";
    }
    return display + "> ";
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
    newMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

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
    const std::string prompt = printPrompt();
    std::cout << m_inputBuffer;
    std::cout << "\x1b[" << (prompt.length() + m_cursorPos + 1) << "G";
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
            m_inHistoryRecall = false;
            m_historyIndex = 0;
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

        bool ctrl = (record.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;

        if (vk == 'C' && ctrl) {
            std::cout << "^C\n";
            m_inputBuffer.clear();
            m_cursorPos = 0;
            return std::string(1, kCtrlCSentinel);
        }

        if (vk == VK_UP) {
            if (!m_history.empty()) {
                recallHistory(+1);
            }
            continue;
        }

        if (vk == VK_DOWN) {
            if (m_inHistoryRecall) {
                recallHistory(-1);
            }
            continue;
        }

        if (vk == VK_LEFT && ctrl) {
            moveCursorToPrevSpace();
            continue;
        }

        if (vk == VK_RIGHT && ctrl) {
            moveCursorToNextSpace();
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

void Terminal::addToHistory(const std::string& line)
{
    if (m_history.size() == MAX_HISTORY_SIZE) {
        m_history.erase(m_history.begin());
    }
    m_history.push_back(line);
}

void Terminal::recallHistory(int direction)
{
    if (direction == +1) {
        if (!m_inHistoryRecall) {
            m_scratchBuffer = m_inputBuffer;
            m_inHistoryRecall = true;
            m_historyIndex = 1;
        } else if (m_historyIndex < m_history.size()) {
            ++m_historyIndex;
        }
        m_inputBuffer = m_history[m_history.size() - m_historyIndex];
    } else if (direction == -1) {
        if (m_historyIndex > 1) {
            --m_historyIndex;
            m_inputBuffer = m_history[m_history.size() - m_historyIndex];
        } else {
            m_historyIndex = 0;
            m_inHistoryRecall = false;
            m_inputBuffer = m_scratchBuffer;
        }
    }
    m_cursorPos = m_inputBuffer.size();
    refreshLine();
}

void Terminal::moveCursorToPrevSpace()
{
    if (m_cursorPos == 0) { refreshLine(); return; }
    size_t i = m_cursorPos - 1;
    while (i > 0 && m_inputBuffer[i] == ' ') { --i; }
    while (i > 0 && m_inputBuffer[i] != ' ') { --i; }
    m_cursorPos = (m_inputBuffer[i] == ' ') ? i + 1 : 0;
    refreshLine();
}

void Terminal::moveCursorToNextSpace()
{
    if (m_cursorPos >= m_inputBuffer.size()) { refreshLine(); return; }
    size_t i = m_cursorPos;
    while (i < m_inputBuffer.size() && m_inputBuffer[i] != ' ') { ++i; }
    while (i < m_inputBuffer.size() && m_inputBuffer[i] == ' ') { ++i; }
    m_cursorPos = i;
    refreshLine();
}

void Terminal::waitForForegroundJob()
{
    while (m_fgJob.isActive()) {
        DWORD wait = WaitForSingleObject(m_fgJob.hProcess, 100);
        if (wait == WAIT_OBJECT_0) {
            m_fgJob.reset();
            break;
        }

        INPUT_RECORD rec;
        DWORD count = 0;
        if (!PeekConsoleInput(m_hInput, &rec, 1, &count) || count == 0) {
            continue;
        }

        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            WORD vk = rec.Event.KeyEvent.wVirtualKeyCode;
            bool ctrl = (rec.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            if (vk == 'C' && ctrl) {
                ReadConsoleInput(m_hInput, &rec, 1, &count);
                std::cout << "^C\n";
                m_fgJob.interrupt();
            }
        }
    }
}

void Terminal::run()
{
    bool raw = setupRawInput();
    if (raw) {
        while (true) {
            printPrompt();
            std::string line = readLineRaw();
            if (line == std::string(1, kCtrlCSentinel)) {
                continue;
            }
            if (!line.empty()) {
                addToHistory(line);
            }

            std::istringstream iss(line);
            std::string firstToken;
            if (!(iss >> firstToken)) {
                continue;
            }

            if (firstToken == "exit" || firstToken == "quit") {
                break;
            }

            if (m_parser.hasCommand(firstToken)) {
                try {
                    m_parser.execute(line);
                } catch (const std::exception& e) {
                    std::cerr << "error: " << e.what() << '\n';
                }
            } else {
                int wlen = MultiByteToWideChar(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0);
                if (wlen > 0) {
                    std::vector<wchar_t> wbuf(wlen + 1, L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), wbuf.data(), wlen);
                    std::wstring wline(wbuf.data());
                    if (m_fgJob.start(wline)) {
                        waitForForegroundJob();
                    } else {
                        std::cerr << "command not found: " << firstToken << '\n';
                    }
                } else {
                    std::cerr << "command not found: " << firstToken << '\n';
                }
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
