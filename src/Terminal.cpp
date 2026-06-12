#include "Terminal.hpp"
#include "Commands.hpp"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <limits>
#include <atomic>

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

    // Register all command handlers
    m_parser.registerCommand("ls",   [](const commands::Args& a) { commands::ls(a); });
    m_parser.registerCommand("rm",   [](const commands::Args& a) { commands::rm(a); });
    m_parser.registerCommand("cp",   [](const commands::Args& a) { commands::cp(a); });
    m_parser.registerCommand("mv",   [](const commands::Args& a) { commands::mv(a); });
    m_parser.registerCommand("cat",  [](const commands::Args& a) { commands::cat(a); });
    m_parser.registerCommand("tail", [](const commands::Args& a) { commands::tail(a); });
    m_parser.registerCommand("grep", [](const commands::Args& a) { commands::grep(a); });
    m_parser.registerCommand("cd",   [](const commands::Args& a) { commands::cd(a); });
    m_parser.registerCommand("clear",[](const commands::Args& a) { commands::clear(a); });
    m_parser.registerCommand("cls",  [](const commands::Args& a) { commands::clear(a); });
    m_parser.registerCommand("pwd",  [](const commands::Args& a) { commands::pwd(a); });
    m_parser.registerCommand("open", [](const commands::Args& a) { commands::open(a); });
    m_parser.registerCommand("echo", [](const commands::Args& a) { commands::echo(a); });
    m_parser.registerCommand("push", [](const commands::Args& a) { commands::push(a); });
    m_parser.registerCommand("pop",  [](const commands::Args& a) { commands::pop(a); });
    m_parser.registerCommand("slots", [](const commands::Args& a) { commands::slots(a); });
    m_parser.registerCommand("more", [](const commands::Args& a) { commands::more(a); });
    m_parser.registerCommand("mkdir", [](const commands::Args& a) { commands::mkdir(a); });

    m_parser.registerPipelineCommand("cat",  [](const commands::Args& a, std::ostream& out, std::istream& in) { commands::cat(a, out, in); });
    m_parser.registerPipelineCommand("grep", [](const commands::Args& a, std::ostream& out, std::istream& in) { commands::grep(a, out, in); });
    m_parser.registerPipelineCommand("tail", [](const commands::Args& a, std::ostream& out, std::istream& in) { commands::tail(a, out, in); });
    m_parser.registerPipelineCommand("echo", [](const commands::Args& a, std::ostream& out, std::istream& in) { commands::echo(a, out, in); });
    m_parser.registerPipelineCommand("more", [](const commands::Args& a, std::ostream& out, std::istream& in) { commands::more(a, out, in); });
    m_parser.registerPipelineCommand("mkdir", [](const commands::Args& a, std::ostream& out, std::istream& in) { commands::mkdir(a, out, in); });
}

Terminal::~Terminal()
{
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

std::string Terminal::printPrompt() const
{
    const std::string raw = std::filesystem::current_path().string();
    const std::string display = formatPromptPath(raw);

    if (m_vtEnabled) {
        writeUtf8ToConsole(m_hConsole, std::string(commands::BOLD_BLUE) + display + commands::RESET + "> ");
    } else {
        writeUtf8ToConsole(m_hConsole, display + "> ");
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
    writeUtf8ToConsole(m_hConsole, "\r\x1b[K");
    const std::string prompt = printPrompt();
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
            writeUtf8ToConsole(m_hConsole, "\n");
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

        // With SetConsoleCP(CP_UTF8), the Windows console host encodes each
        // non-ASCII keystroke as a UTF-8 byte sequence and delivers each
        // byte as a separate KEY_EVENT whose uChar.UnicodeChar is set to
        // the byte value (in 0x00-0xFF range, NOT a true Unicode code point).
        // We reassemble these into the proper UTF-8 string.
        //
        // For a 2-byte UTF-8 sequence (e.g. ò = 0xC3 0xB2): first event has
        // wc in 0xC2-0xDF, second has wc in 0x80-0xBF. The two events are
        // delivered back-to-back.
        //
        // For 3-byte UTF-8 (e.g. € = 0xE2 0x82 0xAC): first event wc in
        // 0xE0-0xEF, then two continuation events wc in 0x80-0xBF.
        //
        // For 4-byte UTF-8 (e.g. 𝛑 = 0xF0 0x9D 0x9B 0x91): wc in 0xF0-0xF4
        // followed by three continuation events.
        //
        // ASCII (wc < 0x80) is a single self-contained event.

        if (wc < 0x80) {
            // ASCII: just insert directly.
            std::string utf8(1, static_cast<char>(wc));
            m_inputBuffer.insert(m_cursorPos, utf8);
            m_cursorPos += utf8.length();
            m_lastWasTab = false;
            refreshLine();
            continue;
        }

        // For multi-byte UTF-8, the console delivers each byte as a separate
        // WCHAR. We need to figure out the sequence length from the first byte
        // and consume the right number of subsequent events.
        // (In practice, conhost delivers all the bytes of a single keystroke
        // back-to-back, so we can just consume them from the input queue
        // synchronously here. This is the only safe approach because the
        // input is delivered in a tight burst.)
        if (wc < 0xC0) {
            // Stray continuation byte — skip it.
            continue;
        }

        size_t seqLen = 0;
        if      (wc < 0xE0) seqLen = 2;
        else if (wc < 0xF0) seqLen = 3;
        else if (wc < 0xF8) seqLen = 4;
        else {
            // Invalid lead byte — skip it.
            continue;
        }

        // Build the UTF-8 sequence. We use PeekConsoleInput + ReadConsoleInput
        // to pull the remaining bytes of this sequence from the queue WITHOUT
        // blocking (PeekConsoleInput returns immediately).
        std::string utf8;
        utf8.push_back(static_cast<char>(wc));

        size_t bytesNeeded = seqLen - 1;
        while (bytesNeeded > 0) {
            // Peek at the next event(s) to see if more UTF-8 continuation
            // bytes are waiting in the queue.
            INPUT_RECORD peekRec;
            DWORD peekCount = 0;
            if (!PeekConsoleInput(m_hInput, &peekRec, 1, &peekCount) || peekCount == 0) {
                break;
            }
            if (peekRec.EventType != KEY_EVENT || !peekRec.Event.KeyEvent.bKeyDown) {
                // Not a continuation byte — stop reading and let the main loop
                // handle this event on the next iteration.
                break;
            }
            WCHAR peekWc = peekRec.Event.KeyEvent.uChar.UnicodeChar;
            if (peekWc < 0x80 || peekWc > 0xBF) {
                // Not a continuation byte — leave it for the main loop.
                break;
            }
            // It IS a continuation byte; consume it.
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
        // If we couldn't get a full UTF-8 sequence (e.g. the events arrived
        // out of order, or PeekConsoleInput is failing for some reason), just
        // drop the partial input rather than inserting garbage.
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
    writeUtf8ToConsole(m_hConsole, "\n");
    for (const auto& c : candidates) {
        writeUtf8ToConsole(m_hConsole, c + "\n");
    }
    writeUtf8ToConsole(m_hConsole, "\n");
    printPrompt();
    writeUtf8ToConsole(m_hConsole, m_inputBuffer);
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

int Terminal::runExternalCommand(const CommandResolver::ResolutionResult& resolved)
{
    if (!m_fgJob.start(resolved.commandLine)) {
        std::cerr << "failed to start: " << resolved.executable.string() << '\n';
        return -1;
    }

    // Hand the console back to cooked mode while the child runs. With
    // ENABLE_PROCESSED_INPUT restored, the console itself turns Ctrl+C into
    // a CTRL_C_EVENT delivered to every process attached to it — the child
    // gets it directly (like in cmd/PowerShell), and the shell's own ctrl
    // handler swallows it and records the request so we can escalate if the
    // child refuses to die. This also gives interactive children normal
    // line-buffered, echoed stdin.
    g_interruptRequested = false;
    restoreRawInput();

    while (m_fgJob.isActive()) {
        DWORD wait = WaitForSingleObject(m_fgJob.hProcess, 100);
        if (wait != WAIT_TIMEOUT) {
            break;  // child exited (or wait error)
        }

        if (g_interruptRequested) {
            // The child received the same Ctrl+C from the console. Give it a
            // moment to exit politely, then escalate to CTRL_BREAK, then to
            // TerminateProcess.
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
    return static_cast<int>(exitCode);
}

void Terminal::run()
{
const char* ascii_art =
    "                                                            [0m\n"
    "                             [38;5;188m0[38;5;188m0[38;5;188m0[38;5;188m0                           [0m\n"
    "          [38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0     [38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0[38;5;188m0[38;5;146m0[38;5;146m0[38;5;188m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0 [38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;230m0[38;5;188m0            [0m\n"
    "         [38;5;231m0[38;5;188m0[38;5;103m1[38;5;188m0[38;5;188m0[38;5;145m0[38;5;145m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;145m0[38;5;102m1[38;5;101m1[38;5;144m0[38;5;186m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;223m0[38;5;187m0[38;5;144m0[38;5;102m1[38;5;102m1[38;5;146m0[38;5;188m0[38;5;188m0[38;5;188m0[38;5;146m0[38;5;145m0[38;5;145m0[38;5;146m0[38;5;188m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0       [0m\n"
    "        [38;5;231m0[38;5;231m0[38;5;102m1[38;5;172m1[38;5;102m1[38;5;188m0[38;5;102m1[38;5;95m1[38;5;172m1[38;5;179m0[38;5;102m1[38;5;231m0[38;5;188m0[38;5;59m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;60m1[38;5;102m1[38;5;180m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;185m0[38;5;101m1[38;5;23m1[38;5;66m1[38;5;73m1[38;5;110m0[38;5;110m0[38;5;66m1[38;5;180m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;221m0[38;5;215m0[38;5;137m1[38;5;102m1[38;5;231m0[38;5;231m0[38;5;231m0      [0m\n"
    "        [38;5;231m0[38;5;231m0[38;5;145m0[38;5;130m1[38;5;172m1[38;5;173m1[38;5;102m1[38;5;145m0[38;5;102m1[38;5;95m1[38;5;143m0[38;5;143m0[38;5;59m1[38;5;24m1[38;5;24m1[38;5;109m0[38;5;188m0[38;5;109m1[38;5;24m1[38;5;24m1[38;5;66m1[38;5;24m1[38;5;66m1[38;5;101m1[38;5;59m1[38;5;67m1[38;5;110m0[38;5;116m0[38;5;110m0[38;5;74m0[38;5;74m0[38;5;74m0[38;5;74m1[38;5;74m1[38;5;67m1[38;5;59m1[38;5;178m0[38;5;214m0[38;5;214m0[38;5;214m0[38;5;214m0[38;5;214m0[38;5;172m1[38;5;102m1[38;5;231m0[38;5;231m0      [0m\n"
    "         [38;5;231m0[38;5;231m0[38;5;102m1[38;5;94m1[38;5;166m1[38;5;166m1[38;5;172m1[38;5;172m1[38;5;172m0[38;5;172m0[38;5;173m0[38;5;173m1[38;5;95m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;59m1[38;5;95m1[38;5;59m1[38;5;59m1[38;5;110m0[38;5;110m0[38;5;74m0[38;5;74m1[38;5;67m1[38;5;66m1[38;5;66m1[38;5;66m1[38;5;66m1[38;5;67m1[38;5;67m1[38;5;74m1[38;5;74m1[38;5;65m1[38;5;178m0[38;5;214m0[38;5;214m0[38;5;214m0[38;5;214m0[38;5;131m1[38;5;145m0[38;5;231m0[38;5;188m0      [0m\n"
    "          [38;5;231m0[38;5;231m0[38;5;188m0[38;5;102m1[38;5;131m1[38;5;130m1[38;5;130m1[38;5;166m1[38;5;166m1[38;5;166m1[38;5;95m1[38;5;95m1[38;5;101m1[38;5;101m1[38;5;173m0[38;5;172m1[38;5;94m1[38;5;94m1[38;5;173m1[38;5;95m1[38;5;137m1[38;5;67m1[38;5;74m1[38;5;74m1[38;5;24m1[38;5;66m1[38;5;107m0[38;5;150m0[38;5;150m0[38;5;150m0[38;5;65m1[38;5;24m1[38;5;67m1[38;5;74m1[38;5;67m1[38;5;59m1[38;5;214m0[38;5;214m0[38;5;208m0[38;5;172m1[38;5;102m1[38;5;231m0[38;5;231m0[38;5;188m0      [0m\n"
    "            [38;5;230m0[38;5;231m0[38;5;231m0[38;5;145m0[38;5;24m1[38;5;24m1[38;5;23m1[38;5;101m1[38;5;137m1[38;5;59m1[38;5;59m1[38;5;59m1[38;5;59m1[38;5;17m1[38;5;24m1[38;5;59m1[38;5;100m1[38;5;59m1[38;5;67m1[38;5;74m1[38;5;74m1[38;5;67m1[38;5;24m1[38;5;65m1[38;5;107m0[38;5;107m0[38;5;107m0[38;5;113m0[38;5;107m0[38;5;24m1[38;5;67m1[38;5;74m1[38;5;74m1[38;5;67m1[38;5;95m1[38;5;208m0[38;5;172m1[38;5;66m1[38;5;231m0[38;5;231m0[38;5;188m0       [0m\n"
    "             [38;5;231m0[38;5;231m0[38;5;66m1[38;5;24m1[38;5;59m1[38;5;101m1[38;5;95m1[38;5;59m1[38;5;59m1[38;5;137m1[38;5;172m1[38;5;59m1[38;5;31m1[38;5;24m1[38;5;66m1[38;5;102m1[38;5;66m1[38;5;67m1[38;5;74m0[38;5;74m0[38;5;74m0[38;5;31m1[38;5;24m1[38;5;30m1[38;5;65m1[38;5;65m1[38;5;59m1[38;5;30m1[38;5;24m1[38;5;74m1[38;5;74m1[38;5;67m1[38;5;31m1[38;5;60m1[38;5;59m1[38;5;145m0[38;5;231m0[38;5;231m0         [0m\n"
    "            [38;5;231m0[38;5;231m0[38;5;231m0[38;5;60m1[38;5;24m1[38;5;67m1[38;5;67m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;59m1[38;5;144m0[38;5;221m0[38;5;214m0[38;5;214m0[38;5;214m0[38;5;137m1[38;5;67m1[38;5;74m0[38;5;74m0[38;5;74m0[38;5;74m0[38;5;74m1[38;5;67m1[38;5;30m1[38;5;24m1[38;5;31m1[38;5;67m1[38;5;74m1[38;5;67m1[38;5;67m1[38;5;67m1[38;5;30m1[38;5;59m1[38;5;95m1[38;5;145m0[38;5;188m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0    [0m\n"
    "            [38;5;188m0[38;5;231m0[38;5;231m0[38;5;60m1[38;5;24m1[38;5;60m1[38;5;60m1[38;5;59m1[38;5;95m1[38;5;59m1[38;5;143m0[38;5;214m0[38;5;214m0[38;5;208m0[38;5;208m0[38;5;172m1[38;5;59m1[38;5;31m1[38;5;74m1[38;5;74m0[38;5;74m0[38;5;74m1[38;5;67m1[38;5;59m1[38;5;59m1[38;5;66m1[38;5;67m1[38;5;31m1[38;5;31m1[38;5;31m1[38;5;31m1[38;5;24m1[38;5;101m1[38;5;179m0[38;5;101m1[38;5;179m0[38;5;166m1[38;5;95m1[38;5;145m0[38;5;188m0[38;5;95m1[38;5;138m0[38;5;231m0[38;5;231m0    [0m\n"
    "            [38;5;231m0[38;5;231m0[38;5;181m0[38;5;173m1[38;5;179m0[38;5;209m0[38;5;215m0[38;5;221m0[38;5;221m0[38;5;179m0[38;5;95m1[38;5;59m1[38;5;59m1[38;5;60m1[38;5;24m1[38;5;31m1[38;5;74m1[38;5;74m1[38;5;74m0[38;5;74m0[38;5;74m1[38;5;66m1[38;5;101m1[38;5;131m1[38;5;137m1[38;5;95m1[38;5;59m1[38;5;31m1[38;5;24m1[38;5;60m1[38;5;180m0[38;5;143m0[38;5;101m1[38;5;101m1[38;5;95m1[38;5;95m1[38;5;23m1[38;5;145m0[38;5;137m1[38;5;173m0[38;5;130m1[38;5;138m1[38;5;231m0[38;5;231m0    [0m\n"
    "          [38;5;230m0[38;5;231m0[38;5;231m0[38;5;181m0[38;5;174m0[38;5;173m1[38;5;215m0[38;5;215m0[38;5;221m0[38;5;222m0[38;5;215m0[38;5;208m0[38;5;208m0[38;5;136m1[38;5;66m1[38;5;73m1[38;5;30m1[38;5;24m1[38;5;31m1[38;5;31m1[38;5;31m1[38;5;31m1[38;5;31m1[38;5;24m1[38;5;59m1[38;5;59m1[38;5;52m1[38;5;94m1[38;5;137m1[38;5;102m1[38;5;152m0[38;5;187m0[38;5;101m1[38;5;173m0[38;5;173m0[38;5;173m0[38;5;173m0[38;5;173m1[38;5;172m1[38;5;173m1[38;5;166m1[38;5;130m1[38;5;95m1[38;5;188m0[38;5;231m0[38;5;188m0    [0m\n"
    "         [38;5;231m0[38;5;231m0[38;5;231m0[38;5;174m1[38;5;173m0[38;5;172m1[38;5;172m0[38;5;215m0[38;5;221m0[38;5;222m0[38;5;215m0[38;5;215m0[38;5;215m0[38;5;215m0[38;5;215m0[38;5;215m0[38;5;95m1[38;5;23m1[38;5;24m1[38;5;23m1[38;5;59m1[38;5;137m1[38;5;136m1[38;5;172m1[38;5;214m0[38;5;220m0[38;5;138m1[38;5;103m1[38;5;95m1[38;5;130m1[38;5;95m1[38;5;101m1[38;5;101m1[38;5;143m0[38;5;95m1[38;5;166m1[38;5;166m1[38;5;166m1[38;5;166m1[38;5;130m1[38;5;130m1[38;5;95m1[38;5;145m0[38;5;231m0[38;5;231m0[38;5;145m0     [0m\n"
    "          [38;5;231m0[38;5;231m0[38;5;231m0[38;5;174m1[38;5;209m0[38;5;214m0[38;5;221m0[38;5;222m0[38;5;221m0[38;5;215m0[38;5;215m0[38;5;222m0[38;5;221m0[38;5;208m0[38;5;130m1[38;5;60m1[38;5;60m1[38;5;66m1[38;5;24m1[38;5;95m1[38;5;208m0[38;5;208m0[38;5;214m0[38;5;221m0[38;5;102m1[38;5;102m1[38;5;152m0[38;5;188m0[38;5;188m0[38;5;102m1[38;5;59m1[38;5;59m1[38;5;59m1[38;5;101m1[38;5;59m1[38;5;59m1[38;5;59m1[38;5;60m1[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0       [0m\n"
    "         [38;5;231m0[38;5;231m0[38;5;231m0[38;5;173m1[38;5;209m0[38;5;208m1[38;5;208m1[38;5;172m1[38;5;166m1[38;5;130m1[38;5;172m1[38;5;208m0[38;5;172m1[38;5;166m1[38;5;95m1[38;5;60m1[38;5;24m1[38;5;24m1[38;5;24m1[38;5;95m1[38;5;172m0[38;5;214m0[38;5;221m0[38;5;101m1[38;5;102m1[38;5;145m0[38;5;152m0[38;5;152m0[38;5;188m0[38;5;223m0[38;5;222m0[38;5;222m0[38;5;221m0[38;5;221m0[38;5;179m0[38;5;178m0[38;5;179m0[38;5;66m1[38;5;189m0[38;5;231m0[38;5;188m0          [0m\n"
    "         [38;5;231m0[38;5;231m0[38;5;188m0[38;5;181m0[38;5;181m0[38;5;181m0[38;5;188m0[38;5;231m0[38;5;188m0[38;5;173m1[38;5;174m1[38;5;138m1[38;5;59m1[38;5;24m1[38;5;66m1[38;5;145m0[38;5;67m1[38;5;24m1[38;5;59m1[38;5;65m1[38;5;59m1[38;5;60m1[38;5;145m0[38;5;152m0[38;5;152m0[38;5;188m0[38;5;187m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;222m0[38;5;221m0[38;5;179m0[38;5;178m0[38;5;178m0[38;5;101m1[38;5;103m1[38;5;231m0[38;5;231m0[38;5;188m0           [0m\n"
    "           [38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0[38;5;145m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;145m0[38;5;23m1[38;5;24m1[38;5;66m1[38;5;103m1[38;5;109m0[38;5;151m0[38;5;187m0[38;5;187m0[38;5;186m0[38;5;222m0[38;5;222m0[38;5;221m0[38;5;221m0[38;5;221m0[38;5;221m0[38;5;179m0[38;5;179m0[38;5;178m0[38;5;137m0[38;5;95m1[38;5;102m1[38;5;231m0[38;5;231m0[38;5;231m0             [0m\n"
    "                     [38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0[38;5;102m1[38;5;59m1[38;5;101m1[38;5;143m0[38;5;179m0[38;5;179m0[38;5;179m0[38;5;179m0[38;5;179m0[38;5;178m0[38;5;179m0[38;5;137m1[38;5;95m1[38;5;59m1[38;5;109m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;188m0               [0m\n"
    "                        [38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0[38;5;188m0[38;5;188m0[38;5;188m0[38;5;188m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;231m0[38;5;188m0                  [0m\n"
    "                                [38;5;145m0[38;5;188m0[38;5;145m0                         [0m\n"
    "                                                            [0m\n"
    "                    Built with ❤️ by Rocket!\n"
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

            // Use dispatch to decide what to do: built-in, external, or not found.
            try {
                auto d = m_parser.dispatch(line);
                switch (d.action) {
                    case CommandParser::CommandDispatch::Action::None:
                        break;
                    case CommandParser::CommandDispatch::Action::RunBuiltin:
                        // For built-in commands (including multi-stage pipelines), fall back
                        // to the legacy execute() path.
                        m_parser.execute(line);
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
                writeUtf8ToConsole(m_hConsole, "\n");
                break;
            }

            if (line == "exit" || line == "quit") {
                break;
            }

            // Use dispatch to decide what to do: built-in, external, or not found.
            try {
                auto d = m_parser.dispatch(line);
                switch (d.action) {
                    case CommandParser::CommandDispatch::Action::None:
                        break;
                    case CommandParser::CommandDispatch::Action::RunBuiltin:
                        m_parser.execute(line);
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
            } catch (const std::exception& e) {
                std::cerr << "error: " << e.what() << '\n';
            }
        }
    }
}
