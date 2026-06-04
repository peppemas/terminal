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

static std::string utf32ToUtf8(uint32_t cp)
{
    std::string result;
    if (cp <= 0x7F) {
        result.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        result.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        result.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        result.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return result;
}

static uint32_t decodeUtf8(const std::string& s, size_t pos, size_t& outBytes)
{
    if (pos >= s.size()) {
        outBytes = 0;
        return 0;
    }

    unsigned char c0 = static_cast<unsigned char>(s[pos]);

    if ((c0 & 0x80) == 0) {
        outBytes = 1;
        return c0;
    }

    size_t len = 0;
    if ((c0 & 0xE0) == 0xC0) {
        len = 2;
    } else if ((c0 & 0xF0) == 0xE0) {
        len = 3;
    } else if ((c0 & 0xF8) == 0xF0) {
        len = 4;
    } else {
        outBytes = 1;
        return 0xFFFD;
    }

    if (pos + len > s.size()) {
        outBytes = 1;
        return 0xFFFD;
    }

    for (size_t i = 1; i < len; ++i) {
        unsigned char ci = static_cast<unsigned char>(s[pos + i]);
        if ((ci & 0xC0) != 0x80) {
            outBytes = 1;
            return 0xFFFD;
        }
    }

    uint32_t cp = 0;
    if (len == 2) {
        cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[pos + 1]) & 0x3F);
        if (cp < 0x80) {
            outBytes = 1;
            return 0xFFFD;
        }
    } else if (len == 3) {
        cp = ((c0 & 0x0F) << 12) |
             ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(s[pos + 2]) & 0x3F);
        if (cp < 0x800) {
            outBytes = 1;
            return 0xFFFD;
        }
    } else if (len == 4) {
        cp = ((c0 & 0x07) << 18) |
             ((static_cast<unsigned char>(s[pos + 1]) & 0x3F) << 12) |
             ((static_cast<unsigned char>(s[pos + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(s[pos + 3]) & 0x3F);
        if (cp < 0x10000) {
            outBytes = 1;
            return 0xFFFD;
        }
    }

    outBytes = len;
    return cp;
}

static size_t prevUtf8Char(const std::string& s, size_t bytePos)
{
    if (bytePos == 0) {
        return 0;
    }
    size_t pos = bytePos - 1;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

static size_t nextUtf8Char(const std::string& s, size_t bytePos)
{
    if (bytePos >= s.size()) {
        return s.size();
    }
    size_t pos = bytePos + 1;
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) {
        ++pos;
    }
    return pos;
}

static int codepointWidth(uint32_t cp)
{
    if (cp < 0x20 || cp == 0x7F) {
        return 0;
    }

    if ((cp >= 0x1100 && cp <= 0x115F) ||
        (cp >= 0x2E80 && cp <= 0x9FFF) ||
        (cp >= 0xAC00 && cp <= 0xD7AF) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6)) {
        return 2;
    }

    if ((cp >= 0x20000 && cp <= 0x2FFFD) ||
        (cp >= 0x30000 && cp <= 0x3FFFD)) {
        return 2;
    }

    if ((cp >= 0x2600 && cp <= 0x26FF) ||
        (cp >= 0x2700 && cp <= 0x27BF) ||
        (cp >= 0x1F300 && cp <= 0x1F9FF)) {
        return 2;
    }

    return 1;
}

static size_t displayWidth(const std::string& s, size_t bytePos)
{
    size_t total = 0;
    size_t pos = 0;
    while (pos < bytePos && pos < s.size()) {
        size_t consumed = 0;
        uint32_t cp = decodeUtf8(s, pos, consumed);
        if (consumed == 0) break;
        total += static_cast<size_t>(codepointWidth(cp));
        pos += consumed;
    }
    return total;
}

static size_t displayWidth(const std::string& s)
{
    return displayWidth(s, s.size());
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

    m_originalCP = GetConsoleCP();
    m_originalOutputCP = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

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
    SetConsoleCP(m_originalCP);
    SetConsoleOutputCP(m_originalOutputCP);
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
        std::size_t cut = maxLen - 3;
        if (cut > 0 && (static_cast<unsigned char>(raw[cut]) & 0xC0) == 0x80) {
            cut = prevUtf8Char(raw, cut);
        }
        return raw.substr(0, cut) + "...";
    }

    std::size_t cut = budget;
    if (cut > 0 && (static_cast<unsigned char>(raw[cut]) & 0xC0) == 0x80) {
        cut = prevUtf8Char(raw, cut);
    }
    return raw.substr(0, cut) + "..." + leaf;
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
    size_t col = 1
               + displayWidth(prompt)
               + displayWidth(m_inputBuffer, m_cursorPos);
    std::cout << "\x1b[" << col << "G";
    std::cout.flush();
}

std::string Terminal::readLineRaw()
{
    m_inputBuffer.clear();
    m_cursorPos = 0;
    m_lastWasTab = false;

    INPUT_RECORD record;
    DWORD count = 0;
    WCHAR pendingHigh = 0;

    while (true) {
        if (!ReadConsoleInput(m_hInput, &record, 1, &count)) {
            std::cout << '\n';
            return m_inputBuffer;
        }

        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            continue;
        }

        WORD vk = record.Event.KeyEvent.wVirtualKeyCode;
        WCHAR wc = record.Event.KeyEvent.uChar.UnicodeChar;

        if (vk == VK_RETURN) {
            m_inHistoryRecall = false;
            m_historyIndex = 0;
            std::cout << '\n';
            return m_inputBuffer;
        }

        if (vk == VK_BACK) {
            if (!m_inputBuffer.empty()) {
                size_t prev = prevUtf8Char(m_inputBuffer, m_inputBuffer.size());
                m_inputBuffer.erase(prev);
                m_cursorPos = prev;
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

        if (vk == VK_LEFT) {
            if (m_cursorPos > 0) {
                m_cursorPos = prevUtf8Char(m_inputBuffer, m_cursorPos);
                refreshLine();
            }
            continue;
        }

        if (vk == VK_RIGHT) {
            if (m_cursorPos < m_inputBuffer.size()) {
                m_cursorPos = nextUtf8Char(m_inputBuffer, m_cursorPos);
                refreshLine();
            }
            continue;
        }

        if (wc >= 0xD800 && wc <= 0xDBFF) {
            pendingHigh = wc;
            continue;
        }

        if (wc >= 0xDC00 && wc <= 0xDFFF) {
            if (pendingHigh != 0) {
                uint32_t cp = 0x10000 + ((pendingHigh - 0xD800) << 10) + (wc - 0xDC00);
                std::string utf8 = utf32ToUtf8(cp);
                m_inputBuffer += utf8;
                m_cursorPos += utf8.length();
                m_lastWasTab = false;
                refreshLine();
            }
            pendingHigh = 0;
            continue;
        }

        pendingHigh = 0;

        if (wc != L'\0') {
            std::string utf8 = utf32ToUtf8(static_cast<uint32_t>(wc));
            m_inputBuffer += utf8;
            m_cursorPos += utf8.length();
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
