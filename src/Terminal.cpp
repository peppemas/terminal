#include "Terminal.hpp"
#include "Commands.hpp"

#include <ftxui/component/screen_interactive.hpp>

#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <limits>
#include <atomic>
#include <cstdlib>

static void writeUtf8ToConsole(HANDLE hOut, const std::string& s)
{
    if (s.empty()) return;
    if (hOut == INVALID_HANDLE_VALUE || hOut == nullptr) {
        std::cout << s;
        return;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) {
        std::cout << s;
        return;
    }
    if (s.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) return;
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (wideLen <= 0) {
        std::cout << s;
        return;
    }
    std::wstring wide(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        &wide[0], wideLen);
    DWORD written = 0;
    if (!WriteConsoleW(hOut, wide.c_str(), static_cast<DWORD>(wide.size()), &written, nullptr)) {
        std::cout << s;
    }
}

// Set by the ctrl handler (which runs on its own thread) when the user
// presses Ctrl+C / Ctrl+Break while a foreground child is running.
static std::atomic<bool> g_interruptRequested{false};

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
        g_interruptRequested = true;
        return TRUE; // swallow the signal so the shell never terminates
    }
    return FALSE;
}

// Intentionally removed: utf32ToUtf8 was unused dead code (C4505)

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
    // VT mode is best-effort: stdout may be redirected to a pipe/file, in
    // which case we still must register the ctrl handler and the commands.
    m_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (m_hConsole != INVALID_HANDLE_VALUE && GetConsoleMode(m_hConsole, &m_originalMode)) {
        DWORD newMode = m_originalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(m_hConsole, newMode)) {
            m_vtEnabled = true;
        }
    }

    // Clear any inherited "ignore Ctrl+C" flag (a parent that called
    // SetConsoleCtrlHandler(NULL, TRUE) passes it on to us, and we would
    // pass it on to our children, leaving them immune to Ctrl+C).
    SetConsoleCtrlHandler(nullptr, FALSE);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    m_originalCP = GetConsoleCP();
    m_originalOutputCP = GetConsoleOutputCP();
    // Set the input code page to UTF-8. The Windows console host will then
    // encode non-ASCII key events as UTF-8 byte sequences, delivered as two
    // (or more) separate KEY_EVENT records with uChar.UnicodeChar set to
    // each byte value. readLineRaw() reassembles these byte pairs back into
    // proper UTF-8 characters before storing in m_inputBuffer.
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // Register all command handlers (unified signature: Args, ostream&, istream&)
    m_parser.registerCommand("ls",    commands::ls);
    m_parser.registerCommand("rm",    commands::rm);
    m_parser.registerCommand("cp",    commands::cp);
    m_parser.registerCommand("mv",    commands::mv);
    m_parser.registerCommand("cat",   commands::cat);
    m_parser.registerCommand("tail",  commands::tail);
    m_parser.registerCommand("grep",  commands::grep);
    m_parser.registerCommand("cd",    commands::cd);
    m_parser.registerCommand("clear", commands::clear);
    m_parser.registerCommand("cls",   commands::clear);
    m_parser.registerCommand("pwd",   commands::pwd);
    m_parser.registerCommand("open",  commands::open);
    m_parser.registerCommand("echo",  commands::echo);
    m_parser.registerCommand("push",  commands::push);
    m_parser.registerCommand("pop",   commands::pop);
    m_parser.registerCommand("slots", commands::slots);
    m_parser.registerCommand("more",  commands::more);
    m_parser.registerCommand("mkdir", commands::mkdir);
    m_parser.registerCommand("bc",    commands::bc);

    loadHistory();
}

Terminal::~Terminal()
{
    saveHistory();

    SetConsoleCP(m_originalCP);
    SetConsoleOutputCP(m_originalOutputCP);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE);
    restoreRawInput();
    if (m_vtEnabled) {
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

std::string Terminal::getPromptString() const
{
    if (!m_promptDirty) {
        return m_cachedPrompt;
    }
    const std::string raw = std::filesystem::current_path().string();
    const std::string display = formatPromptPath(raw);
    m_cachedPrompt = display + "> ";
    m_promptDirty = false;
    return m_cachedPrompt;
}

void Terminal::printPrompt() const
{
    // Move to column 0 of the current line and clear from cursor to end
    // of line. This ensures the prompt always starts at column 0
    // regardless of where the cursor was left by a previous command
    // (e.g. a TUI child that exited without resetting the cursor).
    writeUtf8ToConsole(m_hConsole, "\r\x1b[K");

    const std::string prompt = getPromptString();

    if (m_vtEnabled) {
        // Extract display part (without "> " suffix) for coloring
        const std::string display = prompt.substr(0, prompt.size() - 2);
        writeUtf8ToConsole(m_hConsole, std::string(commands::BOLD_BLUE) + display + commands::RESET + "> ");
    } else {
        writeUtf8ToConsole(m_hConsole, prompt);
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

void Terminal::refreshLine()
{
    writeUtf8ToConsole(m_hConsole, "\r\x1b[K");
    const std::string prompt = getPromptString();
    // Write prompt with colors
    if (m_vtEnabled) {
        const std::string display = prompt.substr(0, prompt.size() - 2);
        writeUtf8ToConsole(m_hConsole, std::string(commands::BOLD_BLUE) + display + commands::RESET + "> ");
    } else {
        writeUtf8ToConsole(m_hConsole, prompt);
    }
    writeUtf8ToConsole(m_hConsole, m_inputBuffer);
    size_t col = 1
               + displayWidth(prompt)
               + displayWidth(m_inputBuffer, m_cursorPos);
    writeUtf8ToConsole(m_hConsole, "\x1b[" + std::to_string(col) + "G");
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
            writeUtf8ToConsole(m_hConsole, "\n");
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
            // In VT mode, \n alone is a Line Feed (moves down without
            // resetting the column). We need \r\n to move to column 0 of
            // the next line, so the next prompt starts at column 0.
            writeUtf8ToConsole(m_hConsole, "\r\n");
            return m_inputBuffer;
        }

        if (vk == VK_BACK) {
            if (m_cursorPos > 0) {
                size_t prev = prevUtf8Char(m_inputBuffer, m_cursorPos);
                m_inputBuffer.erase(prev, m_cursorPos - prev);
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
            writeUtf8ToConsole(m_hConsole, "^C\n");
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

        pendingHigh = 0;

        if (wc == L'\0') {
            continue;
        }

        if (wc < 0x80) {
            std::string utf8(1, static_cast<char>(wc));
            m_inputBuffer.insert(m_cursorPos, utf8);
            m_cursorPos += utf8.length();
            m_lastWasTab = false;
            refreshLine();
            continue;
        }

        size_t seqLen = 0;
        if      (wc < 0xE0) seqLen = 2;
        else if (wc < 0xF0) seqLen = 3;
        else if (wc < 0xF8) seqLen = 4;
        else {
            continue;
        }

        std::string utf8;
        utf8.push_back(static_cast<char>(wc));

        size_t bytesNeeded = seqLen - 1;
        while (bytesNeeded > 0) {
            INPUT_RECORD peekRec;
            DWORD peekCount = 0;
            if (!PeekConsoleInput(m_hInput, &peekRec, 1, &peekCount) || peekCount == 0) {
                break;
            }
            if (peekRec.EventType != KEY_EVENT || !peekRec.Event.KeyEvent.bKeyDown) {
                break;
            }
            WCHAR peekWc = peekRec.Event.KeyEvent.uChar.UnicodeChar;
            if (peekWc < 0x80 || peekWc > 0xBF) {
                break;
            }
            DWORD consumeCount = 0;
            if (!ReadConsoleInput(m_hInput, &peekRec, 1, &consumeCount)) {
                break;
            }
            utf8.push_back(static_cast<char>(peekWc));
            --bytesNeeded;
        }

        if (utf8.length() == seqLen) {
            m_inputBuffer.insert(m_cursorPos, utf8);
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
        // A first token that contains a path separator (e.g. "./cma", "../foo")
        // is an explicit path, not a command name. Route it to path completion.
        if (token.find_first_of("/\\") != std::string::npos) {
            candidates = getPathCandidates(token);
        } else {
            candidates = getCommandCandidates(token);
        }
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

    // Split the token into the directory part and the filename prefix.
    // We do this manually (not via fs::path) to avoid platform-specific
    // quirks with std::filesystem::path on Windows (forward vs back
    // slash, "./" prefix handling, etc.).
    std::string dirPart;
    std::string prefix;

    // Find the last separator ('/' or '\\') in the token.
    size_t sepPos = token.find_last_of("/\\");
    if (sepPos == std::string::npos) {
        // No separator: token is just a filename prefix in the CWD.
        dirPart = ".";
        prefix = token;
    } else {
        // Token has a path component. Split at the last separator.
        dirPart = token.substr(0, sepPos);
        prefix = token.substr(sepPos + 1);
        // Special case: if dirPart is empty (token starts with /), use "/".
        if (dirPart.empty()) {
            dirPart = token.substr(0, 1);  // just the leading separator
        }
        // Special case: if dirPart is "." or "./", keep it as ".".
        if (dirPart == "." || dirPart == "./" || dirPart == ".\\") {
            dirPart = ".";
        }
    }

    std::vector<std::string> candidates;
    try {
        for (const auto& entry : fs::directory_iterator(dirPart)) {
            std::string fname = entry.path().filename().string();
            if (fname.size() >= prefix.size() &&
                fname.compare(0, prefix.size(), prefix) == 0) {
                if (entry.is_directory()) {
                    fname.push_back('/');  // always use forward slash for shell input
                }
                // Reconstruct the full path: original prefix (dirPart) + "/" + fname
                std::string fullPath;
                // Strip trailing separator from dirPart if any
                std::string dirClean = dirPart;
                if (!dirClean.empty() && (dirClean.back() == '/' || dirClean.back() == '\\')) {
                    dirClean.pop_back();
                }
                if (dirClean.empty() || dirClean == ".") {
                    fullPath = fname;
                } else {
                    fullPath = dirClean + "/" + fname;  // use forward slash
                }
                candidates.push_back(fullPath);
            }
        }
    } catch (const fs::filesystem_error&) {
        // directory doesn't exist or can't be read — return empty list
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

void Terminal::displayCandidates(const std::vector<std::string>& candidates)
{
    // In VT mode, "\n" alone is just LF (moves down without resetting the
    // column). We need "\r\n" so each line — including the candidate list
    // and the prompt re-positioning — starts at column 0. This matches the
    // cursor-management fix applied to printPrompt(), readLineRaw()'s
    // Enter handler, and runExternalCommand().
    writeUtf8ToConsole(m_hConsole, "\r\n");
    for (const auto& c : candidates) {
        writeUtf8ToConsole(m_hConsole, c + "\r\n");
    }
    writeUtf8ToConsole(m_hConsole, "\r\n");
    printPrompt();
    writeUtf8ToConsole(m_hConsole, m_inputBuffer);
    std::cout.flush();
}

void Terminal::addToHistory(const std::string& line)
{
    if (m_history.size() == config::MAX_HISTORY_SIZE) {
        m_history.erase(m_history.begin());
    }
    m_history.push_back(line);
}

void Terminal::loadHistory()
{
    char* userProfile = nullptr;
    size_t len = 0;
    if (_dupenv_s(&userProfile, &len, "USERPROFILE") != 0 || !userProfile) {
        return;
    }

    std::filesystem::path historyPath =
        std::filesystem::path(userProfile) / ".terminal_history";
    free(userProfile);

    std::ifstream file(historyPath, std::ios::in);
    if (!file.is_open()) {
        return;
    }

    std::string line;
    try {
        while (std::getline(file, line)) {
            if (!line.empty()) {
                m_history.push_back(line);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "warning: could not read history file: " << e.what() << '\n';
        m_history.clear();
        return;
    }

    if (file.bad()) {
        std::cerr << "warning: error reading history file, starting with empty history\n";
        m_history.clear();
        return;
    }

    if (m_history.size() > config::MAX_HISTORY_SIZE) {
        m_history.erase(m_history.begin(),
                        m_history.begin() + static_cast<std::ptrdiff_t>(
                            m_history.size() - config::MAX_HISTORY_SIZE));
    }
}

void Terminal::saveHistory()
{
    char* userProfile = nullptr;
    size_t len = 0;
    if (_dupenv_s(&userProfile, &len, "USERPROFILE") != 0 || !userProfile) {
        return;
    }

    std::filesystem::path historyPath =
        std::filesystem::path(userProfile) / ".terminal_history";
    free(userProfile);

    std::ofstream file(historyPath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "warning: could not open history file for writing: "
                  << historyPath.string() << '\n';
        return;
    }

    size_t startIdx = 0;
    if (m_history.size() > config::MAX_HISTORY_SIZE) {
        startIdx = m_history.size() - config::MAX_HISTORY_SIZE;
    }

    for (size_t i = startIdx; i < m_history.size(); ++i) {
        file << m_history[i] << '\n';
        if (file.fail()) {
            std::cerr << "warning: failed to write history entry, stopping save\n";
            return;
        }
    }

    file.flush();
    if (file.fail()) {
        std::cerr << "warning: failed to flush history file to disk\n";
    }
}

void Terminal::recallHistory(int direction)
{
    if (m_history.empty()) return;

    if (!m_inHistoryRecall) {
        m_inHistoryRecall = true;
        m_scratchBuffer = m_inputBuffer;
        m_historyIndex = 0;
    }

    if (direction > 0) {
        if (m_historyIndex < m_history.size()) ++m_historyIndex;
    } else {
        if (m_historyIndex > 0) --m_historyIndex;
    }

    if (m_historyIndex == 0) {
        m_inputBuffer = m_scratchBuffer;
        m_inHistoryRecall = false;
    } else {
        m_inputBuffer = m_history[m_history.size() - m_historyIndex];
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

int Terminal::runExternalCommand(const CommandResolver::ResolutionResult& resolved)
{
    if (!m_fgJob.start(resolved.commandLine)) {
        std::cerr << "failed to start: " << resolved.executable.string() << '\n';
        return -1;
    }

    g_interruptRequested = false;
    restoreRawInput();

    while (m_fgJob.isActive()) {
        DWORD wait = WaitForSingleObject(m_fgJob.hProcess, 100);
        if (wait != WAIT_TIMEOUT) {
            break;
        }

        if (g_interruptRequested) {
            if (m_fgJob.wait(1000)) { break; }
            m_fgJob.interruptBreak();
            if (m_fgJob.wait(1000)) { break; }
            m_fgJob.terminate();
            m_fgJob.wait(INFINITE);
            break;
        }
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(m_fgJob.hProcess, &exitCode);
    m_fgJob.reset();
    setupRawInput();

    // Ensure the cursor is at column 0 of a fresh line for the next prompt.
    // A TUI child may have left the cursor at any position; the shell
    // always wants a clean column-0 start. \r\n moves the cursor to
    // (col=0, row+1); in VT mode, \n alone would just move down without
    // resetting the column.
    writeUtf8ToConsole(m_hConsole, "\r\n");

    return static_cast<int>(exitCode);
}

static bool isTuiCommand(const std::string& cmd)
{
    return cmd == "bc";
}

bool Terminal::runTuiCommandIfAny(const std::string& line)
{
    std::istringstream iss(line);
    std::string cmd;
    if (!(iss >> cmd) || !isTuiCommand(cmd)) {
        return false;
    }

    commands::Args args{cmd};
    std::string token;
    while (iss >> token) {
        args.push_back(token);
    }

    commands::bc(args, std::cout, std::cin, std::cerr);

    // TUI session ended. Reset state and leave the cursor at column 1 on a
    // fresh, cleared line. The main loop's printPrompt() will redraw the
    // prompt at the start of the next iteration; we must NOT call
    // refreshLine() here, or the prompt will be printed twice and the
    // second one will be misaligned.
    m_inputBuffer.clear();
    m_cursorPos = 0;
    m_lastWasTab = false;
    m_inHistoryRecall = false;
    m_historyIndex = 0;

    // CR (\r) -> column 1, EL (\x1b[K) -> erase to end of line. This
    // guarantees the prompt is drawn on a clean line regardless of where
    // FTXUI left the cursor on the primary screen buffer.
    writeUtf8ToConsole(m_hConsole, "\r\x1b[K");
    std::cout.flush();

    return true;
}

void Terminal::processLine(const std::string& line)
{
    try {
        if (runTuiCommandIfAny(line)) {
            return;
        }

        auto d = m_parser.dispatch(line);
        switch (d.action) {
            case CommandParser::CommandDispatch::Action::None:
                break;
            case CommandParser::CommandDispatch::Action::RunBuiltin:
                m_parser.executeParsed(d.parsedStages);
                break;
            case CommandParser::CommandDispatch::Action::RunExternal:
                runExternalCommand(d.external);
                break;
            case CommandParser::CommandDispatch::Action::NotFound:
                std::cerr << d.message << '\n';
                break;
            case CommandParser::CommandDispatch::Action::Failed:
                std::cerr << d.message << '\n';
                break;
        }
        if (!d.parsedStages.empty() && !d.parsedStages[0].empty()) {
            const auto& cmd = d.parsedStages[0][0];
            if (cmd == "cd" || cmd == "push" || cmd == "pop") {
                m_promptDirty = true;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
    }
}

void Terminal::run()
{
const char* ascii_art =
    "                                                            \x1b[0m\n"
    "                             \x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;188m0                           \x1b[0m\n"
    "          \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0     \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;146m0\x1b[38;5;146m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0 \x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;230m0\x1b[38;5;188m0            \x1b[0m\n"
    "         \x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;103m1\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;145m0\x1b[38;5;145m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;145m0\x1b[38;5;102m1\x1b[38;5;101m1\x1b[38;5;144m0\x1b[38;5;186m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;223m0\x1b[38;5;187m0\x1b[38;5;144m0\x1b[38;5;102m1\x1b[38;5;102m1\x1b[38;5;146m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;146m0\x1b[38;5;145m0\x1b[38;5;145m0\x1b[38;5;146m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0       \x1b[0m\n"
    "        \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;102m1\x1b[38;5;172m1\x1b[38;5;102m1\x1b[38;5;188m0\x1b[38;5;102m1\x1b[38;5;95m1\x1b[38;5;172m1\x1b[38;5;179m0\x1b[38;5;102m1\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;59m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;60m1\x1b[38;5;102m1\x1b[38;5;180m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;185m0\x1b[38;5;101m1\x1b[38;5;23m1\x1b[38;5;66m1\x1b[38;5;73m1\x1b[38;5;110m0\x1b[38;5;110m0\x1b[38;5;66m1\x1b[38;5;180m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;221m0\x1b[38;5;215m0\x1b[38;5;137m1\x1b[38;5;102m1\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0      \x1b[0m\n"
    "        \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;145m0\x1b[38;5;130m1\x1b[38;5;172m1\x1b[38;5;173m1\x1b[38;5;102m1\x1b[38;5;145m0\x1b[38;5;102m1\x1b[38;5;95m1\x1b[38;5;143m0\x1b[38;5;143m0\x1b[38;5;59m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;109m0\x1b[38;5;188m0\x1b[38;5;109m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;66m1\x1b[38;5;24m1\x1b[38;5;66m1\x1b[38;5;101m1\x1b[38;5;59m1\x1b[38;5;67m1\x1b[38;5;110m0\x1b[38;5;116m0\x1b[38;5;110m0\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;59m1\x1b[38;5;178m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;172m1\x1b[38;5;102m1\x1b[38;5;231m0\x1b[38;5;231m0      \x1b[0m\n"
    "         \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;102m1\x1b[38;5;94m1\x1b[38;5;166m1\x1b[38;5;166m1\x1b[38;5;172m1\x1b[38;5;172m1\x1b[38;5;172m0\x1b[38;5;172m0\x1b[38;5;173m0\x1b[38;5;173m1\x1b[38;5;95m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;59m1\x1b[38;5;95m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;110m0\x1b[38;5;110m0\x1b[38;5;74m0\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;66m1\x1b[38;5;66m1\x1b[38;5;66m1\x1b[38;5;66m1\x1b[38;5;67m1\x1b[38;5;67m1\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;65m1\x1b[38;5;178m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;131m1\x1b[38;5;145m0\x1b[38;5;231m0\x1b[38;5;188m0      \x1b[0m\n"
    "          \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;102m1\x1b[38;5;131m1\x1b[38;5;130m1\x1b[38;5;130m1\x1b[38;5;166m1\x1b[38;5;166m1\x1b[38;5;166m1\x1b[38;5;95m1\x1b[38;5;95m1\x1b[38;5;101m1\x1b[38;5;101m1\x1b[38;5;173m0\x1b[38;5;172m1\x1b[38;5;94m1\x1b[38;5;94m1\x1b[38;5;173m1\x1b[38;5;95m1\x1b[38;5;137m1\x1b[38;5;67m1\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;24m1\x1b[38;5;66m1\x1b[38;5;107m0\x1b[38;5;150m0\x1b[38;5;150m0\x1b[38;5;150m0\x1b[38;5;65m1\x1b[38;5;24m1\x1b[38;5;67m1\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;59m1\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;208m0\x1b[38;5;172m1\x1b[38;5;102m1\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0      \x1b[0m\n"
    "            \x1b[38;5;230m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;145m0\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;23m1\x1b[38;5;101m1\x1b[38;5;137m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;17m1\x1b[38;5;24m1\x1b[38;5;59m1\x1b[38;5;100m1\x1b[38;5;59m1\x1b[38;5;67m1\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;24m1\x1b[38;5;65m1\x1b[38;5;107m0\x1b[38;5;107m0\x1b[38;5;107m0\x1b[38;5;113m0\x1b[38;5;107m0\x1b[38;5;24m1\x1b[38;5;67m1\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;95m1\x1b[38;5;208m0\x1b[38;5;172m1\x1b[38;5;66m1\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0       \x1b[0m\n"
    "             \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;66m1\x1b[38;5;24m1\x1b[38;5;59m1\x1b[38;5;101m1\x1b[38;5;95m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;137m1\x1b[38;5;172m1\x1b[38;5;59m1\x1b[38;5;31m1\x1b[38;5;24m1\x1b[38;5;66m1\x1b[38;5;102m1\x1b[38;5;66m1\x1b[38;5;67m1\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;31m1\x1b[38;5;24m1\x1b[38;5;30m1\x1b[38;5;65m1\x1b[38;5;65m1\x1b[38;5;59m1\x1b[38;5;30m1\x1b[38;5;24m1\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;31m1\x1b[38;5;60m1\x1b[38;5;59m1\x1b[38;5;145m0\x1b[38;5;231m0\x1b[38;5;231m0         \x1b[0m\n"
    "            \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;60m1\x1b[38;5;24m1\x1b[38;5;67m1\x1b[38;5;67m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;59m1\x1b[38;5;144m0\x1b[38;5;221m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;137m1\x1b[38;5;67m1\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;30m1\x1b[38;5;24m1\x1b[38;5;31m1\x1b[38;5;67m1\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;67m1\x1b[38;5;67m1\x1b[38;5;30m1\x1b[38;5;59m1\x1b[38;5;95m1\x1b[38;5;145m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0    \x1b[0m\n"
    "            \x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;60m1\x1b[38;5;24m1\x1b[38;5;60m1\x1b[38;5;60m1\x1b[38;5;59m1\x1b[38;5;95m1\x1b[38;5;59m1\x1b[38;5;143m0\x1b[38;5;214m0\x1b[38;5;214m0\x1b[38;5;208m0\x1b[38;5;208m0\x1b[38;5;172m1\x1b[38;5;59m1\x1b[38;5;31m1\x1b[38;5;74m1\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m1\x1b[38;5;67m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;66m1\x1b[38;5;67m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;24m1\x1b[38;5;101m1\x1b[38;5;179m0\x1b[38;5;101m1\x1b[38;5;179m0\x1b[38;5;166m1\x1b[38;5;95m1\x1b[38;5;145m0\x1b[38;5;188m0\x1b[38;5;95m1\x1b[38;5;138m0\x1b[38;5;231m0\x1b[38;5;231m0    \x1b[0m\n"
    "            \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;181m0\x1b[38;5;173m1\x1b[38;5;179m0\x1b[38;5;209m0\x1b[38;5;215m0\x1b[38;5;221m0\x1b[38;5;221m0\x1b[38;5;179m0\x1b[38;5;95m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;60m1\x1b[38;5;24m1\x1b[38;5;31m1\x1b[38;5;74m1\x1b[38;5;74m1\x1b[38;5;74m0\x1b[38;5;74m0\x1b[38;5;74m1\x1b[38;5;66m1\x1b[38;5;101m1\x1b[38;5;131m1\x1b[38;5;137m1\x1b[38;5;95m1\x1b[38;5;59m1\x1b[38;5;31m1\x1b[38;5;24m1\x1b[38;5;60m1\x1b[38;5;180m0\x1b[38;5;143m0\x1b[38;5;101m1\x1b[38;5;101m1\x1b[38;5;95m1\x1b[38;5;95m1\x1b[38;5;23m1\x1b[38;5;145m0\x1b[38;5;137m1\x1b[38;5;173m0\x1b[38;5;130m1\x1b[38;5;138m1\x1b[38;5;231m0\x1b[38;5;231m0    \x1b[0m\n"
    "          \x1b[38;5;230m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;181m0\x1b[38;5;174m0\x1b[38;5;173m1\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;221m0\x1b[38;5;222m0\x1b[38;5;215m0\x1b[38;5;208m0\x1b[38;5;208m0\x1b[38;5;136m1\x1b[38;5;66m1\x1b[38;5;73m1\x1b[38;5;30m1\x1b[38;5;24m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;31m1\x1b[38;5;24m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;52m1\x1b[38;5;94m1\x1b[38;5;137m1\x1b[38;5;102m1\x1b[38;5;152m0\x1b[38;5;187m0\x1b[38;5;101m1\x1b[38;5;173m0\x1b[38;5;173m0\x1b[38;5;173m0\x1b[38;5;173m0\x1b[38;5;173m1\x1b[38;5;172m1\x1b[38;5;173m1\x1b[38;5;166m1\x1b[38;5;130m1\x1b[38;5;95m1\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;188m0    \x1b[0m\n"
    "         \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;174m1\x1b[38;5;173m0\x1b[38;5;172m1\x1b[38;5;172m0\x1b[38;5;215m0\x1b[38;5;221m0\x1b[38;5;222m0\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;95m1\x1b[38;5;23m1\x1b[38;5;24m1\x1b[38;5;23m1\x1b[38;5;59m1\x1b[38;5;137m1\x1b[38;5;136m1\x1b[38;5;172m1\x1b[38;5;214m0\x1b[38;5;220m0\x1b[38;5;138m1\x1b[38;5;103m1\x1b[38;5;95m1\x1b[38;5;130m1\x1b[38;5;95m1\x1b[38;5;101m1\x1b[38;5;101m1\x1b[38;5;143m0\x1b[38;5;95m1\x1b[38;5;166m1\x1b[38;5;166m1\x1b[38;5;166m1\x1b[38;5;166m1\x1b[38;5;130m1\x1b[38;5;130m1\x1b[38;5;95m1\x1b[38;5;145m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;145m0     \x1b[0m\n"
    "          \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;174m1\x1b[38;5;209m0\x1b[38;5;214m0\x1b[38;5;221m0\x1b[38;5;222m0\x1b[38;5;221m0\x1b[38;5;215m0\x1b[38;5;215m0\x1b[38;5;222m0\x1b[38;5;221m0\x1b[38;5;208m0\x1b[38;5;130m1\x1b[38;5;60m1\x1b[38;5;60m1\x1b[38;5;66m1\x1b[38;5;24m1\x1b[38;5;95m1\x1b[38;5;208m0\x1b[38;5;208m0\x1b[38;5;214m0\x1b[38;5;221m0\x1b[38;5;102m1\x1b[38;5;102m1\x1b[38;5;152m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;102m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;101m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;59m1\x1b[38;5;60m1\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0       \x1b[0m\n"
    "         \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;173m1\x1b[38;5;209m0\x1b[38;5;208m1\x1b[38;5;208m1\x1b[38;5;172m1\x1b[38;5;166m1\x1b[38;5;130m1\x1b[38;5;172m1\x1b[38;5;208m0\x1b[38;5;172m1\x1b[38;5;166m1\x1b[38;5;95m1\x1b[38;5;60m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;24m1\x1b[38;5;95m1\x1b[38;5;172m0\x1b[38;5;214m0\x1b[38;5;221m0\x1b[38;5;101m1\x1b[38;5;102m1\x1b[38;5;145m0\x1b[38;5;152m0\x1b[38;5;152m0\x1b[38;5;188m0\x1b[38;5;223m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;221m0\x1b[38;5;221m0\x1b[38;5;179m0\x1b[38;5;178m0\x1b[38;5;179m0\x1b[38;5;66m1\x1b[38;5;189m0\x1b[38;5;231m0\x1b[38;5;188m0          \x1b[0m\n"
    "         \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;181m0\x1b[38;5;181m0\x1b[38;5;181m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;173m1\x1b[38;5;174m1\x1b[38;5;138m1\x1b[38;5;59m1\x1b[38;5;24m1\x1b[38;5;66m1\x1b[38;5;145m0\x1b[38;5;67m1\x1b[38;5;24m1\x1b[38;5;59m1\x1b[38;5;65m1\x1b[38;5;59m1\x1b[38;5;60m1\x1b[38;5;145m0\x1b[38;5;152m0\x1b[38;5;152m0\x1b[38;5;188m0\x1b[38;5;187m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;221m0\x1b[38;5;179m0\x1b[38;5;178m0\x1b[38;5;178m0\x1b[38;5;101m1\x1b[38;5;103m1\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0           \x1b[0m\n"
    "           \x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;145m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;145m0\x1b[38;5;23m1\x1b[38;5;24m1\x1b[38;5;66m1\x1b[38;5;103m1\x1b[38;5;109m0\x1b[38;5;151m0\x1b[38;5;187m0\x1b[38;5;187m0\x1b[38;5;186m0\x1b[38;5;222m0\x1b[38;5;222m0\x1b[38;5;221m0\x1b[38;5;221m0\x1b[38;5;221m0\x1b[38;5;221m0\x1b[38;5;179m0\x1b[38;5;179m0\x1b[38;5;178m0\x1b[38;5;137m0\x1b[38;5;95m1\x1b[38;5;102m1\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0             \x1b[0m\n"
    "                     \x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;102m1\x1b[38;5;59m1\x1b[38;5;101m1\x1b[38;5;143m0\x1b[38;5;179m0\x1b[38;5;179m0\x1b[38;5;179m0\x1b[38;5;179m0\x1b[38;5;179m0\x1b[38;5;178m0\x1b[38;5;179m0\x1b[38;5;137m1\x1b[38;5;95m1\x1b[38;5;59m1\x1b[38;5;109m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0               \x1b[0m\n"
    "                        \x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;188m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;231m0\x1b[38;5;188m0                  \x1b[0m\n"
    "                                \x1b[38;5;145m0\x1b[38;5;188m0\x1b[38;5;145m0                         \x1b[0m\n"
    "                                                            \x1b[0m\n"
    "                    Built with \xe2\x9d\xa4\xef\xb8\x8f by Rocket!\n"
    ;
    writeUtf8ToConsole(m_hConsole, ascii_art);
    writeUtf8ToConsole(m_hConsole, "\n");

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

            processLine(line);
        }
        restoreRawInput();
    } else {
        while (true) {
            printPrompt();
            std::string line;
            if (!std::getline(std::cin, line)) {
                writeUtf8ToConsole(m_hConsole, "\n");
                break;
            }

            if (line == "exit" || line == "quit") {
                break;
            }

            processLine(line);
        }
    }
}
